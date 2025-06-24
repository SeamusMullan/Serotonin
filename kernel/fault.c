#include "stdio/stdio.h"
#include "kernel.h"
#include "stdlib/stdlib.h"
#include "fault.h"

/**
 * @brief Handle CPU exceptions.
 * 
 * @param vector The interrupt vector number.
 */
void fault_handler(int vector) {
    printfs(PRINT_STATUS_DEBUG,"INT #%d RAISED\n", vector);

    switch ((isr_vector_t)vector) {
        case ISR_DIVIDE_ERROR: 
            kernel_panic("exception - divide by zero (#DE)");
            break;
        case ISR_DEBUG:
            kernel_panic("exception - debug (#DB)");
            break;
        case ISR_NON_MASKABLE_INT:
            kernel_panic("exception - non-maskable interrupt (NMI)");
            break;
        case ISR_BREAKPOINT:
            kernel_panic("exception - breakpoint (#BP)");
            break;
        case ISR_OVERFLOW:
            kernel_panic("exception - overflow (#OF)");
            break;
        case ISR_BOUND_RANGE_EXCEEDED:
            kernel_panic("exception - bound range exceeded (#BR)");
            break;
        case ISR_INVALID_OPCODE: 
            kernel_panic("exception - invalid opcode (#UD)"); 
            break;
        case ISR_DEVICE_NOT_AVAILABLE:
            kernel_panic("exception - device not available (#NM)");
            break;
        case ISR_DOUBLE_FAULT:
            kernel_panic("exception - double fault (#DF)");
            break;
        case ISR_COPROC_SEG_OVERRUN:
            kernel_panic("exception - coprocessor segment overrun");
            break;
        case ISR_INVALID_TSS:
            kernel_panic("exception - invalid TSS (#TS)");
            break;
        case ISR_SEG_NOT_PRESENT:
            kernel_panic("exception - segment not present (#NP)");
            break;
        case ISR_STACK_SEG_FAULT:
            kernel_panic("exception - stack segment fault (#SS)");
            break;
        case ISR_GENERAL_PROTECTION: 
            kernel_panic("exception - general protection fault (#GP)"); 
            break;
        case ISR_PAGE_FAULT: 
            kernel_panic("exception - page fault (#PF)"); 
            break;
        case ISR_FPU_ERROR:
            kernel_panic("exception - x87 floating point error (#MF)");
            break;
        case ISR_ALIGNMENT_CHECK:
            kernel_panic("exception - alignment check (#AC)");
            break;
        case ISR_MACHINE_CHECK:
            kernel_panic("exception - machine check (#MC)");
            break;
        case ISR_SIMD_FP_EXCEPTION:
            kernel_panic("exception - SIMD floating-point exception (#XF)");
            break;
        case ISR_VIRTUALIZATION:
            kernel_panic("exception - virtualization exception");
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

/**
 * @brief Handle page faults.
 * 
 * 
 * @param error_code The error code associated with the page fault.
 */
void page_fault_handler(uint32_t error_code) {
    uint32_t faulting_address;
    // Read CR2 to get faulting address
    asm volatile ("mov %%cr2, %0" : "=r"(faulting_address));

    printfs(PRINT_STATUS_ERROR," Page fault!\n");
    printfs(PRINT_STATUS_ERROR,"     Faulting address = 0x%08x\n", faulting_address);
    printfs(PRINT_STATUS_ERROR,"     Error code = 0x%08x\n", error_code);

    // Decode the error code
    printfs(PRINT_STATUS_ERROR,"     %s, %s, %s\n",
           (error_code & 0x1) ? "protection violation" : "page not present",
           (error_code & 0x2) ? "write access" : "read access",
           (error_code & 0x4) ? "user mode" : "kernel mode");

    kernel_panic("exception - page fault (#PF)");
}

void gp_fault_handler(uint32_t error_code) {
    printfs(PRINT_STATUS_ERROR,"A general protection fault has occured. Error code: 0x%08x\n",error_code);
    kernel_panic("exception - general protection fault (#GP)");
}

