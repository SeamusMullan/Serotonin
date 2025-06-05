#include "../stdlib/stdlib.h"
#include "../tty.h"
#include "stdio.h"
#include <stdint.h>
#include <stddef.h>

void printf(const char* fmt, ...) {
    const char* p = fmt;
    char buffer[32];

    void** arg_ptr = (void**)(&fmt);
    arg_ptr++; // skip format string itself

    while (*p) {
        if (*p == '%' && *(p + 1)) {
            p++;
            char* str = buffer;

            switch (*p) {
                case 'd':
                    itoa((int)(intptr_t)*arg_ptr++, buffer);
                    tty_writestring(str);
                    break;
                case 'u':
                    utoa((uint32_t)(uintptr_t)*arg_ptr++, buffer);
                    tty_writestring(str);
                    break;
                case 'x':
                    utoa_hex((uint32_t)(uintptr_t)*arg_ptr++, buffer);
                    tty_writestring(str);
                    break;
                case 's':
                    tty_writestring((char*)*arg_ptr++);
                    break;
                case 'c':
                    buffer[0] = (char)(intptr_t)*arg_ptr++;
                    buffer[1] = '\0';
                    tty_writestring(buffer);
                    break;
                default:
                    tty_writestring("%");
                    tty_writestring((char[]){*p, '\0'});
                    break;
            }
        } else {
            buffer[0] = *p;
            buffer[1] = '\0';
            tty_writestring(buffer);
        }
        p++;
    }
}