#ifndef _WM_H
#define _WM_H

#include <stdint.h>
#include <stddef.h>
#include <lib5ht.h>

/* Screen and font dimensions */
#define SCREEN_W     1920
#define SCREEN_H     1080
#define FONT_W       8
#define FONT_H       16
#define BPP          4

/* Window limits */
#define MAX_WINDOWS  11

/* Decoration dimensions */
#define TITLEBAR_H   20
#define BORDER_W     2
#define CLOSE_BTN_W  16
#define CLOSE_BTN_H  16

/* Inactive window opacity (0x00=invisible, 0xFF=opaque) */
#define INACTIVE_ALPHA 0xC0

/* Taskbar */
#define TASKBAR_H    24

/* Launcher */
#define LAUNCHER_W       400
#define LAUNCHER_H       500
#define LAUNCHER_ITEM_H  20
#define LAUNCHER_PAD     8
#define LAUNCHER_MAX_ITEMS 256

#define WM_THEME_COUNT       7

/* Taskbar settings button (opens Settings GUI) */
#define SETTINGS_BTN_W    20
#define SETTINGS_BTN_H    18

/* Wallpapers */
#define WM_WALLPAPER_COUNT 8

/* Layer assignments */
#define LAYER_DESKTOP   1
#define LAYER_WIN_BASE  2
#define LAYER_WIN_MAX   12
#define LAYER_LAUNCHER  13
#define LAYER_TASKBAR   14
#define LAYER_CURSOR    15

typedef struct {
    char name[24];
    uint32_t bg_dark;
    uint32_t bg_medium;
    uint32_t accent;
    uint32_t accent_dim;
    uint32_t text_primary;
    uint32_t text_dim;
    uint32_t border;
    uint32_t close_btn;
    uint32_t titlebar_fg;
    uint32_t titlebar_bg;
    uint32_t titlebar_inactive;
    uint32_t border_active;
    uint32_t border_inactive;
    uint32_t term_fg;
    uint32_t term_bg;
} wm_theme_t;

extern wm_theme_t g_wm_theme;

/* Active theme colors (ARGB) */
#define THEME_BG_DARK          (g_wm_theme.bg_dark)
#define THEME_BG_MEDIUM        (g_wm_theme.bg_medium)
#define THEME_ACCENT           (g_wm_theme.accent)
#define THEME_ACCENT_DIM       (g_wm_theme.accent_dim)
#define THEME_TEXT_PRIMARY     (g_wm_theme.text_primary)
#define THEME_TEXT_DIM         (g_wm_theme.text_dim)
#define THEME_BORDER           (g_wm_theme.border)
#define THEME_CLOSE_BTN        (g_wm_theme.close_btn)
#define THEME_TITLEBAR_FG      (g_wm_theme.titlebar_fg)
#define THEME_TITLEBAR_BG      (g_wm_theme.titlebar_bg)
#define THEME_TITLEBAR_INACTIVE (g_wm_theme.titlebar_inactive)
#define THEME_BORDER_ACTIVE    (g_wm_theme.border_active)
#define THEME_BORDER_INACTIVE  (g_wm_theme.border_inactive)
#define THEME_TERM_FG          (g_wm_theme.term_fg)
#define THEME_TERM_BG          (g_wm_theme.term_bg)

/* ANSI 16-color table (no alpha, added at render time) */
static const uint32_t ansi_color_table[16] = {
    0x00000000, 0x00FF0000, 0x0000FF00, 0x00FFFF00,
    0x000000FF, 0x00FF00FF, 0x0000FFFF, 0x00FFFFFF,
    0x00404040, 0x00FF4040, 0x0040FF40, 0x00FFFF40,
    0x004040FF, 0x00FF40FF, 0x0040FFFF, 0x00FFFFFF
};

/* VBE base palette for 256-color index 0-15 */
static const uint32_t vbe_base_colors[16] = {
    0x00000000, 0x000000AA, 0x0000AA00, 0x0000AAAA,
    0x00AA0000, 0x00AA00AA, 0x00AA5500, 0x00AAAAAA,
    0x00555555, 0x005555FF, 0x0055FF55, 0x0055FFFF,
    0x00FF5555, 0x00FF55FF, 0x00FFFF00, 0x00FFFFFF
};

/* Escape parser states */
enum {
    ESC_NORMAL = 0,
    ESC_GOT_ESC,
    ESC_IN_CSI
};

/* Terminal cell */
typedef struct {
    uint32_t fg;
    uint32_t bg;
    uint8_t  ch;
    uint8_t  bold;
    uint8_t  dirty;
} term_cell_t;

/* Terminal emulator state */
typedef struct {
    uint32_t cursor_col, cursor_row;
    uint32_t saved_col, saved_row;
    uint32_t fg, bg;
    uint8_t  bold;
    uint8_t  cursor_visible;
    uint32_t scroll_top, scroll_bot;
    uint8_t  in_alt_screen;
    uint32_t alt_col, alt_row;
    /* ESC parser */
    uint8_t  esc_state;
    char     esc_buf[64];
    int      esc_len;
    /* Grid */
    uint32_t cols, rows;
    term_cell_t *cells;
    term_cell_t *alt_cells;
    /* Last rendered cursor position (for clean blink transitions) */
    uint32_t render_cursor_col, render_cursor_row;
    /* Framebuffer scroll accumulator: renderer can memmove pixels
       instead of re-rendering every cell after a scroll. */
    int32_t  fb_scroll_delta;   /* >0 = scrolled up N lines, <0 = down */
    uint32_t fb_scroll_top;     /* top row of scroll region */
    uint32_t fb_scroll_bot;     /* bottom row of scroll region */
} term_state_t;

/* Window mode */
typedef enum {
    WIN_TILED = 0,
    WIN_FLOATING
} win_mode_t;

/* Drag mode */
typedef enum {
    DRAG_NONE = 0,
    DRAG_MOVE,
    DRAG_RESIZE
} drag_mode_t;

/* Window structure */
typedef struct {
    uint8_t  active;
    uint8_t  is_gui; /* 1: child owns compositor layer; pty_master_fd is WM end of socketpair */
    uint16_t layer_id;
    int      pty_master_fd;
    pid_t    child_pid;
    /* Full geometry (including decorations) */
    uint16_t x, y, w, h;
    /* Content area (inside decorations) */
    uint16_t cx, cy, cw, ch;
    /* Layer framebuffer */
    uint32_t *fb;
    volatile fb_layer_metadata_t *meta;
    uint32_t fb_stride_px;
    /* Terminal state */
    term_state_t term;
    /* Mode and focus */
    win_mode_t mode;
    uint8_t    focused;
    /* Dirty region (local to layer fb) */
    uint16_t dirty_x0, dirty_y0, dirty_x1, dirty_y1;
    /* Title */
    char title[64];
    /** Reassembled client→WM @c sg_gui_event_t records (GUI windows only). */
    uint8_t gui_rx_buf[128];
    uint16_t gui_rx_len;
} wm_window_t;

/* Drag state */
typedef struct {
    drag_mode_t mode;
    int win_idx;
    int start_mx, start_my;
    int start_wx, start_wy;
    int start_ww, start_wh;
} drag_state_t;

/* Tiling layout state */
typedef struct {
    float master_ratio;
    int   master_count;
} tiling_state_t;

/* Global WM state */
typedef struct {
    wm_window_t windows[MAX_WINDOWS];
    int         num_windows;
    int         focused_idx;
    int         mouse_x, mouse_y;
    uint8_t     mouse_buttons;
    /* Input fds */
    int kb_fd;
    int mouse_fd;
    /* Taskbar layer */
    uint16_t     taskbar_layer;
    uint32_t    *taskbar_fb;
    volatile fb_layer_metadata_t *taskbar_meta;
    /* Cursor layer */
    uint16_t     cursor_layer;
    uint32_t    *cursor_fb;
    volatile fb_layer_metadata_t *cursor_meta;
    /* Tiling */
    tiling_state_t tiling;
    /* Drag */
    drag_state_t drag;
    /* Launcher */
    uint8_t  launcher_active;
    uint32_t *launcher_fb;
    volatile fb_layer_metadata_t *launcher_meta;
    int      launcher_selected;
    int      launcher_scroll;
    int      launcher_count;
    char     launcher_items[LAUNCHER_MAX_ITEMS][32];
    char     launcher_search[32];
    int      launcher_search_len;
    int      launcher_filtered[LAUNCHER_MAX_ITEMS];
    int      launcher_filtered_count;
    int      theme_current;
    int      wallpaper_current;
    /* Desktop background layer */
    uint32_t    *desktop_fb;
    volatile fb_layer_metadata_t *desktop_meta;
    uint16_t     desk_dirty_x0, desk_dirty_y0, desk_dirty_x1, desk_dirty_y1;
    /* 256-color palette */
    uint32_t palette[256];
} wm_state_t;

/* --- wm_draw.c --- */
void sse2_copy_fwd(uint32_t *dst, const uint32_t *src, uint32_t count);
void sse2_copy_bwd(uint32_t *dst, const uint32_t *src, uint32_t count);
void draw_fill_rect(uint32_t *fb, uint32_t stride_px, int x, int y,
                    int w, int h, uint32_t color);
void draw_glyph(uint32_t *fb, uint32_t stride_px, int x, int y,
                const uint8_t *data, uint32_t fg, uint32_t bg);
void draw_char(uint32_t *fb, uint32_t stride_px, int x, int y,
               char ch, uint8_t bold, uint32_t fg, uint32_t bg);
void draw_text(uint32_t *fb, uint32_t stride_px, int x, int y,
               const char *str, uint32_t fg, uint32_t bg);
void fb_set_alpha(uint32_t *fb, uint32_t pixel_count, uint8_t alpha);

/* --- wm_terminal.c --- */
void term_init(term_state_t *ts, uint32_t cols, uint32_t rows);
void term_free(term_state_t *ts);
void term_resize(term_state_t *ts, uint32_t new_cols, uint32_t new_rows);
void term_process(term_state_t *ts, const char *data, int len);
void term_render(wm_window_t *win);
void term_mark_all_dirty(term_state_t *ts);

/* --- wm_layout.c --- */
void layout_compute(wm_state_t *wm);
int  count_tiled_windows(wm_state_t *wm);

/* --- wm_input.c --- */
void wm_handle_keyboard(wm_state_t *wm, keyboard_event_t *ev);
void wm_handle_mouse(wm_state_t *wm, mouse_event_t *ev);

/* --- wm.c --- */
int  wm_create_window(wm_state_t *wm);
int  wm_launch_window(wm_state_t *wm, const char *program);
int  wm_launch_gui_window(wm_state_t *wm, const char *program);
void wm_close_window(wm_state_t *wm, int idx);
void wm_focus_window(wm_state_t *wm, int idx);
void wm_render_window(wm_state_t *wm, int idx);
void wm_render_decorations(wm_state_t *wm, int idx);
void wm_submit_frame(wm_window_t *win);
void wm_dirty_reset(wm_window_t *win);
void wm_dirty_expand(wm_window_t *win, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void wm_init_palette(wm_state_t *wm);
void desktop_mark_dirty(wm_state_t *wm, uint16_t x, uint16_t y, uint16_t w, uint16_t h);
void desktop_submit(wm_state_t *wm);
void launcher_open(wm_state_t *wm);
void launcher_close(wm_state_t *wm);
void launcher_render(wm_state_t *wm);
void launcher_key(wm_state_t *wm, keyboard_event_t *ev);
void wm_apply_theme(wm_state_t *wm, int theme_idx);
void wm_render_wallpaper(wm_state_t *wm);
void wm_apply_wallpaper(wm_state_t *wm, int wp_idx);

#endif
