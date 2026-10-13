typedef struct { const char *summary; void *image; int updates; } Notification;
typedef int (*Update)(Notification *, const char *, const char *, const char *);
int notification_invoke(Update update, Notification *note) {
    volatile int result = update(note, "current track", "artist", 0);
    return result;
}
int notify_notification_update(Notification *note, const char *summary,
                               const char *body, const char *icon) {
    (void)body; (void)icon;
    if (!summary || !summary[0]) return 0;
    note->summary = summary;
    note->updates++;
    /* Like libnotify, a text update retains the previous image-data hint. */
    return 1;
}
#ifndef OMIT_SET_IMAGE
void notify_notification_set_image_from_pixbuf(Notification *note, void *image) {
    note->image = image;
}
#endif
