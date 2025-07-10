#include "../stdio/stdio.h"
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
    if (!stdin_lock)
        return;
    
    if (scancode > 127)
        return;

    if (scancode & 0x80) {
        // key release
    } 
    else if (scancode == 0x1C) 
    {
        stdin_ptr[stdin_idx] = '\0';
        stdin_idx++;
        printf("\n");
        stdin_lock = 0;
        task_unblock(stdin_pcb);
    }
    else if (scancode == 0x0E) 
    {
        stdin_ptr[stdin_idx] = '\0';
        stdin_idx--;
        vbe_terminal_back();
    }
    else 
    {
        char c = scancode_map[scancode];
        if (c) {
            stdin_ptr[stdin_idx] = c;
            stdin_idx++;
            printf("%c", c);
        }
    }
    vbe_flip();
}
