#define _GNU_SOURCE
#include <dlfcn.h>
#include <elf.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_once_t pixbuf_once = PTHREAD_ONCE_INIT;
static GdkPixbuf *(*original_get)(GdkPixbufLoader *);
static gboolean (*original_close)(GdkPixbufLoader *, GError **);
static GdkPixbuf *(*original_from_file)(const char *, GError **);
static void (*original_unref)(void *);
static void *(*original_lookup)(void *, const char *);
static gboolean (*original_update)(void *, const char *, const char *, const char *);
static void (*set_image)(void *, GdkPixbuf *);
static void (*get_properties)(void *, const char *, ...);
static void (*free_property)(void *);
static gboolean (*original_show)(void *, GError **);
static __thread void *updating_notification;
static pthread_mutex_t notification_mutex = PTHREAD_MUTEX_INITIALIZER;
static uintptr_t main_base;
static const ElfW(Phdr) *main_headers;
static ElfW(Half) main_header_count;

static int inspect_main(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size; (void)data;
    if (info->dlpi_name && info->dlpi_name[0]) return 0;
    main_base = info->dlpi_addr;
    main_headers = info->dlpi_phdr;
    main_header_count = info->dlpi_phnum;
    return 1;
}

static int main_code_contains(uintptr_t caller, size_t before, size_t after) {
    for (ElfW(Half) i = 0; i < main_header_count; ++i) {
        const ElfW(Phdr) *ph = &main_headers[i];
        if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_X) || !(ph->p_flags & PF_R)) continue;
        uintptr_t begin = main_base + ph->p_vaddr;
        if (caller >= begin && caller - begin < ph->p_memsz &&
            before <= caller - begin && after <= ph->p_memsz - (caller - begin)) return 1;
    }
    return 0;
}

static int notification_image_call(uintptr_t caller) {
    if (!main_code_contains(caller, 18, 14)) return 0;
    const unsigned char *p = (const unsigned char *)caller;
    /* Complete-image write, same loader lookup, saved result, NULL check.
     * Relative call displacements vary across updates and ASLR. */
    return memcmp(p - 18, "\x48\x89\xdf\x31\xc9\xe8", 6) == 0 &&
           memcmp(p - 8, "\x48\x89\xdf\xe8", 4) == 0 &&
           memcmp(p, "\x49\x89\xc6\x48\x89\x84\x24\x80\x00\x00\x00\x48\x85\xc0", 14) == 0;
}

static void initialize(void) {
    /* Bootstrap without calling the dlsym interposer recursively. */
    original_lookup = (void *(*)(void *, const char *))dlvsym(RTLD_NEXT, "dlsym", "GLIBC_2.2.5");
    dl_iterate_phdr(inspect_main, NULL);
}

static void initialize_pixbuf(void) {
    pthread_once(&once, initialize);
    original_get = (GdkPixbuf *(*)(GdkPixbufLoader *))original_lookup(RTLD_NEXT, "gdk_pixbuf_loader_get_pixbuf");
    original_close = (gboolean (*)(GdkPixbufLoader *, GError **))original_lookup(RTLD_NEXT, "gdk_pixbuf_loader_close");
    original_from_file = original_lookup(RTLD_NEXT, "gdk_pixbuf_new_from_file");
    original_unref = original_lookup(RTLD_NEXT, "g_object_unref");
}

static gboolean update_notification(void *notification, const char *summary,
                                    const char *body, const char *icon) {
    uintptr_t caller = (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
    int main_update = main_code_contains(caller, 0, 0);
    int same_text = 0;
    if (main_update && get_properties && free_property) {
        char *old_summary = NULL, *old_body = NULL;
        get_properties(notification, "summary", &old_summary, "body", &old_body, NULL);
        same_text = summary && old_summary && strcmp(summary, old_summary) == 0 &&
            strcmp(body ? body : "", old_body ? old_body : "") == 0;
        free_property(old_summary); free_property(old_body);
    }
    gboolean result = original_update(notification, summary, body, icon);
    if (result && main_update) {
        /* Text-only refreshes of this track still need its existing artwork. */
        if (!same_text) set_image(notification, NULL);
        updating_notification = notification;
    }
    return result;
}

static gboolean show_notification(void *notification, GError **error) {
    if (updating_notification == notification) updating_notification = NULL;
    return original_show(notification, error);
}

static void *lookup_notification(void *handle, const char *name) {
    void *result = original_lookup(handle, name);
    if (!result) return NULL;
    if (strcmp(name, "notify_notification_show") == 0) {
        pthread_mutex_lock(&notification_mutex);
        if (!original_show) original_show = result;
        int matches = original_show == result;
        pthread_mutex_unlock(&notification_mutex);
        return matches ? (void *)show_notification : result;
    }
    void (*clear)(void *, GdkPixbuf *) = original_lookup(handle, "notify_notification_set_image_from_pixbuf");
    if (!clear) return result;
    pthread_mutex_lock(&notification_mutex);
    if (!original_update) {
        original_update = result;
        set_image = clear;
        get_properties = original_lookup(handle, "g_object_get");
        free_property = original_lookup(handle, "g_free");
    }
    int matches = original_update == result && set_image == clear;
    pthread_mutex_unlock(&notification_mutex);
    return matches ? (void *)update_notification : result;
}

/* Tail-forward unrelated lookups so RTLD_NEXT retains the original caller. */
__attribute__((used, noinline))
static void *select_lookup(void *handle, const char *name, uintptr_t caller) {
    pthread_once(&once, initialize);
    if (main_code_contains(caller, 0, 0) && handle != RTLD_NEXT &&
        handle != RTLD_DEFAULT && (strcmp(name, "notify_notification_update") == 0 ||
                                  strcmp(name, "notify_notification_show") == 0))
        return lookup_notification;
    return original_lookup;
}

__attribute__((visibility("default"), naked))
void *dlsym(void *handle __attribute__((unused)), const char *name __attribute__((unused))) {
    __asm__(
        "push %rdi\n"
        "push %rsi\n"
        "sub $8, %rsp\n"
        "mov 24(%rsp), %rdx\n"
        "call select_lookup\n"
        "add $8, %rsp\n"
        "pop %rsi\n"
        "pop %rdi\n"
        "jmp *%rax\n");
}

__attribute__((used, noinline))
static gboolean close_notification_loader(GdkPixbufLoader *loader, GError **error,
                                          uintptr_t caller, const unsigned char *stack) {
    pthread_once(&pixbuf_once, initialize_pixbuf);
    gboolean result = original_close(loader, error);
    if (!updating_notification || !set_image || !original_from_file || !original_unref ||
        original_get(loader) || !main_code_contains(caller, 19, 5)) return result;
    const unsigned char *code = (const unsigned char *)caller;
    /* Notification cleanup leaves the optional image filename at stack + 0x78. */
    if (memcmp(code - 19, "\x48\xc7\x44\x24\x70\x00\x00\x00\x00\x48\x89\xdf\x31\xf6\xe8", 15) ||
        memcmp(code, "\xbf\x20\x00\x00\x00", 5)) return result;
    const unsigned char *file_result;
    memcpy(&file_result, stack + 0x78, sizeof(file_result));
    if (!file_result || file_result[24] != 1) return result;
    const char *path;
    size_t length;
    if (file_result[0] & 1) {
        memcpy(&length, file_result + 8, sizeof(length));
        memcpy(&path, file_result + 16, sizeof(path));
    } else {
        length = file_result[0] >> 1;
        if (length > 22) return result;
        path = (const char *)file_result + 1;
    }
    if (!path || !length || length > 4096 || memchr(path, '\0', length)) return result;
    char *filename = malloc(length + 1);
    if (!filename) return result;
    memcpy(filename, path, length); filename[length] = '\0';
    /* The client's reader rejects files >= 128 KiB. Decode the full file instead. */
    GdkPixbuf *image = original_from_file(filename, NULL);
    free(filename);
    if (image) {
        set_image(updating_notification, image);
        original_unref(image);
    }
    return result;
}

__attribute__((visibility("default"), naked))
gboolean gdk_pixbuf_loader_close(GdkPixbufLoader *loader __attribute__((unused)),
                                 GError **error __attribute__((unused))) {
    __asm__(
        "mov (%rsp), %rdx\n"
        "lea 8(%rsp), %rcx\n"
        "sub $8, %rsp\n"
        "call close_notification_loader\n"
        "add $8, %rsp\n"
        "ret\n");
}

__attribute__((visibility("default")))
GdkPixbuf *gdk_pixbuf_loader_get_pixbuf(GdkPixbufLoader *loader) {
    uintptr_t caller = (uintptr_t)__builtin_extract_return_addr(__builtin_return_address(0));
    pthread_once(&pixbuf_once, initialize_pixbuf);
    if (!original_get) return NULL;
    GdkPixbuf *pixbuf = original_get(loader);
    if (pixbuf || !notification_image_call(caller) || !original_close) return pixbuf;
    /* Only this call site has already written the entire image. */
    if (!original_close(loader, NULL)) return NULL;
    return original_get(loader);
}
