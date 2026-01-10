/**
 * @file fb_layer_test.c
 * @brief Framebuffer layer animation test program
 *
 * Demonstrates the 5HT framebuffer layer system by creating a layer,
 * drawing animated rectangles, and using double-buffering with
 * the compositor for smooth animation.
 */

#include <stdint.h>
#include "syscall/lib5ht/lib5ht.h"

/** @brief Bytes per pixel (32-bit ARGB) */
#define BYTES_PER_PIXEL 4
/** @brief Layer width in pixels */
#define LAYER_WIDTH 480
/** @brief Layer height in pixels */
#define LAYER_HEIGHT 300
/** @brief Layer X position on screen */
#define LAYER_X0 200
/** @brief Layer Y position on screen */
#define LAYER_Y0 150
/** @brief Stride in bytes per row */
#define STRIDE_BYTES (LAYER_WIDTH * BYTES_PER_PIXEL)

/**
 * @brief Fill a rectangular region with a solid color
 *
 * @param fb Pointer to framebuffer
 * @param stride_pixels Stride in pixels (not bytes)
 * @param x X position of rectangle
 * @param y Y position of rectangle
 * @param w Width of rectangle
 * @param h Height of rectangle
 * @param color 32-bit ARGB color value
 */
static void fill_rect(uint32_t *fb, uint32_t stride_pixels, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    uint32_t *row = fb + y * stride_pixels + x;
    for (uint32_t yy = 0; yy < h; yy++) {
        uint32_t *p = row;
        for (uint32_t xx = 0; xx < w; xx++) {
            *p++ = color;
        }
        row += stride_pixels;
    }
}

/**
 * @brief Clear the entire framebuffer with a solid color
 *
 * @param fb Pointer to framebuffer
 * @param stride_pixels Stride in pixels (not bytes)
 * @param width Width of framebuffer in pixels
 * @param height Height of framebuffer in pixels
 * @param color 32-bit ARGB color value
 */
static void clear_fb(uint32_t *fb, uint32_t stride_pixels, uint32_t width, uint32_t height, uint32_t color) {
    for (uint32_t y = 0; y < height; y++) {
        uint32_t *row = fb + y * stride_pixels;
        for (uint32_t x = 0; x < width; x++) {
            row[x] = color;
        }
    }
}

/**
 * @brief Main entry point for framebuffer layer test
 *
 * Creates a framebuffer layer, renders 300 frames of animation
 * showing a bouncing rectangle, then releases the layer.
 *
 * @return 0 on success, 1 on layer allocation failure
 */
int main(void) {
    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = LAYER_X0;
    cfg.y0 = LAYER_Y0;
    cfg.x1 = LAYER_X0 + LAYER_WIDTH;
    cfg.y1 = LAYER_Y0 + LAYER_HEIGHT;
    cfg.alpha = 1;
    cfg.stride = STRIDE_BYTES;

    fb_layer_info_t info = {0};
    if (sys_5ht_req_buf(1, &cfg, &info) != 0) {
        return 1;
    }

    uint32_t *fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    volatile fb_layer_metadata_t *meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;

    const uint32_t rect_w = 120;
    const uint32_t rect_h = 90;
    const uint32_t stride_pixels = STRIDE_BYTES / BYTES_PER_PIXEL;
    uint32_t frame_id = 1;

    for (uint32_t frame = 0; frame < 300; frame++) {
        uint32_t x = (frame * 4) % (LAYER_WIDTH - rect_w);
        uint32_t y = (frame * 2) % (LAYER_HEIGHT - rect_h);

        clear_fb(fb, stride_pixels, LAYER_WIDTH, LAYER_HEIGHT, 0xAA202020);
        fill_rect(fb, stride_pixels, x, y, rect_w, rect_h, 0xAAFFAA00);

        meta->dx0 = 0;
        meta->dx1 = LAYER_WIDTH;
        meta->dy0 = 0;
        meta->dy1 = LAYER_HEIGHT;
        meta->frame_id = frame_id++;
        meta->ready = 1;

        while (meta->ready) {
            pause();
        }
    }

    sys_5ht_rel_buf(1);
    return 0;
}
