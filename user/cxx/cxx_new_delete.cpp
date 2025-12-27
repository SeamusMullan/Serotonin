#include <stdlib.h>

void* operator new(unsigned long n) {
    void* p = malloc(n);
    if (!p) abort();
    return p;
}

void operator delete(void* p) noexcept {
    free(p);
}

void* operator new[](unsigned long n) {
    return operator new(n);
}

void operator delete[](void* p) noexcept {
    operator delete(p);
}
