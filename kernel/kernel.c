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

#define KERNEL_HEAP_START 0x00F000 
#define KERNEL_HEAP_SIZE  0x300000
#define BLOCK_ALIGN 8

typedef struct block_header {
    uint32_t size;
    uint8_t free;
    struct block_header *next;
} block_header_t;

static uint32_t heap_start = KERNEL_HEAP_START;
static uint32_t heap_end = KERNEL_HEAP_START + KERNEL_HEAP_SIZE;
static uint32_t current_heap = KERNEL_HEAP_START;
static block_header_t *heap_list = NULL;

#define CHECK_FLAG(flags,bit)   ((flags) & (1 << (bit)))

uint32_t align(uint32_t size) {
    return (size + BLOCK_ALIGN - 1) & ~(BLOCK_ALIGN - 1);
}

/**
 * @brief Sleep for a specified number of milliseconds.
 * 
 * This function provides a busy-wait loop to create a delay in the kernel.
 * It is not an efficient way to sleep, as it consumes CPU cycles while waiting.
 * @param mili The number of milliseconds to sleep.
 */
 void kernel_sleep(unsigned int mili)
{
    volatile unsigned int count = mili * 100000;
    while (count--) {
        asm volatile("nop"); 
    }
}

/**
 * @brief Trigger a kernel panic with a specified message.
 * 
 * This function is called when a critical error occurs in the kernel.
 * It prints the panic message along with the current state of the CPU registers
 * and halts the system.
 * @param str The panic message to display.
 */
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

void *kernel_malloc(uint32_t size) {
    size = align(size);
    block_header_t *curr = heap_list;

    // First allocation
    if (!heap_list) {
        heap_list = (block_header_t *)current_heap;
        heap_list->size = size;
        heap_list->free = 0;
        heap_list->next = NULL;
        current_heap += sizeof(block_header_t) + size;
        return (void *)(heap_list + 1);
    }

    // Look for a free block
    while (curr) {
        if (curr->free && curr->size >= size) {
            curr->free = 0;
            return (void *)(curr + 1);
        }
        if (!curr->next) break;
        curr = curr->next;
    }

    // Allocate new block
    block_header_t *new_block = (block_header_t *)current_heap;
    current_heap += sizeof(block_header_t) + size;
    if (current_heap >= heap_end) {
        kernel_panic("out of kernel heap memory");
        return NULL;
    }

    new_block->size = size;
    new_block->free = 0;
    new_block->next = NULL;
    curr->next = new_block;

    return (void *)(new_block + 1);
}

void kernel_free(void *ptr) {
    if (!ptr) return;

    block_header_t *block = ((block_header_t *)ptr) - 1;
    block->free = 1;
}

/**
 * @brief The main entry point of the kernel.
 *
 * @param magic The magic number passed by the bootloader.
 * @param addr The address of the multiboot information structure.
 */
void kernel_main(unsigned long magic, unsigned long addr)
{
	tty_initialize();

    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t mem_total;

	printf("serotonin kernel - version %d.%d.%d\n",KERNEL_VERSION_HIGH,KERNEL_VERSION_MID,KERNEL_VERSION_LOW);
    printf("kernel start: 0x%08x, kernel heap: 0x%08x, magic: 0x%08x, multiboot_addr:0x%08x\n",&kernel_main,KERNEL_HEAP_START,magic,addr);
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
    {
        kernel_panic("multiboot - invalid magic number");
    }
    printfs(PRINT_STATUS_INFO,"Multiboot header loaded, mbi=0x%08x\n",addr);
    multiboot_info_t *mbi = (multiboot_info_t *) addr;
    //printf("flags = 0x%x\n", (unsigned) mbi->flags);

    if (CHECK_FLAG (mbi->flags, 0))
    {
        mem_lower = (unsigned) mbi->mem_lower;
        mem_upper = (unsigned) mbi->mem_upper;
        mem_total = mem_lower+mem_upper;
        printfs(PRINT_STATUS_INFO,"Detected lower memory: %uKB\n", mem_lower);
        printfs(PRINT_STATUS_INFO,"Detected upper memory: %uKB\n", mem_upper);
        printfs(PRINT_STATUS_INFO,"Total memory detected: %uKB\n", mem_total);
    }
    else {
        kernel_panic("multiboot - unable to detect memory"); 
    }

    uint16_t cs;
    asm volatile ("mov %%cs, %0" : "=r"(cs));


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