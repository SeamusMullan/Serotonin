#ifndef _KERNEL_LIB5HT
#define _KERNEL_LIB5HT

#include <stdint.h>
#include <stddef.h>

typedef struct proc_5ht {
    int pid;
    char name[32];
    int priority;
    int priv;
} proc_5ht_t;

typedef struct sysinfo_5ht {
    uint32_t mem_free;
    uint32_t mem_total;
    uint32_t cpu_used;
    uint32_t cpu_free;
} sysinfo_5ht_t;

typedef struct fb_info {
    uint32_t size;
    uint32_t fb_size;
    uint32_t layer_window_size;
    uint32_t metadata_size;
    uint32_t alignment;
} fb_info_t;

#define FB_LAYER_ALPHA_OPAQUE 0
#define FB_LAYER_ALPHA_BLEND  1

#define FB_LAYER_HINT_NONE              0x0000u
#define FB_LAYER_HINT_OPAQUE_CONTENT    0x0001u
#define FB_LAYER_HINT_STATIC_CONTENT    0x0002u
#define FB_LAYER_HINT_FREQUENT_UPDATES  0x0004u
#define FB_LAYER_HINT_CURSOR_SPRITE     0x0008u
#define FB_LAYER_HINT_TRANSIENT         0x0010u
#define FB_LAYER_HINT_ALL_MASK          0x001Fu

typedef struct fb_layer_config {
    uint32_t size;
    uint16_t x0;
    uint16_t x1;
    uint16_t y0;
    uint16_t y1;
    uint8_t  alpha;
    uint16_t stride;
    uint16_t hints;
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

#endif
