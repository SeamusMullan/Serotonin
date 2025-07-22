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
    static uint32_t stdin_idx = 0;

    if (stdin_idx < 0)
        stdin_idx = 0;

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
        stdio_buffer[stdin_idx] = '\0';
        memcpy(stdin_ptr,stdio_buffer,stdin_idx+1);
        stdin_idx = 0;
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
}
