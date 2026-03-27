#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../syscall/lib5ht/lib5ht.h"

#define CORTEX_EDITOR_VERSION "0.2.0"
#define CTRL_KEY(k) ((k) & 0x1f)
#define GUTTER_WIDTH 6
#define DEFAULT_HINT_MSG "CTRL+Q quit | CTRL+S save | CTRL+F find | CTRL+G goto"
#define SELECT_STYLE_ON "\x1b[30;47m"
#define SELECT_STYLE_OFF "\x1b[0m"
/* Cursor style when moving with arrow keys: bright white foreground. */
#define CURSOR_STYLE_ON "\x1b[97m"
#define CURSOR_STYLE_OFF "\x1b[0m"

/* Simple syntax highlight styles */
#define HL_KEYWORD_ON "\x1b[36m"
#define HL_STRING_ON "\x1b[32m"
#define HL_COMMENT_ON "\x1b[90m"
#define HL_NUMBER_ON "\x1b[35m"
#define HL_PPRE_ON "\x1b[95m"
#define HL_OFF "\x1b[0m"

enum editor_key {
	KEY_NULL = 0,
	KEY_ARROW_LEFT = 1000,
	KEY_ARROW_RIGHT,
	KEY_ARROW_UP,
	KEY_ARROW_DOWN,
	KEY_DELETE,
	KEY_HOME,
	KEY_END,
	KEY_PAGE_UP,
	KEY_PAGE_DOWN
};

typedef struct markup_row {
	int size;
	char *chars;
} markup_row_t;

typedef struct editor_config {
	int cx;
	int cy;
	int rowoff;
	int coloff;

	int screenrows;
	int screencols;

	int numrows;
	markup_row_t *rows;

	int dirty;
	char *filename;
	char statusmsg[96];
	time_t statusmsg_time;

	int raw_enabled;
	pty_attr_t orig_termios;
	int kbfd;

	int sel_active;
	int sel_anchor_cx;
	int sel_anchor_cy;

	/* When set, the character under the cursor is rendered in white. */
	int cursor_white;

	char *copybuf;
	int copybuf_len;
} editor_config_t;

static editor_config_t E;

struct abuf {
	char *b;
	int len;
	int cap;
};

#define ABUF_INIT { NULL, 0, 0 }

static void ab_append(struct abuf *ab, const char *s, int len) {
	if (len <= 0) {
		return;
	}

	int need = ab->len + len;
	if (need > ab->cap) {
		int new_cap = (ab->cap > 0) ? ab->cap : 256;
		while (new_cap < need) {
			new_cap *= 2;
		}
		char *new_buf = realloc(ab->b, (size_t)new_cap);
		if (!new_buf) {
			return;
		}
		ab->b = new_buf;
		ab->cap = new_cap;
	}

	memcpy(ab->b + ab->len, s, (size_t)len);
	ab->len += len;
}

static void ab_free(struct abuf *ab) {
	free(ab->b);
	ab->b = NULL;
	ab->len = 0;
	ab->cap = 0;
}

static void ab_move_cursor(struct abuf *ab, int row, int col) {
	char buf[32];
	if (row < 1) row = 1;
	if (col < 1) col = 1;
	snprintf(buf, sizeof(buf), "\x1b[%d;%dH", row, col);
	ab_append(ab, buf, (int)strlen(buf));
}

static void editor_apply_attr_on_stdio(const pty_attr_t *attr) {
	for (int fd = 0; fd <= 2; fd++) {
		ioctl(fd, TCSETS, (void *)attr);
	}
}

static void editor_restore_attr_on_stdio(const pty_attr_t *attr) {
	for (int fd = 0; fd <= 2; fd++) {
		ioctl(fd, TCSETS, (void *)attr);
	}
}

static void editor_free_rows(void);
static void disable_raw_mode(void);
static void editor_refresh_screen(void);
static int get_window_size(int *rows, int *cols);
static void editor_set_status_message(const char *fmt, ...);
static void editor_insert_char(int c);
static void editor_insert_newline(void);
static void editor_delete_selection(void);
static void editor_paste(void);
static int is_cpp_keyword(const char *s, int len);

static int g_last_key_flags = 0;
/* Simple runtime keymap: parsed from environment variable CORTEX_KEYMAP.
 * Format: entry;entry;...
 * entry := <dec bytes separated by ,>:ACTION
 * ACTION := CTRL | SHIFT | CTRL+SHIFT | MAP:<dec>
 * Example: "127:CTRL;27,91,65:MAP:1000" maps 0x7f to set CTRL flag,
 * and ESC [ A (27,91,65) to map to code 1000 (e.g. KEY_ARROW_UP).
 */
typedef struct {
	unsigned char seq[8];
	int len;
	int add_flags;
	int mapped_char; /* 0 = none */
} keymap_entry_t;

static keymap_entry_t g_keymap[16];
static int g_keymap_count = 0;

static void editor_load_keymap(void) {
	const char *env = getenv("CORTEX_KEYMAP");
	if (!env) return;

	char *copy = strdup(env);
	if (!copy) return;

	char *saveptr = NULL;
	char *tok = strtok_r(copy, ";", &saveptr);
	while (tok && g_keymap_count < (int)(sizeof(g_keymap)/sizeof(g_keymap[0]))) {
		char *sep = strchr(tok, ':');
		if (!sep) { tok = strtok_r(NULL, ";", &saveptr); continue; }
		*sep = '\0';
		char *left = tok;
		char *right = sep + 1;

		keymap_entry_t ent;
		memset(&ent, 0, sizeof(ent));

		/* parse left as comma-separated decimals */
		int idx = 0;
		char *p = strtok(left, ",");
		while (p && idx < (int)sizeof(ent.seq)) {
			int v = atoi(p);
			ent.seq[idx++] = (unsigned char)v;
			p = strtok(NULL, ",");
		}
		ent.len = idx;

		if (strcmp(right, "CTRL") == 0) {
			ent.add_flags = KEY_FLAG_CTRL;
		} else if (strcmp(right, "SHIFT") == 0) {
			ent.add_flags = KEY_FLAG_SHIFT;
		} else if (strcmp(right, "CTRL+SHIFT") == 0 || strcmp(right, "SHIFT+CTRL") == 0) {
			ent.add_flags = KEY_FLAG_CTRL | KEY_FLAG_SHIFT;
		} else if (strncmp(right, "MAP:", 4) == 0) {
			ent.mapped_char = atoi(right + 4);
		} else if (strncmp(right, "FLAGS:", 6) == 0) {
			/* numeric flags */
			ent.add_flags = atoi(right + 6);
		}

		if (ent.len > 0 && (ent.add_flags || ent.mapped_char)) {
			g_keymap[g_keymap_count++] = ent;
		}

		tok = strtok_r(NULL, ";", &saveptr);
	}

	free(copy);
}

static keymap_entry_t *editor_keymap_match(const unsigned char *buf, int len) {
	for (int i = 0; i < g_keymap_count; i++) {
		if (g_keymap[i].len == len && len <= (int)sizeof(g_keymap[i].seq) && len > 0) {
			if (memcmp(g_keymap[i].seq, buf, (size_t)len) == 0) return &g_keymap[i];
		}
	}
	return NULL;
}

static void editor_apply_csi_modifier_flags(int mod) {
	g_last_key_flags = 0;
	/* xterm-style modifier parameter: 2=Shift, 5=Ctrl, 6=Shift+Ctrl */
	if (mod == 2 || mod == 4 || mod == 6 || mod == 8) {
		g_last_key_flags |= KEY_FLAG_SHIFT;
	}
	if (mod == 5 || mod == 6 || mod == 7 || mod == 8) {
		g_last_key_flags |= KEY_FLAG_CTRL;
	}
}

static void editor_clear_selection(void) {
	E.sel_active = 0;
}

static int editor_selection_is_nonempty(void) {
	return E.sel_active && (E.sel_anchor_cx != E.cx || E.sel_anchor_cy != E.cy);
}

static void editor_selection_bounds(int *sx, int *sy, int *ex, int *ey) {
	if (!editor_selection_is_nonempty()) {
		*sx = *sy = *ex = *ey = 0;
		return;
	}

	int ax = E.sel_anchor_cx;
	int ay = E.sel_anchor_cy;
	int bx = E.cx;
	int by = E.cy;

	if (ay < 0) ay = 0;
	if (ay > E.numrows) ay = E.numrows;
	if (by < 0) by = 0;
	if (by > E.numrows) by = E.numrows;

	int amax = (ay < E.numrows) ? E.rows[ay].size : 0;
	int bmax = (by < E.numrows) ? E.rows[by].size : 0;
	if (ax < 0) ax = 0;
	if (ax > amax) ax = amax;
	if (bx < 0) bx = 0;
	if (bx > bmax) bx = bmax;

	if (ay < by || (ay == by && ax <= bx)) {
		*sx = ax; *sy = ay;
		*ex = bx; *ey = by;
	} else {
		*sx = bx; *sy = by;
		*ex = ax; *ey = ay;
	}
}

static int editor_is_selected_pos(int row, int col) {
	if (!editor_selection_is_nonempty()) {
		return 0;
	}

	int sx, sy, ex, ey;
	editor_selection_bounds(&sx, &sy, &ex, &ey);

	if (row < sy || row > ey) {
		return 0;
	}

	if (sy == ey) {
		return (row == sy && col >= sx && col < ex);
	}

	if (row == sy) {
		return col >= sx;
	}
	if (row == ey) {
		return col < ex;
	}

	return 1;
}

static void editor_set_copybuf(const char *data, int len) {
	free(E.copybuf);
	E.copybuf = NULL;
	E.copybuf_len = 0;

	if (!data || len <= 0) {
		return;
	}

	E.copybuf = malloc((size_t)len + 1);
	if (!E.copybuf) {
		return;
	}

	memcpy(E.copybuf, data, (size_t)len);
	E.copybuf[len] = '\0';
	E.copybuf_len = len;
}

static void editor_copy_selection(void) {
	if (!editor_selection_is_nonempty()) {
		editor_set_status_message("Copy: no selection");
		return;
	}

	int sx, sy, ex, ey;
	editor_selection_bounds(&sx, &sy, &ex, &ey);

	int first_row = sy;
	int last_row = ey;
	if (E.numrows <= 0) {
		editor_set_status_message("Copy: empty buffer");
		return;
	}
	if (first_row >= E.numrows) {
		first_row = E.numrows - 1;
		sx = E.rows[first_row].size;
	}
	if (last_row >= E.numrows) {
		last_row = E.numrows - 1;
		ex = E.rows[last_row].size;
	}

	int total = 0;
	for (int row = first_row; row <= last_row; row++) {
		if (row < 0 || row >= E.numrows) {
			continue;
		}
		int start = (row == sy) ? sx : 0;
		int end = (row == last_row) ? ex : E.rows[row].size;
		if (start < 0) start = 0;
		if (start > E.rows[row].size) start = E.rows[row].size;
		if (end < start) end = start;
		if (end > E.rows[row].size) end = E.rows[row].size;
		if (end > start) {
			total += end - start;
		}
		if (row != last_row) {
			total++;
		}
	}

	if (total <= 0) {
		editor_set_status_message("Copy: empty selection");
		return;
	}

	char *buf = malloc((size_t)total);
	if (!buf) {
		editor_set_status_message("Copy failed: out of memory");
		return;
	}

	char *p = buf;
	for (int row = first_row; row <= last_row; row++) {
		int start = (row == first_row) ? sx : 0;
		int end = (row == last_row) ? ex : E.rows[row].size;
		if (start < 0) start = 0;
		if (start > E.rows[row].size) start = E.rows[row].size;
		if (end < start) end = start;
		if (end > E.rows[row].size) end = E.rows[row].size;
		if (end > start) {
			memcpy(p, &E.rows[row].chars[start], (size_t)(end - start));
			p += end - start;
		}
		if (row != last_row) {
			*p++ = '\n';
		}
	}

	editor_set_copybuf(buf, total);
	free(buf);
	editor_set_status_message("Copied %d bytes", E.copybuf_len);
}

static void editor_delete_selection(void) {
	if (!editor_selection_is_nonempty()) return;

	int sx, sy, ex, ey;
	editor_selection_bounds(&sx, &sy, &ex, &ey);

	if (sy == ey) {
		/* single-line deletion */
		markup_row_t *row = &E.rows[sy];
		int tail = row->size - ex;
		if (tail > 0) {
			memmove(&row->chars[sx], &row->chars[ex], (size_t)tail + 1);
		} else {
			row->chars[sx] = '\0';
		}
		row->size -= (ex - sx);
		E.cx = sx;
		E.cy = sy;
	} else {
		/* multi-line deletion: merge head of first and tail of last */
		markup_row_t *first = &E.rows[sy];
		markup_row_t *last = &E.rows[ey];

		int new_first_size = sx + (last->size - ex);
		char *new_chars = malloc((size_t)new_first_size + 1);
		if (!new_chars) return;

		if (sx > 0) memcpy(new_chars, first->chars, (size_t)sx);
		if (last->size > ex) memcpy(new_chars + sx, last->chars + ex, (size_t)(last->size - ex));
		new_chars[new_first_size] = '\0';

		free(first->chars);
		first->chars = new_chars;
		first->size = new_first_size;

		/* remove rows sy+1 .. ey inclusive */
		int remove_count = ey - sy;
		for (int i = sy + 1; i + remove_count < E.numrows; i++) {
			E.rows[i] = E.rows[i + remove_count];
		}
		E.numrows -= remove_count;

		E.cx = sx;
		E.cy = sy;
	}

	E.dirty = 1;
	editor_clear_selection();
}

static void editor_paste(void) {
	if (!E.copybuf || E.copybuf_len <= 0) {
		editor_set_status_message("Paste: clipboard empty");
		return;
	}

	if (editor_selection_is_nonempty()) {
		editor_delete_selection();
	}

	for (int i = 0; i < E.copybuf_len; i++) {
		unsigned char ch = (unsigned char)E.copybuf[i];
		if (ch == '\n') {
			editor_insert_newline();
		} else {
			editor_insert_char((int)ch);
		}
	}

	editor_set_status_message("Pasted %d bytes", E.copybuf_len);
}

static void editor_update_window_size(void) {
	int rows = 0;
	int cols = 0;

	if (get_window_size(&rows, &cols) == -1) {
		rows = 25;
		cols = 80;
	}

	if (cols < GUTTER_WIDTH + 1) {
		cols = GUTTER_WIDTH + 1;
	}

	rows -= 2; /* status + message bar */
	if (rows < 1) {
		rows = 1;
	}

	E.screenrows = rows;
	E.screencols = cols;
}

static void editor_clamp_state(void) {
	if (E.numrows < 0) {
		E.numrows = 0;
	}

	if (E.cy < 0) {
		E.cy = 0;
	}
	if (E.cy > E.numrows) {
		E.cy = E.numrows;
	}

	int rowlen = 0;
	if (E.cy < E.numrows) {
		rowlen = E.rows[E.cy].size;
	}
	if (E.cx < 0) {
		E.cx = 0;
	}
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}

	if (E.rowoff < 0) {
		E.rowoff = 0;
	}
	if (E.coloff < 0) {
		E.coloff = 0;
	}
}

static void editor_shutdown(int clear_screen) {
	disable_raw_mode();
	if (clear_screen) {
		write(STDOUT_FILENO, "\x1b[2J", 4);
		write(STDOUT_FILENO, "\x1b[H", 3);
	}
	editor_free_rows();
	if (E.kbfd >= 0) {
		close(E.kbfd);
		E.kbfd = -1;
	}
	free(E.filename);
	E.filename = NULL;
	free(E.copybuf);
	E.copybuf = NULL;
	E.copybuf_len = 0;
}

static void die(const char *s) {
	editor_shutdown(1);
	perror(s);
	exit(1);
}

static void editor_set_status_message(const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
	va_end(ap);
	E.statusmsg_time = time(NULL);
}

static void editor_set_filename(const char *name) {
	char *new_name = NULL;
	if (name) {
		size_t len = strlen(name);
		new_name = malloc(len + 1);
		if (!new_name) {
			return;
		}
		memcpy(new_name, name, len + 1);
	}

	free(E.filename);
	E.filename = new_name;
}

static void disable_raw_mode(void) {
	if (!E.raw_enabled) {
		return;
	}
	editor_restore_attr_on_stdio(&E.orig_termios);
	E.raw_enabled = 0;
}

/* Kernel event input removed: editor uses raw stdin only. */

static void enable_raw_mode(void) {
	pty_attr_t raw;

	if (ioctl(STDIN_FILENO, TCGETS, &E.orig_termios) == -1) {
		die("ioctl TCGETS");
	}

	raw = E.orig_termios;
	raw.echo = 0;
	raw.icanon = 0;
	raw.isig = 1;
	raw.onlcr = 0;

	if (ioctl(STDIN_FILENO, TCSETS, &raw) == -1) {
		die("ioctl TCSETS");
	}

	/*
	 * Ensure the foreground PTY has echo/canonical disabled even when
	 * stdin routing differs between process setups.
	 */
	editor_apply_attr_on_stdio(&raw);

	E.raw_enabled = 1;
}

static int editor_read_key(void) {
	/* Use raw stdin only; do not poll kernel keyboard events. */
	g_last_key_flags = 0;

	char c;
	ssize_t nread;

	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) {
			die("read");
		}
	}

	if (c == '\x1b') {
		char seq0;
		if (read(STDIN_FILENO, &seq0, 1) != 1) {
			return '\x1b';
		}

		/* Support both CSI '[' and SS3 'O' sequences. */
		if (seq0 == 'O') {
			char final;
			if (read(STDIN_FILENO, &final, 1) != 1) return '\x1b';
			switch (final) {
				case 'A': return KEY_ARROW_UP;
				case 'B': return KEY_ARROW_DOWN;
				case 'C': return KEY_ARROW_RIGHT;
				case 'D': return KEY_ARROW_LEFT;
				case 'H': return KEY_HOME;
				case 'F': return KEY_END;
			}
			return '\x1b';
		}

		if (seq0 != '[') {
			return '\x1b';
		}

		char csi[16];
		int n = 0;
		for (;;) {
			char ch;
			if (read(STDIN_FILENO, &ch, 1) != 1) {
				return '\x1b';
			}
			if (n < (int)sizeof(csi) - 1) {
				csi[n++] = ch;
			}
			if ((ch >= 'A' && ch <= 'Z') || ch == '~') {
				break;
			}
		}
		csi[n] = '\0';

		g_last_key_flags = 0;

		/* Plain arrows/home/end */
		if (n == 1) {
			switch (csi[0]) {
				case 'A': return KEY_ARROW_UP;
				case 'B': return KEY_ARROW_DOWN;
				case 'C': return KEY_ARROW_RIGHT;
				case 'D': return KEY_ARROW_LEFT;
				case 'H': return KEY_HOME;
				case 'F': return KEY_END;
			}
		}

		/* Tilde keys: 1~,3~,4~,5~,6~,7~,8~ */
		if (csi[n - 1] == '~') {
			int p1 = 0;
			int p2 = 0;
			(void)sscanf(csi, "%d;%d~", &p1, &p2);
			if (p1 == 0) {
				(void)sscanf(csi, "%d~", &p1);
			}
			if (p2 > 0) {
				editor_apply_csi_modifier_flags(p2);
			}
			switch (p1) {
				case 1: return KEY_HOME;
				case 3: return KEY_DELETE;
				case 4: return KEY_END;
				case 5: return KEY_PAGE_UP;
				case 6: return KEY_PAGE_DOWN;
				case 7: return KEY_HOME;
				case 8: return KEY_END;
			}
		}

		/* Modified arrows/home/end, e.g. 1;2A or 1;5C */
		{
			int p1 = 0;
			int p2 = 0;
			char final = 0;
			if (sscanf(csi, "%d;%d%c", &p1, &p2, &final) == 3) {
				editor_apply_csi_modifier_flags(p2);
				switch (final) {
					case 'A': return KEY_ARROW_UP;
					case 'B': return KEY_ARROW_DOWN;
					case 'C': return KEY_ARROW_RIGHT;
					case 'D': return KEY_ARROW_LEFT;
					case 'H': return KEY_HOME;
					case 'F': return KEY_END;
				}
			}
		}

		return '\x1b';
	}

	/* Convert LF to CR so editor recognizes Enter. */
	if (c == '\n') return '\r';

	/* If mapping exists for the single byte, apply it. */
	unsigned char ub = (unsigned char)c;
	keymap_entry_t *me = editor_keymap_match(&ub, 1);
	if (me) {
		if (me->add_flags) g_last_key_flags |= me->add_flags;
		if (me->mapped_char) return me->mapped_char;
	}

	/* If this is a single-byte control character (Ctrl+letter), mark CTRL. */
	if (ub <= 0x1f) {
		g_last_key_flags |= KEY_FLAG_CTRL;
	}

	return (unsigned char)c;
}

static int get_window_size(int *rows, int *cols) {
	pty_winsize_t ws;

	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
		return -1;
	}

	*cols = ws.ws_col;
	*rows = ws.ws_row;
	return 0;
}

static int editor_text_cols(void) {
	int cols = E.screencols - GUTTER_WIDTH;
	if (cols < 1) {
		cols = 1;
	}
	return cols;
}

static void editor_insert_row(int at, const char *s, size_t len) {
	if (at < 0 || at > E.numrows) {
		return;
	}

	markup_row_t *new_rows = realloc(E.rows, sizeof(markup_row_t) * (size_t)(E.numrows + 1));
	if (!new_rows) {
		return;
	}
	E.rows = new_rows;

	memmove(&E.rows[at + 1], &E.rows[at], sizeof(markup_row_t) * (size_t)(E.numrows - at));

	E.rows[at].size = (int)len;
	E.rows[at].chars = malloc(len + 1);
	if (!E.rows[at].chars) {
		E.rows[at].size = 0;
		return;
	}

	memcpy(E.rows[at].chars, s, len);
	E.rows[at].chars[len] = '\0';

	E.numrows++;
	E.dirty++;
}

static void editor_del_row(int at) {
	if (at < 0 || at >= E.numrows) {
		return;
	}

	free(E.rows[at].chars);
	memmove(&E.rows[at], &E.rows[at + 1], sizeof(markup_row_t) * (size_t)(E.numrows - at - 1));
	E.numrows--;
	E.dirty++;
}

static void editor_row_insert_char(markup_row_t *row, int at, int c) {
	if (at < 0 || at > row->size) {
		at = row->size;
	}

	char *new_chars = realloc(row->chars, (size_t)row->size + 2);
	if (!new_chars) {
		return;
	}
	row->chars = new_chars;

	memmove(&row->chars[at + 1], &row->chars[at], (size_t)(row->size - at + 1));
	row->size++;
	row->chars[at] = (char)c;
	E.dirty++;
}

static void editor_row_append_string(markup_row_t *row, const char *s, size_t len) {
	char *new_chars = realloc(row->chars, (size_t)row->size + len + 1);
	if (!new_chars) {
		return;
	}
	row->chars = new_chars;

	memcpy(&row->chars[row->size], s, len);
	row->size += (int)len;
	row->chars[row->size] = '\0';
	E.dirty++;
}

static void editor_row_del_char(markup_row_t *row, int at) {
	if (at < 0 || at >= row->size) {
		return;
	}

	memmove(&row->chars[at], &row->chars[at + 1], (size_t)(row->size - at));
	row->size--;
	E.dirty++;
}

static void editor_free_rows(void) {
	for (int i = 0; i < E.numrows; i++) {
		free(E.rows[i].chars);
	}
	free(E.rows);
	E.rows = NULL;
	E.numrows = 0;
}

static void editor_open(const char *filename) {
	FILE *fp = fopen(filename, "r");
	if (!fp) {
		editor_set_status_message("[new file] %s", filename);
		return;
	}

	char line[1024];
	while (fgets(line, sizeof(line), fp)) {
		size_t linelen = strlen(line);
		while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) {
			linelen--;
		}
		editor_insert_row(E.numrows, line, linelen);
	}

	fclose(fp);
	E.dirty = 0;
}

static void editor_insert_char(int c) {
	if (E.cy == E.numrows) {
		editor_insert_row(E.numrows, "", 0);
	}

	editor_row_insert_char(&E.rows[E.cy], E.cx, c);
	E.cx++;
}

static void editor_insert_newline(void) {
	if (E.cx == 0) {
		editor_insert_row(E.cy, "", 0);
	} else {
		markup_row_t *row = &E.rows[E.cy];
		editor_insert_row(E.cy + 1, &row->chars[E.cx], (size_t)(row->size - E.cx));
		row = &E.rows[E.cy];
		row->size = E.cx;
		row->chars[row->size] = '\0';
	}

	E.cy++;
	E.cx = 0;
}

static void editor_del_char(void) {
	if (E.cy == E.numrows) {
		return;
	}
	if (E.cx == 0 && E.cy == 0) {
		return;
	}

	markup_row_t *row = &E.rows[E.cy];
	if (E.cx > 0) {
		editor_row_del_char(row, E.cx - 1);
		E.cx--;
	} else {
		E.cx = E.rows[E.cy - 1].size;
		editor_row_append_string(&E.rows[E.cy - 1], row->chars, (size_t)row->size);
		editor_del_row(E.cy);
		E.cy--;
	}
}

static void editor_del_forward(void) {
	if (E.cy == E.numrows) return;
	markup_row_t *row = &E.rows[E.cy];
	if (E.cx < row->size) {
		/* delete character at cursor */
		editor_row_del_char(row, E.cx);
	} else {
		/* at end of line: merge next line into this one if present */
		if (E.cy + 1 >= E.numrows) return;
		markup_row_t *next = &E.rows[E.cy + 1];
		int new_size = row->size + next->size;
		char *new_chars = malloc((size_t)new_size + 1);
		if (!new_chars) return;
		if (row->size > 0) memcpy(new_chars, row->chars, (size_t)row->size);
		if (next->size > 0) memcpy(new_chars + row->size, next->chars, (size_t)next->size);
		new_chars[new_size] = '\0';
		free(row->chars);
		row->chars = new_chars;
		row->size = new_size;

		/* remove next row */
		for (int i = E.cy + 1; i + 1 < E.numrows; i++) {
			E.rows[i] = E.rows[i + 1];
		}
		E.numrows--;
	}
	E.dirty = 1;
}

static char *editor_rows_to_string(int *buflen) {
	int total_len = 0;
	for (int j = 0; j < E.numrows; j++) {
		total_len += E.rows[j].size + 1;
	}

	*buflen = total_len;
	char *buf = malloc((size_t)total_len);
	if (!buf) {
		return NULL;
	}

	char *p = buf;
	for (int j = 0; j < E.numrows; j++) {
		memcpy(p, E.rows[j].chars, (size_t)E.rows[j].size);
		p += E.rows[j].size;
		*p = '\n';
		p++;
	}

	return buf;
}

static void editor_save(void) {
	if (!E.filename || E.filename[0] == '\0') {
		return;
	}

	int len = 0;
	char *buf = editor_rows_to_string(&len);
	if (!buf && len != 0) {
		editor_set_status_message("Save failed: out of memory");
		return;
	}

	int fd = open(E.filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd == -1) {
		free(buf);
		editor_set_status_message("Save failed: open errno=%d", errno);
		return;
	}

	ssize_t n = write(fd, buf, (size_t)len);
	if (n == len) {
		E.dirty = 0;
		editor_set_status_message("%d bytes written to %s", len, E.filename);
	} else {
		editor_set_status_message("Save failed: write errno=%d", errno);
	}

	close(fd);
	free(buf);
}

static char *editor_prompt(const char *prompt) {
	size_t bufsize = 64;
	size_t buflen = 0;
	char *buf = malloc(bufsize);
	if (!buf) {
		return NULL;
	}
	buf[0] = '\0';

	for (;;) {
		editor_set_status_message(prompt, buf);
		editor_refresh_screen();

		int c = editor_read_key();
		if (c == '\x1b') {
			editor_set_status_message("Action canceled");
			free(buf);
			return NULL;
		} else if (c == '\r') {
			if (buflen != 0) {
				editor_set_status_message("");
				return buf;
			}
		} else if (c == 127 || c == CTRL_KEY('h')) {
			if (buflen > 0) {
				buf[--buflen] = '\0';
			}
		} else if (!iscntrl(c) && c < 128) {
			if (buflen + 1 >= bufsize) {
				size_t new_size = bufsize * 2;
				char *new_buf = realloc(buf, new_size);
				if (!new_buf) {
					free(buf);
					editor_set_status_message("Prompt failed: out of memory");
					return NULL;
				}
				buf = new_buf;
				bufsize = new_size;
			}
			buf[buflen++] = (char)c;
			buf[buflen] = '\0';
		}
	}
}

static void editor_save_with_prompt(void) {
	if (!E.filename || E.filename[0] == '\0') {
		char *name = editor_prompt("Write file: %s (ESC to cancel)");
		if (!name) {
			return;
		}
		editor_set_filename(name);
		free(name);
	}

	if (!E.filename || E.filename[0] == '\0') {
		editor_set_status_message("Save failed: no filename");
		return;
	}
	editor_save();
}

static int editor_find_in_row(const markup_row_t *row, const char *query) {
	if (!row || !query || query[0] == '\0') {
		return -1;
	}

	char *match = strstr(row->chars, query);
	if (!match) {
		return -1;
	}

	return (int)(match - row->chars);
}

static void editor_find(void) {
	if (E.numrows == 0) {
		editor_set_status_message("Search: empty buffer");
		return;
	}

	char *query = editor_prompt("Search: %s (ENTER to jump, ESC to cancel)");
	if (!query) {
		return;
	}

	if (query[0] == '\0') {
		free(query);
		editor_set_status_message("Search canceled");
		return;
	}

	int start = E.cy;
	int found_row = -1;
	int found_col = -1;

	for (int i = 0; i < E.numrows; i++) {
		int row = (start + i + 1) % E.numrows;
		int col = editor_find_in_row(&E.rows[row], query);
		if (col >= 0) {
			found_row = row;
			found_col = col;
			break;
		}
	}

	if (found_row >= 0) {
		E.cy = found_row;
		E.cx = found_col;
		E.rowoff = E.numrows;
		editor_set_status_message("Found '%s' at %d:%d", query, found_row + 1, found_col + 1);
	} else {
		editor_set_status_message("Not found: %s", query);
	}

	free(query);
}

static void editor_goto_line(void) {
	char *answer = editor_prompt("Goto line: %s (ENTER to jump, ESC to cancel)");
	if (!answer) {
		return;
	}

	if (answer[0] == '\0') {
		free(answer);
		editor_set_status_message("Goto canceled");
		return;
	}

	char *endptr = NULL;
	long line = strtol(answer, &endptr, 10);
	if (endptr == answer || *endptr != '\0') {
		editor_set_status_message("Goto failed: invalid number");
		free(answer);
		return;
	}

	if (line < 1) {
		line = 1;
	}
	if (line > E.numrows + 1) {
		line = E.numrows + 1;
	}

	E.cy = (int)line - 1;
	if (E.cy < E.numrows) {
		if (E.cx > E.rows[E.cy].size) {
			E.cx = E.rows[E.cy].size;
		}
	} else {
		E.cx = 0;
	}

	E.rowoff = E.numrows;
	editor_set_status_message("Jumped to line %ld", line);
	free(answer);
}

static void editor_scroll(void) {
	int text_cols = editor_text_cols();

	if (E.cy < E.rowoff) {
		E.rowoff = E.cy;
	}
	if (E.cy >= E.rowoff + E.screenrows) {
		E.rowoff = E.cy - E.screenrows + 1;
	}
	if (E.cx < E.coloff) {
		E.coloff = E.cx;
	}
	if (E.cx >= E.coloff + text_cols) {
		E.coloff = E.cx - text_cols + 1;
	}
}

static void editor_draw_rows(struct abuf *ab) {
	int text_cols = editor_text_cols();

	for (int y = 0; y < E.screenrows; y++) {
		int filerow = y + E.rowoff;
		char gutter[GUTTER_WIDTH + 1];

		ab_move_cursor(ab, y + 1, 1);

		if (filerow < E.numrows) {
			snprintf(gutter, sizeof(gutter), "%4d| ", filerow + 1);
		} else {
			/* Empty line gutter: no tilde, leave gutter blank with separator. */
			memcpy(gutter, "     |", GUTTER_WIDTH);
			gutter[GUTTER_WIDTH] = '\0';
		}
		ab_append(ab, gutter, GUTTER_WIDTH);

		if (filerow >= E.numrows) {
			if (E.numrows == 0 && y == E.screenrows / 3) {
				char welcome[96];
				int welcomelen = snprintf(welcome, sizeof(welcome),
									  "cortex editor %s  -- nano-like editor scaffold",
									  CORTEX_EDITOR_VERSION);
				if (welcomelen > text_cols) {
					welcomelen = text_cols;
				}
				int padding = (text_cols - welcomelen) / 2;
				while (padding-- > 0) {
					ab_append(ab, " ", 1);
				}
				ab_append(ab, welcome, welcomelen);
			}
		} else {
			int len = E.rows[filerow].size - E.coloff;
			if (len < 0) {
				len = 0;
			}
			if (len > text_cols) {
				len = text_cols;
			}
			int in_selected = 0;
			int in_cursor = 0;
			/* token map for visible columns (0=normal,1=keyword,2=string,3=comment,4=number,5=preproc) */
			int *tok = malloc(sizeof(int) * (size_t)len);
			if (tok) memset(tok, 0, sizeof(int) * (size_t)len);
			if (tok) {
				char *rowchars = E.rows[filerow].chars;
				int rowlen = E.rows[filerow].size;
				/* preprocessor lines */
				int sc = 0; while (sc < rowlen && (rowchars[sc] == ' ' || rowchars[sc] == '\t')) sc++;
				if (sc < rowlen && rowchars[sc] == '#') {
					for (int j = 0; j < len; j++) tok[j] = 5;
				} else {
					int i = 0;
					while (i < rowlen) {
						/* line comment */
						if (i + 1 < rowlen && rowchars[i] == '/' && rowchars[i+1] == '/') {
							int vs = i - E.coloff; for (int j = vs; j < len; j++) if (j >= 0) tok[j] = 3;
							break;
						}
						/* string literal */
						if (rowchars[i] == '"') {
							int j = i+1; while (j < rowlen) { if (rowchars[j] == '\\') { j += 2; continue;} if (rowchars[j] == '"') { j++; break; } j++; }
							for (int k = i; k < j; k++) { int vi = k - E.coloff; if (vi >= 0 && vi < len) tok[vi] = 2; }
							i = j; continue;
						}
						/* numbers */
						if (isdigit((unsigned char)rowchars[i])) {
							int j = i; while (j < rowlen && (isdigit((unsigned char)rowchars[j]) || rowchars[j] == '.')) j++;
							for (int k = i; k < j; k++) { int vi = k - E.coloff; if (vi >= 0 && vi < len) tok[vi] = 4; }
							i = j; continue;
						}
						/* identifiers -> keywords */
						if (isalnum((unsigned char)rowchars[i]) || rowchars[i] == '_') {
							int j = i; while (j < rowlen && (isalnum((unsigned char)rowchars[j]) || rowchars[j] == '_')) j++;
							if (is_cpp_keyword(&rowchars[i], j - i)) {
								for (int k = i; k < j; k++) { int vi = k - E.coloff; if (vi >= 0 && vi < len) tok[vi] = 1; }
							}
							i = j; continue;
						}
						i++;
					}
				}
			}

			for (int i = 0; i < len; i++) {
				int col = E.coloff + i;
				int selected = editor_is_selected_pos(filerow, col);
				int cursor_here = (E.cursor_white && filerow == E.cy && col == E.cx);

				if (cursor_here) {
					if (in_selected) { ab_append(ab, SELECT_STYLE_OFF, (int)strlen(SELECT_STYLE_OFF)); in_selected = 0; }
					if (!in_cursor) { ab_append(ab, CURSOR_STYLE_ON, (int)strlen(CURSOR_STYLE_ON)); in_cursor = 1; }
				} else {
					if (in_cursor) { ab_append(ab, CURSOR_STYLE_OFF, (int)strlen(CURSOR_STYLE_OFF)); in_cursor = 0; }
					if (selected && !in_selected) { ab_append(ab, SELECT_STYLE_ON, (int)strlen(SELECT_STYLE_ON)); in_selected = 1; }
					else if (!selected && in_selected) { ab_append(ab, SELECT_STYLE_OFF, (int)strlen(SELECT_STYLE_OFF)); in_selected = 0; }
				}

				unsigned char ch = (unsigned char)E.rows[filerow].chars[col];
				char out = (ch >= 0x20 && ch != 0x7f) ? (char)ch : '?';
				int tt = tok ? tok[i] : 0;
				/* If selected or cursor, skip highlight and just print (selection/cursor overrides) */
				if (!selected && !cursor_here) {
					switch (tt) {
						case 1: ab_append(ab, HL_KEYWORD_ON, (int)strlen(HL_KEYWORD_ON)); break;
						case 2: ab_append(ab, HL_STRING_ON, (int)strlen(HL_STRING_ON)); break;
						case 3: ab_append(ab, HL_COMMENT_ON, (int)strlen(HL_COMMENT_ON)); break;
						case 4: ab_append(ab, HL_NUMBER_ON, (int)strlen(HL_NUMBER_ON)); break;
						case 5: ab_append(ab, HL_PPRE_ON, (int)strlen(HL_PPRE_ON)); break;
						default: break;
					}
					ab_append(ab, &out, 1);
					ab_append(ab, HL_OFF, (int)strlen(HL_OFF));
				} else {
					ab_append(ab, &out, 1);
				}
			}
			if (in_cursor) ab_append(ab, CURSOR_STYLE_OFF, (int)strlen(CURSOR_STYLE_OFF));
			if (in_selected) ab_append(ab, SELECT_STYLE_OFF, (int)strlen(SELECT_STYLE_OFF));
			if (tok) free(tok);
		}

		ab_append(ab, "\x1b[K", 3);
	}
}

static void editor_draw_status_bar(struct abuf *ab) {
	char status[96];
	char rstatus[48];

	int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
					   E.filename ? E.filename : "[No Name]",
					   E.numrows,
					   E.dirty ? "(modified)" : "");
	int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
						E.cy + 1,
						E.numrows == 0 ? 1 : E.numrows);

	if (len > E.screencols) {
		len = E.screencols;
	}

	ab_move_cursor(ab, E.screenrows + 1, 1);
	ab_append(ab, "\x1b[7m", 4);
	ab_append(ab, status, len);
	while (len < E.screencols) {
		if (E.screencols - len == rlen) {
			ab_append(ab, rstatus, rlen);
			break;
		}
		ab_append(ab, " ", 1);
		len++;
	}
	ab_append(ab, "\x1b[m", 3);
	ab_append(ab, "\x1b[K", 3);
}

static void editor_draw_message_bar(struct abuf *ab) {
	const char *msg = DEFAULT_HINT_MSG;
	if (E.statusmsg[0] != '\0' && time(NULL) - E.statusmsg_time < 5) {
		msg = E.statusmsg;
	}

	int msglen = (int)strlen(msg);
	if (msglen > E.screencols) {
		msglen = E.screencols;
	}
	ab_move_cursor(ab, E.screenrows + 2, 1);
	ab_append(ab, "\x1b[K", 3);
	if (msglen > 0) {
		ab_append(ab, msg, msglen);
	}
}

static void editor_refresh_screen(void) {
	editor_update_window_size();
	editor_clamp_state();
	editor_scroll();

	struct abuf ab = ABUF_INIT;

	/* Reset scroll region in case a previous app left it altered. */
	ab_append(&ab, "\x1b[r", 3);
	ab_append(&ab, "\x1b[?25l", 6);
	ab_move_cursor(&ab, 1, 1);

	editor_draw_rows(&ab);
	editor_draw_status_bar(&ab);
	editor_draw_message_bar(&ab);

	ab_move_cursor(&ab, (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1 + GUTTER_WIDTH);

	ab_append(&ab, "\x1b[?25h", 6);

	if (write(STDOUT_FILENO, ab.b, (size_t)ab.len) == -1) {
		die("write");
	}
	ab_free(&ab);
}

static void editor_move_cursor(int key) {
	markup_row_t *row = (E.cy >= E.numrows) ? NULL : &E.rows[E.cy];

	switch (key) {
		case KEY_ARROW_LEFT:
			if (E.cx != 0) {
				E.cx--;
			} else if (E.cy > 0) {
				E.cy--;
				E.cx = E.rows[E.cy].size;
			}
			break;
		case KEY_ARROW_RIGHT:
			if (row && E.cx < row->size) {
				E.cx++;
			} else if (row && E.cx == row->size) {
				E.cy++;
				E.cx = 0;
			}
			break;
		case KEY_ARROW_UP:
			if (E.cy != 0) {
				E.cy--;
			}
			break;
		case KEY_ARROW_DOWN:
			if (E.cy < E.numrows) {
				E.cy++;
			}
			break;
	}

	row = (E.cy >= E.numrows) ? NULL : &E.rows[E.cy];
	int rowlen = row ? row->size : 0;
	if (E.cx > rowlen) {
		E.cx = rowlen;
	}
}

static int char_category(unsigned char ch) {
	if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') return 0; /* space */
	if (isalnum(ch) || ch == '_') return 1; /* word */
	return 2; /* other/punctuation */
}

static void editor_move_word_left(void) {
	if (E.cy >= E.numrows) return;
	markup_row_t *row = &E.rows[E.cy];

	if (E.cx == 0) {
		if (E.cy > 0) {
			E.cy--;
			E.cx = E.rows[E.cy].size;
		}
		return;
	}

	int i = E.cx;
	/* Look at the char just to the left */
	unsigned char prev = (unsigned char)row->chars[i - 1];
	int cat = char_category(prev);

	if (cat == 0) {
		/* skip spaces left */
		while (i > 0 && char_category((unsigned char)row->chars[i - 1]) == 0) i--;
		/* then skip the previous token */
		if (i > 0) {
			int subcat = char_category((unsigned char)row->chars[i - 1]);
			while (i > 0 && char_category((unsigned char)row->chars[i - 1]) == subcat) i--;
		}
	} else {
		/* skip left while same category */
		while (i > 0 && char_category((unsigned char)row->chars[i - 1]) == cat) i--;
	}

	E.cx = i;
}

static void editor_move_word_right(void) {
	if (E.cy >= E.numrows) return;
	markup_row_t *row = &E.rows[E.cy];
	int rowlen = row->size;

	if (E.cx >= rowlen) {
		if (E.cy + 1 < E.numrows) {
			E.cy++;
			E.cx = 0;
		}
		return;
	}

	int i = E.cx;
	unsigned char cur = (unsigned char)row->chars[i];
	int cat = char_category(cur);

	/* advance while same category */
	while (i < rowlen && char_category((unsigned char)row->chars[i]) == cat) i++;
	/* if we landed on spaces, skip them to reach next token start */
	while (i < rowlen && char_category((unsigned char)row->chars[i]) == 0) i++;

	E.cx = i;
}

/* Minimal C/C++ keyword list used for simple highlighting. */
static const char *cpp_keywords[] = {
	"if","else","for","while","do","switch","case","break","continue",
	"return","struct","class","public","private","protected","virtual",
	"template","typename","using","namespace","auto","static","const",
	"constexpr","inline","enum","union","sizeof","new","delete","this",
	"try","catch","throw","operator","bool","int","long","short","char",
	"float","double","void","signed","unsigned","volatile","mutable",
	NULL
};

static int is_cpp_keyword(const char *s, int len) {
	if (len <= 0) return 0;
	for (const char **p = cpp_keywords; *p; p++) {
		if ((int)strlen(*p) == len && strncmp(*p, s, (size_t)len) == 0) return 1;
	}
	return 0;
}

static void editor_process_keypress(void) {
	static int quit_times = 1;
	int c = editor_read_key();
	int shift_down = (g_last_key_flags & KEY_FLAG_SHIFT) != 0;

	switch (c) {
		case CTRL_KEY('q'):
			if (E.dirty && quit_times > 0) {
				editor_set_status_message("Unsaved changes. Press Ctrl+Q again to quit");
				quit_times--;
				return;
			}
			editor_shutdown(1);
			exit(0);
			break;
		case CTRL_KEY('s'):
			editor_clear_selection();
			editor_save_with_prompt();
			break;
		case CTRL_KEY('f'):
			editor_clear_selection();
			editor_find();
			break;
		case CTRL_KEY('g'):
			editor_clear_selection();
			editor_goto_line();
			break;
		case CTRL_KEY('c'):
			editor_copy_selection();
			break;
		case CTRL_KEY('v'):
			editor_paste();
			break;
		case '\r':
			editor_clear_selection();
			editor_insert_newline();
			break;
		case KEY_DELETE:
			if (editor_selection_is_nonempty()) {
				editor_delete_selection();
			} else {
				int ctrl_down_inner = (g_last_key_flags & KEY_FLAG_CTRL) != 0;
				if (ctrl_down_inner) {
					/* delete to end of next word */
					int ax = E.cx, ay = E.cy;
					/* mark anchor at cursor, move to word end, then delete region */
					E.sel_active = 1;
					E.sel_anchor_cx = ax;
					E.sel_anchor_cy = ay;
					editor_move_word_right();
					editor_delete_selection();
				} else {
					editor_del_forward();
				}
			}
			break;
		case 127:
		case CTRL_KEY('h'):
			if (editor_selection_is_nonempty()) {
				editor_delete_selection();
			} else {
				int ctrl_down_inner = (g_last_key_flags & KEY_FLAG_CTRL) != 0;
				if (ctrl_down_inner) {
					/* delete previous word */
					int ax = E.cx, ay = E.cy;
					E.sel_active = 1;
					E.sel_anchor_cx = ax;
					E.sel_anchor_cy = ay;
					editor_move_word_left();
					editor_delete_selection();
				} else {
					editor_del_char();
				}
			}
			break;
		case KEY_HOME:
			/* Home is not an arrow-key move: clear cursor-white state. */
			E.cursor_white = 0;
			if (!shift_down) {
				editor_clear_selection();
			}
			E.cx = 0;
			break;
		case KEY_END:
			/* End is not an arrow-key move: clear cursor-white state. */
			E.cursor_white = 0;
			if (!shift_down) {
				editor_clear_selection();
			}
			if (E.cy < E.numrows) {
				E.cx = E.rows[E.cy].size;
			}
			break;
		case KEY_PAGE_UP:
		case KEY_PAGE_DOWN:
		{
			if (c == KEY_PAGE_UP) {
				E.cy = E.rowoff;
			} else if (c == KEY_PAGE_DOWN) {
				E.cy = E.rowoff + E.screenrows - 1;
				if (E.cy > E.numrows) {
					E.cy = E.numrows;
				}
			}

			if (!shift_down) {
				editor_clear_selection();
				/* Page moves are not treated as arrow-key moves for white cursor */
				E.cursor_white = 0;
			}

			int times = E.screenrows;
			while (times--) {
				editor_move_cursor(c == KEY_PAGE_UP ? KEY_ARROW_UP : KEY_ARROW_DOWN);
			}
			break;
		}
		case KEY_ARROW_UP:
		case KEY_ARROW_DOWN:
		case KEY_ARROW_LEFT:
		case KEY_ARROW_RIGHT:
			/* Mark that this movement came from arrow keys so cursor is white. */
			E.cursor_white = 1;
			int ctrl_down = (g_last_key_flags & KEY_FLAG_CTRL) != 0;
			if (shift_down) {
				if (!E.sel_active) {
					E.sel_active = 1;
					E.sel_anchor_cx = E.cx;
					E.sel_anchor_cy = E.cy;
				}
			} else {
				editor_clear_selection();
			}
			if (ctrl_down) {
				if (c == KEY_ARROW_LEFT) editor_move_word_left();
				else if (c == KEY_ARROW_RIGHT) editor_move_word_right();
				else editor_move_cursor(c);
			} else {
				editor_move_cursor(c);
			}
			break;
		default:
			if (c >= 0x20 && c <= 0x7e) {
				editor_clear_selection();
				editor_insert_char(c);
			}
			break;
	}

	quit_times = 1;
}

static void init_editor(void) {
	E.cx = 0;
	E.cy = 0;
	E.rowoff = 0;
	E.coloff = 0;
	E.numrows = 0;
	E.rows = NULL;
	E.dirty = 0;
	E.filename = NULL;
	E.statusmsg[0] = '\0';
	E.statusmsg_time = 0;
	E.raw_enabled = 0;
	E.sel_active = 0;
	E.sel_anchor_cx = 0;
	E.sel_anchor_cy = 0;
	E.cursor_white = 0;
	E.copybuf = NULL;
	E.copybuf_len = 0;
	/* Prefer raw stdin input; do not use kernel keyboard events. */
	E.kbfd = -1;

	/* Load optional keymap from environment. */
	editor_load_keymap();

	editor_update_window_size();
}

static void editor_ensure_initial_line(void) {
	if (E.numrows == 0) {
		editor_insert_row(0, "", 0);
		E.dirty = 0;
	}
}

int main(int argc, char **argv) {
	init_editor();
	enable_raw_mode();
	atexit(disable_raw_mode);

	if (argc >= 2) {
		editor_set_filename(argv[1]);
		editor_open(argv[1]);
	}

	editor_ensure_initial_line();

	editor_set_status_message(DEFAULT_HINT_MSG);

	for (;;) {
		editor_refresh_screen();
		editor_process_keypress();
	}

	editor_free_rows();
	return 0;
}
