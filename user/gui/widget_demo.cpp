/**
 * Widget demo — exercises gooey_draw shapes + gooey_widgets UI components.
 * Launch from WM program launcher (Alt+P) as "widget_demo".
 */

#include "gui/gooey.h"
#include "gui/gooey_draw.h"
#include "gui/gooey_widgets.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace gooey;
using namespace gooey::draw;
using namespace gooey::widgets;

int main(void) {
    printf("widget_demo: starting\n");

    Window win;
    if (!win.attach_layer_from_wm_environment()) {
        printf("widget_demo: attach failed\n");
        return 1;
    }
    printf("widget_demo: attached layer\n");

    int evfd = Window::events_fd_from_environment();
    printf("widget_demo: evfd=%d\n", evfd);

    if (!win.surface().valid()) {
        printf("widget_demo: surface invalid after attach\n");
        return 1;
    }
    printf("widget_demo: surface %ux%u stride=%u\n",
           win.surface().width(), win.surface().height(), win.surface().stride_px());

    /* Phase 1: just fill background — does basic drawing work? */
    win.surface().fill_rect(0, 0, win.surface().width(), win.surface().height(),
                            rgb(30, 30, 30));
    printf("widget_demo: fill_rect ok\n");

    /* Phase 2: draw a few primitives */
    line(win.surface(), 10, 10, 100, 50, rgb(255, 0, 0));
    printf("widget_demo: line ok\n");

    circle(win.surface(), 60, 80, 20, rgb(0, 255, 0));
    printf("widget_demo: circle ok\n");

    draw_text(win.surface(), 10, 120, "Hello Gooey!", rgb(255, 255, 255), rgb(30, 30, 30));
    printf("widget_demo: draw_text ok\n");

    /* Phase 3: one widget */
    Theme th = default_theme();
    Button btn;
    btn.bounds = Rect(10, 150, 100, 28);
    btn.text = "Test";
    btn.paint(win.surface(), th);
    printf("widget_demo: button paint ok\n");

    win.damage_all();
    win.present();
    win.wait_until_presented();
    printf("widget_demo: first frame presented\n");

    /* Event loop */
    bool prev_left = false;
    for (;;) {
        struct pollfd pfd;
        pfd.fd = evfd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        (void)poll(&pfd, 1, 80);

        for (;;) {
            Event ev;
            std::memset(&ev, 0, sizeof(ev));
            ssize_t r = poll_gui_event(evfd, &ev);
            if (r == 0) break;
            if (r < 0) goto done;
            if (ev.kind == Event::k_configure) {
                if (!win.apply_configure_event(ev)) goto done;
            } else if (ev.kind == Event::k_mouse) {
                MouseState ms = make_mouse_state(ev, prev_left);
                prev_left = ms.left_down;
                btn.handle_mouse(ms);
            }
        }

        if (!win.surface().valid()) continue;

        Surface &s = win.surface();
        rect_filled(s, 0, 0, s.width(), s.height(), th.bg);
        line(s, 10, 10, 100, 50, rgb(255, 0, 0));
        circle(s, 60, 80, 20, rgb(0, 255, 0));
        draw_text(s, 10, 120, "Hello Gooey!", th.fg, th.bg);
        btn.paint(s, th);

        win.damage_all();
        win.present();
        win.wait_until_presented();
    }

done:
    win.close();
    return 0;
}
