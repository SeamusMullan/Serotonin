#include "../stdio/stdio.h"
#include "../tty.h"
#include <stdint.h>

static const char scancode_map[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`',   0,'\\',
    'z','x','c','v','b','n','m',',','.','/',   0, '*',  0, ' ',
};

void handle_scancode(uint8_t scancode) {
    if (scancode > 127)
        return;

    if (scancode & 0x80) {
        // key release
    } 
    else if (scancode == 0x1C) 
    {
        printf("\n");
    }
    else if (scancode == 0x0E) 
    {
        tty_back();
    }
    else 
    {
        char c = scancode_map[scancode];
        if (c)
            printf("%c", c);
    }
}
