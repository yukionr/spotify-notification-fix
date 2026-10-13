#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

typedef struct { const char *summary; void *image; int updates; } Notification;
typedef int (*Update)(Notification *, const char *, const char *, const char *);
extern void *notification_lookup(void *, const char *);
extern int notification_update(Update, Notification *, const char *, const char *, const char *);

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    void *handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!handle) { fprintf(stderr, "%s\n", dlerror()); return 3; }
    Update update = (Update)notification_lookup(handle, "notify_notification_update");
    void (*set_image)(Notification *, void *) = dlsym(handle, "notify_notification_set_image_from_pixbuf");
    if (!update) return 4;
    int (*invoke)(Update, Notification *) = dlsym(handle, "notification_invoke");
    if (!invoke) return 4;
    Notification note = {"old track", (void *)1, 0};
    int outside = strcmp(argv[2], "outside") == 0;
    int cleared = strcmp(argv[3], "cleared") == 0;
    for (int i = 0; i < 3; ++i) {
        int result = outside ? invoke(update, &note) :
            notification_update(update, &note, "current track", "artist", NULL);
        if (result != 1 || note.updates != i + 1 || strcmp(note.summary, "current track")) return 5;
        if ((note.image == NULL) != cleared) {
            fprintf(stderr, "previous artwork retained=%d, expected cleared=%d\n", note.image != NULL, cleared);
            return 7;
        }
        /* A successful image fetch still replaces the cleared artwork. */
        if (set_image) set_image(&note, (void *)(long)(i + 2));
    }
    void *last_image = note.image;
    if (notification_update(update, &note, "", "artist", NULL) != 0 ||
        note.image != last_image || note.updates != 3) return 8;
    dlclose(handle);
    return 0;
}
