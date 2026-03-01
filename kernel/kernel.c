#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <cpuid.h>
#include "kernel.h"
#include "device/mouse/dev_mouse.h"
#include "tty.h"
#include "string.h"
#include "stdio/stdio.h"
#include "stdlib/stdlib.h"
#include "multiboot.h"
#include "idt.h"
#include "io/io.h"
#include "vmm/paging_init.h"
#include "vmm/vmm.h"
#include "video/vbe/vbe.h"
#include "video/font.h"
#include "filesystem/vfs.h"
#include "filesystem/ide.h"
#include "filesystem/tmpfs/tmpfs.h"
#include "filesystem/devfs/devfs.h"
#include "filesystem/fat32/fat32.h"
#include "schedule/schedule.h"
#include "audio/pcspeaker/pcspeaker.h"
#include "gdt.h"
#include "audio/startup/opl2_sound/opl2_startup.h"
#include "io/serial.h"
#include "device/devfs_example.h"
#include "device/mouse/dev_mouse.h"
#include "device/keyboard/dev_keyboard.h"

#define KERNEL_VERSION_HIGH 0
#define KERNEL_VERSION_MID 4
#define KERNEL_VERSION_LOW 0

#define HEAP_START  ((uint8_t*) (KERNEL_HEAP_VMA))
#define HEAP_SIZE   (KERNEL_HEAP_SIZE)
#define HEAP_MAGIC  0x48454150U
#define HEAP_GUARD  0xDEADC0DEU
#define HEAP_GUARD_SIZE 4U

extern char __kernel_start[];
extern char __kernel_end[];
extern char __kernel_load_base[];
extern char __kernel_virtual_base[];

/**
 * @brief Block header for memory allocation.
 *
 * contains the size and amount of free space, as well as a pointer to the next block.
 *
 */
typedef struct block_header {
    uint32_t magic;
    uint32_t size;
    uint8_t free;
    uint8_t guard;
    struct block_header *next;
} block_header_t;

static uint32_t heap_start = (uint32_t)HEAP_START;
static uint32_t heap_end = (uint32_t)(KERNEL_HEAP_VMA + KERNEL_HEAP_SIZE);
static uint32_t current_heap = (uint32_t)KERNEL_HEAP_VMA;
static block_header_t *heap_list = NULL;
static uint8_t debug_mode = 0;
extern uint8_t signal_trampoline[];
extern uint8_t signal_trampoline_end[];

#define CHECK_FLAG(flags,bit)   ((flags) & (1 << (bit)))

/**
 * @brief Align a size to the next block boundary.
 * @param size The size to align.
 * @return uint32_t The aligned size.
 */
uint32_t align(uint32_t size) {
    return (size + BLOCK_ALIGN - 1) & ~(BLOCK_ALIGN - 1);
}

static inline uint32_t kernel_heap_guard_size(void) {
    return debug_mode ? HEAP_GUARD_SIZE : 0;
}

static inline void kernel_heap_set_guard(block_header_t *block) {
    if (!block->guard) return;
    uint32_t *guard = (uint32_t *)((uint8_t *)(block + 1) + block->size);
    *guard = HEAP_GUARD;
}

static inline void kernel_heap_init_guard(block_header_t *block) {
    block->guard = debug_mode ? 1 : 0;
    kernel_heap_set_guard(block);
}

static void kernel_heap_check_block(const block_header_t *block, const char *where) {
    uintptr_t block_addr = (uintptr_t)block;
    uintptr_t heap_start_addr = (uintptr_t)heap_start;
    uintptr_t heap_end_addr = (uintptr_t)heap_end;
    if (block_addr < heap_start_addr || block_addr + sizeof(*block) > heap_end_addr) {
        printfs(PRINT_STATUS_FATAL,
            "heap check failed at %s: block=0x%08x heap=[0x%08x..0x%08x)\n",
            where, (unsigned int)block_addr, (unsigned int)heap_start_addr, (unsigned int)heap_end_addr);
        kernel_panic("heap block out of range");
    }
    uint32_t guard_size = block->guard ? HEAP_GUARD_SIZE : 0;
    if (block->magic != HEAP_MAGIC) {
        printfs(PRINT_STATUS_FATAL,
            "heap check failed at %s: block=0x%08x magic=0x%08x\n",
            where, (unsigned int)block_addr, (unsigned int)block->magic);
        kernel_panic("heap block corrupted");
    }
    if (block->size > HEAP_SIZE ||
        block_addr + sizeof(*block) + block->size + guard_size > heap_end_addr) {
        printfs(PRINT_STATUS_FATAL,
            "heap check failed at %s: block=0x%08x size=0x%08x\n",
            where, (unsigned int)block_addr, (unsigned int)block->size);
        kernel_panic("heap block size corrupted");
    }
    if (block->guard) {
        const uint32_t *guard = (const uint32_t *)((const uint8_t *)(block + 1) + block->size);
        if (*guard != HEAP_GUARD) {
            printfs(PRINT_STATUS_FATAL,
                "heap check failed at %s: block=0x%08x guard=0x%08x size=0x%08x\n",
                where, (unsigned int)block_addr, (unsigned int)(*guard), (unsigned int)block->size);
            kernel_panic("heap block overflow");
        }
    }
    if (block->next) {
        uintptr_t next_addr = (uintptr_t)block->next;
        if (next_addr < heap_start_addr || next_addr + sizeof(*block) > heap_end_addr) {
            printfs(PRINT_STATUS_FATAL,
                "heap check failed at %s: block=0x%08x next=0x%08x\n",
                where, (unsigned int)block_addr, (unsigned int)next_addr);
            kernel_panic("heap next pointer corrupted");
        }
    }
}

/**
 * @brief Read the CR0 register.
 *
 * This function reads the value of the CR0 register.
 * Target is "no-sse" to avoid SSE instructions in case they are not enabled yet.
 * @return uint32_t The value of the CR0 register.
 */
__attribute__((target("no-sse"))) static inline uint32_t kernel_read_cr0(void) {
    uint32_t val;
    asm volatile("mov %%cr0, %0" : "=r"(val));
    return val;
}

/**
 * @brief Write to the CR0 register.
 *
 * This function writes the specified value to the CR0 register.
 */
__attribute__((target("no-sse"))) static inline void kernel_write_cr0(uint32_t val) {
    asm volatile("mov %0, %%cr0" : : "r"(val));
}

/**
 * @brief Read the CR4 register.
 *
 * This function reads the value of the CR4 register.
 * @return uint32_t The value of the CR4 register.
 */
__attribute__((target("no-sse"))) static inline uint32_t kernel_read_cr4(void) {
    uint32_t val;
    asm volatile("mov %%cr4, %0" : "=r"(val));
    return val;
}

/**
 * @brief Write to the CR4 register.
 *
 * This function writes the specified value to the CR4 register.
 */
__attribute__((target("no-sse"))) static inline void kernel_write_cr4(uint32_t val) {
    asm volatile("mov %0, %%cr4" : : "r"(val));
}

/**
 * @brief Check if the CPU supports SSE2.
 *
 * This function checks if the CPU supports SSE2 by examining the CPUID instruction.
 *
 * @return int 1 if SSE2 is supported, 0 otherwise.
 */
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

/**
 * @brief Check if a hypervisor is present.
 *
 * This function checks if a hypervisor is present by examining the CPUID instruction.
 *
 * @return int 1 if a hypervisor is present, 0 otherwise.
 */
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

/**
 * @brief Get the CPU manufacturer string.
 *
 * This function retrieves the CPU manufacturer string by reading the CPUID instruction.
 * The string is built from EBX, EDX, and ECX registers and is guaranteed to be 12 characters long.
 *
 * @return char* Pointer to a static string containing the CPU manufacturer.
 */
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

/**
 * @brief Setup the FPU (Floating Point Unit) for the kernel.
 *
 * This function enables the FPU in CR0, checks for SSE2 support, and initializes the FPU state.
 * It is called during kernel initialization to ensure that the FPU is ready for use.
 */
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
 * @brief Get the CPU features.
 *
 * This function retrieves the CPU features by reading the CPUID instruction.
 *
 * @param f Pointer to a cpu_features_t structure to store the features.
 */
static void kernel_get_cpu_features(cpu_features_t *f) {
    unsigned int eax = 0;
    unsigned int ebx = 0;
    unsigned int ecx = 0;
    unsigned int edx = 0;

    /* Leaf 1 */
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx)) {
        f->sse3       = (ecx & BIT(0))  != 0;
        f->pclmulqdq  = (ecx & BIT(1))  != 0;
        f->monitor    = (ecx & BIT(3))  != 0;
        f->ssse3      = (ecx & BIT(9))  != 0;
        f->fma        = (ecx & BIT(12)) != 0;
        f->cx16       = (ecx & BIT(13)) != 0;
        f->sse4_1     = (ecx & BIT(19)) != 0;
        f->sse4_2     = (ecx & BIT(20)) != 0;
        f->x2apic     = (ecx & BIT(21)) != 0;
        f->popcnt     = (ecx & BIT(23)) != 0;
        f->aes        = (ecx & BIT(25)) != 0;
        f->xsave      = (ecx & BIT(26)) != 0;
        f->osxsave    = (ecx & BIT(27)) != 0;
        f->avx        = (ecx & BIT(28)) != 0;
        f->f16c       = (ecx & BIT(29)) != 0;
        f->rdrand     = (ecx & BIT(30)) != 0;

        f->fpu        = (edx & BIT(0))  != 0;
        f->mmx        = (edx & BIT(23)) != 0;
        f->sse        = (edx & BIT(25)) != 0;
        f->sse2       = (edx & BIT(26)) != 0;
        f->htt        = (edx & BIT(28)) != 0;
    }

    /* Leaf 7/subleaf 0 */
    if (__get_cpuid_max(0, NULL) >= 7) {
        __cpuid_count(7, 0, eax, ebx, ecx, edx);
        f->bmi1           = (ebx & BIT(3))  != 0;
        f->hle            = (ebx & BIT(4))  != 0;
        f->avx2           = (ebx & BIT(5))  != 0;
        f->smep           = (ebx & BIT(7))  != 0;
        f->bmi2           = (ebx & BIT(8))  != 0;
        f->erms           = (ebx & BIT(9))  != 0;
        f->invpcid        = (ebx & BIT(10)) != 0;
        f->rtm            = (ebx & BIT(11)) != 0;

        f->pku            = (ecx & BIT(3))  != 0;
        f->avx512f        = (ecx & BIT(16)) != 0;
        f->avx512dq       = (ecx & BIT(17)) != 0;
        f->avx512pf       = (ecx & BIT(26)) != 0;
        f->avx512er       = (ecx & BIT(27)) != 0;
        f->avx512cd       = (ecx & BIT(28)) != 0;
        f->sha            = (ecx & BIT(29)) != 0;
        f->avx512_vbmi    = (ecx & BIT(1))  != 0;

        f->avx512bw       = (edx & BIT(30)) != 0;
        f->avx512vl       = (edx & BIT(31)) != 0;
    }

    /* Extended leaf 0x80000001 */
    unsigned int max_ext = __get_cpuid_max(0x80000000, NULL);
    if (max_ext >= 0x80000001) {
        __get_cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
        f->lahf_lm        = (ecx & BIT(0))  != 0;
        f->abm            = (ecx & BIT(5))  != 0;
        f->sse4a          = (ecx & BIT(6))  != 0;
        f->fma4           = (ecx & BIT(16)) != 0;
        f->xop            = (ecx & BIT(11)) != 0;

        f->syscall_sysret = (edx & BIT(11)) != 0;
        f->mmxext         = (edx & BIT(22)) != 0;
        f->rdtscp         = (edx & BIT(27)) != 0;
        f->lm             = (edx & BIT(29)) != 0;
    }
}

/**
 * @brief Print the CPU features.
 *
 * This function prints the CPU features stored in the cpu_features_t structure.
 *
 * @param f Pointer to a cpu_features_t structure containing the features.
 */
static void kernel_print_cpu_features(const cpu_features_t *f) {
    if (!debug_mode) return;
    struct { const char *name; uint8_t val; } feat_map[] = {
        {"sse3",         f->sse3},
        {"pclmulqdq",    f->pclmulqdq},
        {"monitor",      f->monitor},
        {"ssse3",        f->ssse3},
        {"fma",          f->fma},
        {"cx16",         f->cx16},
        {"sse4_1",       f->sse4_1},
        {"sse4_2",       f->sse4_2},
        {"x2apic",       f->x2apic},
        {"popcnt",       f->popcnt},
        {"aes",          f->aes},
        {"xsave",        f->xsave},
        {"osxsave",      f->osxsave},
        {"avx",          f->avx},
        {"f16c",         f->f16c},
        {"rdrand",       f->rdrand},
        {"fpu",          f->fpu},
        {"mmx",          f->mmx},
        {"sse",          f->sse},
        {"sse2",         f->sse2},
        {"htt",          f->htt},
        {"bmi1",         f->bmi1},
        {"hle",          f->hle},
        {"avx2",         f->avx2},
        {"smep",         f->smep},
        {"bmi2",         f->bmi2},
        {"erms",         f->erms},
        {"invpcid",      f->invpcid},
        {"rtm",          f->rtm},
        {"pku",          f->pku},
        {"avx512f",      f->avx512f},
        {"avx512dq",     f->avx512dq},
        {"avx512pf",     f->avx512pf},
        {"avx512er",     f->avx512er},
        {"avx512cd",     f->avx512cd},
        {"sha",          f->sha},
        {"avx512bw",     f->avx512bw},
        {"avx512vl",     f->avx512vl},
        {"avx512_vbmi",  f->avx512_vbmi},
        {"lahf_lm",      f->lahf_lm},
        {"abm",          f->abm},
        {"sse4a",        f->sse4a},
        {"fma4",         f->fma4},
        {"xop",          f->xop},
        {"syscall_sysret", f->syscall_sysret},
        {"mmxext",       f->mmxext},
        {"rdtscp",       f->rdtscp},
        {"lm",           f->lm},
    };

    printfs(PRINT_STATUS_DEBUG,"CPU features:\n");
    for (size_t i = 0; i < sizeof(feat_map)/sizeof(feat_map[0]); i++) {
        if (feat_map[i].val)
            printf("  %s", feat_map[i].name);
    }
    printf("\n");
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
            vbe_fast_putpixel(x, y, 0xFF5A000F);

    vbe_set_cursor(0,0);
    vbe_setcolor_bg(0xFF5A000F);

    printf(" _   _   _ \n");
    printf("| | | | | |\n");
    printf("| | | | | |\n");
    printf("| | | | | |\n");
    printf("| | | | | |\n");
    printf("|_| |_| |_|\n");
    printf("(_) (_) (_)\n\n");

    printf("Kernel panic: system must be manually restarted. Press and hold the power button until the computer is shut down.\n");
    printf("*** Guru Meditation: %s ***\n\n", str);

    printf("Stack trace:\n");

    uint32_t old_ebp = ebp;

    for (uint32_t i = 0; i < 10; i++) {
        if (!ebp)
            break;

        uint32_t *frame = (uint32_t*)ebp;
        uint32_t ret_addr = frame[1];

        printf("  #%d:0x%08x:0x%08x\n", i, ebp, ret_addr);

        ebp = frame[0];

        if (ebp == 0 || ebp == (uint32_t)frame)
            break;
        if (ebp < 0x1000)
            break;
    }

    printf("\n");

    ebp = old_ebp;

    if (multitasking_ready == 1) {
        const char *mode = (current_task->priv == 0) ? "kernel" : (current_task->priv == 3) ? "user" : "whatthefuck";
        printf("Process: %s (pid=%d)\n",current_task->name,current_task->pid);
        printf("Process was running in %s mode (ring:%d)\n",mode,current_task->priv);
        printf("Last signal: %d, process state: %d\n", current_task->signal, current_task->state);
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
    printf("TSS: 0x%08x, TSS.ESP0: 0x%08x,  TSS.SS0: 0x%04x, TSS.IOMAP:0x%08x TR: 0x%08x\n", sys_tss, sys_tss.esp0, sys_tss.ss0, sys_tss.iomap, tr);

    vbe_flip_all();

    abort();
}

/**
 * @brief Allocate memory from the kernel heap.
 * @param size The size of memory to allocate.
 * @return void* A pointer to the allocated memory, or NULL on failure.
 */
void *kernel_malloc(uint32_t size) {
    size = align(size);
    uint32_t guard_size = kernel_heap_guard_size();
    uint32_t alloc_size = size + guard_size;
    block_header_t *curr = heap_list;

    // First allocation
    if (!heap_list) {
        heap_list = (block_header_t *)current_heap;
        heap_list->magic = HEAP_MAGIC;
        heap_list->size = size;
        heap_list->free = 0;
        heap_list->next = NULL;
        current_heap += sizeof(block_header_t) + alloc_size;
        if (current_heap > heap_end) {
            kernel_panic("out of kernel heap memory");
            return NULL;
        }
        kernel_heap_init_guard(heap_list);
        return (void *)(heap_list + 1);
    }

    // Look for a free block
    while (curr) {
        kernel_heap_check_block(curr, "kernel_malloc");
        if (curr->free && curr->size >= size) {
            curr->free = 0;
            kernel_heap_set_guard(curr);
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
    current_heap += sizeof(block_header_t) + alloc_size;
    if (current_heap > heap_end) {
        kernel_panic("out of kernel heap memory");
        return NULL;
    }

    new_block->magic = HEAP_MAGIC;
    new_block->size = size;
    new_block->free = 0;
    new_block->next = NULL;
    curr->next = new_block;
    kernel_heap_init_guard(new_block);

    return (void *)(new_block + 1);
}

/**
 * @brief Free memory allocated from the kernel heap.
 *
 * @param ptr A pointer to the memory to free.
 */
void kernel_free(void *ptr) {
    if (!ptr) return;

    uintptr_t ptr_addr = (uintptr_t)ptr;
    uintptr_t heap_start_addr = (uintptr_t)heap_start;
    uintptr_t heap_end_addr = (uintptr_t)heap_end;
    if (ptr_addr < heap_start_addr + sizeof(block_header_t) || ptr_addr >= heap_end_addr) {
        kernel_panic("kernel_free: pointer out of heap range");
    }

    block_header_t *block = ((block_header_t *)ptr) - 1;
    kernel_heap_check_block(block, "kernel_free");
    if (block->free) {
        kernel_panic("kernel_free: double free");
    }
    block->free = 1;
}

void *kernel_malloc_align(uint32_t align, uint32_t size) {
    uintptr_t raw = (uintptr_t)kernel_malloc(size + align - 1 + sizeof(uintptr_t));
    if (!raw) return NULL;

    uintptr_t aligned = ALIGN_UP(raw + sizeof(uintptr_t), align);
    ((uintptr_t*)aligned)[-1] = raw;
    return (void*)aligned;
}

void kernel_free_align(void *p) {
    if (p) kernel_free((void*)((uintptr_t*)p)[-1]);
}

/**
 * @brief Put the CPU into an idle state.
 *
 * This function puts the CPU into an idle state by executing the HLT instruction.
 * It is called when there are no runnable tasks in the system.
 */
void kernel_idle_task(void) {
    while (1) {
        asm volatile ("hlt");
        kernel_yield();
    }
}

/**
 * @brief Load an ELF executable into memory.
 *
 * This function loads an ELF executable from the specified path.
 *
 * @param pcb The pointer to the process control block.
 * @param path The path to the ELF executable.
 * @param pname The name of the process.
 * @return int 1 on failure, 0 on success
 */
int kernel_load_elf(process_control_block_t *pcb, const char *path, const char *pname, const char *const *argv, int argc, const char *const *envp, int envc) {
    vfs_node_t *node = vfs_open(path);
    if (!node) {
        return 1;
    }

    uint32_t file_size = node->size;
    uint8_t *elf_data = kernel_malloc(file_size);
    if (!elf_data) {
        vfs_close(node);
        return 1;
    }
    if (vfs_read(node, 0, file_size, (char *)elf_data) < 0) {
        kernel_free(elf_data);
        vfs_close(node);
        return 1;
    }
    vfs_close(node);

    Elf32_Ehdr *ehdr = (Elf32_Ehdr *)elf_data;
    if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0 ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS32 ||
        ehdr->e_ident[EI_DATA]  != ELFDATA2LSB ||
        ehdr->e_type             != ET_EXEC ||
        ehdr->e_machine          != EM_386) {
        kernel_free(elf_data);
        return 1;
    }

    address_space_t *as = create_address_space();

    uint32_t old_cr3 = read_cr3();
    uint32_t new_cr3 = as->phys_pdir;
    write_cr3(as->phys_pdir);
    lock_scheduler();

    Elf32_Phdr *phdr = (Elf32_Phdr *)(elf_data + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; ++i) {
        if (phdr[i].p_type != PT_LOAD) continue;

        uint32_t vaddr   = phdr[i].p_vaddr;
        uint32_t memsz   = phdr[i].p_memsz;
        uint32_t filesz  = phdr[i].p_filesz;
        uint32_t offset  = phdr[i].p_offset;
        uint32_t flags   = phdr[i].p_flags;

        uint32_t seg_base = vaddr & PAGE_MASK;
        uint32_t seg_end  = (vaddr + memsz + PAGE_SIZE-1) & PAGE_MASK;

        for (uint32_t va = seg_base; va < seg_end; va += PAGE_SIZE) {
            uint32_t frame = (uint32_t)alloc_frame();
            map_page(as, va, frame, USER_PAGE_FLAGS, 0);
        }

        // Copy data and zero BSS
        memcpy((void *)vaddr, elf_data + offset, filesz);
        if (memsz > filesz) {
            memset((void *)(vaddr + filesz), 0, memsz - filesz);
        }
    }

    uint32_t entry_point = ehdr->e_entry;
    kernel_free(elf_data);

    void *stack_base = (void*)USER_STACK_TOP;
    uint32_t stack_top = (uint32_t)stack_base + USER_STACK_SIZE - 4; // GHETTO SOLUTION. DO NOT QUESTION IT. DO NOT ASK WHY ITS 4.
    for (uint32_t va_stk = (uint32_t)stack_base; va_stk < stack_top; va_stk += PAGE_SIZE) {
        uint32_t frame_stk = (uint32_t)alloc_frame();
        if (!frame_stk) kernel_panic("kernel_load_elf: out of memory mapping user stack");
        map_page(as, va_stk, frame_stk, USER_PAGE_FLAGS, 0);
    }

    memset(stack_base, 0, USER_STACK_SIZE);

    uint32_t signal_trampoline_size = (uint32_t)(signal_trampoline_end - signal_trampoline);
    void* phys_signal_trampoline = alloc_frame();
    map_page(as, SIGNAL_TRAMPOLINE_ADDR, (uint32_t)phys_signal_trampoline, USER_PAGE_FLAGS, 0);
    memcpy((void*)SIGNAL_TRAMPOLINE_ADDR, signal_trampoline, signal_trampoline_size);

    uint32_t strings_sz = count_total_string_bytes(envp, envc) + count_total_string_bytes(argv, argc);
    uint32_t ptrs_sz =  sizeof(uint32_t) * (1 /*argc*/ + (size_t)argc + 1 /*NULL*/ + (size_t)envc + 1 /*NULL*/);
    uint32_t sp = align_down(stack_top - strings_sz - ptrs_sz, 16);
    uint32_t strings_start = sp + ptrs_sz;
    uint32_t cur_str = strings_start;

    ((uint32_t*)sp)[0] = (uint32_t)argc;

    uint32_t *argv_user_array = (uint32_t*)sp + 1;
    for (int i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]) + 1;
        memcpy((void*)cur_str, argv[i], len);
        argv_user_array[i] = cur_str;
        cur_str += (uint32_t)len;
    }
    argv_user_array[argc] = 0;

    uint32_t *envp_user_array = argv_user_array + argc + 1;
    for (int i = 0; i < envc; i++) {
        size_t len = strlen(envp[i]) + 1;
        memcpy((void*)cur_str, envp[i], len);
        envp_user_array[i] = cur_str;
        cur_str += (uint32_t)len;
    }
    envp_user_array[envc] = 0;

    write_cr3(old_cr3);
    unlock_scheduler();

    strncpy(pcb->name, pname, sizeof(pcb->name));

    pcb->esp                            = (void*)sp;
    pcb->processor_context->esp_at_trap = sp;
    pcb->cr3                            = (void*)new_cr3;
    pcb->address_space                  = as;
    pcb->esp_min                        = stack_base;
    pcb->esp_max                        = (void*)stack_top;
    pcb->entry                          = (void (*)(void))entry_point;
    pcb->processor_context->eip         = entry_point;
    pcb->argv                           = argv_user_array;
    pcb->envp                           = envp_user_array;

    return 0;
}

/**
 * @brief The main entry point of the kernel.
 *
 * @param magic The magic number passed by the bootloader.
 * @param addr The address of the multiboot information structure.
 */
void kernel_main_high(unsigned long magic, unsigned long addr)
{
    serial_puts(COM1_BASE,"init_high: in higher half\n");
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC)
    {
        kernel_panic("multiboot - invalid magic number");
    }
    multiboot_info_t *mbi = (multiboot_info_t *) addr;
    // page_directory_t *page_dir = (page_directory_t*)page_dir_ptr;
    const char *cmdline = (const char *)(uintptr_t)mbi->cmdline;
    char* cpu_manufacturer = kernel_get_cpu_manufacturer();

    serial_puts(COM1_BASE,"init_high: initializing rtc\n");

    rtc_init();

    serial_puts(COM1_BASE,"init_high: remapping pic\n");

    pic_remap(0x20, 0x28);

    serial_puts(COM1_BASE,"init_high: initializing idt\n");

    init_idt();
    struct idt_ptr idtp_read;
    asm volatile ("sidt %0" : "=m"(idtp_read));
    serial_puts(COM1_BASE,"init_high: irqs ready\n");

    vbe_init(mbi);
    vbe_palette_init();
    vbe_flip();

    serial_puts(COM1_BASE,"init_high: framebuffer ready, early init complete\n");

    enable_interrupts();

    printfs_set_mask(
        (1 << PRINT_STATUS_WARNING) |
        (1 << PRINT_STATUS_ERROR) |
        (1 << PRINT_STATUS_FATAL)
    );

    char cmdline_buf[256];

    if (strlen(cmdline) >= sizeof(cmdline_buf)) {
        kernel_panic("cmdline too long");
    }

    strncpy(cmdline_buf, cmdline, sizeof(cmdline_buf));
    cmdline_buf[sizeof(cmdline_buf) - 1] = '\0';

    for (char* token = strtok(cmdline_buf, " "); token != NULL; token = strtok(NULL, " ")) {
        // yanderedev, should use a struct table in the future, but for now, we only have two args.
        if (strcmp(token, "debug") == 0) {
            printfs_set_mask(
                (1 << PRINT_STATUS_DEBUG) |
                (1 << PRINT_STATUS_INFO) |
                (1 << PRINT_STATUS_WARNING) |
                (1 << PRINT_STATUS_ERROR) |
                (1 << PRINT_STATUS_FATAL) |
                (1 << PRINT_STATUS_SUCCESS)
            );
            debug_mode = 1;
        } else if (strcmp(token, "info") == 0) {
            printfs_set_mask(
                (1 << PRINT_STATUS_INFO) |
                (1 << PRINT_STATUS_WARNING) |
                (1 << PRINT_STATUS_ERROR) |
                (1 << PRINT_STATUS_FATAL) |
                (1 << PRINT_STATUS_SUCCESS)
            );
        } else if (strcmp(token, "quiet") == 0) {
            printfs_set_mask(0);
        }
    }

    vbe_set_cursor(0,0);

    cpu_features_t processor_features = {0};
    kernel_get_cpu_features(&processor_features);

	printfs(PRINT_STATUS_INFO,"Serotonin Kernel %d.%d.%d | Compile Time: %s %s | %d physical pages free | Hypervisor:%d\n",KERNEL_VERSION_HIGH,KERNEL_VERSION_MID,KERNEL_VERSION_LOW,__DATE__,__TIME__,buddy_free_pages(), kernel_hypervisor_present());
    kernel_print_cpu_features(&processor_features);
    printfs(PRINT_STATUS_INFO,"Booted with arguments: %s\n",cmdline);
    printfs(PRINT_STATUS_INFO,"VBE graphics mode framebuffer, resolution %dx%dx%d\n",vbe_info.width,vbe_info.height,vbe_info.bpp);

    printfs(PRINT_STATUS_INFO,"vfs: init\n");
    vbe_flip();

    vfs_init();

    printfs(PRINT_STATUS_INFO,"ide: init\n");
    vbe_flip();

    ide_init();

    printfs(PRINT_STATUS_INFO,"fat32: init\n");
    vbe_flip();

    fat32_init();

    printfs(PRINT_STATUS_INFO,"devfs: init\n");
    vbe_flip();

    devfs_init();

    printfs(PRINT_STATUS_INFO,"tmpfs: init\n");
    vbe_flip();

    tmpfs_init();

    printfs(PRINT_STATUS_INFO,"vfs: mounting root filesystem\n");
    vbe_flip();

    int mount_result = vfs_mount("1", "/", "fat32");

    if (mount_result != 0) {
        kernel_panic("unable to mount rootfs on drive 1");
    }
    printfs(PRINT_STATUS_INFO,"vfs: root filesystem mounted\n");
    vbe_flip();

    printfs(PRINT_STATUS_INFO,"vfs: mounting /dev\n");
    vbe_flip();

    if (vfs_mount("devfs", "/dev", "devfs") != 0) {
        kernel_panic("unable to mount devfs on /dev");
    }

    printfs(PRINT_STATUS_INFO,"vfs: mounting /tmp\n");
    vbe_flip();

    if (vfs_mount("tmpfs", "/tmp", "tmpfs") != 0) {
        kernel_panic("unable to mount tmpfs on /tmp");
    }

    printfs(PRINT_STATUS_INFO,"devices: mouse init\n");
    vbe_flip();
    ps2_mouse_init();
    dev_mouse_init();

    printfs(PRINT_STATUS_INFO,"devices: keyboard init\n");
    vbe_flip();
    dev_keyboard_init();

    multitasking_init();

    char* init_loc = "/bin/init";

    process_control_block_t *init = task_create(NULL, init_loc, CPU_USER_MODE, 254);
    const char *argv[1] = {"/bin/init"}; int argc = 1;
    const char *envp[3] = {"PATH=/bin","TERM=xterm-256color","COLORTERM=truecolor"}; int envc = 3;
    int init_status = kernel_load_elf(init, init_loc, init_loc, argv, argc, envp, envc);
    if (init_status) {
        kernel_panic("unable to load init process!");
    }

    process_control_block_t *idle_task = task_create(kernel_idle_task, "kernel: idle", CPU_KERNEL_MODE, 0);
    enqueue(idle_task);

    vbe_worker_task = task_create(vbe_worker, "kernel: compositor", CPU_KERNEL_MODE, 255);
    vbe_worker_task->no_requeue = 1;

    enqueue(init);
    multitasking_make_ready();

    #ifdef KERNEL_TEST_MODE
        extern void ktest_run_all_suites(void);
        serial_puts(COM1_BASE, "\n\n===== STARTING KERNEL TEST SUITE =====\n");
        printfs(PRINT_STATUS_INFO,"===== RUNNING KERNEL TEST SUITE =====\n");
        ktest_run_all_suites();
        serial_puts(COM1_BASE, "===== KERNEL TESTS COMPLETED =====\n\n");
        printfs(PRINT_STATUS_INFO,"===== KERNEL TESTS COMPLETED =====\n");
    #endif

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

    serial_init(COM1_BASE);

    serial_puts(COM1_BASE, "init: paging enabled\n");

    kernel_setup_fpu();

    serial_puts(COM1_BASE, "init: fpu, sse2 enabled\n");

    uint32_t kernel_phys_start = (uint32_t)__kernel_load_base;
    uint32_t kernel_phys_end   = (uint32_t)__kernel_end - (uint32_t)__kernel_virtual_base + (uint32_t)__kernel_load_base;
    buddy_init(mbi, kernel_phys_start, kernel_phys_end, (uint32_t)mbi->framebuffer_addr, (uint32_t)(mbi->framebuffer_height) * (uint32_t)(mbi->framebuffer_pitch));

    serial_puts(COM1_BASE, "init: physical memory manager initialized\n");

    vmm_init();

    serial_puts(COM1_BASE, "init: virtual memory manager initialized\n");

    serial_puts(COM1_BASE, "init: jumping to higher half kernel\n");

    asm volatile (
        "movl %0, %%esp\n"
        "xor %%ebp, %%ebp\n"
        "pushl %2\n"
        "pushl %1\n"
        "pushl $0\n"
        "jmp kernel_main_high"
        :
        : "r"(KERNEL_ESP), "r"(arg1), "r"(arg2)
        : "memory"
    );
}
