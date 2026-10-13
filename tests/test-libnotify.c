#define _GNU_SOURCE
#include <dlfcn.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <string.h>

typedef gboolean (*Update)(void *, const char *, const char *, const char *);
extern void *notification_lookup(void *, const char *);
extern int notification_update(Update, void *, const char *, const char *, const char *);

static void image_released(gpointer data, GObject *object) {
    (void)object;
    *(gboolean *)data = TRUE;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    /* Exercise real libnotify ownership without showing notifications or using D-Bus. */
    void *handle = dlopen("libnotify.so.4", RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "%s\n", dlerror()); return 3; }
    GObject *(*create)(const char *, const char *, const char *) = dlsym(handle, "notify_notification_new");
    void (*set_image)(void *, GdkPixbuf *) = dlsym(handle, "notify_notification_set_image_from_pixbuf");
    Update update = notification_lookup(handle, "notify_notification_update");
    if (!create || !set_image || !update) return 4;
    GObject *note = create("old track", "artist", NULL);
    gboolean cleared = strcmp(argv[1], "cleared") == 0;
    for (int i = 0; i < 3; ++i) {
        char title[32];
        snprintf(title, sizeof(title), "current track %d", i);
        gboolean released = FALSE;
        GdkPixbuf *image = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 64, 64);
        gdk_pixbuf_fill(image, 0x3c78b4ff);
        g_object_weak_ref(G_OBJECT(image), image_released, &released);
        set_image(note, image);
        g_object_unref(image);
        if (released) return 5;
        if (!notification_update(update, note, title, "artist", NULL)) return 6;
        if (released != cleared) {
            fprintf(stderr, "libnotify previous image released=%d, expected=%d\n", released, cleared);
            g_object_unref(note);
            dlclose(handle);
            return 7;
        }
        char *summary = NULL;
        g_object_get(note, "summary", &summary, NULL);
        int matches = strcmp(summary, title) == 0;
        g_free(summary);
        if (!matches) return 8;
        /* Release retained baseline images before the stack flag goes out of scope. */
        set_image(note, NULL);
        if (!released) return 9;
    }
    g_object_unref(note);
    dlclose(handle);
    return 0;
}
