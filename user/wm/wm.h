#ifndef _WM_H
#define _WM_H

#include <stdint.h>
#include <stddef.h>
#include "../syscall/lib5ht/lib5ht.h"

/* Screen and font dimensions */
#define SCREEN_W     1920
#define SCREEN_H     1080
#define FONT_W       8
#define FONT_H       16
#define BPP          4

/* Window limits */
#define MAX_WINDOWS  12

/* Decoration dimensions */
#define TITLEBAR_H   20
#define BORDER_W     2
#define CLOSE_BTN_W  16
#define CLOSE_BTN_H  16

/* Taskbar */
#define TASKBAR_H    24

/* Layer assignments */
#define LAYER_WIN_BASE  1
#define LAYER_WIN_MAX   12
#define LAYER_LAUNCHER  13
#define LAYER_TASKBAR   14
#define LAYER_CURSOR    15

/* Theme colors (ARGB) */
#define THEME_BG_DARK       0xFF1A1A2E
#define THEME_BG_MEDIUM     0xFF252540
#define THEME_ACCENT        0xFF7A98FF
#define THEME_ACCENT_DIM    0xFF4A6099
#define THEME_TEXT_PRIMARY   0xFFE0E0E0
#define THEME_TEXT_DIM       0xFF808090
#define THEME_BORDER         0xFF3A3A50
#define THEME_CLOSE_BTN     0xFFFF4040
#define THEME_TITLEBAR_FG   0xFFFFFFFF
#define THEME_TITLEBAR_BG   0xFF7A98FF
#define THEME_TITLEBAR_INACTIVE 0xFF404050
#define THEME_BORDER_ACTIVE 0xFF7A98FF
#define THEME_BORDER_INACTIVE 0xFF303040
#define THEME_TERM_FG       0xFFE0E0E0
#define THEME_TERM_BG       0xFF1A1A2E

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
    /* Title */
    char title[64];
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
    /* Launcher active */
    uint8_t launcher_active;
    /* 256-color palette */
    uint32_t palette[256];
} wm_state_t;

/* --- wm_draw.c --- */
void draw_fill_rect(uint32_t *fb, uint32_t stride_px, int x, int y,
                    int w, int h, uint32_t color);
void draw_glyph(uint32_t *fb, uint32_t stride_px, int x, int y,
                const uint8_t *data, uint32_t fg, uint32_t bg);
void draw_char(uint32_t *fb, uint32_t stride_px, int x, int y,
               char ch, uint8_t bold, uint32_t fg, uint32_t bg);
void draw_text(uint32_t *fb, uint32_t stride_px, int x, int y,
               const char *str, uint32_t fg, uint32_t bg);

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
void wm_close_window(wm_state_t *wm, int idx);
void wm_focus_window(wm_state_t *wm, int idx);
void wm_render_window(wm_state_t *wm, int idx);
void wm_render_decorations(wm_state_t *wm, int idx);
void wm_submit_frame(wm_window_t *win);
void wm_init_palette(wm_state_t *wm);

#endif
