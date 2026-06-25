/**
 * System Settings — gooey window; drives WM color theme and desktop wallpaper.
 * Launch from the taskbar gear icon or Alt+P as "settings".
 *
 * Theme / wallpaper names must stay in the same order as @c wm_themes and
 * @c wm_render_wallpaper switch cases in @c user/wm/wm.c .
 */

#include "gui/gooey.h"
#include "gui/gooey_draw.h"
#include "gui/gooey_widgets.h"
#include "gui/gooey_frame.h"
#include "gui/gooey_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

using namespace gooey;
using namespace gooey::draw;
using namespace gooey::widgets;
using namespace gooey::frame;

static const int N_THEMES = 7;
static const int N_WP = 8;

static const char *k_theme_names[N_THEMES] = {
    "Default", "Dracula", "Monokai", "Nord", "Gruvbox", "Solarized Dark", "Rose Pine",
};

static const char *k_wp_names[N_WP] = {
    "Solid", "Static", "Grid", "Diamonds", "Checkerboard", "Gradient", "Rings", "Plasma",
};

static int parse_idx_env(const char *name, int lo, int hi, int defv) {
    const char *s = getenv(name);
    if (!s || !*s)
        return defv;
    char *end = nullptr;
    long v = strtol(s, &end, 10);
    if (end == s)
        return defv;
    if (v < lo || v > hi)
        return defv;
    return static_cast<int>(v);
}

int main(void) {
    Window win;
    if (!win.attach_layer_from_wm_environment()) {
        printf("settings: attach_layer_from_wm_environment failed\n");
        return 1;
    }

    int evfd = Window::events_fd_from_environment();
    Theme th = default_theme();
    ChromeColors chrome = chrome_colors_wm_default();
    (void)gooey::theme::sync_from_wm_environment(&th, &chrome);

    if (!win.surface().valid())
        return 1;

    int theme_sel = parse_idx_env(SG_GUI_ENV_THEME_IDX, 0, N_THEMES - 1, 0);
    int wp_sel = parse_idx_env(SG_GUI_ENV_WALLPAPER_IDX, 0, N_WP - 1, 0);

    Panel root;
    root.border = false;
    root.bg_color = th.bg;

    Label title;
    title.text = "System Settings";
    title.bold = true;

    Label hint;
    hint.text = "Changes apply to the whole desktop.";
    hint.bold = false;

    GroupBox gb_theme;
    gb_theme.text = "Color theme";

    RadioButton rad_theme[N_THEMES];
    for (int i = 0; i < N_THEMES; i++) {
        rad_theme[i].text = k_theme_names[i];
        rad_theme[i].selected = (i == theme_sel);
    }

    GroupBox gb_wp;
    gb_wp.text = "Wallpaper";

    RadioButton rad_wp[N_WP];
    for (int i = 0; i < N_WP; i++) {
        rad_wp[i].text = k_wp_names[i];
        rad_wp[i].selected = (i == wp_sel);
    }

    bool prev_left = false;
    bool focused = true;

    for (;;) {
        struct pollfd pfd;
        pfd.fd = evfd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        (void)poll(&pfd, 1, 40);

        int sw = static_cast<int>(win.surface().width());
        int sh = static_cast<int>(win.surface().height());

        MouseState ms;
        bool got_mouse = false;

        for (;;) {
            Event ev;
            memset(&ev, 0, sizeof(ev));
            ssize_t r = poll_gui_event(evfd, &ev);
            if (r == 0)
                break;
            if (r < 0)
                goto done;

            if (ev.kind == Event::k_configure) {
                if (!win.apply_configure_event(ev))
                    goto done;
            } else if (ev.kind == Event::k_focus) {
                focused = ev.focus.focused != 0;
            } else if (ev.kind == Event::k_layer) {
                (void)win.apply_layer_event(ev);
            } else if (ev.kind == Event::k_theme) {
                (void)gooey::theme::apply_gui_event(ev, &th, &chrome);
                root.bg_color = th.bg;
            } else if (ev.kind == Event::k_mouse) {
                ms = make_mouse_state(ev, prev_left);
                prev_left = ms.left_down;
                got_mouse = true;
            }
        }

        sw = static_cast<int>(win.surface().width());
        sh = static_cast<int>(win.surface().height());
        int ox = 0, oy = 0, cw = 0, ch = 0;
        content_bounds(sw, sh, &ox, &oy, &cw, &ch);

        root.bounds = Rect(ox, oy, cw, ch);
        title.bounds = Rect(ox + 12, oy + 8, cw - 24, 20);
        hint.bounds = Rect(ox + 12, oy + 28, cw - 24, 16);

        const int row = 20;
        const int gb_tw = (cw - 28) / 2;
        gb_theme.bounds = Rect(ox + 8, oy + 50, gb_tw, 8 + N_THEMES * row);
        gb_wp.bounds = Rect(ox + 16 + gb_tw, oy + 50, gb_tw, 8 + N_WP * row);

        for (int i = 0; i < N_THEMES; i++)
            rad_theme[i].bounds = Rect(ox + 20, oy + 68 + i * row, gb_tw - 24, row);
        for (int i = 0; i < N_WP; i++)
            rad_wp[i].bounds = Rect(ox + 28 + gb_tw, oy + 68 + i * row, gb_tw - 24, row);

        if (got_mouse) {
            for (int i = 0; i < N_THEMES; i++)
                rad_theme[i].handle_mouse(ms);
            for (int i = 0; i < N_WP; i++)
                rad_wp[i].handle_mouse(ms);

            for (int i = 0; i < N_THEMES; i++) {
                if (rad_theme[i].clicked) {
                    for (int j = 0; j < N_THEMES; j++)
                        rad_theme[j].selected = (j == i);
                    theme_sel = i;
                    (void)wm_send_gui_cli_index(evfd, SG_GUI_CLI_SET_THEME, i);
                    break;
                }
            }
            for (int i = 0; i < N_WP; i++) {
                if (rad_wp[i].clicked) {
                    for (int j = 0; j < N_WP; j++)
                        rad_wp[j].selected = (j == i);
                    wp_sel = i;
                    (void)wm_send_gui_cli_index(evfd, SG_GUI_CLI_SET_WALLPAPER, i);
                    break;
                }
            }
        }

        if (!win.surface().valid())
            continue;

        Surface &s = win.surface();

        root.paint(s, th);
        title.paint(s, th);
        hint.paint(s, th);
        gb_theme.paint(s, th);
        gb_wp.paint(s, th);
        for (int i = 0; i < N_THEMES; i++)
            rad_theme[i].paint(s, th);
        for (int i = 0; i < N_WP; i++)
            rad_wp[i].paint(s, th);

        hline(s, ox + 12, ox + cw - 12, oy + 26, th.border);

        paint(s, sw, sh, "settings", focused, chrome);

        win.damage_all();
        win.present();
        win.wait_until_presented();
    }

done:
    win.close();
    return 0;
}
