/**
 * Minimal WM-hosted GUI using gooey + Serotonin AF_UNIX socket events.
 * Launch from the WM program launcher (Alt+P) as "gooey_demo".
 */

#include "gui/gooey.h"
#include "gui/gooey_frame.h"
#include "gui/gooey_theme.h"
#include "gui/gooey_widgets.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>

int main(void) {
    gooey::Window win;
    if (!win.attach_layer_from_wm_environment()) {
        printf("gooey_demo: attach_layer_from_wm_environment failed\n");
        return 1;
    }

    int evfd = gooey::Window::events_fd_from_environment();
    gooey::widgets::Theme th = gooey::widgets::default_theme();
    gooey::frame::ChromeColors chrome = gooey::frame::chrome_colors_wm_default();
    (void)gooey::theme::sync_from_wm_environment(&th, &chrome);

    bool focused = true;
    for (;;) {
        struct pollfd pfd;
        pfd.fd = evfd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        (void)poll(&pfd, 1, 80);

        /* Drain available events without blocking */
        for (;;) {
            gooey::Event ev;
            std::memset(&ev, 0, sizeof(ev));
            ssize_t r = gooey::poll_gui_event(evfd, &ev);
            if (r == 0)
                break;
            if (r < 0)
                goto done;
            if (ev.kind == gooey::Event::k_configure) {
                if (!win.apply_configure_event(ev))
                    goto done;
            } else if (ev.kind == gooey::Event::k_focus) {
                focused = ev.focus.focused != 0;
            } else if (ev.kind == gooey::Event::k_layer) {
                (void)win.apply_layer_event(ev);
            } else if (ev.kind == gooey::Event::k_theme) {
                (void)gooey::theme::apply_gui_event(ev, &th, &chrome);
            }
        }

        if (!win.surface().valid())
            continue;

        int sw = static_cast<int>(win.surface().width());
        int sh = static_cast<int>(win.surface().height());
        int ox = 0, oy = 0, cw = 0, ch = 0;
        gooey::frame::content_bounds(sw, sh, &ox, &oy, &cw, &ch);

        win.surface().fill_rect(ox, oy, cw, ch, th.bg);
        gooey::frame::paint(win.surface(), sw, sh, "gooey_demo", focused, chrome);
        win.damage_all();
        win.present();
        win.wait_until_presented();
    }

done:
    win.close();
    return 0;
}
