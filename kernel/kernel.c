#define KERNEL_VERSION_HIGH 0
#define KERNEL_VERSION_MID 0
#define KERNEL_VERSION_LOW 1 

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tty.h"
#include "string.h"
#include "stdio/stdio.h"
#include "stdlib/stdlib.h"

void kernel_sleep(unsigned int mili)
{
    volatile unsigned int count = mili * 10000;
    while (count--) {
        asm volatile("nop"); 
    }
}

void kernel_panic(char* str) {
    uintptr_t eip = (uintptr_t)__builtin_return_address(0);

    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eflags;
    uint16_t cs, ds, ss;

    asm volatile ("mov %%eax, %0" : "=r"(eax));
    asm volatile ("mov %%ebx, %0" : "=r"(ebx));
    asm volatile ("mov %%ecx, %0" : "=r"(ecx));
    asm volatile ("mov %%edx, %0" : "=r"(edx));
    asm volatile ("mov %%esi, %0" : "=r"(esi));
    asm volatile ("mov %%edi, %0" : "=r"(edi));
    asm volatile ("mov %%ebp, %0" : "=r"(ebp));
    asm volatile ("mov %%esp, %0" : "=r"(esp));
    asm volatile ("pushf\n\tpop %0" : "=r"(eflags));
    asm volatile ("mov %%cs, %0" : "=r"(cs));
    asm volatile ("mov %%ds, %0" : "=r"(ds));
    asm volatile ("mov %%ss, %0" : "=r"(ss));

    printf("[!!!] Kernel panic. Please reboot your computer.\n");
    printf("Reason: %s\n", str);
    printf("Kernel version: %d.%d.%d\n", KERNEL_VERSION_HIGH, KERNEL_VERSION_MID, KERNEL_VERSION_LOW);
    printf("EIP: 0x%x\n", (uint32_t)eip);
    printf("EAX: 0x%x  EBX: 0x%x  ECX: 0x%x  EDX: 0x%x\n", eax, ebx, ecx, edx);
    printf("ESI: 0x%x  EDI: 0x%x  EBP: 0x%x  ESP: 0x%x\n", esi, edi, ebp, esp);
    printf("EFLAGS: 0x%x  CS: 0x%x  DS: 0x%x  SS: 0x%x\n", eflags, cs, ds, ss);

    abort();
}

void kernel_main(void) 
{
	/* Initialize terminal interface */
	tty_initialize();

	/* Newline support is left as an exercise. */
	printf("serotonin kernel - version %d.%d.%d\n",KERNEL_VERSION_HIGH,KERNEL_VERSION_MID,KERNEL_VERSION_LOW);

    /*
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
    */

    kernel_panic("end of kernel_main");
}