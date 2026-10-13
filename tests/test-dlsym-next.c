#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>

#if defined(FIRST_PROVIDER)
int next_symbol(void) { return 1; }
int lookup_next(void) {
    int (*next)(void) = dlsym(RTLD_NEXT, "next_symbol");
    return next ? next() : -1;
}
#elif defined(SECOND_PROVIDER)
int next_symbol(void) { return 2; }
#else
int main(void) {
    dlerror();
    int (*lookup)(void) = dlsym(RTLD_DEFAULT, "lookup_next");
    if (dlerror() != NULL) return 8;
    return lookup && lookup() == 2 ? 0 : 7;
}
#endif
