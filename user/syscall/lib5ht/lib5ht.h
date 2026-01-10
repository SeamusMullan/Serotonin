#ifndef _LIB5HT
#define _LIB5HT

#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct proc_5ht {
    int pid;
    char name[32];
    int priority;
    int priv;
} proc_5ht_t;

typedef struct fb_info {
    uint32_t size;
    uint32_t fb_size;
    uint32_t layer_window_size;
    uint32_t metadata_size;
    uint32_t alignment;
} fb_info_t;

typedef struct fb_layer_config {
    uint32_t size;
    uint16_t x0;
    uint16_t x1;
    uint16_t y0;
    uint16_t y1;
    uint8_t  alpha;
    uint16_t stride;
} fb_layer_config_t;

typedef struct fb_layer_info {
    uint32_t size;
    uint16_t layer_id;
    uint16_t owned;
    uintptr_t fb_user_va;
    uintptr_t metadata_user_va;
    uint32_t fb_size;
    uint32_t metadata_size;
    fb_layer_config_t cfg;
} fb_layer_info_t;

typedef struct fb_layer_metadata {
    uint8_t  ready;
    uint16_t dx0;
    uint16_t dx1;
    uint16_t dy0;
    uint16_t dy1;
    uint32_t frame_id;
} fb_layer_metadata_t;

/**
 * @brief Execute a system call via interrupt 0x80
 * 
 * Low-level function that performs the actual system call by triggering
 * interrupt 0x80 with the appropriate register values.
 * 
 * @param num System call number
 * @param arg1 First argument
 * @param arg2 Second argument
 * @param arg3 Third argument
 * @return System call return value, or sets errno and returns error code on failure
 */
static inline int do_syscall(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    register uint32_t eax asm("eax") = num;
    register uint32_t ebx asm("ebx") = arg1;
    register uint32_t ecx asm("ecx") = arg2;
    register uint32_t edx asm("edx") = arg3;

    asm volatile("int $0x80"
                 : "+a"(eax)
                 : "b"(ebx), "c"(ecx), "d"(edx)
                 : "memory");

    if ((int)eax < 0) {
        errno = -(int)eax;
        return errno;
    }
    return eax;
}

int sys_5ht_list_processes(proc_5ht_t *buf, size_t max);
int sys_5ht_req_buf(uint16_t id, const fb_layer_config_t *cfg, fb_layer_info_t *out);
int sys_5ht_rel_buf(uint16_t id);
int sys_5ht_rcfg_layer(uint16_t id, const fb_layer_config_t *cfg, fb_layer_info_t *out);
int sys_5ht_query_info(fb_info_t *out);
int sys_5ht_query_layer(uint16_t id, fb_layer_info_t *out);

#ifdef __cplusplus
}
#endif

#endif
