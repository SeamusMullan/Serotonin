#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "syscall/lib5ht/lib5ht.h"

#define BYTES_PER_PIXEL 4

#define LAYER_WIDTH  1280
#define LAYER_HEIGHT 800
#define LAYER_X0     0
#define LAYER_Y0     0

#define STRIDE_BYTES (LAYER_WIDTH * BYTES_PER_PIXEL)

#define CURSOR_WIDTH  16
#define CURSOR_HEIGHT 16

#define COLOR_TRANSPARENT 0x00000000
#define COLOR_CURSOR      0xFFFFFFFF
#define COLOR_CURSOR_BORDER 0xFF000000

void fill_rect(uint32_t *fb, uint32_t stride_pixels, int x, int y, int w, int h, uint32_t color) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)LAYER_WIDTH) w = LAYER_WIDTH - x;
    if (y + h > (int)LAYER_HEIGHT) h = LAYER_HEIGHT - y;
    if (w <= 0 || h <= 0) return;

    uint32_t *row = fb + y * stride_pixels + x;
    for (int yy = 0; yy < h; yy++) {
        uint32_t *p = row;
        for (int xx = 0; xx < w; xx++) {
            *p++ = color;
        }
        row += stride_pixels;
    }
}

void draw_cursor(uint32_t *fb, uint32_t stride_pixels, int x, int y) {
    fill_rect(fb, stride_pixels, x, y, CURSOR_WIDTH, CURSOR_HEIGHT, COLOR_CURSOR_BORDER);
    fill_rect(fb, stride_pixels, x + 1, y + 1, CURSOR_WIDTH - 2, CURSOR_HEIGHT - 2, COLOR_CURSOR);
}

void clear_layer(uint32_t *fb, uint32_t stride_pixels) {
    for (uint32_t y = 0; y < LAYER_HEIGHT; y++) {
        uint32_t *row = fb + y * stride_pixels;
        for (uint32_t x = 0; x < LAYER_WIDTH; x++) {
            row[x] = COLOR_TRANSPARENT;
        }
    }
}

int parse_mouse_pos(const char *str, int *x, int *y) {
    char *comma = strchr(str, ',');
    if (!comma) return -1;

    *x = atoi(str);
    *y = atoi(comma + 1);
    return 0;
}

int read_mouse_pos(int fd, int *x, int *y) {
    mouse_event_t ev;

    int n = read(fd, &ev, sizeof(ev));
    if (n <= 0) return -1;

    *x = ev.x;
    *y = ev.y;
    return 0;
}

int main(void) {
    int mouse_fd = open("/dev/mouse/event", O_RDONLY);
    if (mouse_fd < 0) {
        printf("Failed to open /dev/mouse/event\n");
        return 1;
    }

    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = LAYER_X0;
    cfg.y0 = LAYER_Y0;
    cfg.x1 = LAYER_X0 + LAYER_WIDTH;
    cfg.y1 = LAYER_Y0 + LAYER_HEIGHT;
    cfg.alpha = 0;
    cfg.stride = STRIDE_BYTES;

    fb_layer_info_t info = {0};
    if (sys_5ht_req_buf(2, &cfg, &info) != 0) {
        printf("Failed to allocate framebuffer layer\n");
        close(mouse_fd);
        return 1;
    }

    uint32_t *fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    volatile fb_layer_metadata_t *meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;

    const uint32_t stride_pixels = STRIDE_BYTES / BYTES_PER_PIXEL;
    uint32_t frame_id = 1;
    int last_x = -1, last_y = -1;

    clear_layer(fb, stride_pixels);

    while (1) {
        int mouse_x, mouse_y;

        if (read_mouse_pos(mouse_fd, &mouse_x, &mouse_y) < 0) {
            continue;
        }

        if (mouse_x != last_x || mouse_y != last_y) {
            if (last_x >= 0 && last_y >= 0) {
                fill_rect(fb, stride_pixels, last_x, last_y, CURSOR_WIDTH, CURSOR_HEIGHT, COLOR_TRANSPARENT);
            }

            draw_cursor(fb, stride_pixels, mouse_x, mouse_y);

            int dirty_x0 = (last_x >= 0) ? ((mouse_x < last_x) ? mouse_x : last_x) : mouse_x;
            int dirty_y0 = (last_y >= 0) ? ((mouse_y < last_y) ? mouse_y : last_y) : mouse_y;
            int dirty_x1 = (last_x >= 0) ? ((mouse_x > last_x) ? mouse_x : last_x) : mouse_x;
            int dirty_y1 = (last_y >= 0) ? ((mouse_y > last_y) ? mouse_y : last_y) : mouse_y;
            dirty_x1 += CURSOR_WIDTH;
            dirty_y1 += CURSOR_HEIGHT;

            if (dirty_x0 < 0) dirty_x0 = 0;
            if (dirty_y0 < 0) dirty_y0 = 0;
            if (dirty_x1 > (int)LAYER_WIDTH) dirty_x1 = LAYER_WIDTH;
            if (dirty_y1 > (int)LAYER_HEIGHT) dirty_y1 = LAYER_HEIGHT;

            meta->dx0 = dirty_x0;
            meta->dx1 = dirty_x1;
            meta->dy0 = dirty_y0;
            meta->dy1 = dirty_y1;
            meta->frame_id = frame_id++;
            meta->ready = 1;

            while (meta->ready) {
                pause();
            }

            last_x = mouse_x;
            last_y = mouse_y;
        }
    }

    sys_5ht_rel_buf(2);
    close(mouse_fd);
    return 0;
}
