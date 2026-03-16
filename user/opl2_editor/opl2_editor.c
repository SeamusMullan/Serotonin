/*
 * opl2_editor.c  –  Interactive TUI preset editor for YM3812 (OPL2) instruments
 *
 * All 26 opl2_instrument_t parameters are exposed in a split MOD/CAR layout.
 * Navigation: ↑↓ move between params, ←→ change value, Tab swaps MOD↔CAR.
 * N = name edit, S = save .opl2, L = load .opl2, Q/Esc = quit.
 *
 * Keyboard: blocking read on /dev/keyboard/event (keyboard_event_t).
 * PS/2 arrow scancodes arrive after the 0xE0 prefix; the kernel turns that
 * prefix into a phantom release event for scancode 0x60 – we skip it.
 *
 * VBE terminal ANSI support notes (verified from vbe.c):
 *   Supported   : \033[r;cH  \033[2J  SGR m (colors, bold, reset)
 *   Unsupported : \033[?25l/h  \033[2K  \033[1D  \b
 *   Consequence : cursor hide/show are no-ops; backspace in prompts must
 *                 repaint the whole status line.
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "syscall/lib5ht/lib5ht.h"

/* ── ANSI helpers ─────────────────────────────────────────────────────────── */
#define CLR          "\033[2J\033[H"
#define RESET        "\033[0m"
#define BOLD         "\033[1m"

#define COL_YELLOW   "\033[38;2;255;190;60m"
#define COL_GREY     "\033[38;2;170;170;170m"
#define COL_GREEN    "\033[38;2;80;210;110m"
#define COL_WHITE    "\033[38;2;255;255;255m"
#define BG_SEL       "\033[48;2;40;60;90m"

/* ── PS/2 scancodes of interest ───────────────────────────────────────────── */
#define SC_ESC       0x01
#define SC_BKSP      0x0E
#define SC_TAB       0x0F
#define SC_ENTER     0x1C
#define SC_ARROW_UP  0x48
#define SC_ARROW_DN  0x50
#define SC_ARROW_LT  0x4B
#define SC_ARROW_RT  0x4D
#define SC_PHANTOM   0x60   /* phantom scancode from 0xE0 extended-key prefix */

/* ── Layout constants ─────────────────────────────────────────────────────── */
#define COL_MOD   2
#define COL_CAR  42
#define ROW_HDR   3
#define ROW_SEP1  4
#define ROW_PARAMS 5   /* first param row  (rows 5-16 for 12 MOD/CAR pairs) */
#define ROW_SEP2  17
#define ROW_CHAN  18
#define ROW_FB    19
#define ROW_CON   20
#define ROW_SEP3  21
#define ROW_NAME  22
#define ROW_HINTS 23
#define ROW_STATUS 24

#define CELL_WIDTH 38   /* total chars per cell including leading/trailing spaces */
#define LABEL_W    10   /* chars reserved for parameter label ("CONNECTION" = 10) */

/* ── Preset data model ────────────────────────────────────────────────────── */
typedef struct {
    char    name[48];
    /* modulator (op1) */
    uint8_t mod_am, mod_vib, mod_egt, mod_ksr;
    uint8_t mod_mult, mod_ksl, mod_tl;
    uint8_t mod_ar,  mod_dr,  mod_sl, mod_rr, mod_ws;
    /* carrier (op2) */
    uint8_t car_am, car_vib, car_egt, car_ksr;
    uint8_t car_mult, car_ksl, car_tl;
    uint8_t car_ar,  car_dr,  car_sl, car_rr, car_ws;
    /* channel */
    uint8_t feedback, connection;
} preset_t;

typedef struct {
    const char *label;
    const char *hint;
    uint8_t     min;
    uint8_t     max;
    uint8_t    *val;
} param_t;

#define N_PARAMS 26

static preset_t  g_preset;
static param_t   g_params[N_PARAMS];
static int       g_kbfd   = -1;
static int       g_cursor = 0;
static char      g_status[80];

/* ── File format ──────────────────────────────────────────────────────────── */
typedef struct {
    char     magic[4];   /* "OPL2" */
    preset_t preset;
} preset_file_t;

/* ── Build the parameter table (call after any struct copy) ───────────────── */
static void build_params(void)
{
    g_params[ 0] = (param_t){"MOD AM",   "Tremolo (amplitude modulation) enable",  0,  1, &g_preset.mod_am};
    g_params[ 1] = (param_t){"MOD VIB",  "Vibrato enable",                          0,  1, &g_preset.mod_vib};
    g_params[ 2] = (param_t){"MOD EGT",  "Envelope: 0=decay after sustain, 1=hold", 0,  1, &g_preset.mod_egt};
    g_params[ 3] = (param_t){"MOD KSR",  "Key scale rate (faster env at hi pitch)", 0,  1, &g_preset.mod_ksr};
    g_params[ 4] = (param_t){"MOD MULT", "Freq multiplier: 0=x0.5, 1-15=x1..x15",  0, 15, &g_preset.mod_mult};
    g_params[ 5] = (param_t){"MOD KSL",  "Key scale level: 0=off, 1-3=atten/oct",  0,  3, &g_preset.mod_ksl};
    g_params[ 6] = (param_t){"MOD TL",   "Total level: 0=loudest, 63=silent",       0, 63, &g_preset.mod_tl};
    g_params[ 7] = (param_t){"MOD AR",   "Attack  rate: 0=slowest, 15=fastest",     0, 15, &g_preset.mod_ar};
    g_params[ 8] = (param_t){"MOD DR",   "Decay   rate: 0=slowest, 15=fastest",     0, 15, &g_preset.mod_dr};
    g_params[ 9] = (param_t){"MOD SL",   "Sustain level: 0=loudest, 15=silent",     0, 15, &g_preset.mod_sl};
    g_params[10] = (param_t){"MOD RR",   "Release rate: 0=slowest, 15=fastest",     0, 15, &g_preset.mod_rr};
    g_params[11] = (param_t){"MOD WS",   "Waveform: 0=sine 1=half 2=abs 3=pulse",   0,  3, &g_preset.mod_ws};

    g_params[12] = (param_t){"CAR AM",   "Tremolo (amplitude modulation) enable",  0,  1, &g_preset.car_am};
    g_params[13] = (param_t){"CAR VIB",  "Vibrato enable",                          0,  1, &g_preset.car_vib};
    g_params[14] = (param_t){"CAR EGT",  "Envelope: 0=decay after sustain, 1=hold", 0,  1, &g_preset.car_egt};
    g_params[15] = (param_t){"CAR KSR",  "Key scale rate (faster env at hi pitch)", 0,  1, &g_preset.car_ksr};
    g_params[16] = (param_t){"CAR MULT", "Freq multiplier: 0=x0.5, 1-15=x1..x15",  0, 15, &g_preset.car_mult};
    g_params[17] = (param_t){"CAR KSL",  "Key scale level: 0=off, 1-3=atten/oct",  0,  3, &g_preset.car_ksl};
    g_params[18] = (param_t){"CAR TL",   "Total level: 0=loudest, 63=silent",       0, 63, &g_preset.car_tl};
    g_params[19] = (param_t){"CAR AR",   "Attack  rate: 0=slowest, 15=fastest",     0, 15, &g_preset.car_ar};
    g_params[20] = (param_t){"CAR DR",   "Decay   rate: 0=slowest, 15=fastest",     0, 15, &g_preset.car_dr};
    g_params[21] = (param_t){"CAR SL",   "Sustain level: 0=loudest, 15=silent",     0, 15, &g_preset.car_sl};
    g_params[22] = (param_t){"CAR RR",   "Release rate: 0=slowest, 15=fastest",     0, 15, &g_preset.car_rr};
    g_params[23] = (param_t){"CAR WS",   "Waveform: 0=sine 1=half 2=abs 3=pulse",   0,  3, &g_preset.car_ws};

    g_params[24] = (param_t){"FEEDBACK",   "Op1 self-feedback: 0=none, 7=max",        0,  7, &g_preset.feedback};
    g_params[25] = (param_t){"CONNECTION", "0=FM (op1 mod op2), 1=additive output",   0,  1, &g_preset.connection};
}

/* ── Low-level write helper ───────────────────────────────────────────────── */
static void outs(const char *s)
{
    write(1, s, strlen(s));
}

/* ── Move cursor to row r, column c (both 1-based) ───────────────────────── */
static void goto_rc(int r, int c)
{
    char buf[20];
    int  i = 0;
    buf[i++] = '\033'; buf[i++] = '[';
    if (r >= 10) buf[i++] = (char)('0' + r / 10);
    buf[i++] = (char)('0' + r % 10);
    buf[i++] = ';';
    if (c >= 10) buf[i++] = (char)('0' + c / 10);
    buf[i++] = (char)('0' + c % 10);
    buf[i++] = 'H';
    buf[i]   = '\0';
    outs(buf);
}

/* ── Print an unsigned byte value as decimal (max 2 digits) ──────────────── */
static void put_uint(uint8_t v, char *dst, int *pi)
{
    if (v >= 100) { dst[(*pi)++] = (char)('0' + v / 100); }
    if (v >=  10) { dst[(*pi)++] = (char)('0' + (v / 10) % 10); }
    dst[(*pi)++] = (char)('0' + v % 10);
}

/* ── Waveform and connection name annotations ─────────────────────────────── */
static const char *ws_name(uint8_t ws)
{
    switch (ws) {
        case 0: return "sine    ";
        case 1: return "half-sin";
        case 2: return "abs-sin ";
        case 3: return "pls-sin ";
    }
    return "?       ";
}

static const char *conn_name(uint8_t c)
{
    return c ? "additive   " : "FM (op1+op2)";
}

/* ── Render one parameter cell (CELL_WIDTH chars) ────────────────────────── *
 * Caller has already positioned the cursor.                                  *
 * is_sel: 1 = highlight this cell as selected.                               */
static void draw_cell(int idx, int is_sel)
{
    if (idx < 0 || idx >= N_PARAMS) {
        outs("                                      ");
        return;
    }

    param_t *p = &g_params[idx];

    /*
     * Build content string (36 chars), then surround with one space each
     * side to make 38 == CELL_WIDTH.
     *
     * Layout: LABEL(10) SP VALUE(3) SP ANNOTATION(up to 21) ... pad to 36
     */
    char tmp[48];
    int  ti = 0;

    /* label – left-padded to LABEL_W chars */
    const char *lp = p->label;
    while (*lp && ti < LABEL_W) tmp[ti++] = *lp++;
    while (ti < LABEL_W)        tmp[ti++] = ' ';
    tmp[ti++] = ' ';  /* separator */

    /* value string (3 chars, right-aligned) */
    uint8_t v = *p->val;
    if (p->max == 1 && idx != 25) {
        /* boolean toggle: show ON / OFF */
        tmp[ti++] = 'O';
        tmp[ti++] = v ? 'N' : 'F';
        tmp[ti++] = v ? ' ' : 'F';
    } else {
        /* numeric – right-align in 3 chars */
        if (v >= 100) { put_uint(v, tmp, &ti); }
        else          { tmp[ti++] = ' '; if (v >= 10) { put_uint(v, tmp, &ti); }
                        else             { tmp[ti++] = ' '; put_uint(v, tmp, &ti); } }
    }
    tmp[ti++] = ' ';  /* separator after value */

    /* annotation */
    const char *ann = NULL;
    if (idx == 11 || idx == 23) ann = ws_name(v);        /* MOD/CAR WS */
    else if (idx == 25)         ann = conn_name(v);       /* CONNECTION  */

    if (ann) {
        while (*ann && ti < 36) tmp[ti++] = *ann++;
    }

    /* pad to exactly 36 chars */
    while (ti < 36) tmp[ti++] = ' ';

    /* emit: colour + " " + 36 chars + " " + reset */
    if (is_sel) outs(BG_SEL BOLD COL_WHITE);
    else        outs(COL_GREY);

    outs(" ");
    write(1, tmp, 36);
    outs(" ");
    outs(RESET);
}

/* ── Draw a separator line spanning both columns ─────────────────────────── */
static void draw_sep(int row)
{
    goto_rc(row, 1);
    outs(COL_GREY "------------------------------------------------------------------------" RESET);
}

/* ── Full UI redraw ─────────────────────────────────────────────────────── */
static void draw_ui(void)
{
    outs(CLR);

    /* Row 1 – title + key hint */
    goto_rc(1, 1);
    outs(BOLD COL_YELLOW "  OPL2 Preset Editor" RESET
         "   [N]ame  [S]ave  [L]oad  [Q]uit  "
         COL_GREY "UP/DN=nav  LT/RT=value  Tab=MOD<>CAR" RESET);

    /* Row 3 – section headers */
    goto_rc(ROW_HDR, COL_MOD);
    outs(COL_YELLOW BOLD "-- MODULATOR --" RESET);
    goto_rc(ROW_HDR, COL_CAR);
    outs(COL_YELLOW BOLD "-- CARRIER --" RESET);

    draw_sep(ROW_SEP1);

    /* Rows 5-16: 12 paired param rows */
    for (int i = 0; i < 12; i++) {
        goto_rc(ROW_PARAMS + i, COL_MOD); draw_cell(i,      g_cursor == i);
        goto_rc(ROW_PARAMS + i, COL_CAR); draw_cell(i + 12, g_cursor == (i + 12));
    }

    draw_sep(ROW_SEP2);

    /* Row 18 – channel header */
    goto_rc(ROW_CHAN, COL_MOD);
    outs(COL_YELLOW BOLD "-- CHANNEL --" RESET);

    /* Rows 19-20 – FEEDBACK, CONNECTION */
    goto_rc(ROW_FB,  COL_MOD); draw_cell(24, g_cursor == 24);
    goto_rc(ROW_CON, COL_MOD); draw_cell(25, g_cursor == 25);

    draw_sep(ROW_SEP3);

    /* Row 22 – preset name */
    goto_rc(ROW_NAME, 1);
    outs(COL_YELLOW "  Name: " RESET BOLD);
    outs(g_preset.name[0] ? g_preset.name : "(unnamed)");
    outs(RESET);

    /* Row 23 – key hints */
    goto_rc(ROW_HINTS, 1);
    outs(COL_GREY "  [N] rename  [S] save .opl2  [L] load .opl2  [Q/Esc] quit" RESET);

    /* Row 24 – status / current param hint */
    goto_rc(ROW_STATUS, 1);
    outs(COL_GREEN);
    if (g_status[0]) {
        outs(g_status);
        g_status[0] = '\0';
    } else {
        outs(g_params[g_cursor].hint);
    }
    outs(RESET);
}

/* ── Clear the status row by overwriting with spaces ─────────────────────── */
static void clear_status_row(void)
{
    goto_rc(ROW_STATUS, 1);
    outs("                                                                                ");
    goto_rc(ROW_STATUS, 1);
}

/* ── Inline string prompt at the status row ──────────────────────────────── *
 * Repaints the whole row on every keystroke (VBE terminal has no cursor-    *
 * left or erase-line support, so we can't do in-place editing).             *
 * Returns length written, or 0 if cancelled with Esc.                       */
static int prompt_string(const char *msg, char *out_buf, int max_len)
{
    int pos = 0;
    out_buf[0] = '\0';

    for (;;) {
        /* repaint status row */
        clear_status_row();
        outs(COL_GREEN);
        outs(msg);
        if (pos > 0) write(1, out_buf, pos);
        outs("_" RESET);   /* show a blinking-cursor substitute */

        keyboard_event_t ev;
        int n = read(g_kbfd, &ev, sizeof(ev));
        if (n != (int)sizeof(ev)) continue;
        if (ev.flags & KEY_FLAG_RELEASED) continue;
        if (ev.scancode == SC_PHANTOM) continue;

        if (ev.scancode == SC_ENTER) {
            break;
        } else if (ev.scancode == SC_ESC) {
            out_buf[0] = '\0';
            return 0;
        } else if (ev.scancode == SC_BKSP) {
            if (pos > 0) { pos--; out_buf[pos] = '\0'; }
        } else if (ev.ascii >= 0x20 && ev.ascii < 0x7F) {
            if (pos < max_len - 1) {
                out_buf[pos++] = (char)ev.ascii;
                out_buf[pos]   = '\0';
            }
        }
    }
    return pos;
}

/* ── File I/O ───────────────────────────────────────────────────────────── */

static void sanitise_filename(char *dst, const char *src, int max)
{
    int i = 0;
    for (; *src && i < max - 6; src++) {
        char c = *src;
        if (c == ' ' || c == '/') c = '_';
        dst[i++] = c;
    }
    dst[i++] = '.'; dst[i++] = 'o'; dst[i++] = 'p';
    dst[i++] = 'l'; dst[i++] = '2'; dst[i]   = '\0';
}

static void do_save(void)
{
    char fname[64];
    const char *nm = g_preset.name[0] ? g_preset.name : "untitled";
    sanitise_filename(fname, nm, 60);

    int fd = open(fname, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        snprintf(g_status, sizeof(g_status), "Save failed: cannot open %s", fname);
        return;
    }

    preset_file_t pf;
    pf.magic[0] = 'O'; pf.magic[1] = 'P';
    pf.magic[2] = 'L'; pf.magic[3] = '2';
    pf.preset = g_preset;

    int wr = write(fd, &pf, sizeof(pf));
    close(fd);

    if (wr == (int)sizeof(pf))
        snprintf(g_status, sizeof(g_status), "Saved to %s", fname);
    else
        snprintf(g_status, sizeof(g_status), "Save error: %d/%d bytes written",
                 wr, (int)sizeof(pf));
}

static void do_load(void)
{
    char fname[64];
    int  n = prompt_string("Load file: ", fname, sizeof(fname));
    if (n == 0) {
        snprintf(g_status, sizeof(g_status), "Load cancelled");
        return;
    }

    int fd = open(fname, O_RDONLY);
    if (fd < 0) {
        snprintf(g_status, sizeof(g_status), "Load failed: cannot open %s", fname);
        return;
    }

    preset_file_t pf;
    int nr = read(fd, &pf, sizeof(pf));
    close(fd);

    if (nr != (int)sizeof(pf)) {
        snprintf(g_status, sizeof(g_status), "Load error: short read (%d bytes)", nr);
        return;
    }
    if (pf.magic[0] != 'O' || pf.magic[1] != 'P' ||
        pf.magic[2] != 'L' || pf.magic[3] != '2') {
        snprintf(g_status, sizeof(g_status), "Bad magic in %s – not an .opl2 file", fname);
        return;
    }

    g_preset = pf.preset;
    build_params();   /* rebuild val pointers into the new g_preset copy */
    snprintf(g_status, sizeof(g_status), "Loaded %s", fname);
}

static void do_rename(void)
{
    char buf[48];
    int  n = prompt_string("New name: ", buf, sizeof(buf));
    if (n > 0) {
        strncpy(g_preset.name, buf, sizeof(g_preset.name) - 1);
        g_preset.name[sizeof(g_preset.name) - 1] = '\0';
        snprintf(g_status, sizeof(g_status), "Name: %s", g_preset.name);
    }
}

/* ── Main event loop ─────────────────────────────────────────────────────── */
static void editor_loop(void)
{
    draw_ui();

    for (;;) {
        keyboard_event_t ev;
        int n = read(g_kbfd, &ev, sizeof(ev));
        if (n != (int)sizeof(ev)) continue;
        if (ev.flags & KEY_FLAG_RELEASED) continue;
        if (ev.scancode == SC_PHANTOM) continue;  /* 0xE0 prefix artefact */

        switch (ev.scancode) {
        case SC_ARROW_UP:
            if (g_cursor > 0) g_cursor--;
            break;

        case SC_ARROW_DN:
            if (g_cursor < N_PARAMS - 1) g_cursor++;
            break;

        case SC_ARROW_LT:
            if (*g_params[g_cursor].val > g_params[g_cursor].min)
                (*g_params[g_cursor].val)--;
            break;

        case SC_ARROW_RT:
            if (*g_params[g_cursor].val < g_params[g_cursor].max)
                (*g_params[g_cursor].val)++;
            break;

        case SC_TAB:
            /* swap between matching MOD (0-11) and CAR (12-23) param */
            if      (g_cursor <  12) g_cursor += 12;
            else if (g_cursor <  24) g_cursor -= 12;
            /* indices 24-25 (channel) have no counterpart; ignore Tab */
            break;

        case SC_ESC:
            goto quit;

        default:
            if (!ev.ascii) break;
            switch (ev.ascii) {
            case 'n': case 'N': do_rename(); break;
            case 's': case 'S': do_save();   break;
            case 'l': case 'L': do_load();   break;
            case 'q': case 'Q': goto quit;
            default: break;
            }
            break;
        }

        draw_ui();
        continue;
quit:
        break;
    }
}

/* ── Entry point ────────────────────────────────────────────────────────── */
int main(void)
{
    /* default patch: gentle sine organ */
    memset(&g_preset, 0, sizeof(g_preset));
    strncpy(g_preset.name, "New Preset", sizeof(g_preset.name) - 1);
    g_preset.mod_mult = 1;  g_preset.mod_tl = 20;
    g_preset.mod_ar   = 15; g_preset.mod_dr =  0;
    g_preset.mod_sl   =  0; g_preset.mod_rr =  7;
    g_preset.car_mult = 1;  g_preset.car_tl =  0;
    g_preset.car_ar   = 15; g_preset.car_dr =  0;
    g_preset.car_sl   =  0; g_preset.car_rr =  7;
    g_preset.feedback = 0;  g_preset.connection = 0;

    build_params();

    g_kbfd = open("/dev/keyboard/event", O_RDONLY);
    if (g_kbfd < 0) {
        write(1, "opl2_editor: cannot open /dev/keyboard/event\n", 45);
        return 1;
    }

    editor_loop();

    outs(CLR "OPL2 Editor closed.\n");
    close(g_kbfd);
    return 0;
}
