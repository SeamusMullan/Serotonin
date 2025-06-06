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

    printfs(PRINT_STATUS_FATAL, "Kernel panic. Please reboot your computer.\n");
    printfs(PRINT_STATUS_FATAL, "Reason: %s\n", str);
    printfs(PRINT_STATUS_FATAL, "Kernel version: %d.%d.%d\n", KERNEL_VERSION_HIGH, KERNEL_VERSION_MID, KERNEL_VERSION_LOW);
    printfs(PRINT_STATUS_FATAL, "EIP: 0x%08x\n", (unsigned int)eip);
    printfs(PRINT_STATUS_FATAL, "EAX: 0x%08x  EBX: 0x%08x  ECX: 0x%08x  EDX: 0x%08x\n",(unsigned int)eax, (unsigned int)ebx, (unsigned int)ecx, (unsigned int)edx);
    printfs(PRINT_STATUS_FATAL, "ESI: 0x%08x  EDI: 0x%08x  EBP: 0x%08x  ESP: 0x%08x\n",(unsigned int)esi, (unsigned int)edi, (unsigned int)ebp, (unsigned int)esp);
    printfs(PRINT_STATUS_FATAL, "EFLAGS: 0x%08x  CS: 0x%04x  DS: 0x%04x  SS: 0x%04x\n",(unsigned int)eflags, (unsigned int)cs, (unsigned int)ds, (unsigned int)ss);

    abort();
}

void kernel_main(void) 
{
	tty_initialize();

	printf("serotonin kernel - version %d.%d.%d\n",KERNEL_VERSION_HIGH,KERNEL_VERSION_MID,KERNEL_VERSION_LOW);

    /*
    printfs(PRINT_STATUS_DEBUG,"Test\n");
    printfs(PRINT_STATUS_INFO,"Test\n");
    printfs(PRINT_STATUS_WARNING,"Test\n");
    printfs(PRINT_STATUS_ERROR,"Test\n");
    printfs(PRINT_STATUS_FATAL,"Test\n");
    */

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