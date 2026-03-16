#include <stdlib.h>
#include <stddef.h>

extern "C" {

typedef struct {
    void (*func)(void*);
    void* arg;
    void* dso;
} dtor_entry;

#define MAX_DTORS 128

static dtor_entry dtors[MAX_DTORS];
static size_t dtor_count = 0;
void* __dso_handle = 0;

int __cxa_atexit(void (*func)(void*), void* arg, void* dso) {
    if (!func)
        return -1;
    if (dtor_count >= MAX_DTORS)
        return -1;
    dtors[dtor_count].func = func;
    dtors[dtor_count].arg = arg;
    dtors[dtor_count].dso = dso;
    dtor_count++;
    return 0;
}

void __cxa_finalize(void* dso) {
    if (dso == NULL) {
        while (dtor_count > 0) {
            dtor_entry d = dtors[--dtor_count];
            d.func(d.arg);
        }
        return;
    }

    for (size_t i = dtor_count; i > 0; i--) {
        size_t idx = i - 1;
        if (dtors[idx].dso != dso)
            continue;
        dtor_entry d = dtors[idx];
        dtors[idx] = dtors[--dtor_count];
        d.func(d.arg);
    }
}

void __cxa_pure_virtual(void) {
    abort();
}

}
