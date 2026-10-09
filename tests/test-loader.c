#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern GdkPixbuf *notification_get_pixbuf(GdkPixbufLoader *);
int main(int argc, char **argv) {
    if (argc != 4) return 2;
    gchar *data = NULL; gsize size = 0; GError *error = NULL;
    if (!g_file_get_contents(argv[1], &data, &size, &error)) return 3;
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    gboolean valid = strcmp(argv[3], "invalid") != 0;
    gsize write_size = strcmp(argv[2], "outside") == 0 ? size / 2 : size;
    if (!gdk_pixbuf_loader_write(loader, (guchar *)data, write_size, &error)) {
        g_clear_error(&error);
        if (valid) return 4;
    }
    GdkPixbuf *pixbuf;
    if (strcmp(argv[2], "outside") == 0) {
        pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
        if (pixbuf != NULL) return 5;
        /* An unrelated incremental loader must still accept another write. */
        if (!gdk_pixbuf_loader_write(loader, (guchar *)data + write_size, size - write_size, &error)) return 6;
    } else {
        if (strcmp(argv[2], "closed") == 0 && !gdk_pixbuf_loader_close(loader, &error)) return 11;
        pixbuf = notification_get_pixbuf(loader);
        gboolean expected = (strcmp(argv[2], "fixed") == 0 || strcmp(argv[2], "closed") == 0) && valid;
        if ((pixbuf != NULL) != expected) {
            fprintf(stderr, "expected pixbuf=%d, actual=%d\n", expected, pixbuf != NULL);
            return 7;
        }
        if (pixbuf && (gdk_pixbuf_get_width(pixbuf) != 64 || gdk_pixbuf_get_height(pixbuf) != 64)) return 8;
    }
    gboolean closed = gdk_pixbuf_loader_close(loader, &error);
    if (valid && !closed) return 9;
    if (valid && !gdk_pixbuf_loader_get_pixbuf(loader)) return 10;
    g_clear_error(&error); g_object_unref(loader); g_free(data);
    return 0;
}
