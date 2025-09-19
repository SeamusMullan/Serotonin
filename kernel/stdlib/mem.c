#include "stdlib.h"
#include <stddef.h>

/**
 * @brief Move memory area.
 * 
 * @param dstptr Destination pointer.
 * @param srcptr Source pointer.
 * @param size Size of the memory area to move.
 * @return void* Pointer to the destination.
 */
void* memmove(void* dstptr, const void* srcptr, size_t size) {
	unsigned char *dst = dstptr;
    const unsigned char *src = srcptr;

    if (dst < src || dst >= src + size) {
        // forward copy: identical to memcpy
        return memcpy(dstptr, srcptr, size);
    } else {
        // backward copy
        unsigned char *dend = dst + size;
        const unsigned char *send = src + size;

        // peel tail bytes until dend is 16-byte aligned
        uintptr_t mis = (uintptr_t)dend & 15;
        if (mis) {
            size_t tail = mis;
            if (tail > size) tail = size;
            dend -= tail;
            send -= tail;
            size -= tail;
            for (size_t i = 0; i < tail; i++) {
                dend[i] = send[i];
            }
        }

        // SSE2 backward: 16-byte blocks
        while (size >= 16) {
            dend -= 16;
            send -= 16;
            size -= 16;
            asm volatile (
                "movdqu (%[s]), %%xmm0\n\t"
                "movdqa %%xmm0, (%[d])\n\t"
                : [d] "+r"(dend), [s] "+r"(send)
                :
                : "xmm0","memory"
            );
        }

        // any remaining head bytes (should be zero)
        while (size--) {
            *--dend = *--send;
        }
        return dstptr;
    }
}

/**
 * @brief Compare two memory areas.
 * 
 * @param aptr Pointer to the first memory area.
 * @param bptr Pointer to the second memory area.
 * @param size Size of the memory areas to compare.
 * @return int Negative if a < b, positive if a > b, zero if equal.
 */
int memcmp(const void* aptr, const void* bptr, size_t size) {
	const uint8_t *a = aptr, *b = bptr;
    size_t offset = 0;

    // 16-byte SSE2 compare
    while (size >= 16) {
        unsigned int eqmask;
        asm volatile (
            "movdqu   (%[pa]), %%xmm0\n\t"
            "movdqu   (%[pb]), %%xmm1\n\t"
            "pcmpeqb  %%xmm1, %%xmm0\n\t"
            "pmovmskb %%xmm0, %[mask]\n\t"
            : [mask] "=r"(eqmask)
            : [pa] "r"(a + offset), [pb] "r"(b + offset)
            : "xmm0","xmm1","memory"
        );
        if (eqmask != 0xFFFFu) {
            // find first differing byte
            unsigned int diff = (~eqmask) & 0xFFFFu;
            unsigned int idx;
            asm ("bsf %1, %0" : "=r"(idx) : "r"(diff));
            uint8_t ca = a[offset + idx],
                    cb = b[offset + idx];
            return (ca < cb) ? -1 : 1;
        }
        offset += 16;
        size   -= 16;
    }

    // remaining bytes
    for (size_t i = 0; i < size; i++) {
        uint8_t ca = a[offset + i],
                cb = b[offset + i];
        if (ca != cb) return (ca < cb) ? -1 : 1;
    }
    return 0;
}

/**
 * @brief Set memory area to a specific value.
 * 
 * @param bufptr Pointer to the memory area.
 * @param value Value to set.
 * @param size Size of the memory area.
 * @return void* Pointer to the memory area.
 */
void* memset(void* bufptr, int value, size_t size) {
	unsigned char* dst = bufptr;
    size_t n = size;

    // head: align dst to 16 bytes
    uintptr_t mis = (uintptr_t)dst & 15;
    if (mis) {
        size_t head = 16 - mis;
        if (head > n) head = n;
        for (size_t i = 0; i < head; i++)
            *dst++ = (unsigned char)value;
        n -= head;
    }

    // SSE2 main loop: 128 bytes at a time
    if (n >= 128) {
        uint32_t cnt = (uint8_t)value;
        cnt |= cnt << 8;
        cnt |= cnt << 16;
        asm volatile (
            "movd   %0, %%xmm0       \n\t" // load 32‐bit
            "pshufd $0, %%xmm0, %%xmm0\n\t" // broadcast to all lanes
            "movdqa %%xmm0, %%xmm1      \n\t"
            "movdqa %%xmm0, %%xmm2      \n\t"
            "movdqa %%xmm0, %%xmm3      \n\t"
            "movdqa %%xmm0, %%xmm4      \n\t"
            "movdqa %%xmm0, %%xmm5      \n\t"
            "movdqa %%xmm0, %%xmm6      \n\t"
            "movdqa %%xmm0, %%xmm7      \n\t"
            "1:                         \n\t"
            "movdqa %%xmm0,   0(%[p])   \n\t"
            "movdqa %%xmm1,  16(%[p])   \n\t"
            "movdqa %%xmm2,  32(%[p])   \n\t"
            "movdqa %%xmm3,  48(%[p])   \n\t"
            "movdqa %%xmm4,  64(%[p])   \n\t"
            "movdqa %%xmm5,  80(%[p])   \n\t"
            "movdqa %%xmm6,  96(%[p])   \n\t"
            "movdqa %%xmm7, 112(%[p])   \n\t"
            "add    $128, %[p]          \n\t"
            "dec    %[c]                \n\t"
            "jnz    1b                  \n\t"
            : [p] "+r"(dst), [c] "+r"(cnt)
            :
            : "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory"
        );

        n &= 127;
    }

    // handle any remaining 16-byte chunks
    if (n >= 16) {
        uint32_t cnt = (uint8_t)value;
        cnt |= cnt << 8;
        cnt |= cnt << 16;

        asm volatile (
            "movd   %[cnt], %%xmm0       \n\t"
            "pshufd $0, %%xmm0, %%xmm0 \n\t"
            : : [cnt]"r"(cnt) : "xmm0"
        );

        size_t cnt = n / 16;
        asm volatile (
            "1:                        \n\t"
            "movdqa %%xmm0, (%[p])     \n\t"
            "add    $16, %[p]          \n\t"
            "dec    %[c]               \n\t"
            "jnz    1b                 \n\t"
            : [p] "+r"(dst), [c] "+r"(cnt)
            :
            : "xmm0","memory"
        );
        n &= 15;
    }

    // tail: leftover bytes
    while (n--) {
        *dst++ = (unsigned char)value;
    }
    return bufptr;
}

/**
 * @brief Copy memory area.
 * 
 * @param dstptr Destination pointer.
 * @param srcptr Source pointer.
 * @param size Size of the memory area to copy.
 * @return void* Pointer to the destination.
 */
void* memcpy(void* restrict dstptr, const void* restrict srcptr, size_t size) {
    unsigned char *dst = dstptr;
    const unsigned char *src = srcptr;

    // align dst up to 16 bytes
    uintptr_t mis = (uintptr_t)dst & 15;
    if (mis) {
        size_t head = 16 - mis;
        if (head > size) head = size;

        switch (head) {
            case 15: dst[14] = src[14];
            case 14: dst[13] = src[13];
            case 13: dst[12] = src[12];
            case 12: dst[11] = src[11];
            case 11: dst[10] = src[10];
            case 10: dst[9]  = src[9];
            case  9: dst[8]  = src[8];
            case  8: dst[7]  = src[7];
            case  7: dst[6]  = src[6];
            case  6: dst[5]  = src[5];
            case  5: dst[4]  = src[4];
            case  4: dst[3]  = src[3];
            case  3: dst[2]  = src[2];
            case  2: dst[1]  = src[1];
            case  1: dst[0]  = src[0];
            case  0: break;
        }

        dst += head;
        src += head;
        size -= head;
    }

    uintptr_t src_mis = (uintptr_t)src & 15;
    if (mis == src_mis) {
        // aligned src

        // 128 byte copy
        while (size >= 128) {
            asm volatile (
                "movdqa 0(%[s]), %%xmm0\n\t"
                "movdqa 16(%[s]), %%xmm1\n\t"
                "movdqa 32(%[s]), %%xmm2\n\t"
                "movdqa 48(%[s]), %%xmm3\n\t"
                "movdqa 64(%[s]), %%xmm4\n\t"
                "movdqa 80(%[s]), %%xmm5\n\t"
                "movdqa 96(%[s]), %%xmm6\n\t"
                "movdqa 112(%[s]), %%xmm7\n\t"
                "movdqa %%xmm0, 0(%[d])\n\t"
                "movdqa %%xmm1, 16(%[d])\n\t"
                "movdqa %%xmm2, 32(%[d])\n\t"
                "movdqa %%xmm3, 48(%[d])\n\t"
                "movdqa %%xmm4, 64(%[d])\n\t"
                "movdqa %%xmm5, 80(%[d])\n\t"
                "movdqa %%xmm6, 96(%[d])\n\t"
                "movdqa %%xmm7, 112(%[d])\n\t"
                : [d] "+r"(dst), [s] "+r"(src)
                :
                : "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory"
            );
            dst += 128;
            src += 128;
            size -= 128;
        }


        // 64 byte copy
        while (size >= 64) {
            asm volatile (
                "movdqa 0(%[s]), %%xmm0\n\t"
                "movdqa 16(%[s]), %%xmm1\n\t"
                "movdqa 32(%[s]), %%xmm2\n\t"
                "movdqa 48(%[s]), %%xmm3\n\t"
                "movdqa %%xmm0, 0(%[d])\n\t"
                "movdqa %%xmm1, 16(%[d])\n\t"
                "movdqa %%xmm2, 32(%[d])\n\t"
                "movdqa %%xmm3, 48(%[d])\n\t"
                : [d] "+r"(dst), [s] "+r"(src)
                :
                : "xmm0","xmm1","xmm2","xmm3","memory"
            );
            dst += 64;
            src += 64;
            size -= 64;
        }

        // remainder 16 byte chunks
        while (size >= 16) {
            asm volatile (
                "movdqa (%[s]), %%xmm0\n\t"
                "movdqa %%xmm0, (%[d])\n\t"
                : [d] "+r"(dst), [s] "+r"(src)
                :
                : "xmm0","memory"
            );
            dst += 16;
            src += 16;
            size -= 16;
        }

        // tail bytes
        switch (size) {
            case 15: dst[14] = src[14];
            case 14: dst[13] = src[13];
            case 13: dst[12] = src[12];
            case 12: dst[11] = src[11];
            case 11: dst[10] = src[10];
            case 10: dst[9]  = src[9];
            case  9: dst[8]  = src[8];
            case  8: dst[7]  = src[7];
            case  7: dst[6]  = src[6];
            case  6: dst[5]  = src[5];
            case  5: dst[4]  = src[4];
            case  4: dst[3]  = src[3];
            case  3: dst[2]  = src[2];
            case  2: dst[1]  = src[1];
            case  1: dst[0]  = src[0];
            case  0: break;
        }
        return dstptr;
    } else {
        // unaligned src

        // 128 byte copy
        while (size >= 128) {
            asm volatile (
                "movdqu 0(%[s]), %%xmm0\n\t"
                "movdqu 16(%[s]), %%xmm1\n\t"
                "movdqu 32(%[s]), %%xmm2\n\t"
                "movdqu 48(%[s]), %%xmm3\n\t"
                "movdqu 64(%[s]), %%xmm4\n\t"
                "movdqu 80(%[s]), %%xmm5\n\t"
                "movdqu 96(%[s]), %%xmm6\n\t"
                "movdqu 112(%[s]), %%xmm7\n\t"
                "movdqa %%xmm0, 0(%[d])\n\t"
                "movdqa %%xmm1, 16(%[d])\n\t"
                "movdqa %%xmm2, 32(%[d])\n\t"
                "movdqa %%xmm3, 48(%[d])\n\t"
                "movdqa %%xmm4, 64(%[d])\n\t"
                "movdqa %%xmm5, 80(%[d])\n\t"
                "movdqa %%xmm6, 96(%[d])\n\t"
                "movdqa %%xmm7, 112(%[d])\n\t"
                : [d] "+r"(dst), [s] "+r"(src)
                :
                : "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory"
            );
            dst += 128;
            src += 128;
            size -= 128;
        }


        // 64 byte copy
        while (size >= 64) {
            asm volatile (
                "movdqu 0(%[s]), %%xmm0\n\t"
                "movdqu 16(%[s]), %%xmm1\n\t"
                "movdqu 32(%[s]), %%xmm2\n\t"
                "movdqu 48(%[s]), %%xmm3\n\t"
                "movdqa %%xmm0, 0(%[d])\n\t"
                "movdqa %%xmm1, 16(%[d])\n\t"
                "movdqa %%xmm2, 32(%[d])\n\t"
                "movdqa %%xmm3, 48(%[d])\n\t"
                : [d] "+r"(dst), [s] "+r"(src)
                :
                : "xmm0","xmm1","xmm2","xmm3","memory"
            );
            dst += 64;
            src += 64;
            size -= 64;
        }

        // remainder 16 byte chunks
        while (size >= 16) {
            asm volatile (
                "movdqu (%[s]), %%xmm0\n\t"
                "movdqa %%xmm0, (%[d])\n\t"
                : [d] "+r"(dst), [s] "+r"(src)
                :
                : "xmm0","memory"
            );
            dst += 16;
            src += 16;
            size -= 16;
        }

        // tail bytes
        switch (size) {
            case 15: dst[14] = src[14];
            case 14: dst[13] = src[13];
            case 13: dst[12] = src[12];
            case 12: dst[11] = src[11];
            case 11: dst[10] = src[10];
            case 10: dst[9]  = src[9];
            case  9: dst[8]  = src[8];
            case  8: dst[7]  = src[7];
            case  7: dst[6]  = src[6];
            case  6: dst[5]  = src[5];
            case  5: dst[4]  = src[4];
            case  4: dst[3]  = src[3];
            case  3: dst[2]  = src[2];
            case  2: dst[1]  = src[1];
            case  1: dst[0]  = src[0];
            case  0: break;
        }
        return dstptr;
    }
}

/**
 * @brief Non temporal copy memory area.
 * 
 * @param dstptr Destination pointer.
 * @param srcptr Source pointer.
 * @param size Size of the memory area to copy.
 * @return void* Pointer to the destination.
 */
void* memcpy_nt(void* restrict dstptr, const void* restrict srcptr, size_t size) {
    unsigned char *dst = dstptr;
    const unsigned char *src = srcptr;

    // align dst up to 16 bytes
    uintptr_t mis = (uintptr_t)dst & 15;
    if (mis) {
        size_t head = 16 - mis;
        if (head > size) head = size;

        switch (head) {
            case 15: dst[14] = src[14];
            case 14: dst[13] = src[13];
            case 13: dst[12] = src[12];
            case 12: dst[11] = src[11];
            case 11: dst[10] = src[10];
            case 10: dst[9]  = src[9];
            case  9: dst[8]  = src[8];
            case  8: dst[7]  = src[7];
            case  7: dst[6]  = src[6];
            case  6: dst[5]  = src[5];
            case  5: dst[4]  = src[4];
            case  4: dst[3]  = src[3];
            case  3: dst[2]  = src[2];
            case  2: dst[1]  = src[1];
            case  1: dst[0]  = src[0];
            case  0: break;
        }

        dst += head;
        src += head;
        size -= head;
    }

    uintptr_t src_mis = (uintptr_t)src & 15;
    if (mis == src_mis) {
        // aligned src

        // 128 byte copy
        while (size >= 128) {
            asm volatile (
                "movdqa 0(%[s]), %%xmm0\n\t"
                "movdqa 16(%[s]), %%xmm1\n\t"
                "movdqa 32(%[s]), %%xmm2\n\t"
                "movdqa 48(%[s]), %%xmm3\n\t"
                "movdqa 64(%[s]), %%xmm4\n\t"
                "movdqa 80(%[s]), %%xmm5\n\t"
                "movdqa 96(%[s]), %%xmm6\n\t"
                "movdqa 112(%[s]), %%xmm7\n\t"
                "movntdq %%xmm0, 0(%[d])\n\t"
                "movntdq %%xmm1, 16(%[d])\n\t"
                "movntdq %%xmm2, 32(%[d])\n\t"
                "movntdq %%xmm3, 48(%[d])\n\t"
                "movntdq %%xmm4, 64(%[d])\n\t"
                "movntdq %%xmm5, 80(%[d])\n\t"
                "movntdq %%xmm6, 96(%[d])\n\t"
                "movntdq %%xmm7, 112(%[d])\n\t"
                : [d] "+r"(dst), [s] "+r"(src)
                :
                : "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory"
            );
            dst += 128;
            src += 128;
            size -= 128;
        }


        // 64 byte copy
        while (size >= 64) {
            asm volatile (
                "movdqa 0(%[s]), %%xmm0\n\t"
                "movdqa 16(%[s]), %%xmm1\n\t"
                "movdqa 32(%[s]), %%xmm2\n\t"
                "movdqa 48(%[s]), %%xmm3\n\t"
                "movntdq %%xmm0, 0(%[d])\n\t"
                "movntdq %%xmm1, 16(%[d])\n\t"
                "movntdq %%xmm2, 32(%[d])\n\t"
                "movntdq %%xmm3, 48(%[d])\n\t"
                : [d] "+r"(dst), [s] "+r"(src)
                :
                : "xmm0","xmm1","xmm2","xmm3","memory"
            );
            dst += 64;
            src += 64;
            size -= 64;
        }

        // remainder 16 byte chunks
        while (size >= 16) {
            asm volatile (
                "movdqa (%[s]), %%xmm0\n\t"
                "movntdq %%xmm0, (%[d])\n\t"
                : [d] "+r"(dst), [s] "+r"(src)
                :
                : "xmm0","memory"
            );
            dst += 16;
            src += 16;
            size -= 16;
        }

        // tail bytes
        switch (size) {
            case 15: dst[14] = src[14];
            case 14: dst[13] = src[13];
            case 13: dst[12] = src[12];
            case 12: dst[11] = src[11];
            case 11: dst[10] = src[10];
            case 10: dst[9]  = src[9];
            case  9: dst[8]  = src[8];
            case  8: dst[7]  = src[7];
            case  7: dst[6]  = src[6];
            case  6: dst[5]  = src[5];
            case  5: dst[4]  = src[4];
            case  4: dst[3]  = src[3];
            case  3: dst[2]  = src[2];
            case  2: dst[1]  = src[1];
            case  1: dst[0]  = src[0];
            case  0: break;
        }

        // order streaming stores before returning
        asm volatile("sfence");

        return dstptr;
    } else {
        // unaligned src

        // 128 byte copy
        while (size >= 128) {
            asm volatile (
                "movdqu 0(%[s]), %%xmm0\n\t"
                "movdqu 16(%[s]), %%xmm1\n\t"
                "movdqu 32(%[s]), %%xmm2\n\t"
                "movdqu 48(%[s]), %%xmm3\n\t"
                "movdqu 64(%[s]), %%xmm4\n\t"
                "movdqu 80(%[s]), %%xmm5\n\t"
                "movdqu 96(%[s]), %%xmm6\n\t"
                "movdqu 112(%[s]), %%xmm7\n\t"
                "movntdq %%xmm0, 0(%[d])\n\t"
                "movntdq %%xmm1, 16(%[d])\n\t"
                "movntdq %%xmm2, 32(%[d])\n\t"
                "movntdq %%xmm3, 48(%[d])\n\t"
                "movntdq %%xmm4, 64(%[d])\n\t"
                "movntdq %%xmm5, 80(%[d])\n\t"
                "movntdq %%xmm6, 96(%[d])\n\t"
                "movntdq %%xmm7, 112(%[d])\n\t"
                : [d] "+r"(dst), [s] "+r"(src)
                :
                : "xmm0","xmm1","xmm2","xmm3","xmm4","xmm5","xmm6","xmm7","memory"
            );
            dst += 128;
            src += 128;
            size -= 128;
        }


        // 64 byte copy
        while (size >= 64) {
            asm volatile (
                "movdqu 0(%[s]), %%xmm0\n\t"
                "movdqu 16(%[s]), %%xmm1\n\t"
                "movdqu 32(%[s]), %%xmm2\n\t"
                "movdqu 48(%[s]), %%xmm3\n\t"
                "movntdq %%xmm0, 0(%[d])\n\t"
                "movntdq %%xmm1, 16(%[d])\n\t"
                "movntdq %%xmm2, 32(%[d])\n\t"
                "movntdq %%xmm3, 48(%[d])\n\t"
                : [d] "+r"(dst), [s] "+r"(src)
                :
                : "xmm0","xmm1","xmm2","xmm3","memory"
            );
            dst += 64;
            src += 64;
            size -= 64;
        }

        // remainder 16 byte chunks
        while (size >= 16) {
            asm volatile (
                "movdqu (%[s]), %%xmm0\n\t"
                "movntdq %%xmm0, (%[d])\n\t"
                : [d] "+r"(dst), [s] "+r"(src)
                :
                : "xmm0","memory"
            );
            dst += 16;
            src += 16;
            size -= 16;
        }

        // tail bytes
        switch (size) {
            case 15: dst[14] = src[14];
            case 14: dst[13] = src[13];
            case 13: dst[12] = src[12];
            case 12: dst[11] = src[11];
            case 11: dst[10] = src[10];
            case 10: dst[9]  = src[9];
            case  9: dst[8]  = src[8];
            case  8: dst[7]  = src[7];
            case  7: dst[6]  = src[6];
            case  6: dst[5]  = src[5];
            case  5: dst[4]  = src[4];
            case  4: dst[3]  = src[3];
            case  3: dst[2]  = src[2];
            case  2: dst[1]  = src[1];
            case  1: dst[0]  = src[0];
            case  0: break;
        }

        // order streaming stores before returning
        asm volatile("sfence");

        return dstptr;
    }
}