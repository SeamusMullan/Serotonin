#include "../stdio/stdio.h"
#include "../stdlib/stdlib.h"
#include "../video/vbe/vbe.h"
#include "../syscall/syscall.h"
#include "../syscall/sys/errno.h"
#include "../schedule/schedule.h"
#include "../vmm/vmm.h"
#include "../device/keyboard/dev_keyboard.h"
#include "../pty/pty.h"
#include "io.h"
#include "serial.h"
#include <stdint.h>

static const char scancode_map[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',   0,'\\',
    'z','x','c','v','b','n','m',',','.','/',   0, '*',  0, ' ',
};

static const char scancode_map_shift[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~',  0, '|',
    'Z','X','C','V','B','N','M','<','>','?',   0, '*',  0, ' ',
};


char stdio_buffer[STDIO_INPUT_BUFFER];
static uint8_t shift_pressed = 0;
static uint8_t ctrl_pressed = 0;
static uint8_t alt_pressed = 0;
volatile int keyboard_grab_active = 0;

/**
 * @brief Handle keyboard scancodes.
 *
 * Routes input through PTY line discipline for the active VTY.
 * Handles Alt+F1-F4 for VTY switching.
 */
void handle_scancode(uint8_t scancode) {
    lock_scheduler();

    if (scancode > 255) {
        unlock_scheduler();
        return;
    }

    keyboard_event_t ev = {0};

    if (scancode & 0x80) {
        // key release
        uint8_t released = scancode & 0x7F;
        if (released == 0x2A || released == 0x36) {
            shift_pressed = 0;
        } else if (released == 0x1D) {
            ctrl_pressed = 0;
        } else if (released == 0x38) {
            alt_pressed = 0;
        }
        ev.scancode = released;
        ev.ascii = 0;
        ev.flags = KEY_FLAG_RELEASED;
        if (shift_pressed) ev.flags |= KEY_FLAG_SHIFT;
        if (ctrl_pressed) ev.flags |= KEY_FLAG_CTRL;
        dev_keyboard_push_event(&ev);
    }
    else if (scancode == 0x38) {
        // Alt press
        alt_pressed = 1;
        ev.scancode = scancode;
        ev.ascii = 0;
        ev.flags = 0;
        dev_keyboard_push_event(&ev);
    }
    else if (scancode == 0x2A || scancode == 0x36) {
        // Shift press
        shift_pressed = 1;
        ev.scancode = scancode;
        ev.ascii = 0;
        ev.flags = KEY_FLAG_SHIFT;
        dev_keyboard_push_event(&ev);
    }
    else if (scancode == 0x1D) {
        // Ctrl press
        ctrl_pressed = 1;
        ev.scancode = scancode;
        ev.ascii = 0;
        ev.flags = KEY_FLAG_CTRL;
        dev_keyboard_push_event(&ev);
    }
    else {
        // Regular key press
        char c;
        if (scancode == 0x1C) {
            c = '\n';
        } else if (scancode == 0x0E) {
            c = '\b';
        } else {
            c = shift_pressed ? scancode_map_shift[scancode] : scancode_map[scancode];
        }

        ev.scancode = scancode;
        ev.ascii = (uint8_t)c;
        if (shift_pressed) ev.flags |= KEY_FLAG_SHIFT;
        if (ctrl_pressed) ev.flags |= KEY_FLAG_CTRL;

        // Route through PTY line discipline (unless WM has grabbed input)
        if (!keyboard_grab_active) {
            char ldisc_c = c;
            if (ctrl_pressed && ldisc_c >= 'a' && ldisc_c <= 'z') {
                ldisc_c = (char)(ldisc_c - 'a' + 1);
            }
            if (ldisc_c) {
                pty_ldisc_input(&pty_table[active_vty], ldisc_c);
            }
            // Alt+F1-F4: VTY switching
            if (alt_pressed && scancode >= 0x3B && scancode <= 0x3E) {
                uint32_t vty_id = (uint32_t)(scancode - 0x3B);
                pty_switch_vty(vty_id);
            }
        }

        // Always push to /dev/keyboard for raw consumers
        dev_keyboard_push_event(&ev);
    }

    unlock_scheduler();
}

inline void ps2_send_mouse_command(uint8_t cmd) {
    io_wait();
    outb(PS2_STATUS_PORT, PS2_SEND_BYTE);
    io_wait();
    outb(PS2_DATA_PORT, cmd);
}

inline uint8_t ps2_read_mouse_response() {
    io_wait();
    return inb(PS2_DATA_PORT);
}

/**
 * @brief Set the sample rate for the PS/2 mouse.
 *
 * @param rate The desired sample rate (in Hz).
 */
void ps2_mouse_set_sample_rate(uint8_t rate) {
    outb(PS2_STATUS_PORT, PS2_MOUSE_BYTE);
    outb(PS2_DATA_PORT, PS2_MOUSE_SET_SAMPLE_RATE);
    io_wait();
    //uint8_t ack = inb(PS2_DATA_PORT); // Should be 0xFA, check this in tests.
    outb(PS2_STATUS_PORT, PS2_MOUSE_BYTE);
    outb(PS2_DATA_PORT, rate);        // sample rate
    io_wait();
    //ack = inb(PS2_DATA_PORT);
}

void ps2_mouse_init(void) {
    clear_interrupts();
    while (inb(PS2_STATUS_PORT) & 1) inb(PS2_DATA_PORT);

    outb(PS2_STATUS_PORT, PS2_GET_COMPAQ_STATUS);
    io_wait();

    uint8_t status_byte = inb(PS2_DATA_PORT);

    status_byte |= (1 << 1); // enable irq12
    status_byte &= ~(1 << 5); // enable mouse clock

    io_wait();
    outb(PS2_STATUS_PORT, PS2_SET_COMPAQ_STATUS);
    io_wait();
    outb(PS2_DATA_PORT, status_byte);

    io_wait();
    outb(PS2_STATUS_PORT, PS2_ENABLE_AUX_DEVICE);

    ps2_send_mouse_command(PS2_MOUSE_RESET);
    uint8_t response = ps2_read_mouse_response();
    if (response == PS2_MOUSE_ACK) {
        uint8_t selftest = ps2_read_mouse_response();
        if (selftest != PS2_MOUSE_SELFTEST_GOOD) return;
        //uint8_t mouseid = ps2_read_mouse_response();
        ps2_send_mouse_command(PS2_MOUSE_ENABLE_PACKET_STREAMING);
        uint8_t ack = ps2_read_mouse_response();
        if (ack != PS2_MOUSE_ACK) return;
        ps2_mouse_set_sample_rate(200);
        enable_interrupts();
    } else {
        printfs(PRINT_STATUS_ERROR, "Something went wrong while trying to init ps/2 mouse: %p\n",response);
        enable_interrupts();
        return;
    }
}
