#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cpuid.h>
#include "kernel.h"
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
#include "filesystem/vfs.h"
#include "filesystem/ide.h"
#include "filesystem/tmpfs/tmpfs.h"
#include "filesystem/fat32/fat32.h"
#include "schedule/schedule.h"
#include "audio/pcspeaker/pcspeaker.h"
#include "gdt.h"
#include "audio/startup/opl2_sound/opl2_startup.h"

#define KERNEL_VERSION_HIGH 0
#define KERNEL_VERSION_MID 1
#define KERNEL_VERSION_LOW 1

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
process_control_block_t *pcbA;
process_control_block_t *pcbB;
process_control_block_t *pcbC;

#define CHECK_FLAG(flags,bit)   ((flags) & (1 << (bit)))

/**
 * @brief Align a size to the next block boundary.
 * @param size The size to align.
 * @return uint32_t The aligned size.
 */
uint32_t align(uint32_t size) {
    return (size + BLOCK_ALIGN - 1) & ~(BLOCK_ALIGN - 1);
}

__attribute__((target("no-sse"))) static inline uint32_t kernel_read_cr0(void) {
    uint32_t val;
    asm volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

__attribute__((target("no-sse"))) static inline void kernel_write_cr0(uint32_t val) {
    asm volatile("mov %0, %%cr0" : : "r"(val));
}

__attribute__((target("no-sse"))) static inline uint32_t kernel_read_cr4(void) {
    uint32_t val;
    asm volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}

__attribute__((target("no-sse"))) static inline void kernel_write_cr4(uint32_t val) {
    asm volatile("mov %0, %%cr4" : : "r"(val));
}


__attribute__((target("no-sse"))) static int kernel_cpu_has_sse2(void) {
    unsigned int eax, ebx, ecx, edx;
    unsigned int ret;

    ret = __get_cpuid(1, &eax, &ebx, &ecx, &edx);
    if (ret != 1) {
        abort();
    }

    // Bit 26 = SSE2
    return (edx & (1 << 26)) != 0;
}

static int kernel_hypervisor_present(void) {
    unsigned int eax, ebx, ecx, edx;
    unsigned int ret;

    ret = __get_cpuid(1, &eax, &ebx, &ecx, &edx);
    if (ret != 1) {
        abort();
    }

    // Bit 31 of ECX indicates a presence of a hypervisor
    return (ecx & (1 << 31)) != 0;
}

static char* kernel_get_cpu_manufacturer(void) {
    unsigned int eax, ebx, ecx, edx;
    unsigned int ret;

    ret = __get_cpuid(0, &eax, &ebx, &ecx, &edx);
    if (ret != 1) {
        abort();
    }

    // Build manufacturer string, should be 12 chars long unless a hypervisor defies the laws of x86
    static char manufacturer[13];
    *(uint32_t *)&manufacturer[0] = ebx;
    *(uint32_t *)&manufacturer[4] = edx;
    *(uint32_t *)&manufacturer[8] = ecx;
    manufacturer[12] = '\0';

    return manufacturer;
}

__attribute__((target("no-sse"))) void kernel_setup_fpu(void) {
    // Enable FPU in CR0
    uint32_t cr0 = kernel_read_cr0();
    cr0 &= ~(1 << 2); // Clear EM
    cr0 |=  (1 << 1); // Set MP
    cr0 &= ~(1 << 3); // Clear TS
    kernel_write_cr0(cr0);

    // Check for SSE2 support
    if (kernel_cpu_has_sse2()) {
        uint32_t cr4 = kernel_read_cr4();
        cr4 |= (1 << 9);  // OSFXSR
        cr4 |= (1 << 10); // OSXMMEXCPT
        kernel_write_cr4(cr4);
    }
    else {
        // SSE should be supported
        abort();
    }

    // Initialize the FPU to default state
    asm volatile("fninit");
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

    unsigned int target_ticks = MILLISECONDS_TO_TICKS(milliseconds);

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
    unsigned int eip;

    asm volatile (
        "movl 4(%%ebp), %0"
        : "=r"(eip)
        :
        :
    );

    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t eflags;
    uint16_t cs, ds, ss, tr;

    uint32_t cr0, cr2, cr3, cr4;

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
    asm volatile ("mov %%cr0, %0" : "=r"(cr0));
    asm volatile ("mov %%cr2, %0" : "=r"(cr2));
    asm volatile ("mov %%cr3, %0" : "=r"(cr3));
    asm volatile ("mov %%cr4, %0" : "=r"(cr4));
    asm volatile ("str %0" : "=r"(tr));;

    for (int y = 0; y < SCREEN_HEIGHT; ++y)
        for (int x = 0; x < SCREEN_WIDTH; ++x)
            vbe_fast_putpixel(x, y, 0xFF880000);

    vbe_set_cursor(0,0);
    vbe_setcolor_bg(0xFF880000);

    printf(" _   _   _ \n");
    printf("| | | | | |\n");
    printf("| | | | | |\n");
    printf("| | | | | |\n");
    printf("| | | | | |\n");
    printf("|_| |_| |_|\n");
    printf("(_) (_) (_)\n\n");

    printf("The Serotonin kernel has entered into an unrecoverable state and must be restarted manually.\n");
    printf("*** Guru Meditation: %s ***\n\n", str);

    if (multitasking_ready == 1) {
        const char *mode = (current_task->priv == 0) ? "kernel" : (current_task->priv == 3) ? "user" : "whatthefuck";
        printf("Process: %s (pid=%d)\n",current_task->name,current_task->pid);
        printf("Process was running in %s mode (ring:%d)\n",mode,current_task->priv);
        printf("Last signal: %d, process state: %d\n", current_task->signal, current_task->state);

        uint8_t* ptr = (uint8_t*)current_task->processor_context->eip;

        for (int i = 0; i < 0x8C; i++) {
            if (i % 20 == 0) {
                printf("\n0x%08x: ", (unsigned int)(ptr + i));
            } else if (i % 4 == 0) {
                printf(" ");
            }
            printf("%02x", ptr[i]);
        }
        printf("\n");
    } else {
        printf("[multitasking not ready!]\n");
    }

    uint8_t* ptr = (uint8_t*)eip;

    for (int i = 0; i < 0x8C; i++) {
        if (i % 20 == 0) {
            printf("\n0x%08x: ", (unsigned int)(ptr + i));
        } else if (i % 4 == 0) {
            printf(" ");
        }
        printf("%02x", ptr[i]);
    }
    printf("\n\n");

    printf("Kernel version: %d.%d.%d\n", KERNEL_VERSION_HIGH, KERNEL_VERSION_MID, KERNEL_VERSION_LOW);
    printf("EIP: 0x%08x\n", (unsigned int)eip);
    printf("EAX: 0x%08x  EBX: 0x%08x  ECX: 0x%08x  EDX: 0x%08x\n",(unsigned int)eax, (unsigned int)ebx, (unsigned int)ecx, (unsigned int)edx);
    printf("ESI: 0x%08x  EDI: 0x%08x  EBP: 0x%08x  ESP: 0x%08x\n",(unsigned int)esi, (unsigned int)edi, (unsigned int)ebp, (unsigned int)esp);
    printf("EFLAGS: 0x%08x  CS: 0x%04x  DS: 0x%04x  SS: 0x%04x\n",(unsigned int)eflags, (unsigned int)cs, (unsigned int)ds, (unsigned int)ss);
    printf("CR0: 0x%08x  CR2 (fault addr): 0x%08x  CR3 (page directory base): 0x%08x  CR4: 0x%08x\n",
            (unsigned int)cr0, (unsigned int)cr2, (unsigned int)cr3, (unsigned int)cr4);
    printf("TSS.ESP0: 0x%08x,  TSS.SS0: 0x%04x, TR: 0x%04x\n", sys_tss.esp0, sys_tss.ss0,tr);

    vbe_flip();

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

void task_A(void) {
    unsigned int i;
    while (true) {
        i++;
        unsigned int esp;
        asm volatile (
            "movl %%esp, %0"
            : "=r" (esp)
            :
            :
        );
        printf("[A] tick %d, esp=%p, since_last_quantum=%d\n", i, esp,last_quantum_tick);
    }
}

void task_B(void) {
    unsigned int i;
    while (true) {
        i++;
        uint32_t* esp;
        asm volatile (
            "movl %%esp, %0"
            : "=r" (esp)
            :
            :
        );

        printf("esp top: %08x %08x %08x %08x\n", esp[0], esp[1], esp[2], esp[3]);
        printf("[B] tick %d, esp=%p\n", i, esp);
        kernel_yield();
    }
}

void task_C(void) {
    unsigned int i = 0;
    while (true) {
        i++;
        unsigned int esp;
        asm volatile (
            "movl %%esp, %0"
            : "=r" (esp)
            :
            :
        );
        printf("[C] tick %d, esp=%p\n", i, esp);
    }
}

process_control_block_t *kernel_load_elf(const char *path) {
    vfs_node_t *node = vfs_open(path);
    if (!node) {
        return NULL;
    }

    uint32_t file_size = node->size;
    uint8_t *elf_data = kernel_malloc(file_size);
    if (!elf_data) {
        vfs_close(node);
        return NULL;
    }
    if (vfs_read(node, 0, file_size, (char *)elf_data) < 0) {
        kernel_free(elf_data);
        vfs_close(node);
        return NULL;
    }
    vfs_close(node);

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)elf_data;
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS32 ||
        ehdr->e_ident[EI_DATA]  != ELFDATA2LSB ||
        ehdr->e_type             != ET_EXEC ||
        ehdr->e_machine          != EM_386) {
        kernel_free(elf_data);
        return NULL;
    }

    Elf32_Phdr *phdr = (Elf32_Phdr *)(elf_data + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type != PT_LOAD) continue;

        uint32_t vaddr   = phdr[i].p_vaddr;
        uint32_t memsz   = phdr[i].p_memsz;
        uint32_t filesz  = phdr[i].p_filesz;
        uint32_t offset  = phdr[i].p_offset;
        uint32_t flags   = phdr[i].p_flags;

        // Copy data and zero BSS
        memcpy((void *)vaddr, elf_data + offset, filesz);
        if (memsz > filesz) {
            memset((void *)(vaddr + filesz), 0, memsz - filesz);
        }
    }

    kernel_free(elf_data);

    process_control_block_t *pcb = task_create((void (*)(void))ehdr->e_entry, "init", CPU_USER_MODE);

    enqueue(pcb);

    return pcb;
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

    char* cpu_manufacturer = kernel_get_cpu_manufacturer();

    vbe_init(mbi);
    vbe_palette_init();
    vbe_flip();
    splash_render(0,10);
    create_color_render(275);

    vbe_set_cursor(0,15);

    printf("   _____                _              _       \n");
    printf("  / ____|              | |            (_)      \n");
    printf(" | (___   ___ _ __ ___ | |_ ___  _ __  _ _ __  \n");
    printf("  \\___ \\ / _ \\ '__/ _ \\| __/ _ \\| '_ \\| | '_ \\ \n");
    printf("  ____) |  __/ | | (_) | || (_) | | | | | | | |\n");
    printf(" |_____/ \\___|_|  \\___/ \\__\\___/|_| |_|_|_| |_|\n");


	printf(" serotonin kernel (higher half) - version %d.%d.%d\n",KERNEL_VERSION_HIGH,KERNEL_VERSION_MID,KERNEL_VERSION_LOW);
    printf("kernel now (eip): 0x%08x, kernel heap: 0x%08x, magic: 0x%08x, multiboot_addr:0x%08x, cpu:%s\n",kernel_current_eip(),HEAP_START,magic,addr,cpu_manufacturer);
    printfs(PRINT_STATUS_INFO,"Running in VESA VBE Graphics Mode: %dx%dx%d, pitch: %d\n",vbe_info.width,vbe_info.height,vbe_info.bpp,vbe_info.pitch);

    pic_remap(0x20, 0x28);

    init_idt();
    struct idt_ptr idtp_read;
    asm volatile ("sidt %0" : "=m"(idtp_read));
    printfs(PRINT_STATUS_INFO,"IDT base:  0x%08x\n", idtp_read.base);
    printfs(PRINT_STATUS_INFO,"IDT limit: 0x%04x\n", idtp_read.limit);

    // Interrupts ready to be enabled
    enable_interrupts();

    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t mem_total;

    if (CHECK_FLAG (mbi->flags, 0))
    {
        mem_lower = (unsigned) mbi->mem_lower;
        mem_upper = (unsigned) mbi->mem_upper;
        mem_total = mem_lower+mem_upper;
        printfs(PRINT_STATUS_INFO,"Detected lower memory: %uKB\n", mem_lower);
        printfs(PRINT_STATUS_INFO,"Detected extended memory: %uKB\n", mem_upper);
        printfs(PRINT_STATUS_INFO,"Total memory detected: %uKB\n", mem_total);
    }
    else {
        kernel_panic("multiboot - unable to detect memory");
    }

    if (kernel_hypervisor_present()) {
        printfs(PRINT_STATUS_INFO,"A hypervisor is present.\n");
    }

    printfs(PRINT_STATUS_INFO,"Attempting to mount rootfs drive 1\n");

    vfs_init();
    ide_init();
    fat32_init();
    int mount_result = vfs_mount("1", "/", "fat32");

    if (mount_result != 0) {
        kernel_panic("unable to mount rootfs on drive 1");
    }

    demo_arpeggio();

    multitasking_init();

    printfs(PRINT_STATUS_INFO,"Attempting to load /bin/init\n");

    process_control_block_t *init = kernel_load_elf("/bin/init");
    if (!init) {
        kernel_panic("unable to load init process!");
    }

    process_control_block_t *t = task_list;
    printf("Task list:\n");
    do {
        printf("  Task %s (pid=%u), esp=%p, cr3=%p, state=%d\n",
               t->name, t->pid, t->esp, t->cr3, t->state);
        t = t->next;
        if (t == NULL) {
            break;
        }
    } while (t != task_list);

    multitasking_make_ready();

    task_yield(0);

    abort();
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
__attribute__((target("no-sse"))) __attribute__((section(".identity"))) void kernel_main(unsigned long arg1, unsigned long arg2) {
    multiboot_info_t *mbi = (multiboot_info_t *) arg2;

    paging_init((uintptr_t)mbi->framebuffer_addr);

    kernel_setup_fpu();
    kernel_main_high(arg1,arg2);

    kernel_panic("returned from higher half kernel!");
}
