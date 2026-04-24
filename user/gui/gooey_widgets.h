/**
 * @file gooey_widgets.h
 * @brief WinForms-style UI widgets for gooey applications.
 *
 * Header-only C++11. All widgets draw to a gooey::Surface via gooey_draw.h.
 * Widgets receive events via handle_mouse() / handle_keyboard() and render
 * via paint(). Explicit pixel positioning — no layout engine.
 *
 * Widgets: Label, Button, Checkbox, RadioButton, TextBox, Panel,
 *          ProgressBar, Slider, ListBox, GroupBox.
 */

#ifndef GOOEY_WIDGETS_H
#define GOOEY_WIDGETS_H

#include "gooey.h"
#include "gooey_draw.h"

namespace gooey {
namespace widgets {

/* ── Theme ────────────────────────────────────────────────── */

struct Theme {
    uint32_t bg;
    uint32_t fg;
    uint32_t accent;
    uint32_t accent_hover;
    uint32_t accent_pressed;
    uint32_t border;
    uint32_t border_focused;
    uint32_t input_bg;
    uint32_t input_fg;
    uint32_t disabled_fg;
    uint32_t disabled_bg;
    uint32_t selection;
    uint32_t scrollbar_bg;
    uint32_t scrollbar_thumb;
};

inline Theme default_theme() {
    Theme t;
    t.bg              = rgb(30,  30,  30);
    t.fg              = rgb(220, 220, 220);
    t.accent          = rgb(60,  120, 215);
    t.accent_hover    = rgb(75,  135, 230);
    t.accent_pressed  = rgb(45,  100, 190);
    t.border          = rgb(80,  80,  80);
    t.border_focused  = rgb(60,  120, 215);
    t.input_bg        = rgb(45,  45,  45);
    t.input_fg        = rgb(220, 220, 220);
    t.disabled_fg     = rgb(100, 100, 100);
    t.disabled_bg     = rgb(50,  50,  50);
    t.selection        = rgb(60,  120, 215);
    t.scrollbar_bg    = rgb(40,  40,  40);
    t.scrollbar_thumb = rgb(100, 100, 100);
    return t;
}

inline Theme light_theme() {
    Theme t;
    t.bg              = rgb(240, 240, 240);
    t.fg              = rgb(20,  20,  20);
    t.accent          = rgb(0,   120, 215);
    t.accent_hover    = rgb(20,  140, 235);
    t.accent_pressed  = rgb(0,   100, 190);
    t.border          = rgb(170, 170, 170);
    t.border_focused  = rgb(0,   120, 215);
    t.input_bg        = rgb(255, 255, 255);
    t.input_fg        = rgb(20,  20,  20);
    t.disabled_fg     = rgb(160, 160, 160);
    t.disabled_bg     = rgb(220, 220, 220);
    t.selection        = rgb(0,   120, 215);
    t.scrollbar_bg    = rgb(230, 230, 230);
    t.scrollbar_thumb = rgb(180, 180, 180);
    return t;
}

/* ── Rect helper ──────────────────────────────────────────── */

struct Rect {
    int x, y, w, h;

    Rect() : x(0), y(0), w(0), h(0) {}
    Rect(int x_, int y_, int w_, int h_) : x(x_), y(y_), w(w_), h(h_) {}

    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }

    int right() const { return x + w; }
    int bottom() const { return y + h; }
};

/* ── Mouse state for widgets ──────────────────────────────── */

struct MouseState {
    int x, y;
    bool left_down;
    bool left_clicked;
    bool left_released;

    MouseState() : x(0), y(0), left_down(false), left_clicked(false), left_released(false) {}
};

inline MouseState make_mouse_state(const Event &ev, bool prev_left_down) {
    MouseState ms;
    ms.x = ev.mouse.lx;
    ms.y = ev.mouse.ly;
    bool cur_down = (ev.mouse.buttons & 0x01) != 0;
    ms.left_down = cur_down;
    ms.left_clicked = cur_down && !prev_left_down;
    ms.left_released = !cur_down && prev_left_down;
    return ms;
}

/* ── Label ────────────────────────────────────────────────── */

struct Label {
    Rect bounds;
    const char *text;
    uint32_t color;
    bool bold;
    bool visible;

    Label() : text(""), color(0), bold(false), visible(true) {}

    void paint(Surface &s, const Theme &th) const {
        if (!visible) return;
        uint32_t fg = color ? color : th.fg;
        draw::draw_text_transparent(s, bounds.x, bounds.y, text, fg, bold);
    }
};

/* ── Button ───────────────────────────────────────────────── */

struct Button {
    Rect bounds;
    const char *text;
    bool enabled;
    bool visible;

    bool hovered;
    bool pressed;
    bool clicked;

    Button() : text(""), enabled(true), visible(true), hovered(false), pressed(false), clicked(false) {}

    void handle_mouse(const MouseState &ms) {
        clicked = false;
        if (!enabled || !visible) { hovered = false; pressed = false; return; }
        hovered = bounds.contains(ms.x, ms.y);
        if (hovered && ms.left_clicked) pressed = true;
        if (pressed && ms.left_released) {
            if (hovered) clicked = true;
            pressed = false;
        }
        if (!ms.left_down) pressed = false;
    }

    void paint(Surface &s, const Theme &th) const {
        if (!visible) return;
        uint32_t bg, fg, border;
        if (!enabled) {
            bg = th.disabled_bg;
            fg = th.disabled_fg;
            border = th.border;
        } else if (pressed) {
            bg = th.accent_pressed;
            fg = rgb(255, 255, 255);
            border = th.accent_pressed;
        } else if (hovered) {
            bg = th.accent_hover;
            fg = rgb(255, 255, 255);
            border = th.accent_hover;
        } else {
            bg = th.accent;
            fg = rgb(255, 255, 255);
            border = th.accent;
        }
        draw::rect_rounded_filled(s, bounds.x, bounds.y, bounds.w, bounds.h, 3, bg);
        draw::rect_rounded(s, bounds.x, bounds.y, bounds.w, bounds.h, 3, border);

        int tw = draw::text_width(text);
        int tx = bounds.x + (bounds.w - tw) / 2;
        int ty = bounds.y + (bounds.h - draw::FONT_H) / 2;
        draw::draw_text_transparent(s, tx, ty, text, fg);
    }
};

/* ── FlatButton (outline style) ───────────────────────────── */

struct FlatButton {
    Rect bounds;
    const char *text;
    bool enabled;
    bool visible;
    bool hovered;
    bool pressed;
    bool clicked;

    FlatButton() : text(""), enabled(true), visible(true), hovered(false), pressed(false), clicked(false) {}

    void handle_mouse(const MouseState &ms) {
        clicked = false;
        if (!enabled || !visible) { hovered = false; pressed = false; return; }
        hovered = bounds.contains(ms.x, ms.y);
        if (hovered && ms.left_clicked) pressed = true;
        if (pressed && ms.left_released) {
            if (hovered) clicked = true;
            pressed = false;
        }
        if (!ms.left_down) pressed = false;
    }

    void paint(Surface &s, const Theme &th) const {
        if (!visible) return;
        uint32_t bg, fg, border;
        if (!enabled) {
            bg = th.disabled_bg;
            fg = th.disabled_fg;
            border = th.border;
        } else if (pressed) {
            bg = th.accent_pressed;
            fg = rgb(255, 255, 255);
            border = th.accent;
        } else if (hovered) {
            bg = th.input_bg;
            fg = th.fg;
            border = th.accent;
        } else {
            bg = th.bg;
            fg = th.fg;
            border = th.border;
        }
        draw::rect_filled(s, bounds.x, bounds.y, bounds.w, bounds.h, bg);
        draw::rect(s, bounds.x, bounds.y, bounds.w, bounds.h, border);

        int tw = draw::text_width(text);
        int tx = bounds.x + (bounds.w - tw) / 2;
        int ty = bounds.y + (bounds.h - draw::FONT_H) / 2;
        draw::draw_text_transparent(s, tx, ty, text, fg);
    }
};

/* ── Checkbox ─────────────────────────────────────────────── */

struct Checkbox {
    Rect bounds;
    const char *text;
    bool checked;
    bool enabled;
    bool visible;
    bool hovered;
    bool clicked;

    static const int BOX_SIZE = 14;
    static const int BOX_PAD = 4;

    Checkbox() : text(""), checked(false), enabled(true), visible(true), hovered(false), clicked(false) {}

    void handle_mouse(const MouseState &ms) {
        clicked = false;
        if (!enabled || !visible) { hovered = false; return; }
        hovered = bounds.contains(ms.x, ms.y);
        if (hovered && ms.left_clicked) {
            checked = !checked;
            clicked = true;
        }
    }

    void paint(Surface &s, const Theme &th) const {
        if (!visible) return;
        int bx = bounds.x;
        int by = bounds.y + (bounds.h - BOX_SIZE) / 2;

        uint32_t box_bg = enabled ? th.input_bg : th.disabled_bg;
        uint32_t box_border = hovered ? th.border_focused : th.border;
        draw::rect_filled(s, bx, by, BOX_SIZE, BOX_SIZE, box_bg);
        draw::rect(s, bx, by, BOX_SIZE, BOX_SIZE, box_border);

        if (checked) {
            uint32_t check_color = enabled ? th.accent : th.disabled_fg;
            draw::rect_filled(s, bx + 3, by + 3, BOX_SIZE - 6, BOX_SIZE - 6, check_color);
        }

        uint32_t fg = enabled ? th.fg : th.disabled_fg;
        draw::draw_text_transparent(s, bx + BOX_SIZE + BOX_PAD, bounds.y + (bounds.h - draw::FONT_H) / 2, text, fg);
    }
};

/* ── RadioButton ──────────────────────────────────────────── */

struct RadioButton {
    Rect bounds;
    const char *text;
    bool selected;
    bool enabled;
    bool visible;
    bool hovered;
    bool clicked;

    static const int RADIO_R = 7;
    static const int RADIO_PAD = 4;

    RadioButton() : text(""), selected(false), enabled(true), visible(true), hovered(false), clicked(false) {}

    void handle_mouse(const MouseState &ms) {
        clicked = false;
        if (!enabled || !visible) { hovered = false; return; }
        hovered = bounds.contains(ms.x, ms.y);
        if (hovered && ms.left_clicked) {
            selected = true;
            clicked = true;
        }
    }

    void paint(Surface &s, const Theme &th) const {
        if (!visible) return;
        int cx = bounds.x + RADIO_R;
        int cy = bounds.y + bounds.h / 2;

        uint32_t border_color = hovered ? th.border_focused : th.border;
        draw::circle(s, cx, cy, RADIO_R, border_color);
        draw::circle_filled(s, cx, cy, RADIO_R - 1, enabled ? th.input_bg : th.disabled_bg);

        if (selected) {
            uint32_t dot_color = enabled ? th.accent : th.disabled_fg;
            draw::circle_filled(s, cx, cy, RADIO_R - 4, dot_color);
        }

        uint32_t fg = enabled ? th.fg : th.disabled_fg;
        draw::draw_text_transparent(s, bounds.x + RADIO_R * 2 + RADIO_PAD, bounds.y + (bounds.h - draw::FONT_H) / 2, text, fg);
    }
};

/* ── TextBox (single-line input) ──────────────────────────── */

struct TextBox {
    Rect bounds;
    char text[256];
    int text_len;
    int cursor;
    int scroll_offset;
    bool focused;
    bool enabled;
    bool visible;
    bool hovered;
    bool changed;

    static const int PAD = 4;

    TextBox() : text_len(0), cursor(0), scroll_offset(0), focused(false),
                enabled(true), visible(true), hovered(false), changed(false) {
        text[0] = '\0';
    }

    void set_text(const char *str) {
        int i = 0;
        while (str[i] && i < 255) { text[i] = str[i]; ++i; }
        text[i] = '\0';
        text_len = i;
        cursor = i;
        scroll_offset = 0;
    }

    void handle_mouse(const MouseState &ms) {
        if (!enabled || !visible) { hovered = false; return; }
        hovered = bounds.contains(ms.x, ms.y);
        if (ms.left_clicked) {
            focused = hovered;
            if (focused) {
                int rel_x = ms.x - bounds.x - PAD + scroll_offset * draw::FONT_W;
                cursor = rel_x / draw::FONT_W;
                if (cursor < 0) cursor = 0;
                if (cursor > text_len) cursor = text_len;
            }
        }
    }

    void handle_keyboard(const keyboard_event_t &kb) {
        if (!focused || !enabled || !visible) return;
        if (kb.flags & KEY_FLAG_RELEASED) return;
        changed = false;

        if (kb.scancode == 0x0E) { // backspace
            if (cursor > 0) {
                for (int i = cursor - 1; i < text_len - 1; ++i)
                    text[i] = text[i + 1];
                text_len--;
                text[text_len] = '\0';
                cursor--;
                changed = true;
            }
        } else if (kb.scancode == 0x53) { // delete
            if (cursor < text_len) {
                for (int i = cursor; i < text_len - 1; ++i)
                    text[i] = text[i + 1];
                text_len--;
                text[text_len] = '\0';
                changed = true;
            }
        } else if (kb.scancode == 0x4B) { // left
            if (cursor > 0) cursor--;
        } else if (kb.scancode == 0x4D) { // right
            if (cursor < text_len) cursor++;
        } else if (kb.scancode == 0x47) { // home
            cursor = 0;
        } else if (kb.scancode == 0x4F) { // end
            cursor = text_len;
        } else if (kb.ascii >= 0x20 && kb.ascii < 0x7F) {
            if (text_len < 255) {
                for (int i = text_len; i > cursor; --i)
                    text[i] = text[i - 1];
                text[cursor] = static_cast<char>(kb.ascii);
                text_len++;
                text[text_len] = '\0';
                cursor++;
                changed = true;
            }
        }

        int visible_chars = (bounds.w - PAD * 2) / draw::FONT_W;
        if (cursor < scroll_offset) scroll_offset = cursor;
        if (cursor >= scroll_offset + visible_chars) scroll_offset = cursor - visible_chars + 1;
    }

    void paint(Surface &s, const Theme &th) const {
        if (!visible) return;
        uint32_t bg = enabled ? th.input_bg : th.disabled_bg;
        uint32_t fg = enabled ? th.input_fg : th.disabled_fg;
        uint32_t border = focused ? th.border_focused : th.border;

        draw::rect_filled(s, bounds.x, bounds.y, bounds.w, bounds.h, bg);
        draw::rect(s, bounds.x, bounds.y, bounds.w, bounds.h, border);

        int visible_chars = (bounds.w - PAD * 2) / draw::FONT_W;
        int tx = bounds.x + PAD;
        int ty = bounds.y + (bounds.h - draw::FONT_H) / 2;

        for (int i = 0; i < visible_chars && (scroll_offset + i) < text_len; ++i) {
            draw::draw_char_transparent(s, tx + i * draw::FONT_W, ty,
                                        text[scroll_offset + i], fg);
        }

        if (focused) {
            int cx = tx + (cursor - scroll_offset) * draw::FONT_W;
            draw::vline(s, cx, bounds.y + 2, bounds.y + bounds.h - 3, fg);
        }
    }
};

/* ── Panel (container background) ─────────────────────────── */

struct Panel {
    Rect bounds;
    uint32_t bg_color;
    bool border;
    bool visible;

    Panel() : bg_color(0), border(true), visible(true) {}

    void paint(Surface &s, const Theme &th) const {
        if (!visible) return;
        uint32_t bg = bg_color ? bg_color : th.bg;
        draw::rect_filled(s, bounds.x, bounds.y, bounds.w, bounds.h, bg);
        if (border)
            draw::rect(s, bounds.x, bounds.y, bounds.w, bounds.h, th.border);
    }
};

/* ── GroupBox (labeled container) ──────────────────────────── */

struct GroupBox {
    Rect bounds;
    const char *text;
    bool visible;

    GroupBox() : text(""), visible(true) {}

    void paint(Surface &s, const Theme &th) const {
        if (!visible) return;
        int tw = draw::text_width(text);
        int label_x = bounds.x + 8;
        int label_y = bounds.y;

        draw::hline(s, bounds.x, label_x - 2, bounds.y + draw::FONT_H / 2, th.border);
        draw::hline(s, label_x + tw + 2, bounds.x + bounds.w - 1, bounds.y + draw::FONT_H / 2, th.border);
        draw::vline(s, bounds.x, bounds.y + draw::FONT_H / 2, bounds.y + bounds.h - 1, th.border);
        draw::vline(s, bounds.x + bounds.w - 1, bounds.y + draw::FONT_H / 2, bounds.y + bounds.h - 1, th.border);
        draw::hline(s, bounds.x, bounds.x + bounds.w - 1, bounds.y + bounds.h - 1, th.border);

        draw::draw_text_transparent(s, label_x, label_y, text, th.fg);
    }
};

/* ── ProgressBar ──────────────────────────────────────────── */

struct ProgressBar {
    Rect bounds;
    int value;
    int max_value;
    bool visible;

    ProgressBar() : value(0), max_value(100), visible(true) {}

    void paint(Surface &s, const Theme &th) const {
        if (!visible) return;
        draw::rect_filled(s, bounds.x, bounds.y, bounds.w, bounds.h, th.input_bg);
        draw::rect(s, bounds.x, bounds.y, bounds.w, bounds.h, th.border);

        if (max_value > 0 && value > 0) {
            int fill_w = (bounds.w - 2) * (value > max_value ? max_value : value) / max_value;
            if (fill_w > 0)
                draw::rect_filled(s, bounds.x + 1, bounds.y + 1, fill_w, bounds.h - 2, th.accent);
        }
    }
};

/* ── Slider ───────────────────────────────────────────────── */

struct Slider {
    Rect bounds;
    int value;
    int min_value;
    int max_value;
    bool enabled;
    bool visible;
    bool hovered;
    bool dragging;
    bool changed;

    static const int THUMB_W = 12;
    static const int TRACK_H = 4;

    Slider() : value(0), min_value(0), max_value(100), enabled(true), visible(true),
               hovered(false), dragging(false), changed(false) {}

    void handle_mouse(const MouseState &ms) {
        if (!enabled || !visible) { hovered = false; dragging = false; return; }
        changed = false;
        hovered = bounds.contains(ms.x, ms.y);

        if (hovered && ms.left_clicked) dragging = true;
        if (!ms.left_down) dragging = false;

        if (dragging) {
            int track_x = bounds.x + THUMB_W / 2;
            int track_w = bounds.w - THUMB_W;
            if (track_w <= 0) return;
            int rel = ms.x - track_x;
            if (rel < 0) rel = 0;
            if (rel > track_w) rel = track_w;
            int new_val = min_value + rel * (max_value - min_value) / track_w;
            if (new_val != value) {
                value = new_val;
                changed = true;
            }
        }
    }

    void paint(Surface &s, const Theme &th) const {
        if (!visible) return;
        int track_y = bounds.y + bounds.h / 2 - TRACK_H / 2;
        draw::rect_filled(s, bounds.x, track_y, bounds.w, TRACK_H, th.input_bg);
        draw::rect(s, bounds.x, track_y, bounds.w, TRACK_H, th.border);

        int range = max_value - min_value;
        if (range <= 0) return;
        int track_w = bounds.w - THUMB_W;
        int thumb_x = bounds.x + (value - min_value) * track_w / range;
        int thumb_y = bounds.y + (bounds.h - bounds.h) / 2;

        uint32_t thumb_color;
        if (!enabled)
            thumb_color = th.disabled_fg;
        else if (dragging)
            thumb_color = th.accent_pressed;
        else if (hovered)
            thumb_color = th.accent_hover;
        else
            thumb_color = th.accent;

        draw::rect_rounded_filled(s, thumb_x, thumb_y, THUMB_W, bounds.h, 3, thumb_color);
    }
};

/* ── ListBox ──────────────────────────────────────────────── */

struct ListBox {
    Rect bounds;
    const char * const *items;
    int item_count;
    int selected;
    int scroll_offset;
    bool enabled;
    bool visible;
    bool hovered;
    bool selection_changed;

    static const int ITEM_H = 20;
    static const int PAD = 2;
    static const int SCROLLBAR_W = 10;

    ListBox() : items(nullptr), item_count(0), selected(-1), scroll_offset(0),
                enabled(true), visible(true), hovered(false), selection_changed(false) {}

    int visible_items() const {
        return bounds.h / ITEM_H;
    }

    void handle_mouse(const MouseState &ms) {
        selection_changed = false;
        if (!enabled || !visible || !items) { hovered = false; return; }
        hovered = bounds.contains(ms.x, ms.y);

        if (hovered && ms.left_clicked) {
            int rel_y = ms.y - bounds.y;
            int idx = scroll_offset + rel_y / ITEM_H;
            if (idx >= 0 && idx < item_count && idx != selected) {
                selected = idx;
                selection_changed = true;
            }
        }
    }

    void handle_keyboard(const keyboard_event_t &kb) {
        if (!enabled || !visible || !items) return;
        if (kb.flags & KEY_FLAG_RELEASED) return;
        selection_changed = false;

        if (kb.scancode == 0x48) { // up
            if (selected > 0) {
                selected--;
                selection_changed = true;
            }
        } else if (kb.scancode == 0x50) { // down
            if (selected < item_count - 1) {
                selected++;
                selection_changed = true;
            }
        }

        int vis = visible_items();
        if (selected < scroll_offset) scroll_offset = selected;
        if (selected >= scroll_offset + vis) scroll_offset = selected - vis + 1;
    }

    void paint(Surface &s, const Theme &th) const {
        if (!visible) return;
        draw::rect_filled(s, bounds.x, bounds.y, bounds.w, bounds.h, th.input_bg);
        draw::rect(s, bounds.x, bounds.y, bounds.w, bounds.h, th.border);

        int vis = visible_items();
        int content_w = bounds.w - (item_count > vis ? SCROLLBAR_W : 0);

        for (int i = 0; i < vis && (scroll_offset + i) < item_count; ++i) {
            int idx = scroll_offset + i;
            int iy = bounds.y + i * ITEM_H;

            if (idx == selected)
                draw::rect_filled(s, bounds.x + 1, iy, content_w - 2, ITEM_H, th.selection);

            uint32_t fg = (idx == selected) ? rgb(255, 255, 255) : (enabled ? th.fg : th.disabled_fg);
            if (items[idx])
                draw::draw_text_transparent(s, bounds.x + PAD + 2, iy + (ITEM_H - draw::FONT_H) / 2,
                                            items[idx], fg);
        }

        if (item_count > vis) {
            int sb_x = bounds.x + bounds.w - SCROLLBAR_W;
            draw::rect_filled(s, sb_x, bounds.y, SCROLLBAR_W, bounds.h, th.scrollbar_bg);

            int thumb_h = (vis * bounds.h) / item_count;
            if (thumb_h < 10) thumb_h = 10;
            int thumb_y = bounds.y + (scroll_offset * (bounds.h - thumb_h)) / (item_count - vis);
            draw::rect_filled(s, sb_x + 1, thumb_y, SCROLLBAR_W - 2, thumb_h, th.scrollbar_thumb);
        }
    }
};

/* ── Graph (rolling line / area plot) ─────────────────────── */

/**
 * Fixed-capacity rolling graph. Call push(v) (or push2(v1, v2) for a two-series
 * graph) each sample; the graph stores the most recent @c CAPACITY samples in
 * internal ring buffers and paints them left-to-right against a
 * [min_value..max_value] range.
 *
 * Values can have whatever units the caller chooses (ticks, %, MB, bytes/sec…)
 * so long as they lie in the configured range. Out-of-range samples are
 * clamped. Set @c line_color / @c second_line_color to 0 to fall back to
 * @c Theme::accent . The second series is only painted once @c push2 has been
 * called at least once (or @c has_second is set manually).
 */
struct Graph {
    Rect bounds;
    bool visible;
    bool filled;                /**< Area-fill under the line (first series). */
    bool second_filled;         /**< Area-fill under the second-series line. */
    bool show_grid;             /**< Light horizontal grid lines at 25/50/75%. */
    int min_value;
    int max_value;
    uint32_t line_color;        /**< First series line color. 0 = use Theme::accent. */
    uint32_t second_line_color; /**< Second series line color. 0 = theme-derived. */
    const char *label;          /**< Optional top-left caption (may be null). */
    const char *second_label;   /**< Optional caption for 2nd series, shown below label. */
    bool has_second;            /**< True if the graph currently carries a 2nd series. */

    static const int CAPACITY = 128;

    Graph()
        : visible(true), filled(true), second_filled(false), show_grid(true),
          min_value(0), max_value(100),
          line_color(0), second_line_color(0),
          label(nullptr), second_label(nullptr), has_second(false),
          head_(0), count_(0) {
        for (int i = 0; i < CAPACITY; ++i) { data_[i] = 0; data2_[i] = 0; }
    }

    void clear() {
        head_ = 0;
        count_ = 0;
        for (int i = 0; i < CAPACITY; ++i) { data_[i] = 0; data2_[i] = 0; }
    }

    void push(int value) {
        if (value < min_value) value = min_value;
        if (value > max_value) value = max_value;
        data_[head_] = value;
        data2_[head_] = 0;
        head_ = (head_ + 1) % CAPACITY;
        if (count_ < CAPACITY) ++count_;
    }

    void push2(int v1, int v2) {
        if (v1 < min_value) v1 = min_value;
        if (v1 > max_value) v1 = max_value;
        if (v2 < min_value) v2 = min_value;
        if (v2 > max_value) v2 = max_value;
        data_[head_]  = v1;
        data2_[head_] = v2;
        has_second = true;
        head_ = (head_ + 1) % CAPACITY;
        if (count_ < CAPACITY) ++count_;
    }

    int sample_count() const { return count_; }

    int sample_at(int i) const {
        if (i < 0 || i >= count_) return min_value;
        int start = (head_ - count_ + CAPACITY) % CAPACITY;
        return data_[(start + i) % CAPACITY];
    }

    int sample2_at(int i) const {
        if (i < 0 || i >= count_) return min_value;
        int start = (head_ - count_ + CAPACITY) % CAPACITY;
        return data2_[(start + i) % CAPACITY];
    }

    void paint(Surface &s, const Theme &th) const {
        if (!visible) return;
        draw::rect_filled(s, bounds.x, bounds.y, bounds.w, bounds.h, th.input_bg);
        draw::rect(s, bounds.x, bounds.y, bounds.w, bounds.h, th.border);

        if (bounds.w <= 2 || bounds.h <= 2)
            return;

        int inner_x = bounds.x + 1;
        int inner_y = bounds.y + 1;
        int inner_w = bounds.w - 2;
        int inner_h = bounds.h - 2;

        if (show_grid) {
            for (int f = 1; f < 4; ++f) {
                int gy = inner_y + (inner_h * f) / 4;
                draw::hline(s, inner_x, inner_x + inner_w - 1, gy, th.scrollbar_bg);
            }
        }

        int range = max_value - min_value;
        if (range <= 0) range = 1;

        int draw_count = count_ < inner_w ? count_ : inner_w;
        if (draw_count < 1) {
            if (label)
                draw::draw_text_transparent(s, bounds.x + 4, bounds.y + 2, label, th.fg);
            return;
        }

        int step_num = inner_w;
        int step_den = draw_count > 1 ? draw_count - 1 : 1;
        int baseline = inner_y + inner_h - 1;

        uint32_t color1 = line_color ? line_color : th.accent;
        uint32_t color2 = second_line_color ? second_line_color : th.accent_hover;

        /* Draw second series first so the primary series renders on top. */
        if (has_second) {
            int prev_x = inner_x;
            int prev_y = 0;
            bool have_prev = false;
            for (int i = 0; i < draw_count; ++i) {
                int sample_idx = count_ - draw_count + i;
                int v = sample2_at(sample_idx);
                int rel = v - min_value;
                int y = inner_y + inner_h - 1 - (rel * (inner_h - 1)) / range;
                int x = inner_x + (i * step_num) / step_den;

                if (second_filled)
                    draw::vline(s, x, y, baseline, color2);

                if (have_prev)
                    draw::line(s, prev_x, prev_y, x, y, color2);
                else
                    s.put_pixel(x, y, color2);

                prev_x = x;
                prev_y = y;
                have_prev = true;
            }
        }

        {
            int prev_x = inner_x;
            int prev_y = 0;
            bool have_prev = false;
            for (int i = 0; i < draw_count; ++i) {
                int sample_idx = count_ - draw_count + i;
                int v = sample_at(sample_idx);
                int rel = v - min_value;
                int y = inner_y + inner_h - 1 - (rel * (inner_h - 1)) / range;
                int x = inner_x + (i * step_num) / step_den;

                if (filled)
                    draw::vline(s, x, y, baseline, color1);

                if (have_prev)
                    draw::line(s, prev_x, prev_y, x, y, color1);
                else
                    s.put_pixel(x, y, color1);

                prev_x = x;
                prev_y = y;
                have_prev = true;
            }
        }

        if (label)
            draw::draw_text_transparent(s, bounds.x + 4, bounds.y + 2, label, th.fg);
        if (second_label && has_second)
            draw::draw_text_transparent(s, bounds.x + 4, bounds.y + 2 + draw::FONT_H, second_label, color2);
    }

private:
    int data_[CAPACITY];
    int data2_[CAPACITY];
    int head_;
    int count_;
};

/* ── Tooltip (renders on demand at mouse position) ────────── */

struct Tooltip {
    const char *text;
    bool visible;

    static const int PAD = 4;

    Tooltip() : text(""), visible(false) {}

    void paint(Surface &s, int mx, int my) const {
        if (!visible || !text || !text[0]) return;
        int tw = draw::text_width(text);
        int w = tw + PAD * 2;
        int h = draw::FONT_H + PAD * 2;
        int x = mx + 12;
        int y = my + 16;

        if (x + w > static_cast<int>(s.width())) x = mx - w - 4;
        if (y + h > static_cast<int>(s.height())) y = my - h - 4;

        draw::rect_filled(s, x, y, w, h, rgb(50, 50, 50));
        draw::rect(s, x, y, w, h, rgb(120, 120, 120));
        draw::draw_text_transparent(s, x + PAD, y + PAD, text, rgb(220, 220, 220));
    }
};

} // namespace widgets
} // namespace gooey

#endif /* GOOEY_WIDGETS_H */
