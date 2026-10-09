#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

/* Spotify 1.2.96.518: return from the notification image's get_pixbuf call. */
#define NOTIFICATION_RETURN_RVA ((uintptr_t)0x1107b6f)
static const unsigned char supported_build_id[] = {
    0xab,0x6b,0xc2,0xbb,0x7c,0xde,0x82,0xbf,0x43,0x7c,
    0x8d,0x19,0xc6,0x73,0xae,0x1c,0x04,0x4f,0x7e,0x25
};
static pthread_once_t once = PTHREAD_ONCE_INIT;
static GdkPixbuf *(*original_get)(GdkPixbufLoader *);
static gboolean (*original_close)(GdkPixbufLoader *, GError **);
static uintptr_t notification_return;
static int supported;

static size_t align_note(size_t n) { return (n + 3) & ~(size_t)3; }

static int inspect_main(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size; (void)data;
    /* Shared libraries and child executables must not match by RVA alone. */
    if (info->dlpi_name && info->dlpi_name[0]) return 0;
    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_NOTE) continue;
        const unsigned char *p = (const unsigned char *)(info->dlpi_addr + ph->p_vaddr);
        size_t remaining = ph->p_memsz;
        while (remaining >= sizeof(ElfW(Nhdr))) {
            ElfW(Nhdr) note;
            memcpy(&note, p, sizeof(note));
            p += sizeof(note); remaining -= sizeof(note);
            size_t names = align_note(note.n_namesz);
            size_t descs = align_note(note.n_descsz);
            if (names > remaining || descs > remaining - names) break;
            if (note.n_type == NT_GNU_BUILD_ID && note.n_namesz == 4 &&
                memcmp(p, "GNU\0", 4) == 0 &&
                note.n_descsz == sizeof(supported_build_id) &&
                memcmp(p + names, supported_build_id, sizeof(supported_build_id)) == 0) {
                notification_return = info->dlpi_addr + NOTIFICATION_RETURN_RVA;
                supported = 1;
                return 1;
            }
            p += names + descs; remaining -= names + descs;
        }
    }
    return 1;
}

static void initialize(void) {
    original_get = (GdkPixbuf *(*)(GdkPixbufLoader *))dlsym(RTLD_NEXT, "gdk_pixbuf_loader_get_pixbuf");
    original_close = (gboolean (*)(GdkPixbufLoader *, GError **))dlsym(RTLD_NEXT, "gdk_pixbuf_loader_close");
    dl_iterate_phdr(inspect_main, NULL);
}

__attribute__((visibility("default")))
GdkPixbuf *gdk_pixbuf_loader_get_pixbuf(GdkPixbufLoader *loader) {
    uintptr_t caller = (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
    pthread_once(&once, initialize);
    if (!original_get) return NULL;
    GdkPixbuf *pixbuf = original_get(loader);
    if (pixbuf || !supported || caller != notification_return || !original_close) return pixbuf;
    /* Only this call site has already written the entire image. */
    if (!original_close(loader, NULL)) return NULL;
    return original_get(loader);
}
