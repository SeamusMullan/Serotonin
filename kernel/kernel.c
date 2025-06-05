#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tty.h"
#include "string.h"

void kernel_sleep(unsigned int mili)
{
    volatile unsigned int count = mili * 10000;
    while (count--) {
        asm volatile("nop"); 
    }
}

void kernel_main(void) 
{
	/* Initialize terminal interface */
	tty_initialize();

	/* Newline support is left as an exercise. */
	tty_writestring("Hello, kernel World!\n");

    for (int i = 0; i < 16; i++) {
        tty_setcolor(i);
        tty_writestring("a\n");
        kernel_sleep(1000);
    }

    for (int i = 0; i < 16; i++) {
        tty_setcolor(i);
        tty_writestring("b\n");
        kernel_sleep(1000);
    }
}