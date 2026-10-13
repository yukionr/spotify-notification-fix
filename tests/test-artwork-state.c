#define _GNU_SOURCE
#include <dlfcn.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gio/gio.h>
#include <string.h>

typedef gboolean (*Update)(void *, const char *, const char *, const char *);
extern int notification_update(Update, void *, const char *, const char *, const char *);
extern gboolean notification_close_loader(GdkPixbufLoader *, GError **, const void *);
static GMutex mutex;
static GCond ready;
static gboolean started;
static GMainLoop *server_loop;
static char *received_image;
static char *received_summary;

static const char xml[] =
    "<node><interface name='org.freedesktop.Notifications'>"
    "<method name='GetServerInformation'><arg type='s' direction='out'/><arg type='s' direction='out'/><arg type='s' direction='out'/><arg type='s' direction='out'/></method>"
    "<method name='GetCapabilities'><arg type='as' direction='out'/></method>"
    "<method name='Notify'><arg type='s' direction='in'/><arg type='u' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='as' direction='in'/><arg type='a{sv}' direction='in'/><arg type='i' direction='in'/><arg type='u' direction='out'/></method>"
    "</interface></node>";

static void method_call(GDBusConnection *connection, const char *sender, const char *path,
                        const char *interface, const char *method, GVariant *args,
                        GDBusMethodInvocation *invocation, gpointer data) {
    (void)connection; (void)sender; (void)path; (void)interface; (void)data;
    if (!strcmp(method, "GetServerInformation")) {
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(ssss)", "test", "test", "1", "1.2"));
    } else if (!strcmp(method, "GetCapabilities")) {
        const char *caps[] = {"body", "icon-static", NULL};
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(^as)", caps));
    } else {
        GVariant *summary = g_variant_get_child_value(args, 3);
        GVariant *hints = g_variant_get_child_value(args, 6);
        GVariant *image = g_variant_lookup_value(hints, "image-data", NULL);
        g_mutex_lock(&mutex);
        g_clear_pointer(&received_image, g_free);
        g_clear_pointer(&received_summary, g_free);
        received_summary = g_variant_dup_string(summary, NULL);
        if (image) {
            GVariant *pixels = g_variant_get_child_value(image, 6);
            gsize length;
            const void *bytes = g_variant_get_fixed_array(pixels, &length, 1);
            received_image = g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes, length);
            g_variant_unref(pixels);
            g_variant_unref(image);
        }
        g_mutex_unlock(&mutex);
        g_variant_unref(hints);
        g_variant_unref(summary);
        g_dbus_method_invocation_return_value(invocation, g_variant_new("(u)", 1));
    }
}

static gpointer serve(gpointer data) {
    GMainContext *context = g_main_context_new();
    g_main_context_push_thread_default(context);
    GDBusConnection *connection = g_dbus_connection_new_for_address_sync(data,
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION,
        NULL, NULL, NULL);
    g_assert_nonnull(connection);
    GDBusNodeInfo *node = g_dbus_node_info_new_for_xml(xml, NULL);
    const GDBusInterfaceVTable vtable = { .method_call = method_call };
    guint registration = g_dbus_connection_register_object(connection, "/org/freedesktop/Notifications",
        node->interfaces[0], &vtable, NULL, NULL, NULL);
    g_assert_cmpuint(registration, !=, 0);
    GVariant *reply = g_dbus_connection_call_sync(connection, "org.freedesktop.DBus",
        "/org/freedesktop/DBus", "org.freedesktop.DBus", "RequestName",
        g_variant_new("(su)", "org.freedesktop.Notifications", 0), NULL,
        G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
    g_assert_nonnull(reply);
    g_variant_unref(reply);
    g_mutex_lock(&mutex);
    server_loop = g_main_loop_new(context, FALSE);
    started = TRUE;
    g_cond_signal(&ready);
    g_mutex_unlock(&mutex);
    g_main_loop_run(server_loop);
    g_dbus_connection_unregister_object(connection, registration);
    g_object_unref(connection);
    g_dbus_node_info_unref(node);
    g_main_loop_unref(server_loop);
    g_main_context_pop_thread_default(context);
    g_main_context_unref(context);
    return NULL;
}

static void assert_image(const char *summary, GdkPixbuf *expected) {
    gsize length = (gdk_pixbuf_get_height(expected) - 1) * gdk_pixbuf_get_rowstride(expected) +
        gdk_pixbuf_get_width(expected) * gdk_pixbuf_get_n_channels(expected);
    char *hash = g_compute_checksum_for_data(G_CHECKSUM_SHA256, gdk_pixbuf_read_pixels(expected), length);
    g_mutex_lock(&mutex);
    g_assert_cmpstr(received_summary, ==, summary);
    g_assert_cmpstr(received_image, ==, hash);
    g_mutex_unlock(&mutex);
    g_free(hash);
}

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) return 2;
    GTestDBus *bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(bus);
    GThread *thread = g_thread_new("notification-test-server", serve, (void *)g_test_dbus_get_bus_address(bus));
    g_mutex_lock(&mutex);
    while (!started) g_cond_wait(&ready, &mutex);
    g_mutex_unlock(&mutex);
    void *handle = dlopen("libnotify.so.4", RTLD_NOW | RTLD_LOCAL);
    g_assert_nonnull(handle);
    gboolean (*init)(const char *) = dlsym(handle, "notify_init");
    void (*uninit)(void) = dlsym(handle, "notify_uninit");
    GObject *(*create)(const char *, const char *, const char *) = dlsym(handle, "notify_notification_new");
    void (*set_image)(void *, GdkPixbuf *) = dlsym(handle, "notify_notification_set_image_from_pixbuf");
    gboolean (*show)(void *, GError **) = dlsym(handle, "notify_notification_show");
    Update update = dlsym(handle, "notify_notification_update");
    g_assert_true(init("notification-test"));
    GObject *note = create("track A", "artist", NULL);
    GdkPixbuf *small = gdk_pixbuf_new_from_file(argv[1], NULL);
    g_assert_nonnull(small);
    set_image(note, small);
    /* A text-only update of the same track must retain its correct artwork. */
    if (argc == 3) g_assert_true(notification_update(update, note, "track A", "artist", NULL));
    g_assert_true(show(note, NULL));
    assert_image("track A", small);

    GdkPixbuf *large = gdk_pixbuf_new_from_file(argv[2], NULL);
    g_assert_nonnull(large);
    g_assert_true(notification_update(update, note, "track B", "artist", NULL));
    /* Model libc++'s heap string and optional file result at the traced boundary. */
    struct { gsize capacity; gsize length; const char *path; guint8 present; } result = {
        1, strlen(argv[2]), argv[2], 1
    };
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    /* Spotify rejects files >= 128 KiB before writing any bytes to this loader. */
    notification_close_loader(loader, NULL, &result);
    g_object_unref(loader);
    g_assert_true(show(note, NULL));
    assert_image("track B", large);

    g_object_unref(small); g_object_unref(large); g_object_unref(note);
    uninit(); dlclose(handle);
    g_main_loop_quit(server_loop);
    g_thread_join(thread);
    g_test_dbus_down(bus); g_object_unref(bus);
    g_free(received_image); g_free(received_summary);
    return 0;
}
