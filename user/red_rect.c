#include <stdint.h>
#include <unistd.h>
#include <lib5ht.h>

#define BYTES_PER_PIXEL 4
#define LAYER_ID 2
#define LAYER_X0 100
#define LAYER_Y0 100
#define LAYER_W 100
#define LAYER_H 100
#define LAYER_STRIDE (LAYER_W * BYTES_PER_PIXEL)

int main(void) {
    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = LAYER_X0;
    cfg.y0 = LAYER_Y0;
    cfg.x1 = LAYER_X0 + LAYER_W;
    cfg.y1 = LAYER_Y0 + LAYER_H;
    cfg.alpha = FB_LAYER_ALPHA_OPAQUE;
    cfg.hints = FB_LAYER_HINT_OPAQUE_CONTENT | FB_LAYER_HINT_STATIC_CONTENT;
    cfg.stride = LAYER_STRIDE;

    fb_layer_info_t info = {0};
    if (sys_5ht_req_buf(LAYER_ID, &cfg, &info) != 0) {
        return 1;
    }

    uint32_t *fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    volatile fb_layer_metadata_t *meta =
        (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;

    for (uint32_t y = 0; y < LAYER_H; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)fb + y * LAYER_STRIDE);
        for (uint32_t x = 0; x < LAYER_W; x++) {
            row[x] = 0xFFFF0000u;
        }
    }

    meta->dx0 = 0;
    meta->dy0 = 0;
    meta->dx1 = LAYER_W;
    meta->dy1 = LAYER_H;
    meta->frame_id++;
    meta->ready = 1;

    //sleep(5);
    sys_5ht_rel_buf(LAYER_ID);
    return 0;
}
