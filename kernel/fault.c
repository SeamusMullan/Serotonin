#include "stdio/stdio.h"
#include "kernel.h"
#include "stdlib/stdlib.h"
#include "fault.h"
#include "schedule/schedule.h"
#include "io/io.h"
#include "vmm/vmm.h"
#include "vmm/paging_init.h"

extern char __kernel_virtual_base[];
extern char __kernel_end[];
/**
 * @brief Handle CPU exceptions.
 *
 * @param vector The interrupt vector number.
 */
void fault_handler(int vector) {
    printfs(PRINT_STATUS_DEBUG,"INT #%d RAISED\n", vector);

    switch ((isr_vector_t)vector) {
        case ISR_DIVIDE_ERROR:
            kernel_panic("unhandled exception - divide by zero (#DE)");
            break;
        case ISR_DEBUG:
            kernel_panic("unhandled exception - debug (#DB)");
            break;
        case ISR_NON_MASKABLE_INT:
            kernel_panic("unhandled exception - non-maskable interrupt (NMI)");
            break;
        case ISR_BREAKPOINT:
            kernel_panic("unhandled exception - breakpoint (#BP)");
            break;
        case ISR_OVERFLOW:
            kernel_panic("unhandled exception - overflow (#OF)");
            break;
        case ISR_BOUND_RANGE_EXCEEDED:
            kernel_panic("unhandled exception - bound range exceeded (#BR)");
            break;
        case ISR_INVALID_OPCODE:
            kernel_panic("unhandled exception - invalid opcode (#UD)");
            break;
        case ISR_DEVICE_NOT_AVAILABLE:
            kernel_panic("unhandled exception - device not available (#NM)");
            break;
        case ISR_DOUBLE_FAULT:
            kernel_panic("fatal exception - double fault (#DF)");
            break;
        case ISR_COPROC_SEG_OVERRUN:
            kernel_panic("unhandled exception - coprocessor segment overrun");
            break;
        case ISR_INVALID_TSS:
            kernel_panic("unhandled exception - invalid TSS (#TS)");
            break;
        case ISR_SEG_NOT_PRESENT:
            kernel_panic("unhandled exception - segment not present (#NP)");
            break;
        case ISR_STACK_SEG_FAULT:
            kernel_panic("unhandled exception - stack segment fault (#SS)");
            break;
        case ISR_GENERAL_PROTECTION:
            kernel_panic("unhandled exception - general protection fault (#GP)");
            break;
        case ISR_PAGE_FAULT:
            kernel_panic("unhandled exception - page fault (#PF)");
            break;
        case ISR_FPU_ERROR:
            kernel_panic("unhandled exception - x87 floating point error (#MF)");
            break;
        case ISR_ALIGNMENT_CHECK:
            kernel_panic("unhandled exception - alignment check (#AC)");
            break;
        case ISR_MACHINE_CHECK:
            kernel_panic("unhandled exception - machine check (#MC)");
            break;
        case ISR_SIMD_FP_EXCEPTION:
            kernel_panic("unhandled exception - SIMD floating-point exception (#XF)");
            break;
        case ISR_VIRTUALIZATION:
            kernel_panic("unhandled exception - virtualization exception");
            break;

        case ISR_RESERVED_15:
        case ISR_RESERVED_21:
        case ISR_RESERVED_22:
        case ISR_RESERVED_23:
        case ISR_RESERVED_24:
        case ISR_RESERVED_25:
        case ISR_RESERVED_26:
        case ISR_RESERVED_27:
        case ISR_RESERVED_28:
        case ISR_RESERVED_29:
        case ISR_RESERVED_30:
        case ISR_RESERVED_31:
            kernel_panic("exception - reserved/unknown CPU exception");
            break;

        default:
            kernel_panic("exception - unknown interrupt vector");
            break;
    }
}

static void dump_user_bytes(uint32_t eip) {
    if (!current_task || !current_task->address_space) return;
    if (eip < USER_SPACE_START || eip >= USER_SPACE_END) return;

    uint32_t phys = get_mapping(current_task->address_space, eip);
    if (!phys) {
        printfs(PRINT_STATUS_ERROR,"EIP bytes: <unmapped>\n");
        return;
    }

    uint32_t offset = eip & (PAGE_SIZE - 1);
    uint8_t *page = (uint8_t*)kmap(phys);
    uint8_t *ptr = page + offset;
    uint32_t remaining = PAGE_SIZE - offset;
    uint32_t count = (remaining < 8) ? remaining : 8;

    printfs(PRINT_STATUS_ERROR,"EIP bytes:");
    for (uint32_t i = 0; i < count; i++) {
        printf(" %02x", ptr[i]);
    }
    printf("\n");

    kunmap();
}

static void dump_kernel_bytes(uint32_t eip) {
    uint32_t kernel_base = (uint32_t)__kernel_virtual_base;
    uint32_t kernel_end = (uint32_t)__kernel_end;
    if (eip < kernel_base || eip >= kernel_end) {
        printfs(PRINT_STATUS_ERROR,"EIP bytes: <kernel address out of range>\n");
        return;
    }

    uint8_t *ptr = (uint8_t*)eip;
    printfs(PRINT_STATUS_ERROR,"EIP bytes:");
    for (uint32_t i = 0; i < 8; i++) {
        printf(" %02x", ptr[i]);
    }
    printf("\n");
}

static void read_exception_frame(uint32_t *stack, int has_error_code, exception_frame_t *frame) {
    if (!stack || !frame) return;

    frame->ds = stack[0];
    frame->edi = stack[1];
    frame->esi = stack[2];
    frame->ebp = stack[3];
    frame->esp_at_pushal = stack[4];
    frame->ebx = stack[5];
    frame->edx = stack[6];
    frame->ecx = stack[7];
    frame->eax = stack[8];

    if (has_error_code) {
        frame->error_code = stack[9];
        frame->eip = stack[10];
        frame->cs = stack[11];
        frame->eflags = stack[12];
        frame->esp_at_trap = stack[13];
        frame->ss = stack[14];
    } else {
        frame->error_code = 0;
        frame->eip = stack[9];
        frame->cs = stack[10];
        frame->eflags = stack[11];
        frame->esp_at_trap = stack[12];
        frame->ss = stack[13];
    }
}

static int exception_from_user(const exception_frame_t *frame) {
    if (!frame) return 0;
    return (frame->cs & 0x3) == 0x3;
}

static void dump_exception_registers(const exception_frame_t *frame, int from_user) {
    if (!frame) return;

    printfs(PRINT_STATUS_ERROR,"Register dump (from stack):\n");
    printfs(PRINT_STATUS_ERROR,"EAX: 0x%08x EBX: 0x%08x ECX: 0x%08x EDX: 0x%08x\n",
            frame->eax, frame->ebx, frame->ecx, frame->edx);
    printfs(PRINT_STATUS_ERROR,"ESI: 0x%08x EDI: 0x%08x EBP: 0x%08x ESP(at pushal): 0x%08x\n",
            frame->esi, frame->edi, frame->ebp, frame->esp_at_pushal);
    printfs(PRINT_STATUS_ERROR,"EIP: 0x%08x CS: 0x%08x EFLAGS: 0x%08x DS: 0x%08x\n",
            frame->eip, frame->cs, frame->eflags, frame->ds);
    if (from_user) {
        printfs(PRINT_STATUS_ERROR,"ESP(at trap): 0x%08x SS: 0x%08x\n",
                frame->esp_at_trap, frame->ss);
    }
}

static void handle_user_exception(const char *name, uint8_t signal, const exception_frame_t *frame) {
    if (!current_task || !frame) return;
    printfs(PRINT_STATUS_ERROR,"Process \"%s\" (pid=%d) triggered %s and will be terminated.\n",
            current_task->name, current_task->pid, name);
    dump_exception_registers(frame, 1);
    dump_user_bytes(frame->eip);
    task_exit(current_task, signal);
}

static void handle_kernel_exception(const char *name, const exception_frame_t *frame) {
    if (frame) {
        printfs(PRINT_STATUS_ERROR,"Kernel mode %s at EIP=0x%08x CS=0x%08x EFLAGS=0x%08x\n",
                name, frame->eip, frame->cs, frame->eflags);
        dump_exception_registers(frame, 0);
        dump_kernel_bytes(frame->eip);
    }
    char panic_msg[64];
    snprintf(panic_msg, sizeof(panic_msg),"unhandled kernel exception - %s",name);
    kernel_panic(panic_msg);
}

static void handle_exception_common(const char *name, isr_vector_t vector, uint32_t *stack, int has_error_code, uint8_t user_signal) {
    exception_frame_t frame = {0};
    read_exception_frame(stack, has_error_code, &frame);

    int from_user = exception_from_user(&frame);

    printfs(PRINT_STATUS_ERROR,"%s exception (#%d)\n", name, vector);
    if (has_error_code) {
        printfs(PRINT_STATUS_ERROR,"error code:0x%08x\n", frame.error_code);
    }
    printfs(PRINT_STATUS_ERROR,"mode:%s\n", from_user ? "user" : "kernel");

    if (multitasking_ready == 1 && from_user && current_task) {
        handle_user_exception(name, user_signal, &frame);
        return;
    }

    handle_kernel_exception(name, &frame);
}

/**
 * @brief Handle divide by zero faults.
 */
void div_zero_fault_handler(uint32_t *stack) {
    printfs(PRINT_STATUS_ERROR,"--- Exception: Divide by Zero ---\n");
    handle_exception_common("divide by zero (#DE)", ISR_DIVIDE_ERROR, stack, 0, EXIT_SIGFPE);
}

/**
 * @brief Handle breakpoint faults.
 */
void breakpoint_fault_handler(uint32_t *stack) {
    // todo: maybe some debugger integration?
    printfs(PRINT_STATUS_ERROR,"--- Exception: Breakpoint ---\n");
    handle_exception_common("breakpoint (#BP)", ISR_BREAKPOINT, stack, 0, EXIT_SIGTRAP);
}

/**
 * @brief Handle overflow faults.
 */
void overflow_fault_handler(uint32_t *stack) {
    printfs(PRINT_STATUS_ERROR,"--- Exception: Overflow ---\n");
    handle_exception_common("overflow (#OF)", ISR_OVERFLOW, stack, 0, EXIT_SIGFPE);
}

/**
 * @brief Handle bound range exceeded faults.
 */
void bound_range_fault_handler(uint32_t *stack) {
    printfs(PRINT_STATUS_ERROR,"--- Exception: Bound Range Exceeded ---\n");
    handle_exception_common("bound range exceeded (#BR)", ISR_BOUND_RANGE_EXCEEDED, stack, 0, EXIT_SIGSEGV);
}

/**
 * @brief Handle invalid opcode faults.
 */
void invalid_opcode_handler(uint32_t *stack) {
    printfs(PRINT_STATUS_ERROR,"--- Exception: Invalid Opcode ---\n");
    handle_exception_common("invalid opcode (#UD)", ISR_INVALID_OPCODE, stack, 0, EXIT_SIGILL);
}

/**
 * @brief Handle general protection faults.
 */
void gp_fault_handler(uint32_t *stack) {
    printfs(PRINT_STATUS_ERROR,"--- Exception: General Protection Fault ---\n");
    handle_exception_common("general protection fault (#GP)", ISR_GENERAL_PROTECTION, stack, 1, EXIT_SIGSEGV);
}

/**
 * @brief Handle page faults.
 */
void page_fault_handler(uint32_t *stack) {
    exception_frame_t frame = {0};
    read_exception_frame(stack, 1, &frame);

    int from_user = exception_from_user(&frame);
    uint32_t faulting_address;
    asm volatile ("mov %%cr2, %0" : "=r"(faulting_address));

    printfs(PRINT_STATUS_ERROR,"--- Exception: Page Fault ---\n");
    printfs(PRINT_STATUS_ERROR,"FAULT:0x%08x ERR:0x%08x\n", faulting_address,frame.error_code);

    int present   =  frame.error_code & (1<<0);
    int write     =  frame.error_code & (1<<1);
    int user      =  frame.error_code & (1<<2);
    int rsvd      =  frame.error_code & (1<<3);
    int ifetch    =  frame.error_code & (1<<4);

    // Decode the error code
    if (rsvd) {
      printfs(PRINT_STATUS_ERROR,"* reserved-bit violation in PDE/PTE *\n");
    } else if (!present) {
      printfs(PRINT_STATUS_ERROR,"* page not present *\n");
    } else {
      printfs(PRINT_STATUS_ERROR,"* protection violation *\n");
    }
    printfs(PRINT_STATUS_ERROR,  "* %s access in %s mode%s *\n",
           write ? "write" : (ifetch ? "instruction-fetch" : "read"),
           user ? "user" : "kernel",
           (frame.error_code & (1<<5)) ? ", reserved violation of PAT bits" : "");

    printfs(PRINT_STATUS_ERROR,"-----------------------------\n");

    if (multitasking_ready == 1 && from_user && current_task) {
        handle_user_exception("page fault (#PF)", EXIT_SIGSEGV, &frame);
        return;
    }

    handle_kernel_exception("kernel mode exception - page fault (#PF)", &frame);
}

/**
 * @brief Handle alignment check faults.
 */
void alignment_check_fault_handler(uint32_t *stack) {
    handle_exception_common("alignment check (#AC)", ISR_ALIGNMENT_CHECK, stack, 1, EXIT_SIGBUS);
}

/**
 * @brief Handle SIMD floating-point exceptions.
 */
void simd_fp_exception_handler(uint32_t *stack) {
    handle_exception_common("SIMD floating-point exception (#XF)", ISR_SIMD_FP_EXCEPTION, stack, 0, EXIT_SIGFPE);
}
