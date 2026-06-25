#include <kernel/stdio/stdio.h>
#include <kernel/stdlib/stdlib.h>
#include <kernel/video/vbe/vbe.h>
#include <kernel/syscall/syscall.h>
#include <kernel/syscall/sys/errno.h>
#include <kernel/schedule/schedule.h>
#include <kernel/vmm/vmm.h>
#include <kernel/device/keyboard/dev_keyboard.h>
#include <kernel/pty/pty.h>
#include <kernel/io/io.h>
#include <kernel/io/serial.h>
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
            // Arrow keys → ANSI escape sequences
            if (scancode == 0x48 || scancode == 0x50 ||
                scancode == 0x4B || scancode == 0x4D) {
                char arrow_seq[3] = { 27, '[', 0 };
                switch (scancode) {
                    case 0x48: arrow_seq[2] = 'A'; break; // up
                    case 0x50: arrow_seq[2] = 'B'; break; // down
                    case 0x4B: arrow_seq[2] = 'D'; break; // left
                    case 0x4D: arrow_seq[2] = 'C'; break; // right
                }
                pty_ldisc_input(&pty_table[active_vty], arrow_seq[0]);
                pty_ldisc_input(&pty_table[active_vty], arrow_seq[1]);
                pty_ldisc_input(&pty_table[active_vty], arrow_seq[2]);
            } else {
                char ldisc_c = c;
                if (ctrl_pressed && ldisc_c >= 'a' && ldisc_c <= 'z') {
                    ldisc_c = (char)(ldisc_c - 'a' + 1);
                }
                if (ldisc_c) {
                    pty_ldisc_input(&pty_table[active_vty], ldisc_c);
                }
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

/**
 * @brief Block until the PS/2 controller's input buffer is empty (bit 1 clear).
 *
 * Writing to 0x60/0x64 while IBF is full silently drops the byte on real
 * hardware, so both the 0xD4 prefix and the command byte need this gate.
 */
static void ps2_wait_input_empty(void) {
    for (uint32_t i = 0; i < 1000000; i++) {
        if (!(inb(PS2_STATUS_PORT) & 0x02)) return;
    }
}

/**
 * @brief Drain any stale bytes from the controller output buffer.
 *
 * Called before issuing a mouse command so that a later OBF-based read
 * can safely treat the first byte it sees as the mouse's reply rather
 * than a leftover keyboard scancode or reset byte.
 */
static void ps2_mouse_flush(void) {
    for (int i = 0; i < 16; i++) {
        if (!(inb(PS2_STATUS_PORT) & 0x01)) return;
        (void)inb(PS2_DATA_PORT);
    }
}

void ps2_send_mouse_command(uint8_t cmd) {
    ps2_mouse_flush();
    ps2_wait_input_empty();
    outb(PS2_STATUS_PORT, PS2_SEND_BYTE);
    ps2_wait_input_empty();
    outb(PS2_DATA_PORT, cmd);
}

/**
 * @brief Wait for a byte in the PS/2 controller output buffer.
 *
 * Only bit 0 (OBF) is required: some controllers do not reliably set the
 * AUX bit (5) for mouse replies. Because ps2_send_mouse_command() flushes
 * stale bytes before issuing a command, the next byte that appears is the
 * mouse's response. The spin budget accommodates the ~500 ms BAT self-test
 * after PS2_MOUSE_RESET.
 *
 * @param timeout_iters Max spin iterations before giving up.
 * @return 1 if data is ready, 0 on timeout.
 */
static int ps2_mouse_wait_aux(uint32_t timeout_iters) {
    for (uint32_t i = 0; i < timeout_iters; i++) {
        if (inb(PS2_STATUS_PORT) & 0x01) return 1;
    }
    return 0;
}

uint8_t ps2_read_mouse_response(void) {
    if (!ps2_mouse_wait_aux(5000000)) return 0xFE; /* RESEND == "no data" */
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

/**
 * @brief Send one byte of the Synaptics "magic knock" encoding.
 *
 * Encodes an 8-bit argument into four 0xE8 (set resolution) commands,
 * each carrying 2 bits. The touchpad decodes the knock into either a
 * mode-byte write or a query selector (used with 0xE9).
 */
static int synaptics_knock(uint8_t arg) {
    // cppcheck-suppress constVariable
    uint8_t parts[4] = { (arg >> 6) & 0x3, (arg >> 4) & 0x3,
                         (arg >> 2) & 0x3,  arg       & 0x3 };
    for (int i = 0; i < 4; i++) {
        ps2_send_mouse_command(PS2_MOUSE_SET_RESOLUTION);
        if (ps2_read_mouse_response() != PS2_MOUSE_ACK) return -1;
        ps2_send_mouse_command(parts[i]);
        if (ps2_read_mouse_response() != PS2_MOUSE_ACK) return -1;
    }
    return 0;
}

/**
 * @brief Issue a Synaptics query (knock + 0xE9) and read the 3-byte reply.
 */
static int synaptics_query(uint8_t selector, uint8_t out[3]) {
    if (synaptics_knock(selector) < 0) return -1;
    ps2_send_mouse_command(PS2_MOUSE_STATUS_RQ);
    if (ps2_read_mouse_response() != PS2_MOUSE_ACK) return -1;
    for (int i = 0; i < 3; i++) {
        if (!ps2_mouse_wait_aux(1000000)) return -1;
        out[i] = inb(PS2_DATA_PORT);
    }
    return 0;
}

/**
 * @brief Probe for a Synaptics touchpad via the identify query.
 *
 * Synaptics pads respond to selector 0x00 with byte[1] == 0x47. A generic
 * PS/2 mouse will either NAK the knock sequence or return a status byte
 * whose middle field is not 0x47. The ID bytes are returned via @p id when
 * a pad is found.
 *
 * @return 1 if a Synaptics touchpad was detected, 0 otherwise.
 */
static int synaptics_detect(uint8_t id[3]) {
    if (synaptics_query(0x00, id) < 0) return 0;
    return id[1] == 0x47;
}

/**
 * @brief Apply Synaptics-specific configuration (mode byte write).
 *
 * Writes mode byte 0x00 via the magic knock + 0xF3/0x14 sequence, which
 * keeps the pad in PS/2-relative mode (compatible with the 3-byte IRQ
 * parser) while re-applying firmware defaults such as tap-to-click.
 */
static void synaptics_configure(void) {
    if (synaptics_knock(0x00) < 0) return;
    ps2_send_mouse_command(PS2_MOUSE_SET_SAMPLE_RATE);
    ps2_read_mouse_response();
    ps2_send_mouse_command(0x14);
    ps2_read_mouse_response();
}

void ps2_mouse_init(void) {
    clear_interrupts();

    /* HP/ENE EC on the G71 is strict about ordering: disable both PS/2
     * channels, drain the output buffer, reconfigure the controller, then
     * re-enable the aux channel. Every controller write gates on IBF-empty
     * because this EC silently drops bytes submitted while IBF is full. */
    ps2_wait_input_empty();
    outb(PS2_STATUS_PORT, 0xAD); /* disable keyboard channel */
    ps2_wait_input_empty();
    outb(PS2_STATUS_PORT, 0xA7); /* disable aux channel */
    ps2_mouse_flush();

    ps2_wait_input_empty();
    outb(PS2_STATUS_PORT, PS2_GET_COMPAQ_STATUS);
    if (!ps2_mouse_wait_aux(1000000)) {
        printfs(PRINT_STATUS_ERROR, "PS/2: controller did not return status byte\n");
        enable_interrupts();
        return;
    }
    uint8_t status_byte = inb(PS2_DATA_PORT);

    status_byte |= (1 << 1);  /* enable IRQ12 */
    status_byte &= ~(1 << 5); /* enable mouse clock */

    ps2_wait_input_empty();
    outb(PS2_STATUS_PORT, PS2_SET_COMPAQ_STATUS);
    ps2_wait_input_empty();
    outb(PS2_DATA_PORT, status_byte);

    ps2_wait_input_empty();
    outb(PS2_STATUS_PORT, PS2_ENABLE_AUX_DEVICE);
    ps2_wait_input_empty();
    outb(PS2_STATUS_PORT, 0xAE); /* re-enable keyboard channel */

    /* G71/Synaptics quirk: a reset issued while the pad is still streaming
     * can be swallowed by the EC. Disable data reporting first, flush any
     * in-flight packet bytes, then issue the reset. */
    ps2_send_mouse_command(PS2_MOUSE_DISABLE_PACKET_STREAMING);
    ps2_read_mouse_response();
    ps2_mouse_flush();

    ps2_send_mouse_command(PS2_MOUSE_RESET);
    uint8_t response = ps2_read_mouse_response();
    if (response == PS2_MOUSE_ACK) {
        uint8_t selftest = ps2_read_mouse_response();
        if (selftest != PS2_MOUSE_SELFTEST_GOOD) return;
        //uint8_t mouseid = ps2_read_mouse_response();

        /* Probe for a Synaptics touchpad. If present, apply its mode-byte
         * config; otherwise reset the mouse to firmware defaults (the
         * identify probe leaves resolution state touched on generic mice)
         * and fall through to the standard PS/2 mouse path. */
        uint8_t syn_id[3];
        if (synaptics_detect(syn_id)) {
            uint8_t caps[3] = {0};
            synaptics_query(0x02, caps);
            synaptics_configure();
            printfs(PRINT_STATUS_INFO,
                    "Synaptics touchpad: v%d.%d caps=%02x%02x%02x\n",
                    syn_id[2] >> 4, syn_id[2] & 0x0F, caps[0], caps[1], caps[2]);
        } else {
            ps2_send_mouse_command(PS2_MOUSE_SET_DEFAULTS);
            ps2_read_mouse_response();
            printfs(PRINT_STATUS_INFO, "No touchpad detected, using generic PS/2 mouse\n");
        }

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
