/**
 * Widget demo — exercises gooey_draw shapes + gooey_widgets UI components.
 * Launch from WM program launcher (Alt+P) as "widget_demo".
 */

#include "gui/gooey.h"
#include "gui/gooey_draw.h"
#include "gui/gooey_widgets.h"
#include "gui/gooey_frame.h"
#include "gui/gooey_theme.h"

#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace gooey;
using namespace gooey::draw;
using namespace gooey::widgets;
using namespace gooey::frame;

int main(void) {
    Window win;
    if (!win.attach_layer_from_wm_environment()) {
        printf("widget_demo: attach_layer_from_wm_environment failed\n");
        return 1;
    }

    int evfd = Window::events_fd_from_environment();
    Theme th = default_theme();
    ChromeColors chrome = chrome_colors_wm_default();
    (void)gooey::theme::sync_from_wm_environment(&th, &chrome);

    if (!win.surface().valid())
        return 1;

    /* -- Setup widgets (positions set each frame from content_bounds) -- */

    Label title_label;
    title_label.text = "Gooey Widget Demo";
    title_label.bold = true;

    Button btn_hello;
    btn_hello.text = "Click Me";

    FlatButton btn_flat;
    btn_flat.text = "Flat Btn";

    Checkbox chk_option;
    chk_option.text = "Enable option";

    RadioButton radio_a;
    radio_a.text = "Alpha";
    radio_a.selected = true;

    RadioButton radio_b;
    radio_b.text = "Beta";

    TextBox input;
    input.set_text("Type here...");

    ProgressBar pbar;
    pbar.value = 65;

    Slider slider;
    slider.value = 50;

    static const char *list_items[] = {
        "Item 0", "Item 1", "Item 2", "Item 3", "Item 4",
        "Item 5", "Item 6", "Item 7", "Item 8", "Item 9"
    };
    ListBox lbox;
    lbox.items = list_items;
    lbox.item_count = 10;
    lbox.selected = 0;

    GroupBox gbox;
    gbox.text = "Shapes";

    Label status_label;
    status_label.text = "Ready.";

    bool focused = true;

    char status_buf[64] = "Ready.";
    int click_count = 0;
    bool prev_left = false;

    /* -- Main loop ----------------------------------------------------- */

    for (;;) {
        struct pollfd pfd;
        pfd.fd = evfd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        (void)poll(&pfd, 1, 30);

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
                ms = make_mouse_state(ev, prev_left);
                prev_left = ms.left_down;
                got_mouse = true;
            } else if (ev.kind == Event::k_keyboard) {
                input.handle_keyboard(ev.keyboard);
                lbox.handle_keyboard(ev.keyboard);
            }
        }

        sw = static_cast<int>(win.surface().width());
        sh = static_cast<int>(win.surface().height());
        int ox = 0, oy = 0, cw = 0, ch = 0;
        content_bounds(sw, sh, &ox, &oy, &cw, &ch);

        title_label.bounds = Rect(ox + 10, oy + 8, 200, 20);
        btn_hello.bounds = Rect(ox + 10, oy + 36, 100, 28);
        btn_flat.bounds = Rect(ox + 120, oy + 36, 100, 28);
        chk_option.bounds = Rect(ox + 10, oy + 74, 160, 20);
        radio_a.bounds = Rect(ox + 10, oy + 100, 100, 20);
        radio_b.bounds = Rect(ox + 110, oy + 100, 100, 20);
        input.bounds = Rect(ox + 10, oy + 130, 220, 24);
        pbar.bounds = Rect(ox + 10, oy + 164, 220, 18);
        slider.bounds = Rect(ox + 10, oy + 192, 220, 20);
        lbox.bounds = Rect(ox + 10, oy + 222, 150, 100);
        gbox.bounds = Rect(ox + 240, oy + 36, 200, 120);
        status_label.bounds = Rect(ox + 10, oy + 330, 400, 16);

        if (got_mouse) {
            btn_hello.handle_mouse(ms);
            btn_flat.handle_mouse(ms);
            chk_option.handle_mouse(ms);
            radio_a.handle_mouse(ms);
            radio_b.handle_mouse(ms);
            input.handle_mouse(ms);
            slider.handle_mouse(ms);
            lbox.handle_mouse(ms);

            if (radio_a.clicked) radio_b.selected = false;
            if (radio_b.clicked) radio_a.selected = false;

            if (btn_hello.clicked) {
                click_count++;
                snprintf(status_buf, sizeof(status_buf), "Clicked %d times!", click_count);
                status_label.text = status_buf;
            }
            if (slider.changed)
                pbar.value = slider.value;
        }

        if (!win.surface().valid()) continue;

        Surface &s = win.surface();
        rect_filled(s, ox, oy, cw, ch, th.bg);

        /* Widgets */
        title_label.paint(s, th);
        btn_hello.paint(s, th);
        btn_flat.paint(s, th);
        chk_option.paint(s, th);
        radio_a.paint(s, th);
        radio_b.paint(s, th);
        input.paint(s, th);
        pbar.paint(s, th);
        slider.paint(s, th);
        lbox.paint(s, th);
        gbox.paint(s, th);
        status_label.paint(s, th);

        /* Shapes in GroupBox area */
        int sx0 = ox + 250, sy0 = oy + 60;
        line(s, sx0, sy0, sx0 + 40, sy0 + 30, rgb(255, 100, 100));
        rect(s, sx0 + 50, sy0, 30, 25, rgb(100, 255, 100));
        circle(s, sx0 + 120, sy0 + 15, 12, rgb(100, 100, 255));
        ellipse(s, sx0 + 165, sy0 + 15, 20, 10, rgb(255, 255, 100));

        triangle_filled(s, sx0 + 10, sy0 + 75, sx0 + 30, sy0 + 45, sx0 + 50, sy0 + 75, rgb(255, 150, 50));
        circle_filled(s, sx0 + 90, sy0 + 60, 12, rgb(150, 50, 255));
        rect_rounded_filled(s, sx0 + 120, sy0 + 45, 50, 35, 6, rgb(50, 200, 150));
        ngon(s, sx0 + 30, sy0 + 60, 15, 6, rgb(200, 200, 50));

        paint(s, sw, sh, "widget_demo", focused, chrome);

        win.damage_all();
        win.present();
        win.wait_until_presented();
    }

done:
    win.close();
    return 0;
}
