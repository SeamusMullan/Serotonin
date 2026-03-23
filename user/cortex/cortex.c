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

#define CORTEX_EDITOR_VERSION "0.1.0-scaffold"
#define CTRL_KEY(k) ((k) & 0x1f)
#define GUTTER_WIDTH 6

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

static int editor_map_scancode(uint8_t scancode) {
	switch (scancode) {
		case 0x48: return KEY_ARROW_UP;
		case 0x50: return KEY_ARROW_DOWN;
		case 0x4b: return KEY_ARROW_LEFT;
		case 0x4d: return KEY_ARROW_RIGHT;
		case 0x47: return KEY_HOME;
		case 0x4f: return KEY_END;
		case 0x49: return KEY_PAGE_UP;
		case 0x51: return KEY_PAGE_DOWN;
		case 0x53: return KEY_DELETE;
		case 0x1c: return '\r';
		case 0x0e: return 127;
		case 0x01: return '\x1b';
		default: return KEY_NULL;
	}
}

static int editor_read_key_from_event(void) {
	if (E.kbfd < 0) {
		return KEY_NULL;
	}

	keyboard_event_t ev;
	for (;;) {
		ssize_t nread = read(E.kbfd, &ev, sizeof(ev));
		if (nread != (ssize_t)sizeof(ev)) {
			if (nread < 0 && errno == EINTR) {
				continue;
			}
			if (nread < 0 && errno == EAGAIN) {
				return KEY_NULL;
			}
			return KEY_NULL;
		}

		if (ev.flags & KEY_FLAG_RELEASED) {
			continue;
		}

		if (ev.flags & KEY_FLAG_CTRL) {
			char c = (char)ev.ascii;
			if (c >= 'A' && c <= 'Z') {
				c = (char)(c - 'A' + 'a');
			}
			if (c >= 'a' && c <= 'z') {
				return CTRL_KEY(c);
			}
		}

		if (ev.ascii != 0) {
			if (ev.ascii == '\n') return '\r';
			return ev.ascii;
		}

		int key = editor_map_scancode(ev.scancode);
		if (key != KEY_NULL) {
			return key;
		}
	}
}

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
	if (E.kbfd >= 0) {
		int evk = editor_read_key_from_event();
		if (evk != KEY_NULL) {
			return evk;
		}
	}

	char c;
	ssize_t nread;

	while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
		if (nread == -1 && errno != EAGAIN) {
			die("read");
		}
	}

	if (c == '\x1b') {
		char seq[3];

		if (read(STDIN_FILENO, &seq[0], 1) != 1) {
			return '\x1b';
		}
		if (read(STDIN_FILENO, &seq[1], 1) != 1) {
			return '\x1b';
		}

		if (seq[0] == '[') {
			if (seq[1] >= '0' && seq[1] <= '9') {
				if (read(STDIN_FILENO, &seq[2], 1) != 1) {
					return '\x1b';
				}
				if (seq[2] == '~') {
					switch (seq[1]) {
						case '1': return KEY_HOME;
						case '3': return KEY_DELETE;
						case '4': return KEY_END;
						case '5': return KEY_PAGE_UP;
						case '6': return KEY_PAGE_DOWN;
						case '7': return KEY_HOME;
						case '8': return KEY_END;
					}
				}
			} else {
				switch (seq[1]) {
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
			memcpy(gutter, "    ~| ", GUTTER_WIDTH);
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
			for (int i = 0; i < len; i++) {
				unsigned char ch = (unsigned char)E.rows[filerow].chars[E.coloff + i];
				char out = (ch >= 0x20 && ch != 0x7f) ? (char)ch : '?';
				ab_append(ab, &out, 1);
			}
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
	int msglen = (int)strlen(E.statusmsg);
	if (msglen > E.screencols) {
		msglen = E.screencols;
	}
	ab_move_cursor(ab, E.screenrows + 2, 1);
	ab_append(ab, "\x1b[K", 3);
	if (msglen > 0 && time(NULL) - E.statusmsg_time < 5) {
		ab_append(ab, E.statusmsg, msglen);
	}
}

static void editor_refresh_screen(void) {
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

static void editor_process_keypress(void) {
	static int quit_times = 1;
	int c = editor_read_key();

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
			editor_save_with_prompt();
			break;
		case CTRL_KEY('f'):
			editor_find();
			break;
		case CTRL_KEY('g'):
			editor_goto_line();
			break;
		case '\r':
			editor_insert_newline();
			break;
		case KEY_DELETE:
			editor_move_cursor(KEY_ARROW_RIGHT);
			editor_del_char();
			break;
		case 127:
		case CTRL_KEY('h'):
			editor_del_char();
			break;
		case KEY_HOME:
			E.cx = 0;
			break;
		case KEY_END:
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
			editor_move_cursor(c);
			break;
		default:
			if (c >= 0x20 && c <= 0x7e) {
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
	E.kbfd = open("/dev/keyboard/event", O_RDONLY | O_NONBLOCK);
	if (E.kbfd >= 0) {
		keyboard_event_t ev;
		while (read(E.kbfd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
			/* drain stale events from shell command entry */
		}
		close(E.kbfd);
		E.kbfd = open("/dev/keyboard/event", O_RDONLY);
	}

	if (get_window_size(&E.screenrows, &E.screencols) == -1) {
		E.screenrows = 25;
		E.screencols = 80;
	}
	E.screenrows -= 2;
}

int main(int argc, char **argv) {
	init_editor();
	enable_raw_mode();
	atexit(disable_raw_mode);

	if (argc >= 2) {
		editor_set_filename(argv[1]);
		editor_open(argv[1]);
	}

	editor_set_status_message("CTRL+Q quit | CTRL+S save | CTRL+F find | CTRL+G goto");

	for (;;) {
		editor_refresh_screen();
		editor_process_keypress();
	}

	editor_free_rows();
	return 0;
}
