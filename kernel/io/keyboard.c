#include "../stdio/stdio.h"
#include "../stdlib/stdlib.h"
#include "../video/vbe/vbe.h"
#include "../syscall/syscall.h"
#include "../schedule/schedule.h"
#include "../io/io.h"
#include <stdint.h>

static const char scancode_map[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',   0,'\\',
    'z','x','c','v','b','n','m',',','.','/',   0, '*',  0, ' ',
};

char stdio_buffer[STDIO_INPUT_BUFFER];

/**
 * @brief Handle keyboard scancodes.
 *
 * This function processes the scancode received from the keyboard.
 * It translates the scancode into a character and handles special keys
 * like Enter and Backspace.
 *
 * @param scancode The scancode received from the keyboard.
 */
void handle_scancode(uint8_t scancode) {
    lock_scheduler();
    preempt_disable();
    static uint32_t stdin_idx = 0;

    if (!stdin_lock->held)
        return;

    stdio_lck_t *task_stdio = (stdio_lck_t*)stdin_lock->owner->lck_ptr;
    uint32_t stdio_buf_size = task_stdio->stdin_buf_size;
    void* stdin_ptr = task_stdio->stdin_ptr;

    if (stdin_idx >= STDIO_INPUT_BUFFER || stdin_idx >= stdio_buf_size)
        return;

    if (scancode > 127)
        return;

    if (scancode & 0x80) {
        // key release
    }
    else if (scancode == 0x1C)
    {

        uint32_t old_cr3 = read_cr3();
        write_cr3(stdin_lock->owner->address_space->phys_pdir);

        stdio_buffer[stdin_idx] = '\0';
        memcpy(stdin_ptr,stdio_buffer,stdin_idx+1);
        stdin_idx = 0;

        write_cr3(old_cr3);

        task_lock_release(stdin_lock);
    }
    else if (scancode == 0x0E)
    {
        stdio_buffer[stdin_idx] = '\0';
        stdin_idx--;
        vbe_terminal_back();
    }
    else
    {
        char c = scancode_map[scancode];
        if (c) {
            stdio_buffer[stdin_idx] = (unsigned char)c;
            stdin_idx++;
            printf("%c", c);
        }
    }
    vbe_flip();
    preempt_enable();
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
    uint8_t ack = inb(PS2_DATA_PORT); // Should be 0xFA
    outb(PS2_STATUS_PORT, PS2_MOUSE_BYTE);
    outb(PS2_DATA_PORT, rate);        // sample rate
    io_wait();
    ack = inb(PS2_DATA_PORT);
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
        uint8_t mouseid = ps2_read_mouse_response();
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
