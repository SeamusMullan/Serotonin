#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "tty.h"
#include "string.h"
#include "stdio/stdio.h"
#include "stdlib/stdlib.h"
#include "multiboot.h"
#include "idt.h"
#include "io/io.h"
#include "paging.h"
#include "video/vbe/vbe.h"
#include "video/font.h"
#include "video/splash.h"

#define KERNEL_VERSION_HIGH 0
#define KERNEL_VERSION_MID 0
#define KERNEL_VERSION_LOW 3 

#define HEAP_START  ((uint8_t*) (KERNEL_HEAP_VMA))
#define HEAP_SIZE   (KERNEL_HEAP_SIZE)

/**
 * @brief Block header for memory allocation.
 * 
 * contains the size and amount of free space, as well as a pointer to the next block.
 * 
 */
typedef struct block_header {
    uint32_t size;
    uint8_t free;
    struct block_header *next;
} block_header_t;

static uint32_t heap_start = (uint32_t)HEAP_START;
static uint32_t heap_end = (uint32_t)(KERNEL_HEAP_VMA + KERNEL_HEAP_SIZE);
static uint32_t current_heap = (uint32_t)KERNEL_HEAP_VMA;
static block_header_t *heap_list = NULL;

#define CHECK_FLAG(flags,bit)   ((flags) & (1 << (bit)))

/**
 * @brief Align a size to the next block boundary.
 * @param size The size to align.
 * @return uint32_t The aligned size.
 */
uint32_t align(uint32_t size) {
    return (size + BLOCK_ALIGN - 1) & ~(BLOCK_ALIGN - 1);
}

/**
 * @brief Jump to the higher half of the kernel address space.
 *
 * @param entry The entry point of the kernel.
 * @param magic The magic number passed by the bootloader.
 * @param multiboot_info The multiboot information structure.
 */
inline void kernel_jump_to_higher_half(void (*entry)(unsigned long, unsigned long), unsigned long magic, unsigned long multiboot_info) {
    uintptr_t flat_addr = (uintptr_t)entry;
    uintptr_t offset    = flat_addr - KERNEL_PHYS_BASE;
    uintptr_t high_addr = KERNEL_VMA_BASE + offset;
    
    printf("calling higher half 0x%08x\n",high_addr);

    asm volatile (
    "push %[arg2]\n"
    "push %[arg1]\n"
    "call *%[func]\n"
    :
    : [func] "r"(high_addr), [arg1] "r"(magic), [arg2] "r"(multiboot_info)
    : "memory"
    );
}

/**
 * @brief Get the current instruction pointer (EIP).
 * 
 * @return void* The current instruction pointer.
 */
static inline void *kernel_current_eip(void) {
    void *eip;
    asm volatile (
        "call 1f       \n"
        "1: pop %%eax  \n"
        : "=a"(eip)
        :
        : "memory"
    );
    return eip;
}

/**
 * @brief Sleep for a specified number of milliseconds.
 * 
 * This function provides a busy-wait loop to create a delay in the kernel.
 * It is not an efficient way to sleep, as it consumes CPU cycles while waiting.
 * @param mili The number of milliseconds to sleep.
 */
void kernel_sleep(unsigned int milliseconds) {
    uint64_t start = timer_ticks;

    unsigned int target_ticks = (milliseconds * 1000U) / 54945U;

    while ((timer_ticks - start) < target_ticks) {
        asm volatile ("hlt");
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
    uintptr_t eip = (uintptr_t)kernel_current_eip();

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

/**
 * @brief Allocate memory from the kernel heap.
 * @param size The size of memory to allocate.
 * @return void* A pointer to the allocated memory, or NULL on failure.
 */
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

    if (size >= PAGE_SIZE) {
        current_heap = PAGE_ALIGN(current_heap);
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

/**
 * @brief Free memory allocated from the kernel heap.
 * 
 * @param ptr A pointer to the memory to free.
 */
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
void kernel_main_high(unsigned long magic, unsigned long addr)
{
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
    {
        kernel_panic("multiboot - invalid magic number");
    }
    multiboot_info_t *mbi = (multiboot_info_t *) addr;

    page_directory_t *page_dir = (page_directory_t*)page_dir_ptr;

    vbe_init(mbi);
    vbe_palette_init();
    vbe_flip();
    splash_render(0,SCREEN_HEIGHT-300);
    create_color_render();

    printf("   _____                _              _       \n");
    printf("  / ____|              | |            (_)      \n");
    printf(" | (___   ___ _ __ ___ | |_ ___  _ __  _ _ __  \n");
    printf("  \\___ \\ / _ \\ '__/ _ \\| __/ _ \\| '_ \\| | '_ \\ \n");
    printf("  ____) |  __/ | | (_) | || (_) | | | | | | | |\n");
    printf(" |_____/ \\___|_|  \\___/ \\__\\___/|_| |_|_|_| |_|\n");


	printf(" serotonin kernel (higher half) - version %d.%d.%d\n",KERNEL_VERSION_HIGH,KERNEL_VERSION_MID,KERNEL_VERSION_LOW);
    printf("kernel now (eip): 0x%08x, kernel heap: 0x%08x, magic: 0x%08x, multiboot_addr:0x%08x\n",kernel_current_eip(),HEAP_START,magic,addr);

    pic_remap(0x20, 0x28);

    init_idt();
    struct idt_ptr idtp_read;
    asm volatile ("sidt %0" : "=m"(idtp_read));
    printfs(PRINT_STATUS_INFO,"IDT base:  0x%08x\n", idtp_read.base);
    printfs(PRINT_STATUS_INFO,"IDT limit: 0x%04x\n", idtp_read.limit);

    // Interrupts ready to be enabled
    asm volatile ("sti");

    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t mem_total;

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

    kernel_sleep(1000);
}

/**
 * @brief The main entry point of the kernel.
 *
 * This function is called by the bootloader with the magic number and multiboot information.
 * It initializes the kernel, sets up paging, and jumps to the higher half of the kernel.
 *
 * @param arg1 The magic number passed by the bootloader.
 * @param arg2 The address of the multiboot information structure.
 */
void kernel_main(unsigned long arg1, unsigned long arg2) {
    multiboot_info_t *mbi = (multiboot_info_t *) arg2;

    unsigned long volatile saved_magic = arg1;
    unsigned long volatile saved_multiboot_info = arg2;

    paging_init((uintptr_t)mbi->framebuffer_addr);
    kernel_jump_to_higher_half(kernel_main_high,arg1,arg2);

    kernel_panic("returned from higher half kernel!"); 
}