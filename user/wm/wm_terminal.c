#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "wm.h"

/* --- helpers --- */

static inline term_cell_t *cell_at(term_state_t *ts, uint32_t col, uint32_t row) {
    return &ts->cells[row * ts->cols + col];
}

static void clear_cell(term_cell_t *c, uint32_t fg, uint32_t bg) {
    c->ch = ' ';
    c->fg = fg;
    c->bg = bg;
    c->bold = 0;
    c->dirty = 1;
}

static void clear_row(term_state_t *ts, uint32_t row) {
    for (uint32_t c = 0; c < ts->cols; c++)
        clear_cell(cell_at(ts, c, row), ts->fg, ts->bg);
}

static uint32_t scroll_bottom(term_state_t *ts) {
    return ts->scroll_bot ? ts->scroll_bot : ts->rows - 1;
}

/* --- scroll --- */

static void scroll_up(term_state_t *ts, uint32_t top, uint32_t bot, uint32_t n) {
    if (n == 0 || top > bot || bot >= ts->rows) return;
    if (n > bot - top + 1) n = bot - top + 1;

    uint32_t cols = ts->cols;
    /* move cell rows up */
    memmove(&ts->cells[top * cols],
            &ts->cells[(top + n) * cols],
            (bot - top + 1 - n) * cols * sizeof(term_cell_t));
    /* clear bottom n rows (clear_row marks them dirty) */
    for (uint32_t r = bot - n + 1; r <= bot; r++)
        clear_row(ts, r);

    /* Accumulate FB scroll: if the region matches the pending one (or no
       pending scroll yet), we can batch.  Otherwise flush to dirty-all. */
    if (ts->fb_scroll_delta >= 0 &&
        (ts->fb_scroll_delta == 0 ||
         (ts->fb_scroll_top == top && ts->fb_scroll_bot == bot))) {
        ts->fb_scroll_delta += (int32_t)n;
        ts->fb_scroll_top = top;
        ts->fb_scroll_bot = bot;
    } else {
        /* Mixed scroll directions or region change — fall back to dirty-all */
        ts->fb_scroll_delta = 0;
        for (uint32_t r = top; r <= bot; r++)
            for (uint32_t c = 0; c < cols; c++)
                ts->cells[r * cols + c].dirty = 1;
    }
}

static void scroll_down(term_state_t *ts, uint32_t top, uint32_t bot, uint32_t n) {
    if (n == 0 || top > bot || bot >= ts->rows) return;
    if (n > bot - top + 1) n = bot - top + 1;

    uint32_t cols = ts->cols;
    memmove(&ts->cells[(top + n) * cols],
            &ts->cells[top * cols],
            (bot - top + 1 - n) * cols * sizeof(term_cell_t));
    for (uint32_t r = top; r < top + n; r++)
        clear_row(ts, r);

    if (ts->fb_scroll_delta <= 0 &&
        (ts->fb_scroll_delta == 0 ||
         (ts->fb_scroll_top == top && ts->fb_scroll_bot == bot))) {
        ts->fb_scroll_delta -= (int32_t)n;
        ts->fb_scroll_top = top;
        ts->fb_scroll_bot = bot;
    } else {
        ts->fb_scroll_delta = 0;
        for (uint32_t r = top; r <= bot; r++)
            for (uint32_t c = 0; c < cols; c++)
                ts->cells[r * cols + c].dirty = 1;
    }
}

/* --- init / free --- */

void term_init(term_state_t *ts, uint32_t cols, uint32_t rows) {
    memset(ts, 0, sizeof(*ts));
    ts->cols = cols;
    ts->rows = rows;
    ts->fg = THEME_TERM_FG;
    ts->bg = THEME_TERM_BG;
    ts->cursor_visible = 1;
    ts->scroll_top = 0;
    ts->scroll_bot = 0; /* 0 = use rows-1 */
    ts->render_cursor_col = 0;
    ts->render_cursor_row = 0;

    size_t sz = cols * rows * sizeof(term_cell_t);
    ts->cells = malloc(sz);
    ts->alt_cells = malloc(sz);
    for (uint32_t i = 0; i < cols * rows; i++) {
        clear_cell(&ts->cells[i], ts->fg, ts->bg);
        clear_cell(&ts->alt_cells[i], ts->fg, ts->bg);
    }
}

void term_free(term_state_t *ts) {
    free(ts->cells);
    free(ts->alt_cells);
    ts->cells = NULL;
    ts->alt_cells = NULL;
}

void term_resize(term_state_t *ts, uint32_t new_cols, uint32_t new_rows) {
    size_t new_sz = new_cols * new_rows * sizeof(term_cell_t);
    term_cell_t *new_cells = malloc(new_sz);
    term_cell_t *new_alt = malloc(new_sz);

    for (uint32_t i = 0; i < new_cols * new_rows; i++) {
        clear_cell(&new_cells[i], ts->fg, ts->bg);
        clear_cell(&new_alt[i], ts->fg, ts->bg);
    }

    /* copy existing content */
    uint32_t copy_rows = (ts->rows < new_rows) ? ts->rows : new_rows;
    uint32_t copy_cols = (ts->cols < new_cols) ? ts->cols : new_cols;
    for (uint32_t r = 0; r < copy_rows; r++) {
        for (uint32_t c = 0; c < copy_cols; c++) {
            new_cells[r * new_cols + c] = ts->cells[r * ts->cols + c];
            new_cells[r * new_cols + c].dirty = 1;
        }
    }

    free(ts->cells);
    free(ts->alt_cells);
    ts->cells = new_cells;
    ts->alt_cells = new_alt;
    ts->cols = new_cols;
    ts->rows = new_rows;
    ts->scroll_top = 0;
    ts->scroll_bot = 0;
    if (ts->cursor_col >= new_cols) ts->cursor_col = new_cols - 1;
    if (ts->cursor_row >= new_rows) ts->cursor_row = new_rows - 1;
}

void term_mark_all_dirty(term_state_t *ts) {
    uint32_t total = ts->cols * ts->rows;
    for (uint32_t i = 0; i < total; i++)
        ts->cells[i].dirty = 1;
}

/* --- 256-color palette generation --- */

static uint32_t palette_256[256];
static int palette_init_done = 0;

static void ensure_palette(void) {
    if (palette_init_done) return;
    palette_init_done = 1;

    /* 0-15: standard + bright colors (use the VBE base colors) */
    palette_256[0]  = 0x000000; palette_256[1]  = 0x0000AA;
    palette_256[2]  = 0x00AA00; palette_256[3]  = 0x00AAAA;
    palette_256[4]  = 0xAA0000; palette_256[5]  = 0xAA00AA;
    palette_256[6]  = 0xAA5500; palette_256[7]  = 0xAAAAAA;
    palette_256[8]  = 0x555555; palette_256[9]  = 0x5555FF;
    palette_256[10] = 0x55FF55; palette_256[11] = 0x55FFFF;
    palette_256[12] = 0xFF5555; palette_256[13] = 0xFF55FF;
    palette_256[14] = 0xFFFF00; palette_256[15] = 0xFFFFFF;

    /* 16-231: 6x6x6 color cube */
    int idx = 16;
    for (int r = 0; r < 6; r++) {
        for (int g = 0; g < 6; g++) {
            for (int b = 0; b < 6; b++) {
                uint8_t rr = (r == 0) ? 0 : 55 + r * 40;
                uint8_t gg = (g == 0) ? 0 : 55 + g * 40;
                uint8_t bb = (b == 0) ? 0 : 55 + b * 40;
                palette_256[idx++] = ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | bb;
            }
        }
    }

    /* 232-255: grayscale ramp */
    for (int i = 0; i < 24; i++) {
        uint8_t level = 8 + i * 10;
        palette_256[idx++] = ((uint32_t)level << 16) | ((uint32_t)level << 8) | level;
    }
}

/* --- CSI handler --- */

static void handle_csi(term_state_t *ts) {
    char buf[64];
    int len = ts->esc_len;
    if (len <= 1) return; /* need at least '[' + command */

    /* esc_buf[0] is '[', content starts at [1] */
    int content_len = len - 1;
    if (content_len >= (int)sizeof(buf)) content_len = sizeof(buf) - 1;
    memcpy(buf, ts->esc_buf + 1, content_len);
    buf[content_len] = '\0';

    char cmd = buf[content_len - 1];
    buf[content_len - 1] = '\0';

    /* DEC private mode? */
    int dec_private = 0;
    char *param_start = buf;
    if (buf[0] == '?') {
        dec_private = 1;
        param_start = buf + 1;
    }

    if (dec_private) {
        int code = atoi(param_start);
        if (cmd == 'h') {
            if (code == 25)
                ts->cursor_visible = 1;
            else if (code == 1049) {
                if (!ts->in_alt_screen) {
                    ts->alt_col = ts->cursor_col;
                    ts->alt_row = ts->cursor_row;
                    ts->in_alt_screen = 1;
                    /* swap to alt buffer */
                    term_cell_t *tmp = ts->cells;
                    ts->cells = ts->alt_cells;
                    ts->alt_cells = tmp;
                    /* clear alt screen */
                    for (uint32_t i = 0; i < ts->cols * ts->rows; i++)
                        clear_cell(&ts->cells[i], ts->fg, ts->bg);
                    ts->cursor_col = 0;
                    ts->cursor_row = 0;
                }
            }
        } else if (cmd == 'l') {
            if (code == 25)
                ts->cursor_visible = 0;
            else if (code == 1049) {
                if (ts->in_alt_screen) {
                    ts->in_alt_screen = 0;
                    term_cell_t *tmp = ts->cells;
                    ts->cells = ts->alt_cells;
                    ts->alt_cells = tmp;
                    ts->cursor_col = ts->alt_col;
                    ts->cursor_row = ts->alt_row;
                    term_mark_all_dirty(ts);
                }
            }
        }
        return;
    }

    /* Parse semicolon-separated params */
    char *params[16];
    int count = 0;
    {
        /* manual tokenize since strtok modifies state */
        char *p = param_start;
        while (*p && count < 16) {
            params[count++] = p;
            while (*p && *p != ';') p++;
            if (*p == ';') { *p = '\0'; p++; }
        }
    }

    uint32_t max_cols = ts->cols;
    uint32_t max_rows = ts->rows;
    uint32_t sbot = scroll_bottom(ts);

    switch (cmd) {
    case 'm': { /* SGR */
        if (count == 0) {
            ts->fg = THEME_TERM_FG;
            ts->bg = THEME_TERM_BG;
            ts->bold = 0;
            return;
        }
        ensure_palette();
        for (int i = 0; i < count; i++) {
            int code = atoi(params[i]);
            if (code == 0) {
                ts->fg = THEME_TERM_FG;
                ts->bg = THEME_TERM_BG;
                ts->bold = 0;
            } else if (code == 1) {
                ts->bold = 1;
            } else if (code == 22) {
                ts->bold = 0;
            } else if (code >= 30 && code <= 37) {
                ts->fg = 0xFF000000 | ansi_color_table[code - 30];
            } else if (code == 39) {
                ts->fg = THEME_TERM_FG;
            } else if (code >= 40 && code <= 47) {
                ts->bg = 0xFF000000 | ansi_color_table[code - 40];
            } else if (code == 49) {
                ts->bg = THEME_TERM_BG;
            } else if (code >= 90 && code <= 97) {
                ts->fg = 0xFF000000 | ansi_color_table[8 + (code - 90)];
            } else if (code >= 100 && code <= 107) {
                ts->bg = 0xFF000000 | ansi_color_table[8 + (code - 100)];
            } else if (code == 38 || code == 48) {
                uint8_t is_fg = (code == 38);
                if (i + 1 < count) {
                    int sub = atoi(params[i + 1]);
                    if (sub == 5 && i + 2 < count) {
                        int idx = atoi(params[i + 2]);
                        if (idx < 0) idx = 0;
                        if (idx > 255) idx = 255;
                        uint32_t color = 0xFF000000 | palette_256[idx];
                        if (is_fg) ts->fg = color; else ts->bg = color;
                        i += 2;
                    } else if (sub == 2 && i + 4 < count) {
                        uint8_t r = (uint8_t)atoi(params[i + 2]);
                        uint8_t g = (uint8_t)atoi(params[i + 3]);
                        uint8_t b = (uint8_t)atoi(params[i + 4]);
                        uint32_t color = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
                        if (is_fg) ts->fg = color; else ts->bg = color;
                        i += 4;
                    }
                }
            }
        }
        break;
    }

    case 'H': case 'f': { /* cursor position */
        uint32_t row = (count >= 1) ? (uint32_t)atoi(params[0]) : 1;
        uint32_t col = (count >= 2) ? (uint32_t)atoi(params[1]) : 1;
        if (row < 1) row = 1;
        if (col < 1) col = 1;
        if (row > max_rows) row = max_rows;
        if (col > max_cols) col = max_cols;
        ts->cursor_row = row - 1;
        ts->cursor_col = col - 1;
        break;
    }

    case 'A': { /* cursor up */
        uint32_t n = (count >= 1 && atoi(params[0]) > 0) ? (uint32_t)atoi(params[0]) : 1;
        ts->cursor_row = (ts->cursor_row >= n) ? ts->cursor_row - n : 0;
        break;
    }

    case 'B': { /* cursor down */
        uint32_t n = (count >= 1 && atoi(params[0]) > 0) ? (uint32_t)atoi(params[0]) : 1;
        uint32_t row = ts->cursor_row + n;
        if (row >= max_rows) row = max_rows - 1;
        ts->cursor_row = row;
        break;
    }

    case 'C': { /* cursor forward */
        uint32_t n = (count >= 1 && atoi(params[0]) > 0) ? (uint32_t)atoi(params[0]) : 1;
        uint32_t col = ts->cursor_col + n;
        if (col >= max_cols) col = max_cols - 1;
        ts->cursor_col = col;
        break;
    }

    case 'D': { /* cursor back */
        uint32_t n = (count >= 1 && atoi(params[0]) > 0) ? (uint32_t)atoi(params[0]) : 1;
        ts->cursor_col = (ts->cursor_col >= n) ? ts->cursor_col - n : 0;
        break;
    }

    case 'J': { /* erase in display */
        int mode = (count > 0) ? atoi(params[0]) : 0;
        if (mode == 0) {
            /* cursor to end */
            for (uint32_t c = ts->cursor_col; c < max_cols; c++)
                clear_cell(cell_at(ts, c, ts->cursor_row), ts->fg, ts->bg);
            for (uint32_t r = ts->cursor_row + 1; r < max_rows; r++)
                clear_row(ts, r);
        } else if (mode == 1) {
            /* start to cursor */
            for (uint32_t r = 0; r < ts->cursor_row; r++)
                clear_row(ts, r);
            for (uint32_t c = 0; c <= ts->cursor_col && c < max_cols; c++)
                clear_cell(cell_at(ts, c, ts->cursor_row), ts->fg, ts->bg);
        } else if (mode == 2) {
            for (uint32_t r = 0; r < max_rows; r++)
                clear_row(ts, r);
            ts->cursor_col = 0;
            ts->cursor_row = 0;
        }
        break;
    }

    case 'K': { /* erase in line */
        int mode = (count > 0) ? atoi(params[0]) : 0;
        if (mode == 0) {
            for (uint32_t c = ts->cursor_col; c < max_cols; c++)
                clear_cell(cell_at(ts, c, ts->cursor_row), ts->fg, ts->bg);
        } else if (mode == 1) {
            for (uint32_t c = 0; c <= ts->cursor_col && c < max_cols; c++)
                clear_cell(cell_at(ts, c, ts->cursor_row), ts->fg, ts->bg);
        } else if (mode == 2) {
            clear_row(ts, ts->cursor_row);
        }
        break;
    }

    case 'r': { /* scroll region */
        uint32_t top = (count >= 1) ? (uint32_t)atoi(params[0]) : 1;
        uint32_t bot = (count >= 2) ? (uint32_t)atoi(params[1]) : max_rows;
        if (top < 1) top = 1;
        if (bot > max_rows) bot = max_rows;
        if (top >= bot) { top = 1; bot = max_rows; }
        ts->scroll_top = top - 1;
        ts->scroll_bot = bot - 1;
        ts->cursor_col = 0;
        ts->cursor_row = 0;
        break;
    }

    case 's': { /* save cursor */
        ts->saved_col = ts->cursor_col;
        ts->saved_row = ts->cursor_row;
        break;
    }

    case 'u': { /* restore cursor */
        ts->cursor_col = ts->saved_col;
        ts->cursor_row = ts->saved_row;
        break;
    }

    case 'L': { /* insert lines */
        uint32_t n = (count >= 1 && atoi(params[0]) > 0) ? (uint32_t)atoi(params[0]) : 1;
        if (ts->cursor_row <= sbot)
            scroll_down(ts, ts->cursor_row, sbot, n);
        break;
    }

    case 'M': { /* delete lines */
        uint32_t n = (count >= 1 && atoi(params[0]) > 0) ? (uint32_t)atoi(params[0]) : 1;
        if (ts->cursor_row <= sbot)
            scroll_up(ts, ts->cursor_row, sbot, n);
        break;
    }

    case '@': { /* insert characters */
        uint32_t n = (count >= 1 && atoi(params[0]) > 0) ? (uint32_t)atoi(params[0]) : 1;
        uint32_t row = ts->cursor_row;
        if (ts->cursor_col + n < max_cols) {
            memmove(cell_at(ts, ts->cursor_col + n, row),
                    cell_at(ts, ts->cursor_col, row),
                    (max_cols - ts->cursor_col - n) * sizeof(term_cell_t));
        }
        for (uint32_t c = ts->cursor_col; c < ts->cursor_col + n && c < max_cols; c++)
            clear_cell(cell_at(ts, c, row), ts->fg, ts->bg);
        for (uint32_t c = ts->cursor_col; c < max_cols; c++)
            cell_at(ts, c, row)->dirty = 1;
        break;
    }

    case 'P': { /* delete characters */
        uint32_t n = (count >= 1 && atoi(params[0]) > 0) ? (uint32_t)atoi(params[0]) : 1;
        uint32_t row = ts->cursor_row;
        if (ts->cursor_col + n < max_cols) {
            memmove(cell_at(ts, ts->cursor_col, row),
                    cell_at(ts, ts->cursor_col + n, row),
                    (max_cols - ts->cursor_col - n) * sizeof(term_cell_t));
        }
        for (uint32_t c = max_cols - n; c < max_cols; c++)
            clear_cell(cell_at(ts, c, row), ts->fg, ts->bg);
        for (uint32_t c = ts->cursor_col; c < max_cols; c++)
            cell_at(ts, c, row)->dirty = 1;
        break;
    }

    case 'S': { /* scroll up */
        uint32_t n = (count >= 1 && atoi(params[0]) > 0) ? (uint32_t)atoi(params[0]) : 1;
        scroll_up(ts, ts->scroll_top, sbot, n);
        break;
    }

    case 'T': { /* scroll down */
        uint32_t n = (count >= 1 && atoi(params[0]) > 0) ? (uint32_t)atoi(params[0]) : 1;
        scroll_down(ts, ts->scroll_top, sbot, n);
        break;
    }

    case 'X': { /* erase characters */
        uint32_t n = (count >= 1 && atoi(params[0]) > 0) ? (uint32_t)atoi(params[0]) : 1;
        for (uint32_t c = ts->cursor_col; c < ts->cursor_col + n && c < max_cols; c++)
            clear_cell(cell_at(ts, c, ts->cursor_row), ts->fg, ts->bg);
        break;
    }

    case 'd': { /* VPA - cursor to row */
        uint32_t row = (count >= 1) ? (uint32_t)atoi(params[0]) : 1;
        if (row < 1) row = 1;
        if (row > max_rows) row = max_rows;
        ts->cursor_row = row - 1;
        break;
    }

    case 'G': { /* CHA - cursor to column */
        uint32_t col = (count >= 1) ? (uint32_t)atoi(params[0]) : 1;
        if (col < 1) col = 1;
        if (col > max_cols) col = max_cols;
        ts->cursor_col = col - 1;
        break;
    }

    default:
        break;
    }
}

/* --- putchar --- */

static void term_putchar(term_state_t *ts, char c) {
    uint32_t sbot = scroll_bottom(ts);

    switch (c) {
    case '\n':
        ts->cursor_col = 0; /* ONLCR-like: newline implies CR */
        if (ts->cursor_row >= sbot) {
            scroll_up(ts, ts->scroll_top, sbot, 1);
        } else {
            ts->cursor_row++;
        }
        return;
    case '\r':
        ts->cursor_col = 0;
        return;
    case '\t': {
        uint32_t next = (ts->cursor_col + 8) & ~7u;
        if (next >= ts->cols) next = ts->cols - 1;
        ts->cursor_col = next;
        return;
    }
    case '\b':
        if (ts->cursor_col > 0) ts->cursor_col--;
        return;
    case '\a': /* bell - ignore */
        return;
    default:
        break;
    }

    if ((unsigned char)c < 0x20) return; /* ignore other control chars */

    /* printable character */
    if (ts->cursor_col >= ts->cols) {
        /* wrap */
        ts->cursor_col = 0;
        if (ts->cursor_row >= sbot) {
            scroll_up(ts, ts->scroll_top, sbot, 1);
        } else {
            ts->cursor_row++;
        }
    }

    term_cell_t *cell = cell_at(ts, ts->cursor_col, ts->cursor_row);
    cell->ch = (uint8_t)c;
    cell->fg = ts->fg;
    cell->bg = ts->bg;
    cell->bold = ts->bold;
    cell->dirty = 1;
    ts->cursor_col++;
}

/* --- process bytes --- */

void term_process(term_state_t *ts, const char *data, int len) {
    for (int i = 0; i < len; i++) {
        char c = data[i];

        switch (ts->esc_state) {
        case ESC_NORMAL:
            if (c == '\033') {
                ts->esc_state = ESC_GOT_ESC;
                ts->esc_len = 0;
            } else {
                term_putchar(ts, c);
            }
            break;

        case ESC_GOT_ESC:
            if (c == '[') {
                ts->esc_buf[0] = '[';
                ts->esc_len = 1;
                ts->esc_state = ESC_IN_CSI;
            } else if (c == '7') {
                ts->saved_col = ts->cursor_col;
                ts->saved_row = ts->cursor_row;
                ts->esc_state = ESC_NORMAL;
            } else if (c == '8') {
                ts->cursor_col = ts->saved_col;
                ts->cursor_row = ts->saved_row;
                ts->esc_state = ESC_NORMAL;
            } else if (c == 'M') {
                /* reverse index */
                if (ts->cursor_row <= ts->scroll_top) {
                    scroll_down(ts, ts->scroll_top, scroll_bottom(ts), 1);
                } else {
                    ts->cursor_row--;
                }
                ts->esc_state = ESC_NORMAL;
            } else if (c == 'c') {
                /* full reset */
                ts->fg = THEME_TERM_FG;
                ts->bg = THEME_TERM_BG;
                ts->bold = 0;
                ts->scroll_top = 0;
                ts->scroll_bot = 0;
                for (uint32_t j = 0; j < ts->cols * ts->rows; j++)
                    clear_cell(&ts->cells[j], ts->fg, ts->bg);
                ts->cursor_col = 0;
                ts->cursor_row = 0;
                ts->esc_state = ESC_NORMAL;
            } else {
                /* unknown escape, discard */
                ts->esc_state = ESC_NORMAL;
            }
            break;

        case ESC_IN_CSI:
            if (ts->esc_len < (int)sizeof(ts->esc_buf) - 1)
                ts->esc_buf[ts->esc_len++] = c;

            if (isalpha(c) || c == '~' || c == '@') {
                ts->esc_buf[ts->esc_len] = '\0';
                handle_csi(ts);
                ts->esc_state = ESC_NORMAL;
            }
            break;
        }
    }
}

/* --- render --- */

/*
 * Framebuffer-level scroll: memmove pixel rows instead of re-rendering
 * every cell. Only the newly exposed rows need character rendering.
 */
static void fb_apply_scroll(wm_window_t *win) {
    term_state_t *ts = &win->term;
    int32_t delta = ts->fb_scroll_delta;
    if (delta == 0) return;
    ts->fb_scroll_delta = 0;

    uint32_t *fb = win->fb;
    uint32_t stride = win->fb_stride_px;
    uint32_t oy = TITLEBAR_H;
    uint32_t top = ts->fb_scroll_top;
    uint32_t bot = ts->fb_scroll_bot;
    uint32_t region_rows = bot - top + 1;

    if (delta > 0) {
        /* Scrolled up by delta lines */
        uint32_t n = (uint32_t)delta;
        if (n >= region_rows) return;

        /* Copy full-stride rows (includes borders — they're identical on
           every row so copying them is harmless and lets us do one big
           contiguous SSE2 copy instead of per-scanline memmove). */
        uint32_t src_py = oy + (top + n) * FONT_H;
        uint32_t dst_py = oy + top * FONT_H;
        uint32_t move_rows_px = (region_rows - n) * FONT_H;

        /* dst < src for scroll-up → forward copy is safe */
        sse2_copy_fwd(fb + dst_py * stride,
                      fb + src_py * stride,
                      move_rows_px * stride);

        wm_dirty_expand(win, 0, oy + top * FONT_H,
                        win->w, region_rows * FONT_H);
    } else {
        uint32_t n = (uint32_t)(-delta);
        if (n >= region_rows) return;

        uint32_t src_py = oy + top * FONT_H;
        uint32_t dst_py = oy + (top + n) * FONT_H;
        uint32_t move_rows_px = (region_rows - n) * FONT_H;

        /* dst > src for scroll-down → backward copy */
        sse2_copy_bwd(fb + dst_py * stride,
                      fb + src_py * stride,
                      move_rows_px * stride);

        wm_dirty_expand(win, 0, oy + top * FONT_H,
                        win->w, region_rows * FONT_H);
    }
}

void term_render(wm_window_t *win) {
    term_state_t *ts = &win->term;
    uint32_t *fb = win->fb;
    uint32_t stride = win->fb_stride_px;
    uint32_t ox = BORDER_W;
    uint32_t oy = TITLEBAR_H;
    uint32_t cols = ts->cols;
    uint32_t rows = ts->rows;

    /* Save scroll delta before fb_apply_scroll clears it */
    int32_t scroll_delta = ts->fb_scroll_delta;

    /* Apply pending FB-level scroll before rendering dirty cells */
    fb_apply_scroll(win);

    /*
     * Cursor ghost cleanup: fb_apply_scroll shifted the pixels, so the
     * previously-rendered cursor block moved with them.  Mark the
     * *shifted* position dirty (not the original render_cursor_row)
     * so it gets redrawn with normal colors.
     */
    int32_t ghost_row = (int32_t)ts->render_cursor_row - scroll_delta;
    if (ghost_row >= 0 && ghost_row < (int32_t)rows && ts->render_cursor_col < cols)
        ts->cells[ghost_row * cols + ts->render_cursor_col].dirty = 1;
    /* Also mark the original position (handles the non-scroll case) */
    if (ts->render_cursor_col < cols && ts->render_cursor_row < rows)
        ts->cells[ts->render_cursor_row * cols + ts->render_cursor_col].dirty = 1;
    /* Mark current cursor position */
    if (ts->cursor_col < cols && ts->cursor_row < rows)
        ts->cells[ts->cursor_row * cols + ts->cursor_col].dirty = 1;

    uint32_t min_c = cols, min_r = rows;
    uint32_t max_c = 0, max_r = 0;

    uint32_t cursor_col = ts->cursor_col;
    uint32_t cursor_row = ts->cursor_row;
    uint8_t  cursor_vis = ts->cursor_visible;

    for (uint32_t r = 0; r < rows; r++) {
        term_cell_t *row_start = &ts->cells[r * cols];

        /* Quick scan: skip entirely clean rows */
        uint32_t any_dirty = 0;
        uint32_t c = 0;
        for (; c + 3 < cols; c += 4) {
            any_dirty |= row_start[c].dirty | row_start[c+1].dirty |
                         row_start[c+2].dirty | row_start[c+3].dirty;
            if (any_dirty) break;
        }
        if (!any_dirty) {
            for (; c < cols; c++)
                if (row_start[c].dirty) { any_dirty = 1; break; }
        }
        if (!any_dirty) continue;

        int py = oy + r * FONT_H;
        if (r < min_r) min_r = r;
        if (r > max_r) max_r = r;

        for (c = 0; c < cols; c++) {
            term_cell_t *cell = &row_start[c];
            if (!cell->dirty) continue;
            cell->dirty = 0;

            uint32_t fg = cell->fg, bg = cell->bg;

            if (cursor_vis && c == cursor_col && r == cursor_row) {
                uint32_t tmp = fg; fg = bg; bg = tmp;
            }

            draw_char(fb, stride, ox + c * FONT_W, py,
                      cell->ch, cell->bold, fg, bg);

            if (c < min_c) min_c = c;
            if (c > max_c) max_c = c;
        }
    }

    if (min_c <= max_c && min_r <= max_r) {
        wm_dirty_expand(win,
                        ox + min_c * FONT_W, oy + min_r * FONT_H,
                        (max_c - min_c + 1) * FONT_W, (max_r - min_r + 1) * FONT_H);
    }

    ts->render_cursor_col = ts->cursor_col;
    ts->render_cursor_row = ts->cursor_row;
}
