#include <stdlib.h>
#include <stddef.h>

// Standard new/delete operators
void* operator new(size_t n) {
    void* p = malloc(n);
    if (!p) abort();
    return p;
}

void operator delete(void* p) noexcept {
    free(p);
}

void* operator new[](size_t n) {
    return operator new(n);
}

void operator delete[](void* p) noexcept {
    operator delete(p);
}

// Sized delete operators (C++14)
void operator delete(void* p, size_t) noexcept {
    free(p);
}

void operator delete[](void* p, size_t) noexcept {
    free(p);
}

// Placement new operators - don't allocate, just return the pointer
// These are required for in-place construction
inline void* operator new(size_t, void* p) noexcept {
    return p;
}

inline void* operator new[](size_t, void* p) noexcept {
    return p;
}

// Placement delete - does nothing (memory not allocated by placement new)
inline void operator delete(void*, void*) noexcept {}
inline void operator delete[](void*, void*) noexcept {}
