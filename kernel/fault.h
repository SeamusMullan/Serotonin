#ifndef _KERNEL_FAULT
#define _KERNEL_FAULT

typedef enum {
    ISR_DIVIDE_ERROR         = 0,   // #DE
    ISR_DEBUG                = 1,   // #DB
    ISR_NON_MASKABLE_INT     = 2,   // NMI
    ISR_BREAKPOINT           = 3,   // #BP
    ISR_OVERFLOW             = 4,   // #OF
    ISR_BOUND_RANGE_EXCEEDED = 5,   // #BR
    ISR_INVALID_OPCODE       = 6,   // #UD
    ISR_DEVICE_NOT_AVAILABLE = 7,   // #NM
    ISR_DOUBLE_FAULT         = 8,   // #DF
    ISR_COPROC_SEG_OVERRUN   = 9,   // (reserved)
    ISR_INVALID_TSS          = 10,  // #TS
    ISR_SEG_NOT_PRESENT      = 11,  // #NP
    ISR_STACK_SEG_FAULT      = 12,  // #SS
    ISR_GENERAL_PROTECTION   = 13,  // #GP
    ISR_PAGE_FAULT           = 14,  // #PF
    ISR_RESERVED_15          = 15,
    ISR_FPU_ERROR            = 16,  // #MF
    ISR_ALIGNMENT_CHECK      = 17,  // #AC
    ISR_MACHINE_CHECK        = 18,  // #MC
    ISR_SIMD_FP_EXCEPTION    = 19,  // #XF
    ISR_VIRTUALIZATION       = 20,  // (Intel/AMD)
    ISR_RESERVED_21          = 21,
    ISR_RESERVED_22          = 22,
    ISR_RESERVED_23          = 23,
    ISR_RESERVED_24          = 24,
    ISR_RESERVED_25          = 25,
    ISR_RESERVED_26          = 26,
    ISR_RESERVED_27          = 27,
    ISR_RESERVED_28          = 28,
    ISR_RESERVED_29          = 29,
    ISR_RESERVED_30          = 30,
    ISR_RESERVED_31          = 31
} isr_vector_t;

#endif
