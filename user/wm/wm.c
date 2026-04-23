#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include "wm.h"
#include "wm_ipc.h"
#include "../syscall/sys/poll.h"

int waitpid(pid_t pid, int *status);
int snprintf(char *str, size_t size, const char *fmt, ...);
int listdir(const char *path, char *buf, size_t size);

/* --- Cursor bitmap (16x16 arrow) --- */
static const uint8_t cursor_bitmap[16][16] = {
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,1,0,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,1,0,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,1,0,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,1,0,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,2,2,1,0,0,0,0,0,0,0},
    {1,2,2,2,2,2,1,1,1,1,0,0,0,0,0,0},
    {1,2,2,1,2,2,1,0,0,0,0,0,0,0,0,0},
    {1,2,1,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,1,0,0,1,2,2,1,0,0,0,0,0,0,0,0},
    {1,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,1,2,2,1,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,0},
};

#define CURSOR_W 16
#define CURSOR_H 16

static wm_state_t wm;
wm_theme_t g_wm_theme;
static uint32_t blink_counter = 0;
static uint8_t  cursor_blink_on = 1;
#define WM_POLL_TIMEOUT_MS 8
#define BLINK_INTERVAL 31  /* ~WM_POLL_TIMEOUT_MS * 31 ~= 248ms */

static const wm_theme_t wm_themes[WM_THEME_COUNT] = {
    { "Default",        0xFF1A1A2E, 0xFF252540, 0xFF7A98FF, 0xFF4A6099, 0xFFE0E0E0, 0xFF808090, 0xFF3A3A50, 0xFFFF4040, 0xFFFFFFFF, 0xFF7A98FF, 0xFF404050, 0xFF7A98FF, 0xFF303040, 0xFFE0E0E0, 0xFF1A1A2E },
    { "Dracula",        0xFF282A36, 0xFF44475A, 0xFFBD93F9, 0xFF6D5FA0, 0xFFF8F8F2, 0xFF6272A4, 0xFF44475A, 0xFFFF5555, 0xFFF8F8F2, 0xFFBD93F9, 0xFF44475A, 0xFFBD93F9, 0xFF383A4A, 0xFFF8F8F2, 0xFF282A36 },
    { "Monokai",        0xFF272822, 0xFF3E3D32, 0xFFA6E22E, 0xFF6D8F1E, 0xFFF8F8F2, 0xFF75715E, 0xFF3E3D32, 0xFFF92672, 0xFF272822, 0xFFA6E22E, 0xFF3E3D32, 0xFFA6E22E, 0xFF3E3D32, 0xFFF8F8F2, 0xFF272822 },
    { "Nord",           0xFF2E3440, 0xFF3B4252, 0xFF88C0D0, 0xFF5E81AC, 0xFFECEFF4, 0xFF4C566A, 0xFF3B4252, 0xFFBF616A, 0xFF2E3440, 0xFF88C0D0, 0xFF3B4252, 0xFF88C0D0, 0xFF3B4252, 0xFFECEFF4, 0xFF2E3440 },
    { "Gruvbox",        0xFF282828, 0xFF3C3836, 0xFFFE8019, 0xFFD65D0E, 0xFFFBF1C7, 0xFF928374, 0xFF3C3836, 0xFFCC241D, 0xFF282828, 0xFFFE8019, 0xFF3C3836, 0xFFFE8019, 0xFF3C3836, 0xFFFBF1C7, 0xFF282828 },
    { "Solarized Dark", 0xFF002B36, 0xFF073642, 0xFF268BD2, 0xFF2176A7, 0xFF93A1A1, 0xFF586E75, 0xFF073642, 0xFFDC322F, 0xFFFDF6E3, 0xFF268BD2, 0xFF073642, 0xFF268BD2, 0xFF073642, 0xFF93A1A1, 0xFF002B36 },
    { "Rose Pine",      0xFF191724, 0xFF26233A, 0xFFC4A7E7, 0xFF6E6A86, 0xFFE0DEF4, 0xFF908CAA, 0xFF403D52, 0xFFEB6F92, 0xFF191724, 0xFFC4A7E7, 0xFF403D52, 0xFFC4A7E7, 0xFF26233A, 0xFFE0DEF4, 0xFF191724 },
};

static const char *wallpaper_names[WM_WALLPAPER_COUNT] = {
    "Solid", "Static", "Grid", "Diamonds",
    "Checkerboard", "Gradient", "Rings", "Plasma"
};

static const int8_t sin_lut[256] = {
    0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46,
    49, 51, 54, 57, 60, 63, 65, 68, 71, 73, 76, 78, 81, 83, 85, 88,
    90, 92, 94, 96, 98, 100, 102, 104, 106, 107, 109, 111, 112, 113, 115, 116,
    117, 118, 120, 121, 122, 122, 123, 124, 125, 125, 126, 126, 126, 127, 127, 127,
    127, 127, 127, 127, 126, 126, 126, 125, 125, 124, 123, 122, 122, 121, 120, 118,
    117, 116, 115, 113, 112, 111, 109, 107, 106, 104, 102, 100, 98, 96, 94, 92,
    90, 88, 85, 83, 81, 78, 76, 73, 71, 68, 65, 63, 60, 57, 54, 51,
    49, 46, 43, 40, 37, 34, 31, 28, 25, 22, 19, 16, 12, 9, 6, 3,
    0, -3, -6, -9, -12, -16, -19, -22, -25, -28, -31, -34, -37, -40, -43, -46,
    -49, -51, -54, -57, -60, -63, -65, -68, -71, -73, -76, -78, -81, -83, -85, -88,
    -90, -92, -94, -96, -98, -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116,
    -117, -118, -120, -121, -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127,
    -127, -127, -127, -127, -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118,
    -117, -116, -115, -113, -112, -111, -109, -107, -106, -104, -102, -100, -98, -96, -94, -92,
    -90, -88, -85, -83, -81, -78, -76, -73, -71, -68, -65, -63, -60, -57, -54, -51,
    -49, -46, -43, -40, -37, -34, -31, -28, -25, -22, -19, -16, -12, -9, -6, -3,
};

static uint32_t wp_hash(uint32_t x, uint32_t y) {
    uint32_t h = x * 374761393u ^ y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static uint32_t color_lerp(uint32_t c1, uint32_t c2, int t) {
    if (t <= 0) return c1;
    if (t >= 255) return c2;
    int r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    int r2 = (c2 >> 16) & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = c2 & 0xFF;
    int r = r1 + ((r2 - r1) * t >> 8);
    int g = g1 + ((g2 - g1) * t >> 8);
    int b = b1 + ((b2 - b1) * t >> 8);
    return 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static int isqrt(uint32_t n) {
    if (n == 0) return 0;
    uint32_t x = n, y = (x + 1) >> 1;
    while (y < x) { x = y; y = (x + n / x) >> 1; }
    return (int)x;
}

static void overlay_reconfigure_alpha(wm_state_t *wm) {
    fb_layer_config_t cfg = {0};
    fb_layer_info_t info = {0};
    cfg.size = sizeof(cfg);
    cfg.alpha = 0;
    cfg.hints = FB_LAYER_HINT_OPAQUE_CONTENT |
                FB_LAYER_HINT_FREQUENT_UPDATES |
                FB_LAYER_HINT_TRANSIENT;

    if (wm->launcher_active) {
        int lx = (SCREEN_W - LAUNCHER_W) / 2;
        int ly = (SCREEN_H - LAUNCHER_H) / 2;
        cfg.x0 = lx; cfg.y0 = ly;
        cfg.x1 = lx + LAUNCHER_W; cfg.y1 = ly + LAUNCHER_H;
        cfg.stride = LAUNCHER_W * BPP;
        if (sys_5ht_rcfg_layer(LAYER_LAUNCHER, &cfg, &info) == 0) {
            wm->launcher_fb = (uint32_t *)(uintptr_t)info.fb_user_va;
            wm->launcher_meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;
        }
    }

    if (wm->settings_active) {
        int sx = SCREEN_W - SETTINGS_W - 8;
        int sh = SETTINGS_POPUP_H;
        int sy = SCREEN_H - TASKBAR_H - sh;
        cfg.x0 = sx; cfg.y0 = sy;
        cfg.x1 = sx + SETTINGS_W; cfg.y1 = sy + sh;
        cfg.stride = SETTINGS_W * BPP;
        if (sys_5ht_rcfg_layer(LAYER_LAUNCHER, &cfg, &info) == 0) {
            wm->settings_fb = (uint32_t *)(uintptr_t)info.fb_user_va;
            wm->settings_meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;
        }
    }
}

/* --- palette init --- */

/**
 * Initialize the color palette for the window manager.
 *
 * This populates the `palette` array with standard VGA colors,
 * a 6×6×6 color cube, and a grayscale ramp.
 *
 * @param wm Pointer to the window manager state structure.
 */
void wm_init_palette(wm_state_t *wm) {
    /* 0-15: standard colors */
    for (int i = 0; i < 16; i++)
        wm->palette[i] = vbe_base_colors[i];
    /* 16-231: 6x6x6 cube */
    int idx = 16;
    for (int r = 0; r < 6; r++)
        for (int g = 0; g < 6; g++)
            for (int b = 0; b < 6; b++) {
                uint8_t rr = (r == 0) ? 0 : 55 + r * 40;
                uint8_t gg = (g == 0) ? 0 : 55 + g * 40;
                uint8_t bb = (b == 0) ? 0 : 55 + b * 40;
                wm->palette[idx++] = ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | bb;
            }
    /* 232-255: grayscale */
    for (int i = 0; i < 24; i++) {
        uint8_t lv = 8 + i * 10;
        wm->palette[idx++] = ((uint32_t)lv << 16) | ((uint32_t)lv << 8) | lv;
    }
}

/* --- cursor layer --- */

/**
 * Initialize the cursor layer.
 *
 * Sets up a framebuffer layer for the mouse cursor, draws the cursor bitmap,
 * and submits the initial frame to the compositor.
 *
 * @param wm Pointer to the window manager state.
 */
static void init_cursor_layer(wm_state_t *wm) {
    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = 0; cfg.y0 = 0;
    cfg.x1 = CURSOR_W; cfg.y1 = CURSOR_H;
    cfg.alpha = 1;
    cfg.hints = FB_LAYER_HINT_CURSOR_SPRITE |
                FB_LAYER_HINT_FREQUENT_UPDATES |
                FB_LAYER_HINT_TRANSIENT;
    cfg.stride = CURSOR_W * BPP;

    fb_layer_info_t info = {0};
    if (sys_5ht_req_buf(LAYER_CURSOR, &cfg, &info) != 0) return;

    wm->cursor_layer = LAYER_CURSOR;
    wm->cursor_fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    wm->cursor_meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;

    /* draw cursor bitmap */
    for (int y = 0; y < CURSOR_H; y++) {
        for (int x = 0; x < CURSOR_W; x++) {
            uint32_t color;
            switch (cursor_bitmap[y][x]) {
            case 1: color = 0xFF000000; break; /* black border */
            case 2: color = 0xFFFFFFFF; break; /* white fill */
            default: color = 0x00000000; break; /* transparent */
            }
            wm->cursor_fb[y * CURSOR_W + x] = color;
        }
    }

    /* submit initial frame */
    wm->cursor_meta->dx0 = 0; wm->cursor_meta->dy0 = 0;
    wm->cursor_meta->dx1 = CURSOR_W; wm->cursor_meta->dy1 = CURSOR_H;
    wm->cursor_meta->frame_id = 1;
    wm->cursor_meta->ready = 1;
}

/**
 * Move the cursor to a new position.
 *
 * Reconfigures the cursor layer coordinates and marks the layer dirty
 * so that the compositor will redraw the cursor at the new location.
 *
 * @param wm Pointer to the window manager state.
 * @param x  New X coordinate for the cursor.
 * @param y  New Y coordinate for the cursor.
 */
static void move_cursor(wm_state_t *wm, int x, int y) {
    static int prev_x = -1;
    static int prev_y = -1;
    int old_x = prev_x;
    int old_y = prev_y;

    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = (uint16_t)x;
    cfg.y0 = (uint16_t)y;
    cfg.x1 = (uint16_t)(x + CURSOR_W);
    cfg.y1 = (uint16_t)(y + CURSOR_H);
    cfg.alpha = 1;
    cfg.hints = FB_LAYER_HINT_CURSOR_SPRITE |
                FB_LAYER_HINT_FREQUENT_UPDATES |
                FB_LAYER_HINT_TRANSIENT;
    cfg.stride = CURSOR_W * BPP;

    fb_layer_info_t info = {0};
    if (sys_5ht_rcfg_layer(LAYER_CURSOR, &cfg, &info) != 0) return;

    /* update pointers in case kernel remapped */
    wm->cursor_fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    wm->cursor_meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;

    /* mark dirty so compositor redraws at new position */
    wm->cursor_meta->dx0 = 0; wm->cursor_meta->dy0 = 0;
    wm->cursor_meta->dx1 = CURSOR_W; wm->cursor_meta->dy1 = CURSOR_H;
    wm->cursor_meta->frame_id++;
    wm->cursor_meta->ready = 1;

    /* Hint desktop damage for old/new cursor extents so underlying content is recomposited. */
    if (old_x >= 0 && old_y >= 0)
        desktop_mark_dirty(wm, (uint16_t)old_x, (uint16_t)old_y, CURSOR_W, CURSOR_H);
    if (x >= 0 && y >= 0)
        desktop_mark_dirty(wm, (uint16_t)x, (uint16_t)y, CURSOR_W, CURSOR_H);

    prev_x = x;
    prev_y = y;
}

/* --- taskbar --- */

/**
 * Initialize the taskbar layer.
 *
 * Allocates a framebuffer layer for the taskbar, sets its geometry
 * to the bottom of the screen and prepares it for rendering.
 *
 * @param wm Pointer to the window manager state.
 */
static void init_taskbar(wm_state_t *wm) {
    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = 0;
    cfg.y0 = SCREEN_H - TASKBAR_H;
    cfg.x1 = SCREEN_W;
    cfg.y1 = SCREEN_H;
    cfg.alpha = 0;
    cfg.hints = FB_LAYER_HINT_OPAQUE_CONTENT | FB_LAYER_HINT_STATIC_CONTENT;
    cfg.stride = SCREEN_W * BPP;

    fb_layer_info_t info = {0};
    if (sys_5ht_req_buf(LAYER_TASKBAR, &cfg, &info) != 0) return;

    wm->taskbar_layer = LAYER_TASKBAR;
    wm->taskbar_fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    wm->taskbar_meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;
}

/* --- wallpaper rendering --- */

void wm_render_wallpaper(wm_state_t *wm) {
    if (!wm->desktop_fb) return;
    uint32_t bg = THEME_BG_DARK;
    uint32_t med = THEME_BG_MEDIUM;
    uint32_t acc = THEME_ACCENT_DIM;

    switch (wm->wallpaper_current) {
    case 0: /* Solid */
        draw_fill_rect(wm->desktop_fb, SCREEN_W, 0, 0, SCREEN_W, SCREEN_H, bg);
        break;
    case 1: { /* Static — grayscale noise */
        int bgr = (bg >> 16) & 0xFF, bgg = (bg >> 8) & 0xFF, bgb = bg & 0xFF;
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < SCREEN_W; x++) {
                int v = (int)(wp_hash(x, y) & 0x3F) - 32;
                int r = bgr + v, g = bgg + v, b = bgb + v;
                if (r < 0) r = 0; if (r > 255) r = 255;
                if (g < 0) g = 0; if (g > 255) g = 255;
                if (b < 0) b = 0; if (b > 255) b = 255;
                wm->desktop_fb[y * SCREEN_W + x] =
                    0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        break;
    }
    case 2: /* Grid */
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < SCREEN_W; x++)
                wm->desktop_fb[y * SCREEN_W + x] =
                    ((x % 32 == 0) || (y % 32 == 0)) ? med : bg;
        break;
    case 3: { /* Diamonds — Manhattan distance from center */
        int cx = SCREEN_W / 2, cy = SCREEN_H / 2;
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < SCREEN_W; x++) {
                int dx = x - cx; if (dx < 0) dx = -dx;
                int dy = y - cy; if (dy < 0) dy = -dy;
                int t = ((dx + dy) % 80) * 255 / 80;
                wm->desktop_fb[y * SCREEN_W + x] = color_lerp(bg, acc, t);
            }
        break;
    }
    case 4: /* Checkerboard */
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < SCREEN_W; x++)
                wm->desktop_fb[y * SCREEN_W + x] =
                    (((x >> 5) + (y >> 5)) & 1) ? bg : med;
        break;
    case 5: /* Gradient — vertical blend */
        for (int y = 0; y < SCREEN_H; y++) {
            int t = y * 255 / SCREEN_H;
            uint32_t c = color_lerp(bg, med, t);
            for (int x = 0; x < SCREEN_W; x++)
                wm->desktop_fb[y * SCREEN_W + x] = c;
        }
        break;
    case 6: { /* Rings — concentric circles from center */
        int cx = SCREEN_W / 2, cy = SCREEN_H / 2;
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < SCREEN_W; x++) {
                int dx = x - cx, dy = y - cy;
                int d = isqrt((uint32_t)(dx * dx + dy * dy));
                int t = (d % 60) * 255 / 60;
                wm->desktop_fb[y * SCREEN_W + x] = color_lerp(bg, med, t);
            }
        break;
    }
    case 7: { /* Plasma — sine wave interference */
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < SCREEN_W; x++) {
                int v1 = sin_lut[(x * 2 + 17) & 0xFF];
                int v2 = sin_lut[(y * 3 + 41) & 0xFF];
                int v3 = sin_lut[((x + y * 2) >> 1) & 0xFF];
                int v4 = sin_lut[((x * 5 + y * 3) >> 2) & 0xFF];
                int v = (v1 + v2 + v3 + v4 + 508) * 255 / 1016;
                wm->desktop_fb[y * SCREEN_W + x] = color_lerp(bg, acc, v);
            }
        break;
    }
    default:
        draw_fill_rect(wm->desktop_fb, SCREEN_W, 0, 0, SCREEN_W, SCREEN_H, bg);
        break;
    }
    desktop_mark_dirty(wm, 0, 0, SCREEN_W, SCREEN_H);
}

void wm_apply_wallpaper(wm_state_t *wm, int wp_idx) {
    if (wp_idx < 0 || wp_idx >= WM_WALLPAPER_COUNT) return;
    wm->wallpaper_current = wp_idx;
    wm_render_wallpaper(wm);
    if (wm->settings_active)
        settings_render(wm);
}

/* --- desktop background layer --- */

/**
 * Initialize the desktop background layer.
 *
 * Creates a fullscreen framebuffer layer, fills it with the background
 * color, and submits the initial frame.
 *
 * @param wm Pointer to the window manager state.
 */
static void init_desktop(wm_state_t *wm) {
    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = 0; cfg.y0 = 0;
    cfg.x1 = SCREEN_W; cfg.y1 = SCREEN_H;
    cfg.alpha = 0;
    cfg.hints = FB_LAYER_HINT_OPAQUE_CONTENT | FB_LAYER_HINT_STATIC_CONTENT;
    cfg.stride = SCREEN_W * BPP;

    fb_layer_info_t info = {0};
    if (sys_5ht_req_buf(LAYER_DESKTOP, &cfg, &info) != 0) return;

    wm->desktop_fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    wm->desktop_meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;

    wm_render_wallpaper(wm);

    /* Reset desktop dirty tracking */
    wm->desk_dirty_x0 = SCREEN_W;
    wm->desk_dirty_y0 = SCREEN_H;
    wm->desk_dirty_x1 = 0;
    wm->desk_dirty_y1 = 0;

    /* Submit full desktop as first frame */
    wm->desktop_meta->dx0 = 0;
    wm->desktop_meta->dy0 = 0;
    wm->desktop_meta->dx1 = SCREEN_W;
    wm->desktop_meta->dy1 = SCREEN_H;
    wm->desktop_meta->frame_id = 1;
    wm->desktop_meta->ready = 1;
}

/**
 * Mark a region of the desktop as dirty.
 *
 * The compositor will redraw only the specified rectangle.
 *
 * @param wm Pointer to the window manager state.
 * @param x  X coordinate of the rectangle.
 * @param y  Y coordinate of the rectangle.
 * @param w  Width of the rectangle.
 * @param h  Height of the rectangle.
 */
void desktop_mark_dirty(wm_state_t *wm, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    uint16_t x1 = x + w;
    uint16_t y1 = y + h;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    if (x < wm->desk_dirty_x0) wm->desk_dirty_x0 = x;
    if (y < wm->desk_dirty_y0) wm->desk_dirty_y0 = y;
    if (x1 > wm->desk_dirty_x1) wm->desk_dirty_x1 = x1;
    if (y1 > wm->desk_dirty_y1) wm->desk_dirty_y1 = y1;
}

/**
 * Submit any pending desktop updates to the compositor.
 *
 * If the desktop layer is not ready or there are no dirty regions,
 * the function returns early. Otherwise it updates the dirty rectangle
 * metadata and marks the layer ready.
 *
 * @param wm Pointer to the window manager state.
 */
void desktop_submit(wm_state_t *wm) {
    if (!wm->desktop_meta) return;
    if (wm->desktop_meta->ready) return;
    if (wm->desk_dirty_x0 >= wm->desk_dirty_x1 ||
        wm->desk_dirty_y0 >= wm->desk_dirty_y1) return;

    wm->desktop_meta->dx0 = wm->desk_dirty_x0;
    wm->desktop_meta->dy0 = wm->desk_dirty_y0;
    wm->desktop_meta->dx1 = wm->desk_dirty_x1;
    wm->desktop_meta->dy1 = wm->desk_dirty_y1;
    wm->desktop_meta->frame_id++;
    wm->desktop_meta->ready = 1;

    wm->desk_dirty_x0 = SCREEN_W;
    wm->desk_dirty_y0 = SCREEN_H;
    wm->desk_dirty_x1 = 0;
    wm->desk_dirty_y1 = 0;
}

/* --- taskbar --- */

/**
 * Render the taskbar.
 *
 * Draws the background, border, window buttons, and the static brand label.
 * Submits the frame to the compositor.
 *
 * @param wm Pointer to the window manager state.
 */
static void render_taskbar(wm_state_t *wm) {
    if (!wm->taskbar_fb) return;

    uint32_t stride = SCREEN_W;
    /* background */
    draw_fill_rect(wm->taskbar_fb, stride, 0, 0, SCREEN_W, TASKBAR_H, THEME_BG_MEDIUM);

    /* top border line */
    draw_fill_rect(wm->taskbar_fb, stride, 0, 0, SCREEN_W, 1, THEME_BORDER);

    /* window buttons */
    int btn_x = 4;
    int btn_y = (TASKBAR_H - FONT_H) / 2 + 1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!wm->windows[i].active) continue;

        uint32_t bg = (i == wm->focused_idx) ? THEME_ACCENT : THEME_BG_DARK;
        uint32_t fg = (i == wm->focused_idx) ? 0xFF000000 : THEME_TEXT_DIM;

        int btn_w = 120;
        draw_fill_rect(wm->taskbar_fb, stride, btn_x, 2, btn_w, TASKBAR_H - 3, bg);

        /* title text (truncated) */
        char label[16];
        strncpy(label, wm->windows[i].title, 14);
        label[14] = '\0';
        draw_text(wm->taskbar_fb, stride, btn_x + 4, btn_y, label, fg, bg);

        btn_x += btn_w + 4;
    }

    /* right side: "Serotonin" label */
    const char *brand = "Serotonin";
    int brand_x = SCREEN_W - (int)strlen(brand) * FONT_W - 8;
    draw_text(wm->taskbar_fb, stride, brand_x, btn_y, brand, THEME_ACCENT, THEME_BG_MEDIUM);

    /* settings button (hamburger icon) left of brand */
    int sbtn_x = brand_x - SETTINGS_BTN_W - 8;
    int sbtn_y = (TASKBAR_H - SETTINGS_BTN_H) / 2 + 1;
    uint32_t sbtn_bg = wm->settings_active ? THEME_ACCENT_DIM : THEME_BG_DARK;
    draw_fill_rect(wm->taskbar_fb, stride, sbtn_x, sbtn_y,
                   SETTINGS_BTN_W, SETTINGS_BTN_H, sbtn_bg);
    uint32_t line_color = wm->settings_active ? 0xFFFFFFFF : THEME_TEXT_PRIMARY;
    int line_w = 12;
    int line_x = sbtn_x + (SETTINGS_BTN_W - line_w) / 2;
    draw_fill_rect(wm->taskbar_fb, stride, line_x, sbtn_y + 3, line_w, 2, line_color);
    draw_fill_rect(wm->taskbar_fb, stride, line_x, sbtn_y + 8, line_w, 2, line_color);
    draw_fill_rect(wm->taskbar_fb, stride, line_x, sbtn_y + 13, line_w, 2, line_color);

    /* submit frame */
    wm->taskbar_meta->dx0 = 0; wm->taskbar_meta->dy0 = 0;
    wm->taskbar_meta->dx1 = SCREEN_W; wm->taskbar_meta->dy1 = TASKBAR_H;
    wm->taskbar_meta->frame_id++;
    wm->taskbar_meta->ready = 1;
}

/* --- program launcher --- */

static const char *launcher_filter[] = {
    "init", "getty", "seriald", "lwipd", "wm", NULL
};

static int is_gui_launch_name(const char *name) {
    if (!name)
        return 0;
    if (strcmp(name, "gooey_demo") == 0)
        return 1;
    if (strcmp(name, "widget_demo") == 0)
        return 1;
    if (strcmp(name, "tax_calc") == 0)
        return 1;
    return 0;
}

static int is_filtered(const char *name) {
    for (int i = 0; launcher_filter[i]; i++)
        if (strcmp(name, launcher_filter[i]) == 0) return 1;
    return 0;
}

static int launcher_has_name(const wm_state_t *wm, const char *name) {
    for (int i = 0; i < wm->launcher_count; i++) {
        if (strcmp(wm->launcher_items[i], name) == 0)
            return 1;
    }
    return 0;
}

/** True if @p name appears as a full line in listdir @p buf (newline-separated). */
static int launcher_name_in_listdir_buf(const char *buf, const char *name) {
    size_t nlen = strlen(name);
    const char *p = buf;
    while (*p) {
        const char *nl = strchr(p, '\n');
        if (!nl)
            break;
        size_t len = (size_t)(nl - p);
        if (len == nlen && strncmp(p, name, len) == 0)
            return 1;
        p = nl + 1;
    }
    return 0;
}

static int launcher_append_name(wm_state_t *wm, const char *name) {
    if (!name || !name[0] || is_filtered(name))
        return 0;
    if (launcher_has_name(wm, name))
        return 0;
    if (wm->launcher_count >= LAUNCHER_MAX_ITEMS)
        return 0;
    strncpy(wm->launcher_items[wm->launcher_count], name, 31);
    wm->launcher_items[wm->launcher_count][31] = '\0';
    wm->launcher_count++;
    return 1;
}

/* WM-hosted GUIs: add first if present in /bin listing (so 64-cap never hides them). */
static const char *launcher_gui_pin[] = { "gooey_demo", "widget_demo", "tax_calc", NULL };

static void theme_remap_term_defaults(term_state_t *ts, uint32_t old_fg, uint32_t old_bg,
                                      uint32_t new_fg, uint32_t new_bg) {
    uint32_t total = ts->cols * ts->rows;
    for (uint32_t i = 0; i < total; i++) {
        if (ts->cells[i].fg == old_fg) ts->cells[i].fg = new_fg;
        if (ts->cells[i].bg == old_bg) ts->cells[i].bg = new_bg;
        ts->cells[i].dirty = 1;
    }
    if (ts->alt_cells) {
        for (uint32_t i = 0; i < total; i++) {
            if (ts->alt_cells[i].fg == old_fg) ts->alt_cells[i].fg = new_fg;
            if (ts->alt_cells[i].bg == old_bg) ts->alt_cells[i].bg = new_bg;
            ts->alt_cells[i].dirty = 1;
        }
    }
    if (ts->fg == old_fg) ts->fg = new_fg;
    if (ts->bg == old_bg) ts->bg = new_bg;
}

void wm_apply_theme(wm_state_t *wm, int theme_idx) {
    if (theme_idx < 0 || theme_idx >= WM_THEME_COUNT) return;
    wm_theme_t old_theme = g_wm_theme;
    g_wm_theme = wm_themes[theme_idx];
    wm->theme_current = theme_idx;

    wm_render_wallpaper(wm);

    for (int i = 0; i < MAX_WINDOWS; i++) {
        wm_window_t *win = &wm->windows[i];
        if (!win->active || win->is_gui) continue;
        theme_remap_term_defaults(&win->term, old_theme.term_fg, old_theme.term_bg,
                                  THEME_TERM_FG, THEME_TERM_BG);
        wm_render_window(wm, i);
    }

    render_taskbar(wm);
    overlay_reconfigure_alpha(wm);
    if (wm->launcher_active)
        launcher_render(wm);
    if (wm->settings_active)
        settings_render(wm);
}

static void launcher_filter_update(wm_state_t *wm) {
    wm->launcher_filtered_count = 0;
    for (int i = 0; i < wm->launcher_count; i++) {
        if (wm->launcher_search_len == 0) {
            wm->launcher_filtered[wm->launcher_filtered_count++] = i;
            continue;
        }
        /* case-insensitive substring match */
        const char *hay = wm->launcher_items[i];
        const char *needle = wm->launcher_search;
        int nlen = wm->launcher_search_len;
        int hlen = (int)strlen(hay);
        int found = 0;
        for (int h = 0; h <= hlen - nlen; h++) {
            int match = 1;
            for (int n = 0; n < nlen; n++) {
                char a = hay[h + n]; if (a >= 'A' && a <= 'Z') a += 32;
                char b = needle[n];  if (b >= 'A' && b <= 'Z') b += 32;
                if (a != b) { match = 0; break; }
            }
            if (match) { found = 1; break; }
        }
        if (found)
            wm->launcher_filtered[wm->launcher_filtered_count++] = i;
    }
    wm->launcher_selected = 0;
    wm->launcher_scroll = 0;
}

void launcher_open(wm_state_t *wm) {
    if (wm->launcher_active) return;
    if (wm->settings_active) settings_close(wm);

    /* enumerate /bin (large buffer: full toolchains can overflow smaller bufs) */
    char buf[8192];
    int rc = listdir("/bin", buf, sizeof(buf));
    if (rc < 0) return;

    wm->launcher_count = 0;

    for (int g = 0; launcher_gui_pin[g]; g++) {
        if (launcher_name_in_listdir_buf(buf, launcher_gui_pin[g]))
            launcher_append_name(wm, launcher_gui_pin[g]);
    }

    char *p = buf;
    while (*p && wm->launcher_count < LAUNCHER_MAX_ITEMS) {
        char *nl = strchr(p, '\n');
        if (!nl) break;
        *nl = '\0';
        if (strlen(p) > 0)
            launcher_append_name(wm, p);
        p = nl + 1;
    }

    if (wm->launcher_count == 0) return;

    wm->launcher_search[0] = '\0';
    wm->launcher_search_len = 0;
    wm->launcher_selected = 0;
    wm->launcher_scroll = 0;
    launcher_filter_update(wm);

    /* allocate layer */
    int lx = (SCREEN_W - LAUNCHER_W) / 2;
    int ly = (SCREEN_H - LAUNCHER_H) / 2;

    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = lx; cfg.y0 = ly;
    cfg.x1 = lx + LAUNCHER_W; cfg.y1 = ly + LAUNCHER_H;
    cfg.alpha = 0;
    cfg.hints = FB_LAYER_HINT_OPAQUE_CONTENT |
                FB_LAYER_HINT_FREQUENT_UPDATES |
                FB_LAYER_HINT_TRANSIENT;
    cfg.stride = LAUNCHER_W * BPP;

    fb_layer_info_t info = {0};
    if (sys_5ht_req_buf(LAYER_LAUNCHER, &cfg, &info) != 0) return;

    wm->launcher_fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    wm->launcher_meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;
    wm->launcher_active = 1;

    launcher_render(wm);
}

void launcher_close(wm_state_t *wm) {
    if (!wm->launcher_active) return;
    sys_5ht_rel_buf(LAYER_LAUNCHER);
    wm->launcher_fb = NULL;
    wm->launcher_meta = NULL;
    wm->launcher_active = 0;
}

void launcher_render(wm_state_t *wm) {
    if (!wm->launcher_fb) return;

    uint32_t stride = LAUNCHER_W;
    uint32_t panel_bg = THEME_BG_DARK;
    uint32_t border = THEME_ACCENT;
    uint32_t sep = THEME_BORDER;

    /* background */
    draw_fill_rect(wm->launcher_fb, stride, 0, 0, LAUNCHER_W, LAUNCHER_H, panel_bg);

    /* border */
    draw_fill_rect(wm->launcher_fb, stride, 0, 0, LAUNCHER_W, 2, border);
    draw_fill_rect(wm->launcher_fb, stride, 0, LAUNCHER_H - 2, LAUNCHER_W, 2, border);
    draw_fill_rect(wm->launcher_fb, stride, 0, 0, 2, LAUNCHER_H, border);
    draw_fill_rect(wm->launcher_fb, stride, LAUNCHER_W - 2, 0, 2, LAUNCHER_H, border);

    /* title */
    draw_text(wm->launcher_fb, stride, LAUNCHER_PAD, LAUNCHER_PAD,
              "Launch Program", THEME_ACCENT, panel_bg);

    /* search box */
    int search_y = LAUNCHER_PAD + FONT_H + 6;
    int search_box_w = LAUNCHER_W - LAUNCHER_PAD * 2;
    int search_box_h = FONT_H + 8;
    uint32_t search_bg = THEME_BG_MEDIUM;
    draw_fill_rect(wm->launcher_fb, stride, LAUNCHER_PAD, search_y,
                   search_box_w, search_box_h, search_bg);
    draw_fill_rect(wm->launcher_fb, stride, LAUNCHER_PAD, search_y,
                   search_box_w, 1, sep);
    draw_fill_rect(wm->launcher_fb, stride, LAUNCHER_PAD, search_y + search_box_h - 1,
                   search_box_w, 1, sep);
    draw_fill_rect(wm->launcher_fb, stride, LAUNCHER_PAD, search_y,
                   1, search_box_h, sep);
    draw_fill_rect(wm->launcher_fb, stride, LAUNCHER_PAD + search_box_w - 1, search_y,
                   1, search_box_h, sep);

    int text_x = LAUNCHER_PAD + 6;
    int text_y = search_y + 4;
    if (wm->launcher_search_len > 0) {
        draw_text(wm->launcher_fb, stride, text_x, text_y,
                  wm->launcher_search, THEME_TEXT_PRIMARY, search_bg);
        /* cursor bar after text */
        int cx = text_x + wm->launcher_search_len * FONT_W;
        draw_fill_rect(wm->launcher_fb, stride, cx, text_y, 2, FONT_H, THEME_ACCENT);
    } else {
        draw_text(wm->launcher_fb, stride, text_x, text_y,
                  "Type to search...", THEME_TEXT_DIM, search_bg);
        draw_fill_rect(wm->launcher_fb, stride, text_x, text_y, 2, FONT_H, THEME_ACCENT);
    }

    /* separator */
    int sep_y = search_y + search_box_h + 4;
    draw_fill_rect(wm->launcher_fb, stride, LAUNCHER_PAD, sep_y,
                   LAUNCHER_W - LAUNCHER_PAD * 2, 1, sep);

    /* item list (filtered) */
    int list_y = sep_y + 6;
    int visible = (LAUNCHER_H - list_y - LAUNCHER_PAD - FONT_H - 4) / LAUNCHER_ITEM_H;

    for (int i = 0; i < visible && (i + wm->launcher_scroll) < wm->launcher_filtered_count; i++) {
        int fi = i + wm->launcher_scroll;
        int src_idx = wm->launcher_filtered[fi];
        int iy = list_y + i * LAUNCHER_ITEM_H;
        int selected = (fi == wm->launcher_selected);

        uint32_t bg = selected ? THEME_ACCENT : panel_bg;
        uint32_t fg = selected ? 0xFF000000 : THEME_TEXT_PRIMARY;

        draw_fill_rect(wm->launcher_fb, stride,
                       LAUNCHER_PAD, iy,
                       LAUNCHER_W - LAUNCHER_PAD * 2, LAUNCHER_ITEM_H, bg);
        draw_text(wm->launcher_fb, stride,
                  LAUNCHER_PAD + 8, iy + (LAUNCHER_ITEM_H - FONT_H) / 2,
                  wm->launcher_items[src_idx], fg, bg);
    }

    if (wm->launcher_filtered_count == 0) {
        draw_text(wm->launcher_fb, stride,
                  LAUNCHER_PAD + 8, list_y + (LAUNCHER_ITEM_H - FONT_H) / 2,
                  "No matches", THEME_TEXT_DIM, panel_bg);
    }

    /* hint text */
    draw_text(wm->launcher_fb, stride,
              LAUNCHER_PAD, LAUNCHER_H - LAUNCHER_PAD - FONT_H,
              "Enter=launch  Esc=close  \x18\x19=navigate",
              THEME_TEXT_DIM, panel_bg);

    /* submit */
    wm->launcher_meta->dx0 = 0; wm->launcher_meta->dy0 = 0;
    wm->launcher_meta->dx1 = LAUNCHER_W; wm->launcher_meta->dy1 = LAUNCHER_H;
    wm->launcher_meta->frame_id++;
    wm->launcher_meta->ready = 1;
}

void launcher_key(wm_state_t *wm, keyboard_event_t *ev) {
    if (ev->flags & KEY_FLAG_RELEASED) return;

    int search_box_h = FONT_H + 8;
    int sep_y = LAUNCHER_PAD + FONT_H + 6 + search_box_h + 4 + 6;
    int visible = (LAUNCHER_H - sep_y - LAUNCHER_PAD - FONT_H - 4) / LAUNCHER_ITEM_H;

    switch (ev->scancode) {
    case 0x01: /* Escape */
        launcher_close(wm);
        return;
    case 0x1C: /* Enter */ {
        if (wm->launcher_selected >= 0 &&
            wm->launcher_selected < wm->launcher_filtered_count) {
            int src = wm->launcher_filtered[wm->launcher_selected];
            char name[32];
            strncpy(name, wm->launcher_items[src], sizeof(name));
            name[31] = '\0';
            launcher_close(wm);
            if (is_gui_launch_name(name))
                wm_launch_gui_window(wm, name);
            else
                wm_launch_window(wm, name);
        }
        return;
    }
    case 0x48: /* Up arrow */
        if (wm->launcher_selected > 0) {
            wm->launcher_selected--;
            if (wm->launcher_selected < wm->launcher_scroll)
                wm->launcher_scroll = wm->launcher_selected;
        }
        break;
    case 0x50: /* Down arrow */
        if (wm->launcher_selected < wm->launcher_filtered_count - 1) {
            wm->launcher_selected++;
            if (wm->launcher_selected >= wm->launcher_scroll + visible)
                wm->launcher_scroll = wm->launcher_selected - visible + 1;
        }
        break;
    case 0x0E: /* Backspace */
        if (wm->launcher_search_len > 0) {
            wm->launcher_search[--wm->launcher_search_len] = '\0';
            launcher_filter_update(wm);
        }
        break;
    default:
        if (ev->ascii >= 0x20 && ev->ascii < 0x7F &&
            wm->launcher_search_len < 30) {
            wm->launcher_search[wm->launcher_search_len++] = ev->ascii;
            wm->launcher_search[wm->launcher_search_len] = '\0';
            launcher_filter_update(wm);
        } else {
            return;
        }
        break;
    }
    launcher_render(wm);
}

/* --- settings popup --- */

void settings_close(wm_state_t *wm) {
    if (!wm->settings_active) return;
    sys_5ht_rel_buf(LAYER_LAUNCHER);
    wm->settings_fb = NULL;
    wm->settings_meta = NULL;
    wm->settings_active = 0;
    render_taskbar(wm);
}

void settings_render(wm_state_t *wm) {
    if (!wm->settings_fb) return;

    uint32_t stride = SETTINGS_W;
    int sh = SETTINGS_POPUP_H;
    uint32_t bg = THEME_BG_DARK;

    draw_fill_rect(wm->settings_fb, stride, 0, 0, SETTINGS_W, sh, bg);

    /* border */
    draw_fill_rect(wm->settings_fb, stride, 0, 0, SETTINGS_W, 2, THEME_ACCENT);
    draw_fill_rect(wm->settings_fb, stride, 0, sh - 2, SETTINGS_W, 2, THEME_ACCENT);
    draw_fill_rect(wm->settings_fb, stride, 0, 0, 2, sh, THEME_ACCENT);
    draw_fill_rect(wm->settings_fb, stride, SETTINGS_W - 2, 0, 2, sh, THEME_ACCENT);

    /* tab bar */
    static const char *tab_labels[SETTINGS_SEC_COUNT] = {"Theme", "Wallpaper", "About"};
    int tab_y = 2 + SETTINGS_PAD;
    int tab_gap = 2;
    int avail_w = SETTINGS_W - SETTINGS_PAD * 2 - tab_gap * (SETTINGS_SEC_COUNT - 1);
    int tab_w = avail_w / SETTINGS_SEC_COUNT;

    for (int t = 0; t < SETTINGS_SEC_COUNT; t++) {
        int tx = SETTINGS_PAD + t * (tab_w + tab_gap);
        uint32_t tbg = (t == wm->settings_section) ? THEME_ACCENT : THEME_BG_MEDIUM;
        uint32_t tfg = (t == wm->settings_section) ? 0xFF000000 : THEME_TEXT_PRIMARY;
        draw_fill_rect(wm->settings_fb, stride, tx, tab_y, tab_w, SETTINGS_TAB_H, tbg);
        int lbl_len = (int)strlen(tab_labels[t]);
        int lbl_x = tx + (tab_w - lbl_len * FONT_W) / 2;
        int lbl_y = tab_y + (SETTINGS_TAB_H - FONT_H) / 2;
        draw_text(wm->settings_fb, stride, lbl_x, lbl_y, tab_labels[t], tfg, tbg);
    }

    /* separator below tabs */
    int sep_y = tab_y + SETTINGS_TAB_H + SETTINGS_PAD;
    draw_fill_rect(wm->settings_fb, stride, SETTINGS_PAD, sep_y,
                   SETTINGS_W - SETTINGS_PAD * 2, 1, THEME_BORDER);

    int content_y = sep_y + 5;

    switch (wm->settings_section) {
    case SETTINGS_SEC_THEME:
        for (int i = 0; i < WM_THEME_COUNT; i++) {
            int iy = content_y + i * SETTINGS_ITEM_H;
            int sel = (i == wm->theme_current);
            uint32_t ibg = sel ? THEME_ACCENT : bg;
            uint32_t ifg = sel ? 0xFF000000 : THEME_TEXT_PRIMARY;
            draw_fill_rect(wm->settings_fb, stride, SETTINGS_PAD, iy,
                           SETTINGS_W - SETTINGS_PAD * 2, SETTINGS_ITEM_H, ibg);
            draw_text(wm->settings_fb, stride,
                      SETTINGS_PAD + 8, iy + (SETTINGS_ITEM_H - FONT_H) / 2,
                      wm_themes[i].name, ifg, ibg);
        }
        break;
    case SETTINGS_SEC_WALLPAPER:
        for (int i = 0; i < WM_WALLPAPER_COUNT; i++) {
            int iy = content_y + i * SETTINGS_ITEM_H;
            int sel = (i == wm->wallpaper_current);
            uint32_t ibg = sel ? THEME_ACCENT : bg;
            uint32_t ifg = sel ? 0xFF000000 : THEME_TEXT_PRIMARY;
            draw_fill_rect(wm->settings_fb, stride, SETTINGS_PAD, iy,
                           SETTINGS_W - SETTINGS_PAD * 2, SETTINGS_ITEM_H, ibg);
            draw_text(wm->settings_fb, stride,
                      SETTINGS_PAD + 8, iy + (SETTINGS_ITEM_H - FONT_H) / 2,
                      wallpaper_names[i], ifg, ibg);
        }
        break;
    case SETTINGS_SEC_ABOUT: {
        int cy = content_y;
        draw_fill_rect(wm->settings_fb, stride,
                       SETTINGS_PAD + 8, cy, 40, 4, THEME_ACCENT);
        cy += 12;
        draw_text(wm->settings_fb, stride, SETTINGS_PAD + 8, cy,
                  "Serotonin", THEME_ACCENT, bg);
        cy += FONT_H + 8;
        draw_text(wm->settings_fb, stride, SETTINGS_PAD + 8, cy,
                  "Version 0.4.3", THEME_TEXT_PRIMARY, bg);
        cy += FONT_H + 12;
        draw_text(wm->settings_fb, stride, SETTINGS_PAD + 8, cy,
                  "A hobby operating system", THEME_TEXT_DIM, bg);
        cy += FONT_H + 2;
        draw_text(wm->settings_fb, stride, SETTINGS_PAD + 8, cy,
                  "written in C and x86 ASM", THEME_TEXT_DIM, bg);
        cy += FONT_H + 12;
        draw_text(wm->settings_fb, stride, SETTINGS_PAD + 8, cy,
                  "(c) 2024-2026", THEME_TEXT_DIM, bg);
        break;
    }
    }

    wm->settings_meta->dx0 = 0;
    wm->settings_meta->dy0 = 0;
    wm->settings_meta->dx1 = SETTINGS_W;
    wm->settings_meta->dy1 = sh;
    wm->settings_meta->frame_id++;
    wm->settings_meta->ready = 1;
}

void settings_open(wm_state_t *wm) {
    if (wm->settings_active) return;
    if (wm->launcher_active) launcher_close(wm);

    int sw = SETTINGS_W;
    int sh = SETTINGS_POPUP_H;
    int sx = SCREEN_W - sw - 8;
    int sy = SCREEN_H - TASKBAR_H - sh;

    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = sx; cfg.y0 = sy;
    cfg.x1 = sx + sw; cfg.y1 = sy + sh;
    cfg.alpha = 0;
    cfg.hints = FB_LAYER_HINT_OPAQUE_CONTENT |
                FB_LAYER_HINT_FREQUENT_UPDATES |
                FB_LAYER_HINT_TRANSIENT;
    cfg.stride = sw * BPP;

    fb_layer_info_t info = {0};
    if (sys_5ht_req_buf(LAYER_LAUNCHER, &cfg, &info) != 0) return;

    wm->settings_fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    wm->settings_meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;
    wm->settings_active = 1;

    settings_render(wm);
    render_taskbar(wm);
}

/* --- inactive window alpha --- */

static void wm_set_win_blend(wm_window_t *win, int blend) {
    if (win->is_gui)
        return;
    fb_layer_config_t cfg = {0};
    fb_layer_info_t info = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = win->x;
    cfg.y0 = win->y;
    cfg.x1 = win->x + win->w;
    cfg.y1 = win->y + win->h;
    cfg.stride = win->w * BPP;
    cfg.hints = FB_LAYER_HINT_FREQUENT_UPDATES;
    if (blend) {
        cfg.alpha = FB_LAYER_ALPHA_BLEND;
    } else {
        cfg.alpha = FB_LAYER_ALPHA_OPAQUE;
        cfg.hints |= FB_LAYER_HINT_OPAQUE_CONTENT;
    }
    if (sys_5ht_rcfg_layer(win->layer_id, &cfg, &info) == 0) {
        win->fb = (uint32_t *)(uintptr_t)info.fb_user_va;
        win->meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;
    }
}

/* --- window decorations --- */

void wm_render_decorations(wm_state_t *wm, int idx) {
    wm_window_t *win = &wm->windows[idx];
    if (!win->active || !win->fb || win->is_gui) return;

    uint32_t stride = win->fb_stride_px;
    uint32_t tb_bg = win->focused ? THEME_TITLEBAR_BG : THEME_TITLEBAR_INACTIVE;
    uint32_t border = win->focused ? THEME_BORDER_ACTIVE : THEME_BORDER_INACTIVE;

    /* title bar */
    draw_fill_rect(win->fb, stride, 0, 0, win->w, TITLEBAR_H, tb_bg);

    /* title text */
    int text_x = BORDER_W + 4;
    int text_y = (TITLEBAR_H - FONT_H) / 2;
    draw_text(win->fb, stride, text_x, text_y, win->title,
              THEME_TITLEBAR_FG, tb_bg);

    /* close button */
    int close_x = win->w - BORDER_W - CLOSE_BTN_W;
    int close_y = (TITLEBAR_H - CLOSE_BTN_H) / 2;
    draw_fill_rect(win->fb, stride, close_x, close_y,
                   CLOSE_BTN_W, CLOSE_BTN_H, THEME_CLOSE_BTN);
    /* X in close button */
    draw_char(win->fb, stride, close_x + 4, close_y + 0, 'x', 1,
              0xFFFFFFFF, THEME_CLOSE_BTN);

    /* borders - left */
    draw_fill_rect(win->fb, stride, 0, TITLEBAR_H, BORDER_W,
                   win->h - TITLEBAR_H, border);
    /* right */
    draw_fill_rect(win->fb, stride, win->w - BORDER_W, TITLEBAR_H, BORDER_W,
                   win->h - TITLEBAR_H, border);
    /* bottom */
    draw_fill_rect(win->fb, stride, 0, win->h - BORDER_W, win->w, BORDER_W, border);

    /* titlebar + borders dirty */
    wm_dirty_expand(win, 0, 0, win->w, TITLEBAR_H);
    wm_dirty_expand(win, 0, TITLEBAR_H, BORDER_W, win->h - TITLEBAR_H);
    wm_dirty_expand(win, win->w - BORDER_W, TITLEBAR_H, BORDER_W, win->h - TITLEBAR_H);
    wm_dirty_expand(win, 0, win->h - BORDER_W, win->w, BORDER_W);
}

/* --- dirty region tracking --- */

void wm_dirty_reset(wm_window_t *win) {
    win->dirty_x0 = win->w;
    win->dirty_y0 = win->h;
    win->dirty_x1 = 0;
    win->dirty_y1 = 0;
}

void wm_dirty_expand(wm_window_t *win, uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    uint16_t x1 = x + w;
    uint16_t y1 = y + h;
    if (x < win->dirty_x0) win->dirty_x0 = x;
    if (y < win->dirty_y0) win->dirty_y0 = y;
    if (x1 > win->dirty_x1) win->dirty_x1 = x1;
    if (y1 > win->dirty_y1) win->dirty_y1 = y1;
}

/* --- frame submission --- */

void wm_submit_frame(wm_window_t *win) {
    if (!win->meta) return;
    if (win->dirty_x0 >= win->dirty_x1 || win->dirty_y0 >= win->dirty_y1) return;

    /* Clamp dirty rect to window bounds */
    uint16_t dx0 = win->dirty_x0;
    uint16_t dy0 = win->dirty_y0;
    uint16_t dx1 = win->dirty_x1;
    uint16_t dy1 = win->dirty_y1;
    if (dx1 > win->w) dx1 = win->w;
    if (dy1 > win->h) dy1 = win->h;
    if (dx0 >= dx1 || dy0 >= dy1) { wm_dirty_reset(win); return; }

    /* Widen the dirty rect if compositor hasn't consumed the previous one yet */
    if (win->meta->ready) {
        if (dx0 > win->meta->dx0) dx0 = win->meta->dx0;
        if (dy0 > win->meta->dy0) dy0 = win->meta->dy0;
        if (dx1 < win->meta->dx1) dx1 = win->meta->dx1;
        if (dy1 < win->meta->dy1) dy1 = win->meta->dy1;
    }

    win->meta->dx0 = dx0;
    win->meta->dy0 = dy0;
    win->meta->dx1 = dx1;
    win->meta->dy1 = dy1;
    win->meta->frame_id++;
    win->meta->ready = 1;
    wm_dirty_reset(win);
}

/* --- window render (decorations + terminal) --- */

void wm_render_window(wm_state_t *wm, int idx) {
    wm_window_t *win = &wm->windows[idx];
    if (!win->active) return;

    if (win->is_gui) {
        win->cx = win->x;
        win->cy = win->y;
        win->cw = win->w;
        win->ch = win->h;
        if (win->pty_master_fd >= 0) {
            sg_gui_event_t ev;
            wm_ipc_fill_configure(&ev, win->x, win->y,
                                  (uint16_t)(win->x + win->w), (uint16_t)(win->y + win->h));
            wm_ipc_send(win->pty_master_fd, &ev);
        }
        return;
    }

    if (!win->fb) return;

    /* Reconfigure layer if geometry changed */
    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = win->x;
    cfg.y0 = win->y;
    cfg.x1 = win->x + win->w;
    cfg.y1 = win->y + win->h;
    cfg.hints = FB_LAYER_HINT_FREQUENT_UPDATES;
    if (win->focused) {
        cfg.alpha = FB_LAYER_ALPHA_OPAQUE;
        cfg.hints |= FB_LAYER_HINT_OPAQUE_CONTENT;
    } else {
        cfg.alpha = FB_LAYER_ALPHA_BLEND;
    }
    cfg.stride = win->w * BPP;

    fb_layer_info_t info = {0};
    sys_5ht_rcfg_layer(win->layer_id, &cfg, &info);
    win->fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    win->meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;
    win->fb_stride_px = win->w;

    /* Update content area */
    win->cx = win->x + BORDER_W;
    win->cy = win->y + TITLEBAR_H;
    win->cw = win->w - BORDER_W * 2;
    win->ch = win->h - TITLEBAR_H - BORDER_W;

    /* Resize terminal if grid changed */
    uint32_t new_cols = win->cw / FONT_W;
    uint32_t new_rows = win->ch / FONT_H;
    if (new_cols == 0) new_cols = 1;
    if (new_rows == 0) new_rows = 1;

    if (new_cols != win->term.cols || new_rows != win->term.rows) {
        term_resize(&win->term, new_cols, new_rows);
        /* notify shell of new size */
        pty_winsize_t ws = {0};
        ws.ws_row = new_rows;
        ws.ws_col = new_cols;
        ws.ws_xpixel = win->cw;
        ws.ws_ypixel = win->ch;
        sys_5ht_pty_winsize(win->pty_master_fd, &ws, 0);
    }

    /* Full re-render: reset dirty and mark everything */
    wm_dirty_reset(win);

    /* Clear the full fb first */
    draw_fill_rect(win->fb, win->fb_stride_px, 0, 0, win->w, win->h, THEME_TERM_BG);
    wm_dirty_expand(win, 0, 0, win->w, win->h);

    /* Render decorations */
    wm_render_decorations(wm, idx);

    /* Mark all cells dirty and render terminal */
    term_mark_all_dirty(&win->term);
    term_render(win);

    if (!win->focused)
        fb_set_alpha(win->fb, (uint32_t)win->w * win->h, INACTIVE_ALPHA);

    /* Force-submit: after a full reconfigure + redraw, we must submit
       regardless of whether the compositor consumed the previous frame */
    win->meta->dx0 = 0;
    win->meta->dy0 = 0;
    win->meta->dx1 = win->w;
    win->meta->dy1 = win->h;
    win->meta->frame_id++;
    win->meta->ready = 1;
    wm_dirty_reset(win);
}

/* --- window lifecycle --- */

int wm_create_window(wm_state_t *wm) {
    return wm_launch_window(wm, NULL);
}

int wm_launch_window(wm_state_t *wm, const char *program) {
    /* find free slot */
    int idx = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!wm->windows[i].active) { idx = i; break; }
    }
    if (idx < 0) return -1;

    /* find free layer */
    int layer_id = -1;
    for (int l = LAYER_WIN_BASE; l <= LAYER_WIN_MAX; l++) {
        int used = 0;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (wm->windows[i].active && wm->windows[i].layer_id == l)
                { used = 1; break; }
        }
        if (!used) { layer_id = l; break; }
    }
    if (layer_id < 0) return -1;

    wm_window_t *win = &wm->windows[idx];
    memset(win, 0, sizeof(*win));
    win->active = 1;
    win->layer_id = (uint16_t)layer_id;
    win->mode = WIN_TILED;
    if (program)
        snprintf(win->title, sizeof(win->title), "%s", program);
    else
        snprintf(win->title, sizeof(win->title), "Terminal %d", idx + 1);

    wm->num_windows++;

    /* compute layout */
    layout_compute(wm);

    /* allocate layer */
    fb_layer_config_t cfg = {0};
    cfg.size = sizeof(cfg);
    cfg.x0 = win->x;
    cfg.y0 = win->y;
    cfg.x1 = win->x + win->w;
    cfg.y1 = win->y + win->h;
    cfg.alpha = 0;
    cfg.hints = FB_LAYER_HINT_OPAQUE_CONTENT | FB_LAYER_HINT_FREQUENT_UPDATES;
    cfg.stride = win->w * BPP;

    fb_layer_info_t info = {0};
    if (sys_5ht_req_buf(layer_id, &cfg, &info) != 0) {
        win->active = 0;
        wm->num_windows--;
        return -1;
    }

    win->fb = (uint32_t *)(uintptr_t)info.fb_user_va;
    win->meta = (volatile fb_layer_metadata_t *)(uintptr_t)info.metadata_user_va;
    win->fb_stride_px = win->w;

    /* compute content area */
    win->cx = win->x + BORDER_W;
    win->cy = win->y + TITLEBAR_H;
    win->cw = win->w - BORDER_W * 2;
    win->ch = win->h - TITLEBAR_H - BORDER_W;

    /* init terminal */
    uint32_t cols = win->cw / FONT_W;
    uint32_t rows = win->ch / FONT_H;
    if (cols == 0) cols = 1;
    if (rows == 0) rows = 1;
    term_init(&win->term, cols, rows);

    /* open PTY */
    int fds[2];
    if (sys_5ht_pty_open(fds) != 0) {
        sys_5ht_rel_buf(layer_id);
        win->active = 0;
        wm->num_windows--;
        return -1;
    }
    win->pty_master_fd = fds[0];

    /* set window size */
    pty_winsize_t ws = {0};
    ws.ws_row = rows;
    ws.ws_col = cols;
    ws.ws_xpixel = win->cw;
    ws.ws_ypixel = win->ch;
    sys_5ht_pty_winsize(fds[0], &ws, 0);

    /* fork shell */
    pid_t pid = fork();
    if (pid == 0) {
        /* child */
        close(fds[0]); /* close master */
        close(wm->kb_fd);
        close(wm->mouse_fd);

        /* set slave as stdio */
        close(0); close(1); close(2);
        dup2(fds[1], 0);
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        if (fds[1] > 2) close(fds[1]);

        sys_5ht_pty_setpgrp(0);

        char *envp[] = { "TERM=xterm", "HOME=/root", "PATH=/bin:/usr/bin", NULL };
        if (program) {
            char path[64];
            snprintf(path, sizeof(path), "/bin/%s", program);
            char *argv[] = { path, NULL };
            execve(path, argv, envp);
        } else {
            char *argv[] = { "/bin/sh", NULL };
            argv[0] = "/bin/sh";
            execve("/bin/sh", argv, envp);
        }
        _exit(127);
    }

    close(fds[1]); /* close slave in parent */
    win->child_pid = pid;

    /* set non-blocking on master */
    /* TODO: if fcntl not available, we rely on poll timeout */

    /* focus new window */
    wm_focus_window(wm, idx);

    /* render all windows (layout may have changed) */
    desktop_mark_dirty(wm, 0, 0, SCREEN_W, SCREEN_H - TASKBAR_H);
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wm->windows[i].active)
            wm_render_window(wm, i);
    }

    render_taskbar(wm);
    return idx;
}

int wm_launch_gui_window(wm_state_t *wm, const char *program) {
    if (!program || !program[0])
        return -1;

    int idx = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!wm->windows[i].active) {
            idx = i;
            break;
        }
    }
    if (idx < 0)
        return -1;

    int layer_id = -1;
    for (int l = LAYER_WIN_BASE; l <= LAYER_WIN_MAX; l++) {
        int used = 0;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (wm->windows[i].active && wm->windows[i].layer_id == l) {
                used = 1;
                break;
            }
        }
        if (!used) {
            layer_id = l;
            break;
        }
    }
    if (layer_id < 0)
        return -1;

    wm_window_t *win = &wm->windows[idx];
    memset(win, 0, sizeof(*win));
    win->active = 1;
    win->is_gui = 1;
    win->layer_id = (uint16_t)layer_id;
    win->mode = WIN_TILED;
    snprintf(win->title, sizeof(win->title), "%s", program);

    wm->num_windows++;
    layout_compute(wm);

    int sv[2] = {-1, -1};
    if (wm_ipc_socketpair(sv) != 0) {
        win->active = 0;
        wm->num_windows--;
        return -1;
    }

    uint16_t gx0 = win->x;
    uint16_t gy0 = win->y;
    uint16_t gx1 = (uint16_t)(win->x + win->w);
    uint16_t gy1 = (uint16_t)(win->y + win->h);
    uint16_t glid = (uint16_t)layer_id;

    pid_t pid = fork();
    if (pid < 0) {
        close(sv[0]);
        close(sv[1]);
        win->active = 0;
        wm->num_windows--;
        return -1;
    }

    if (pid == 0) {
        char e_layer[56], e_x0[40], e_y0[40], e_x1[40], e_y1[40], e_evfd[40];

        snprintf(e_layer, sizeof(e_layer), "SEROTONIN_GUI_LAYER_ID=%u", (unsigned)glid);
        snprintf(e_x0, sizeof(e_x0), "SEROTONIN_GUI_X0=%u", (unsigned)gx0);
        snprintf(e_y0, sizeof(e_y0), "SEROTONIN_GUI_Y0=%u", (unsigned)gy0);
        snprintf(e_x1, sizeof(e_x1), "SEROTONIN_GUI_X1=%u", (unsigned)gx1);
        snprintf(e_y1, sizeof(e_y1), "SEROTONIN_GUI_Y1=%u", (unsigned)gy1);
        snprintf(e_evfd, sizeof(e_evfd), "SEROTONIN_GUI_EVENTS_FD=%d", SG_GUI_EVENTS_FD);

        char *envp[] = {
            "TERM=serotonin-gui",
            "HOME=/root",
            "PATH=/bin:/usr/bin",
            e_layer,
            e_x0,
            e_y0,
            e_x1,
            e_y1,
            e_evfd,
            NULL,
        };

        close(sv[0]);
        close(wm->kb_fd);
        close(wm->mouse_fd);
        if (dup2(sv[1], SG_GUI_EVENTS_FD) < 0)
            _exit(124);
        if (sv[1] != SG_GUI_EVENTS_FD)
            close(sv[1]);

        char path[64];
        snprintf(path, sizeof(path), "/bin/%s", program);
        char *argv[] = {path, NULL};
        execve(path, argv, envp);
        _exit(127);
    }

    close(sv[1]);
    win->pty_master_fd = sv[0];
    win->child_pid = pid;
    win->fb = NULL;
    win->meta = NULL;

    wm_focus_window(wm, idx);

    desktop_mark_dirty(wm, 0, 0, SCREEN_W, SCREEN_H - TASKBAR_H);
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wm->windows[i].active)
            wm_render_window(wm, i);
    }

    render_taskbar(wm);
    return idx;
}

void wm_close_window(wm_state_t *wm, int idx) {
    wm_window_t *win = &wm->windows[idx];
    if (!win->active) return;

    /* kill child */
    if (win->child_pid > 0)
        kill(win->child_pid, 15); /* SIGTERM */

    /* close PTY master or GUI IPC socket */
    if (win->pty_master_fd >= 0)
        close(win->pty_master_fd);

    /* free terminal (terminal windows only) */
    if (!win->is_gui)
        term_free(&win->term);

    /* release layer (WM-owned terminal layers only; GUI child owns its layer) */
    if (!win->is_gui)
        sys_5ht_rel_buf(win->layer_id);

    win->active = 0;
    wm->num_windows--;

    /* reap child */
    if (win->child_pid > 0) {
        int status;
        waitpid(win->child_pid, &status);
    }

    /* refocus */
    if (wm->focused_idx == idx) {
        wm->focused_idx = -1;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (wm->windows[i].active) {
                wm_focus_window(wm, i);
                break;
            }
        }
    }

    /* recompute layout — mark full desktop dirty to erase closed window */
    desktop_mark_dirty(wm, 0, 0, SCREEN_W, SCREEN_H - TASKBAR_H);
    layout_compute(wm);
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (wm->windows[i].active)
            wm_render_window(wm, i);
    }

    render_taskbar(wm);
}

void wm_focus_window(wm_state_t *wm, int idx) {
    if (idx == wm->focused_idx) return;

    int old_idx = wm->focused_idx;

    /* unfocus old */
    if (old_idx >= 0 && wm->windows[old_idx].active)
        wm->windows[old_idx].focused = 0;

    wm->focused_idx = idx;

    /* focus new */
    if (idx >= 0 && wm->windows[idx].active)
        wm->windows[idx].focused = 1;

    /*
     * Promote focused window to the highest active layer so it composites
     * on top of all other windows (critical for overlapping floating windows).
     * Swap layer IDs with whoever currently holds the top layer, then
     * re-render both on their new layers.
     */
    int did_swap = 0;
    if (idx >= 0 && wm->windows[idx].active && wm->num_windows > 1) {
        int top_idx = -1;
        uint16_t top_layer = 0;
        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (wm->windows[i].active && wm->windows[i].layer_id > top_layer) {
                top_layer = wm->windows[i].layer_id;
                top_idx = i;
            }
        }

        int block_swap = wm->windows[idx].is_gui ||
                         (top_idx >= 0 && wm->windows[top_idx].is_gui);

        if (!block_swap && top_idx >= 0 && top_idx != idx) {
            uint16_t tmp = wm->windows[idx].layer_id;
            wm->windows[idx].layer_id = wm->windows[top_idx].layer_id;
            wm->windows[top_idx].layer_id = tmp;

            wm_render_window(wm, top_idx);
            wm_render_window(wm, idx);
            did_swap = 1;

            if (old_idx >= 0 && wm->windows[old_idx].active &&
                old_idx != top_idx && old_idx != idx) {
                wm_render_decorations(wm, old_idx);
                if (!wm->windows[old_idx].is_gui && wm->windows[old_idx].fb) {
                    fb_set_alpha(wm->windows[old_idx].fb,
                                 (uint32_t)wm->windows[old_idx].w * wm->windows[old_idx].h,
                                 INACTIVE_ALPHA);
                    wm_set_win_blend(&wm->windows[old_idx], 1);
                    wm_dirty_expand(&wm->windows[old_idx], 0, 0,
                                    wm->windows[old_idx].w, wm->windows[old_idx].h);
                    wm_submit_frame(&wm->windows[old_idx]);
                }
            }
        }
    }

    if (!did_swap) {
        if (old_idx >= 0 && wm->windows[old_idx].active) {
            wm_render_decorations(wm, old_idx);
            if (!wm->windows[old_idx].is_gui && wm->windows[old_idx].fb) {
                fb_set_alpha(wm->windows[old_idx].fb,
                             (uint32_t)wm->windows[old_idx].w * wm->windows[old_idx].h,
                             INACTIVE_ALPHA);
                wm_set_win_blend(&wm->windows[old_idx], 1);
                wm_dirty_expand(&wm->windows[old_idx], 0, 0,
                                wm->windows[old_idx].w, wm->windows[old_idx].h);
                wm_submit_frame(&wm->windows[old_idx]);
            }
        }
        if (idx >= 0 && wm->windows[idx].active) {
            if (!wm->windows[idx].is_gui && wm->windows[idx].fb) {
                fb_set_alpha(wm->windows[idx].fb,
                             (uint32_t)wm->windows[idx].w * wm->windows[idx].h,
                             0xFF);
                wm_set_win_blend(&wm->windows[idx], 0);
                wm_render_decorations(wm, idx);
                wm_dirty_expand(&wm->windows[idx], 0, 0,
                                wm->windows[idx].w, wm->windows[idx].h);
                wm_submit_frame(&wm->windows[idx]);
            }
        }
    }

    if (old_idx >= 0 && wm->windows[old_idx].active && wm->windows[old_idx].is_gui &&
        wm->windows[old_idx].pty_master_fd >= 0) {
        sg_gui_event_t fe;
        wm_ipc_fill_focus(&fe, 0);
        wm_ipc_send(wm->windows[old_idx].pty_master_fd, &fe);
    }
    if (idx >= 0 && wm->windows[idx].active && wm->windows[idx].is_gui &&
        wm->windows[idx].pty_master_fd >= 0) {
        sg_gui_event_t fe;
        wm_ipc_fill_focus(&fe, 1);
        wm_ipc_send(wm->windows[idx].pty_master_fd, &fe);
    }

    render_taskbar(wm);
}

/* --- signal handler --- */

static void sig_child(int sig) { (void)sig; }

/* --- main --- */

int main(void) {
    memset(&wm, 0, sizeof(wm));
    wm.focused_idx = -1;
    wm.tiling.master_ratio = 0.6f;
    wm.tiling.master_count = 1;
    wm.theme_current = 0;
    g_wm_theme = wm_themes[wm.theme_current];

    setvbuf(stdout, NULL, _IONBF, 0);
    signal(17, (void *)sig_child); /* SIGCHLD */
    signal(13, (void *)sig_child); /* SIGPIPE */

    wm_init_palette(&wm);

    /* grab keyboard input so VTY doesn't consume keystrokes */
    sys_5ht_grab_input(1);

    /* open input devices */
    wm.kb_fd = open("/dev/keyboard/event", O_RDONLY | O_NONBLOCK);
    if (wm.kb_fd < 0) {
        printf("wm: failed to open keyboard\n");
        return 1;
    }

    wm.mouse_fd = open("/dev/mouse/event", O_RDONLY | O_NONBLOCK);
    if (wm.mouse_fd < 0) {
        printf("wm: failed to open mouse\n");
        return 1;
    }

    /* init layers: desktop first (z=1), then cursor, taskbar */
    init_desktop(&wm);
    init_cursor_layer(&wm);
    init_taskbar(&wm);
    render_taskbar(&wm);

    /* create first terminal window */
    if (wm_create_window(&wm) < 0) {
        printf("wm: failed to create initial window\n");
        return 1;
    }

    printf("wm: started\n");

    /* --- event loop --- */
    while (1) {
        /* build pollfd array */
        struct pollfd pfds[2 + MAX_WINDOWS];
        int pfd_map[2 + MAX_WINDOWS]; /* maps pfd index to window index */
        int npfds = 0;

        pfds[npfds].fd = wm.kb_fd;
        pfds[npfds].events = POLLIN;
        pfds[npfds].revents = 0;
        int kb_idx = npfds++;

        pfds[npfds].fd = wm.mouse_fd;
        pfds[npfds].events = POLLIN;
        pfds[npfds].revents = 0;
        int mouse_idx = npfds++;

        for (int i = 0; i < MAX_WINDOWS; i++) {
            if (!wm.windows[i].active) continue;
            pfd_map[npfds] = i;
            pfds[npfds].fd = wm.windows[i].pty_master_fd;
            pfds[npfds].events = POLLIN;
            pfds[npfds].revents = 0;
            npfds++;
        }

        poll(pfds, npfds, WM_POLL_TIMEOUT_MS);

        /* handle keyboard */
        if (pfds[kb_idx].revents & POLLIN) {
            keyboard_event_t ev;
            while (read(wm.kb_fd, &ev, sizeof(ev)) == sizeof(ev)) {
                wm_handle_keyboard(&wm, &ev);
            }
        }

        /* handle mouse — drain all events, update cursor position once */
        if (pfds[mouse_idx].revents & POLLIN) {
            mouse_event_t ev;
            int mouse_moved = 0;
            while (read(wm.mouse_fd, &ev, sizeof(ev)) == sizeof(ev)) {
                wm_handle_mouse(&wm, &ev);
                mouse_moved = 1;
            }
            if (mouse_moved)
                move_cursor(&wm, wm.mouse_x, wm.mouse_y);
        }

        /* handle PTY output - batch: process all data, then render once per window */
        uint8_t pty_got_data[MAX_WINDOWS] = {0};
        int pty_closed = -1;
        for (int p = 2; p < npfds; p++) {
            if (!(pfds[p].revents & (POLLIN | POLLHUP))) continue;

            int win_idx = pfd_map[p];
            wm_window_t *win = &wm.windows[win_idx];

            if (win->is_gui) {
                if (pfds[p].revents & POLLIN) {
                    char drain[256];
                    while (read(win->pty_master_fd, drain, sizeof(drain)) > 0) { }
                }
                if (pfds[p].revents & POLLHUP) {
                    pty_closed = win_idx;
                    break;
                }
                continue;
            }

            char buf[65536];
            int n = read(win->pty_master_fd, buf, sizeof(buf));
            if (n > 0) {
                term_process(&win->term, buf, n);
                pty_got_data[win_idx] = 1;
            } else if (n <= 0 && (pfds[p].revents & POLLHUP)) {
                pty_closed = win_idx;
                break;
            }
        }

        if (pty_closed >= 0) {
            wm_close_window(&wm, pty_closed);
        } else {
            /* Render + submit only windows that received data */
            for (int i = 0; i < MAX_WINDOWS; i++) {
                if (!pty_got_data[i]) continue;
                wm_window_t *win = &wm.windows[i];
                term_render(win);
                if (!win->focused)
                    fb_set_alpha(win->fb, (uint32_t)win->w * win->h,
                                 INACTIVE_ALPHA);
                wm_submit_frame(win);
            }
        }

        /* cursor blink */
        blink_counter++;
        if (blink_counter >= BLINK_INTERVAL) {
            blink_counter = 0;
            cursor_blink_on ^= 1;
            if (wm.focused_idx >= 0 && wm.windows[wm.focused_idx].active &&
                !wm.windows[wm.focused_idx].is_gui) {
                wm_window_t *win = &wm.windows[wm.focused_idx];
                if (win->term.cursor_visible != cursor_blink_on) {
                    win->term.cursor_visible = cursor_blink_on;
                    term_render(win);
                    wm_submit_frame(win);
                }
            }
        }

        /* Flush desktop dirty rect (ghost cleanup for window moves) */
        desktop_submit(&wm);

    }

    return 0;
}
