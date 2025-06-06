#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tty.h"
#include "string.h"
#include "stdio/stdio.h"
#include "stdlib/stdlib.h"
#include "multiboot.h"

#define KERNEL_VERSION_HIGH 0
#define KERNEL_VERSION_MID 0
#define KERNEL_VERSION_LOW 1 

#define CHECK_FLAG(flags,bit)   ((flags) & (1 << (bit)))

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

void kernel_main(unsigned long magic, unsigned long addr) 
{
	tty_initialize();

	printf("serotonin kernel - version %d.%d.%d\n",KERNEL_VERSION_HIGH,KERNEL_VERSION_MID,KERNEL_VERSION_LOW);
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
    {
        kernel_panic("multiboot - invalid magic number");
    }
    printfs(PRINT_STATUS_INFO,"Multiboot header loaded, mbi=0x%08x\n",addr);
    multiboot_info_t *mbi = (multiboot_info_t *) addr;
    printf("flags = 0x%x\n", (unsigned) mbi->flags);

    if (CHECK_FLAG (mbi->flags, 0))
    {
        printf("mem_lower = %uKB, mem_upper = %uKB\n", (unsigned) mbi->mem_lower, (unsigned) mbi->mem_upper);
    }
    else {
        kernel_panic("multiboot - mem_ invalid"); 
    }
    
    if (CHECK_FLAG (mbi->flags, 6))
    {
      multiboot_memory_map_t *mmap;
      
      printf ("mmap_addr = 0x%x, mmap_length = 0x%x\n",
              (unsigned) mbi->mmap_addr, (unsigned) mbi->mmap_length);
      for (mmap = (multiboot_memory_map_t *) mbi->mmap_addr;
           (unsigned long) mmap < mbi->mmap_addr + mbi->mmap_length;
           mmap = (multiboot_memory_map_t *) ((unsigned long) mmap
                                    + mmap->size + sizeof (mmap->size)))
        printf (" size = 0x%x, base_addr = 0x%x%08x,"
                " length = 0x%x%08x, type = 0x%x\n",
                (unsigned) mmap->size,
                (unsigned) (mmap->addr >> 32),
                (unsigned) (mmap->addr & 0xffffffff),
                (unsigned) (mmap->len >> 32),
                (unsigned) (mmap->len & 0xffffffff),
                (unsigned) mmap->type);
    }

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