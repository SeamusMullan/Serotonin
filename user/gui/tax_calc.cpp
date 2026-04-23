/**
 * Tax calculator demo — gooey Window + gooey_widgets + gooey_draw.
 * Launch from WM program launcher (Alt+P) as "tax_calc".
 *
 * Toy model only (not tax advice): flat vs simple progressive bracket,
 * optional standard deduction, W-2 vs 1099 payroll add-on.
 */

#include "gui/gooey.h"
#include "gui/gooey_draw.h"
#include "gui/gooey_widgets.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>

#include <unistd.h>

using namespace gooey;
using namespace gooey::draw;
using namespace gooey::widgets;

static bool parse_income(const char *s, double *out) {
    if (!s || !out)
        return false;
    /* Copy: allow $ , spaces — strtod alone rejects "60,000" / "$50k" style noise */
    char tmp[256];
    int j = 0;
    for (int i = 0; s[i] && j < 255; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == ' ' || c == '\t' || c == ',')
            continue;
        if (c == '$')
            continue;
        tmp[j++] = static_cast<char>(c);
    }
    tmp[j] = '\0';
    if (!tmp[0])
        return false;
    char *end = nullptr;
    double v = std::strtod(tmp, &end);
    if (end == tmp)
        return false;
    while (*end == ' ' || *end == '\t')
        ++end;
    if (*end != '\0')
        return false;
    if (v < 0.0)
        return false;
    *out = v;
    return true;
}

static unsigned long money_ul(double v) {
    if (!(v > 0.0))
        return 0;
    if (v >= static_cast<double>(ULONG_MAX))
        return ULONG_MAX;
    return static_cast<unsigned long>(v + 0.5);
}

static long money_sl(double v) {
    if (v >= 0.0) {
        if (v >= static_cast<double>(LONG_MAX))
            return LONG_MAX;
        return static_cast<long>(v + 0.5);
    }
    if (v <= static_cast<double>(LONG_MIN))
        return LONG_MIN;
    return static_cast<long>(v - 0.5);
}

int main(void) {
    Window win;
    if (!win.attach_layer_from_wm_environment()) {
        printf("tax_calc: attach_layer_from_wm_environment failed\n");
        return 1;
    }

    int evfd = Window::events_fd_from_environment();
    Theme th = default_theme();

    if (!win.surface().valid())
        return 1;

    Panel root;
    root.bounds = Rect(0, 0, static_cast<int>(win.surface().width()), static_cast<int>(win.surface().height()));
    root.border = false;
    root.bg_color = th.bg;

    Label title;
    title.bounds = Rect(12, 8, 320, 20);
    title.text = "Tax Calculator (demo)";
    title.bold = true;

    GroupBox gb_in;
    gb_in.bounds = Rect(8, 28, static_cast<int>(win.surface().width()) - 16, 118);
    gb_in.text = "Inputs";

    Label lbl_gross;
    lbl_gross.bounds = Rect(20, 48, 120, 16);
    lbl_gross.text = "Gross income";

    TextBox tb_income;
    tb_income.bounds = Rect(140, 44, 200, 24);
    tb_income.set_text("60000");

    Label lbl_rate;
    lbl_rate.bounds = Rect(20, 78, 140, 16);
    lbl_rate.text = "Marginal %";

    Slider sl_rate;
    sl_rate.bounds = Rect(140, 74, 200, 22);
    sl_rate.min_value = 10;
    sl_rate.max_value = 37;
    sl_rate.value = 22;

    Checkbox chk_std;
    chk_std.bounds = Rect(20, 100, 200, 20);
    chk_std.text = "Standard deduction (14.6k)";
    chk_std.checked = true;

    GroupBox gb_mode;
    gb_mode.bounds = Rect(8, 152, static_cast<int>(win.surface().width()) - 16, 72);
    gb_mode.text = "Model";

    RadioButton rad_flat;
    rad_flat.bounds = Rect(20, 172, 120, 20);
    rad_flat.text = "Flat rate";
    rad_flat.selected = true;

    RadioButton rad_prog;
    rad_prog.bounds = Rect(150, 172, 160, 20);
    rad_prog.text = "2-bracket";

    RadioButton rad_w2;
    rad_w2.bounds = Rect(20, 194, 100, 20);
    rad_w2.text = "W-2";
    rad_w2.selected = true;

    RadioButton rad_1099;
    rad_1099.bounds = Rect(130, 194, 120, 20);
    rad_1099.text = "1099 (+7.65%)";

    Button btn_calc;
    btn_calc.bounds = Rect(8, 232, 120, 30);
    btn_calc.text = "Compute";

    FlatButton btn_reset;
    btn_reset.bounds = Rect(136, 232, 100, 30);
    btn_reset.text = "Reset";

    Label lbl_tax;
    lbl_tax.bounds = Rect(8, 270, 360, 16);
    lbl_tax.text = "Tax: (press Compute)";

    Label lbl_net;
    lbl_net.bounds = Rect(8, 288, 360, 16);
    lbl_net.text = "Net:";

    Label lbl_eff;
    lbl_eff.bounds = Rect(8, 306, 360, 16);
    lbl_eff.text = "Effective rate:";

    ProgressBar pbar;
    pbar.bounds = Rect(8, 328, static_cast<int>(win.surface().width()) - 16, 18);
    pbar.max_value = 100;
    pbar.value = 0;

    Tooltip tip_slider;
    tip_slider.text = "Top marginal rate for demo";

    char buf_tax[72];
    char buf_net[72];
    char buf_eff[72];

    bool prev_left = false;
    bool show_tip = false;
    int tip_mx = 0, tip_my = 0;

    for (;;) {
        struct pollfd pfd;
        pfd.fd = evfd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        (void)poll(&pfd, 1, 40);

        MouseState ms;
        bool got_mouse = false;

        for (;;) {
            Event ev;
            std::memset(&ev, 0, sizeof(ev));
            ssize_t r = poll_gui_event(evfd, &ev);
            if (r == 0)
                break;
            if (r < 0)
                goto done;

            if (ev.kind == Event::k_configure) {
                if (!win.apply_configure_event(ev))
                    goto done;
                root.bounds = Rect(0, 0, static_cast<int>(win.surface().width()),
                                   static_cast<int>(win.surface().height()));
                gb_in.bounds = Rect(8, 28, static_cast<int>(win.surface().width()) - 16, 118);
                gb_mode.bounds = Rect(8, 152, static_cast<int>(win.surface().width()) - 16, 72);
                pbar.bounds = Rect(8, 328, static_cast<int>(win.surface().width()) - 16, 18);
            } else if (ev.kind == Event::k_mouse) {
                ms = make_mouse_state(ev, prev_left);
                prev_left = ms.left_down;
                got_mouse = true;
                tip_mx = ms.x;
                tip_my = ms.y;
            } else if (ev.kind == Event::k_keyboard) {
                tb_income.handle_keyboard(ev.keyboard);
            }
        }

        if (got_mouse) {
            tb_income.handle_mouse(ms);
            sl_rate.handle_mouse(ms);
            chk_std.handle_mouse(ms);
            rad_flat.handle_mouse(ms);
            rad_prog.handle_mouse(ms);
            rad_w2.handle_mouse(ms);
            rad_1099.handle_mouse(ms);
            btn_calc.handle_mouse(ms);
            btn_reset.handle_mouse(ms);

            if (rad_flat.clicked)
                rad_prog.selected = false;
            if (rad_prog.clicked)
                rad_flat.selected = false;
            if (rad_w2.clicked)
                rad_1099.selected = false;
            if (rad_1099.clicked)
                rad_w2.selected = false;

            show_tip = sl_rate.hovered;

            if (btn_reset.clicked) {
                tb_income.set_text("60000");
                sl_rate.value = 22;
                chk_std.checked = true;
                rad_flat.selected = true;
                rad_prog.selected = false;
                rad_w2.selected = true;
                rad_1099.selected = false;
                lbl_tax.text = "Tax: (press Compute)";
                lbl_net.text = "Net:";
                lbl_eff.text = "Effective rate:";
                pbar.value = 0;
            }

            if (btn_calc.clicked) {
                double gross = 0.0;
                if (!parse_income(tb_income.text, &gross)) {
                    lbl_tax.text = "Tax: invalid income";
                    lbl_net.text = "Net: —";
                    lbl_eff.text = "Effective: —";
                    pbar.value = 0;
                } else {
                    const double std_ded = chk_std.checked ? 14600.0 : 0.0;
                    double taxable = gross - std_ded;
                    if (taxable < 0.0)
                        taxable = 0.0;

                    const double r = static_cast<double>(sl_rate.value) / 100.0;
                    double income_tax = 0.0;
                    if (rad_flat.selected) {
                        income_tax = taxable * r;
                    } else {
                        const double tier = 50000.0;
                        const double t1 = taxable < tier ? taxable : tier;
                        const double t2 = taxable > tier ? taxable - tier : 0.0;
                        const double r2 = r + 0.05;
                        income_tax = t1 * r + t2 * r2;
                    }

                    double payroll = 0.0;
                    if (rad_1099.selected)
                        payroll = gross * 0.0765;

                    const double total_tax = income_tax + payroll;
                    const double net = gross - total_tax;
                    const double eff = gross > 0.0 ? (total_tax / gross) * 100.0 : 0.0;

                    /* Integer printf only — avoids broken/missing float conversion in libc */
                    std::snprintf(buf_tax, sizeof(buf_tax), "Tax: $%lu (inc $%lu + pay $%lu)",
                                  money_ul(total_tax), money_ul(income_tax), money_ul(payroll));
                    std::snprintf(buf_net, sizeof(buf_net), "Net: $%ld", money_sl(net));
                    {
                        int eff10 = static_cast<int>(eff * 10.0 + (eff >= 0.0 ? 0.5 : -0.5));
                        if (eff10 < 0)
                            eff10 = 0;
                        int eff_i = eff10 / 10;
                        int eff_f = eff10 % 10;
                        std::snprintf(buf_eff, sizeof(buf_eff), "Effective rate: %d.%d%%", eff_i, eff_f);
                    }

                    lbl_tax.text = buf_tax;
                    lbl_net.text = buf_net;
                    lbl_eff.text = buf_eff;

                    int pv = static_cast<int>(eff + 0.5);
                    if (pv < 0)
                        pv = 0;
                    if (pv > 100)
                        pv = 100;
                    pbar.value = pv;
                }
            }
        }

        if (!win.surface().valid())
            continue;

        Surface &s = win.surface();
        root.bounds.h = static_cast<int>(s.height());
        root.bounds.w = static_cast<int>(s.width());

        root.paint(s, th);
        title.paint(s, th);
        gb_in.paint(s, th);
        lbl_gross.paint(s, th);
        tb_income.paint(s, th);
        lbl_rate.paint(s, th);
        sl_rate.paint(s, th);
        chk_std.paint(s, th);
        gb_mode.paint(s, th);
        rad_flat.paint(s, th);
        rad_prog.paint(s, th);
        rad_w2.paint(s, th);
        rad_1099.paint(s, th);
        btn_calc.paint(s, th);
        btn_reset.paint(s, th);
        lbl_tax.paint(s, th);
        lbl_net.paint(s, th);
        lbl_eff.paint(s, th);
        pbar.paint(s, th);

        /* Accent line under title */
        hline(s, 12, static_cast<int>(s.width()) - 12, 26, th.border);

        tip_slider.visible = show_tip;
        tip_slider.paint(s, tip_mx, tip_my);

        win.damage_all();
        win.present();
        win.wait_until_presented();
    }

done:
    win.close();
    return 0;
}
