/**
 * Task manager — GUI frontend for sys_5ht_list_processes + sys_5ht_sysinfo.
 *
 * Shows a live-updating table of running tasks (PID / user / name / priv
 * level / CPU ticks / memory / disk I/O), the aggregate kernel/user CPU tick
 * totals, and two rolling Graph widgets for CPU share and memory usage.
 *
 * The "Split user/kernel" checkbox toggles, simultaneously:
 *   - CPU graph: one combined line (unchecked) vs. two series, user + kernel
 *     (checked)
 *   - Table: single CPU% column (unchecked) vs. USR%/SYS% columns (checked)
 *
 * Launched from the WM program launcher (Alt+P) as "taskman".
 */

#include "gui/gooey.h"
#include "gui/gooey_draw.h"
#include "gui/gooey_widgets.h"
#include "gui/gooey_frame.h"
#include "gui/gooey_theme.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <lib5ht.h>

extern "C" int kill(pid_t pid, int sig);

using namespace gooey;
using namespace gooey::draw;
using namespace gooey::widgets;
using namespace gooey::frame;

static const int MAX_PROCS = 128;
static const int ROW_LEN   = 128;

struct ProcRow {
    int pid;
    char priv;
    char line[ROW_LEN];
};

static ProcRow      g_rows[MAX_PROCS];
static const char  *g_row_ptrs[MAX_PROCS];
static int          g_row_count = 0;

static uint32_t     g_prev_kernel_ticks      = 0;
static uint32_t     g_prev_user_ticks        = 0;
static uint32_t     g_prev_idle_kernel_ticks = 0;

/** Name of the kernel idle task — spawned by kernel.c's load_init. */
static const char   g_idle_task_name[]       = "kernel: idle";

static const char *priv_str(int priv) {
    return priv == 0 ? "kern" : "user";
}

static int format_bytes(char *dst, size_t dstsz, uint32_t bytes) {
    if (bytes >= (1u << 20))
        return snprintf(dst, dstsz, "%lu.%luM",
                        (unsigned long)(bytes >> 20),
                        (unsigned long)(((bytes & ((1u << 20) - 1)) * 10u) >> 20));
    if (bytes >= (1u << 10))
        return snprintf(dst, dstsz, "%lu.%luK",
                        (unsigned long)(bytes >> 10),
                        (unsigned long)(((bytes & ((1u << 10) - 1)) * 10u) >> 10));
    return snprintf(dst, dstsz, "%lu", (unsigned long)bytes);
}

/**
 * Look up a username for @p uid from /etc/passwd. Falls back to a decimal
 * string on any failure so the table never shows an empty user column.
 * Reads the whole file on each call — fine for a 10-line passwd.
 */
static void resolve_username(unsigned uid, char *out, size_t outsize) {
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd < 0) { snprintf(out, outsize, "%u", uid); return; }
    char buf[1024];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) { snprintf(out, outsize, "%u", uid); return; }
    buf[n] = '\0';

    char *line = buf;
    while (line < buf + n) {
        char *nl = std::strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line[0] != '\0' && line[0] != '#') {
            char *c1 = std::strchr(line, ':');
            if (c1) {
                char *c2 = std::strchr(c1 + 1, ':');
                if (c2) {
                    unsigned entry_uid = 0;
                    for (char *p = c2 + 1; *p >= '0' && *p <= '9'; ++p)
                        entry_uid = entry_uid * 10u + (unsigned)(*p - '0');
                    if (entry_uid == uid) {
                        size_t ulen = (size_t)(c1 - line);
                        if (ulen >= outsize) ulen = outsize - 1;
                        std::memcpy(out, line, ulen);
                        out[ulen] = '\0';
                        return;
                    }
                }
            }
        }
        if (!nl) break;
        line = nl + 1;
    }
    snprintf(out, outsize, "%u", uid);
}

/**
 * Refresh the process table and per-refresh CPU stats.
 *
 * @p user_frac_permil_out / @p kernel_frac_permil_out are the user / kernel
 * shares of ticks consumed over the refresh interval, each in per-mille.
 * They sum to roughly 1000 (bounded by integer rounding and zero-delta
 * intervals).
 */
static void refresh_process_table(uint32_t *kernel_total_out,
                                  uint32_t *user_total_out,
                                  uint32_t *mem_free_out,
                                  uint32_t *mem_total_out,
                                  int      *current_pid_count,
                                  uint32_t *user_frac_permil_out,
                                  uint32_t *kernel_frac_permil_out,
                                  bool      split_columns) {
    proc_5ht_t procs[MAX_PROCS];
    std::memset(procs, 0, sizeof(procs));
    sys_5ht_list_processes(procs, MAX_PROCS);

    sysinfo_5ht_t totals;
    std::memset(&totals, 0, sizeof(totals));
    sys_5ht_sysinfo(&totals);

    uint32_t dk = totals.cpu_kernel_total - g_prev_kernel_ticks;
    uint32_t du = totals.cpu_user_total   - g_prev_user_ticks;
    g_prev_kernel_ticks = totals.cpu_kernel_total;
    g_prev_user_ticks   = totals.cpu_user_total;

    /* Find the "kernel: idle" task so we can exclude its tick delta from
     * the kernel share of the CPU graph — halted cycles aren't real work. */
    uint32_t cur_idle_kernel = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].name[0] == '\0')
            break;
        if (std::strcmp(procs[i].name, g_idle_task_name) == 0) {
            cur_idle_kernel = procs[i].cpu_kernel_ticks;
            break;
        }
    }
    uint32_t didle = cur_idle_kernel - g_prev_idle_kernel_ticks;
    g_prev_idle_kernel_ticks = cur_idle_kernel;
    uint32_t dk_busy = (dk >= didle) ? (dk - didle) : 0u;

    uint32_t delta_total = dk + du;
    if (delta_total == 0)
        delta_total = 1;

    uint32_t total_ticks = totals.cpu_kernel_total + totals.cpu_user_total + 1u;

    g_row_count = 0;
    for (int i = 0; i < MAX_PROCS; i++) {
        if (procs[i].name[0] == '\0')
            break;

        uint32_t total_proc_ticks = procs[i].cpu_user_ticks + procs[i].cpu_kernel_ticks;

        /* All per-mille figures are share-of-lifetime: relative to total
         * ticks accumulated across the whole system since boot. */
        uint32_t cpu_permil   = (uint32_t)((uint64_t)total_proc_ticks        * 1000u / total_ticks);
        uint32_t usr_permil   = (uint32_t)((uint64_t)procs[i].cpu_user_ticks * 1000u / total_ticks);
        uint32_t sys_permil   = (uint32_t)((uint64_t)procs[i].cpu_kernel_ticks * 1000u / total_ticks);

        char mem_buf[16];
        char disk_buf[16];
        char user_buf[16];
        format_bytes(mem_buf,  sizeof(mem_buf),  procs[i].mem_bytes);
        format_bytes(disk_buf, sizeof(disk_buf), procs[i].disk_bytes);
        resolve_username(procs[i].uid, user_buf, sizeof(user_buf));

        ProcRow &r = g_rows[g_row_count];
        r.pid  = procs[i].pid;
        r.priv = (char)procs[i].priv;

        if (split_columns) {
            std::snprintf(r.line, ROW_LEN,
                          "%4d %-8s %-4s %-16s %3lu.%lu%% %3lu.%lu%% %-8s %-8s",
                          procs[i].pid,
                          user_buf,
                          priv_str(procs[i].priv),
                          procs[i].name,
                          (unsigned long)(usr_permil / 10u),
                          (unsigned long)(usr_permil % 10u),
                          (unsigned long)(sys_permil / 10u),
                          (unsigned long)(sys_permil % 10u),
                          mem_buf, disk_buf);
        } else {
            std::snprintf(r.line, ROW_LEN,
                          "%4d %-8s %-4s %-16s %3lu.%lu%%  %-8s %-8s",
                          procs[i].pid,
                          user_buf,
                          priv_str(procs[i].priv),
                          procs[i].name,
                          (unsigned long)(cpu_permil / 10u),
                          (unsigned long)(cpu_permil % 10u),
                          mem_buf, disk_buf);
        }
        g_row_ptrs[g_row_count] = r.line;
        g_row_count++;
    }

    if (kernel_total_out)       *kernel_total_out       = totals.cpu_kernel_total;
    if (user_total_out)         *user_total_out         = totals.cpu_user_total;
    if (mem_free_out)           *mem_free_out           = totals.mem_free;
    if (mem_total_out)          *mem_total_out          = totals.mem_total;
    if (current_pid_count)      *current_pid_count      = g_row_count;
    if (user_frac_permil_out)
        *user_frac_permil_out   = (uint32_t)((uint64_t)du * 1000u / delta_total);
    if (kernel_frac_permil_out)
        *kernel_frac_permil_out = (uint32_t)((uint64_t)dk_busy * 1000u / delta_total);
}

int main(void) {
    Window win;
    if (!win.attach_layer_from_wm_environment()) {
        printf("taskman: attach_layer_from_wm_environment failed\n");
        return 1;
    }

    int evfd = Window::events_fd_from_environment();
    Theme th = default_theme();
    ChromeColors chrome = chrome_colors_wm_default();
    (void)gooey::theme::sync_from_wm_environment(&th, &chrome);

    if (!win.surface().valid())
        return 1;

    Label header_label;
    char header_buf[128];
    header_label.text = header_buf;
    header_label.bold = true;

    Label totals_label;
    char totals_buf[192] = "";
    totals_label.text = totals_buf;

    ListBox lbox;
    lbox.items = g_row_ptrs;
    lbox.item_count = 0;
    lbox.selected = -1;

    Button btn_refresh;
    btn_refresh.text = "Refresh";

    Button btn_kill;
    btn_kill.text = "Kill";

    Button btn_term;
    btn_term.text = "SIGTERM";

    Checkbox split_check;
    split_check.text = "Split user/kernel";
    split_check.checked = false;

    Label status_label;
    char status_buf[128] = "Ready.";
    status_label.text = status_buf;

    Graph cpu_graph;
    cpu_graph.min_value         = 0;
    cpu_graph.max_value         = 1000;
    cpu_graph.filled            = true;
    cpu_graph.second_filled     = false;
    cpu_graph.show_grid         = true;
    cpu_graph.line_color        = rgb(110, 190, 255);
    cpu_graph.second_line_color = rgb(255, 170,  90);
    cpu_graph.label             = "CPU total";

    Graph mem_graph;
    mem_graph.min_value  = 0;
    mem_graph.max_value  = 1000;
    mem_graph.filled     = true;
    mem_graph.show_grid  = true;
    mem_graph.line_color = rgb(255, 170, 100);
    mem_graph.label      = "MEM used (permil)";

    uint32_t kern_ticks = 0, user_ticks = 0, mem_free = 0, mem_total = 0;
    uint32_t user_frac_permil = 0, kernel_frac_permil = 0;
    int proc_count = 0;
    bool split = split_check.checked;
    refresh_process_table(&kern_ticks, &user_ticks, &mem_free, &mem_total,
                          &proc_count, &user_frac_permil, &kernel_frac_permil, split);
    lbox.item_count = g_row_count;

    bool focused = true;
    bool prev_left = false;
    int refresh_ticks = 0;

    for (;;) {
        struct pollfd pfd;
        pfd.fd = evfd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        (void)poll(&pfd, 1, 500);

        int sw = static_cast<int>(win.surface().width());
        int sh = static_cast<int>(win.surface().height());

        MouseState ms;
        bool got_mouse = false;

        for (;;) {
            Event ev;
            std::memset(&ev, 0, sizeof(ev));
            ssize_t r = poll_gui_event(evfd, &ev);
            if (r == 0) break;
            if (r < 0) goto done;

            if (ev.kind == Event::k_configure) {
                if (!win.apply_configure_event(ev)) goto done;
            } else if (ev.kind == Event::k_focus) {
                focused = ev.focus.focused != 0;
            } else if (ev.kind == Event::k_layer) {
                (void)win.apply_layer_event(ev);
            } else if (ev.kind == Event::k_theme) {
                (void)gooey::theme::apply_gui_event(ev, &th, &chrome);
            } else if (ev.kind == Event::k_mouse) {
                Event evc;
                if (peel_content_mouse(ev, &evc, sw, sh)) {
                    ms = make_mouse_state(evc, prev_left);
                    prev_left = ms.left_down;
                    got_mouse = true;
                } else {
                    prev_left = (ev.mouse.buttons & 0x01) != 0;
                }
            } else if (ev.kind == Event::k_keyboard) {
                lbox.handle_keyboard(ev.keyboard);
            }
        }

        bool new_split = split_check.checked;

        if (++refresh_ticks >= 2 || new_split != split) {
            refresh_ticks = 0;
            split = new_split;
            refresh_process_table(&kern_ticks, &user_ticks, &mem_free, &mem_total,
                                  &proc_count, &user_frac_permil, &kernel_frac_permil, split);
            if (lbox.selected >= g_row_count)
                lbox.selected = g_row_count - 1;
            lbox.item_count = g_row_count;

            if (split) {
                cpu_graph.push2((int)user_frac_permil, (int)kernel_frac_permil);
            } else {
                uint32_t total_busy = user_frac_permil + kernel_frac_permil;
                if (total_busy > 1000u) total_busy = 1000u;
                cpu_graph.has_second = false;
                cpu_graph.push((int)total_busy);
            }
            cpu_graph.label        = split ? "CPU user"   : "CPU total";
            cpu_graph.second_label = split ? "CPU kernel" : nullptr;

            uint32_t mem_permil = mem_total ?
                (uint32_t)((uint64_t)(mem_total - mem_free) * 1000u / mem_total) : 0u;
            mem_graph.push((int)mem_permil);
        }

        std::snprintf(totals_buf, sizeof(totals_buf),
                      "Tasks: %d   Kernel ticks: %lu   User ticks: %lu   Mem: %lu/%lu MB",
                      proc_count,
                      (unsigned long)kern_ticks,
                      (unsigned long)user_ticks,
                      (unsigned long)(mem_total - mem_free),
                      (unsigned long)mem_total);

        if (split) {
            std::snprintf(header_buf, sizeof(header_buf),
                          " PID  USER     PRIV NAME             USR%%    SYS%%    MEM      DISK");
        } else {
            std::snprintf(header_buf, sizeof(header_buf),
                          " PID  USER     PRIV NAME             CPU%%     MEM      DISK");
        }

        sw = static_cast<int>(win.surface().width());
        sh = static_cast<int>(win.surface().height());
        int ox = 0, oy = 0, cw = 0, ch = 0;
        content_bounds(sw, sh, &ox, &oy, &cw, &ch);

        int pad = 10;
        int graph_h         = 72;
        int inter_gap       = 8;
        int graph_w         = (cw - pad * 2 - inter_gap) / 2;
        totals_label.bounds = Rect(ox + pad, oy + 6,              cw - pad * 2, 18);
        cpu_graph.bounds    = Rect(ox + pad,              oy + 28, graph_w, graph_h);
        mem_graph.bounds    = Rect(ox + pad + graph_w + inter_gap, oy + 28, graph_w, graph_h);
        split_check.bounds  = Rect(ox + pad, oy + 28 + graph_h + 6, 180, 20);
        header_label.bounds = Rect(ox + pad, oy + 28 + graph_h + 30, cw - pad * 2, 18);
        int list_top        = oy + 28 + graph_h + 30 + 22;
        int list_bottom_pad = 78;
        lbox.bounds         = Rect(ox + pad, list_top,
                                   cw - pad * 2,
                                   ch - (list_top - oy) - list_bottom_pad);
        int btn_y           = oy + ch - 64;
        btn_refresh.bounds  = Rect(ox + pad,             btn_y, 96, 28);
        btn_kill.bounds     = Rect(ox + pad + 106,       btn_y, 80, 28);
        btn_term.bounds     = Rect(ox + pad + 106 + 90,  btn_y, 96, 28);
        status_label.bounds = Rect(ox + pad, oy + ch - 26, cw - pad * 2, 18);

        if (got_mouse) {
            lbox.handle_mouse(ms);
            btn_refresh.handle_mouse(ms);
            btn_kill.handle_mouse(ms);
            btn_term.handle_mouse(ms);
            split_check.handle_mouse(ms);

            if (btn_refresh.clicked) {
                refresh_process_table(&kern_ticks, &user_ticks, &mem_free, &mem_total,
                                      &proc_count, &user_frac_permil, &kernel_frac_permil, split);
                if (lbox.selected >= g_row_count)
                    lbox.selected = g_row_count - 1;
                lbox.item_count = g_row_count;

                if (split) {
                    cpu_graph.push2((int)user_frac_permil, (int)kernel_frac_permil);
                } else {
                    uint32_t total_busy = user_frac_permil + kernel_frac_permil;
                    if (total_busy > 1000u) total_busy = 1000u;
                    cpu_graph.has_second = false;
                    cpu_graph.push((int)total_busy);
                }
                uint32_t mem_permil = mem_total ?
                    (uint32_t)((uint64_t)(mem_total - mem_free) * 1000u / mem_total) : 0u;
                mem_graph.push((int)mem_permil);

                std::snprintf(status_buf, sizeof(status_buf), "Refreshed.");
                status_label.text = status_buf;
            }

            if ((btn_kill.clicked || btn_term.clicked) &&
                lbox.selected >= 0 && lbox.selected < g_row_count) {
                int target_pid = g_rows[lbox.selected].pid;
                int sig = btn_kill.clicked ? 9 /* SIGKILL */ : 15 /* SIGTERM */;
                if (target_pid <= 0) {
                    std::snprintf(status_buf, sizeof(status_buf),
                                  "Refusing to signal pid %d.", target_pid);
                } else if (kill(target_pid, sig) < 0) {
                    std::snprintf(status_buf, sizeof(status_buf),
                                  "kill(pid=%d, sig=%d) failed: errno=%d",
                                  target_pid, sig, errno);
                } else {
                    std::snprintf(status_buf, sizeof(status_buf),
                                  "Sent signal %d to pid %d (%s)",
                                  sig, target_pid,
                                  g_rows[lbox.selected].priv == 0 ? "kernel" : "user");
                }
                status_label.text = status_buf;
            }
        }

        if (!win.surface().valid()) continue;

        Surface &s = win.surface();
        rect_filled(s, ox, oy, cw, ch, th.bg);

        totals_label.paint(s, th);
        cpu_graph.paint(s, th);
        mem_graph.paint(s, th);
        split_check.paint(s, th);
        header_label.paint(s, th);
        lbox.paint(s, th);
        btn_refresh.paint(s, th);
        btn_kill.paint(s, th);
        btn_term.paint(s, th);
        status_label.paint(s, th);

        paint(s, sw, sh, "taskman", focused, chrome);

        win.damage_all();
        win.present();
        win.wait_until_presented();
    }

done:
    win.close();
    return 0;
}
