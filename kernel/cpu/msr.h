#ifndef _KERNEL_CPU_MSR
#define _KERNEL_CPU_MSR

#include <stdint.h>

/* Common Model-Specific Register addresses */

#define MSR_IA32_TSC                0x00000010
#define MSR_IA32_APIC_BASE          0x0000001B
#define MSR_IA32_FEATURE_CONTROL    0x0000003A
#define MSR_IA32_SYSENTER_CS        0x00000174
#define MSR_IA32_SYSENTER_ESP       0x00000175
#define MSR_IA32_SYSENTER_EIP       0x00000176
#define MSR_IA32_MISC_ENABLE        0x000001A0
#define MSR_IA32_PAT                0x00000277
#define MSR_IA32_EFER               0xC0000080
#define MSR_IA32_STAR               0xC0000081
#define MSR_IA32_LSTAR              0xC0000082
#define MSR_IA32_CSTAR              0xC0000083
#define MSR_IA32_FMASK              0xC0000084
#define MSR_IA32_FS_BASE            0xC0000100
#define MSR_IA32_GS_BASE            0xC0000101
#define MSR_IA32_KERNEL_GS_BASE     0xC0000102

/* IA32_APIC_BASE bit fields */

#define MSR_APIC_BASE_BSP           0x00000100  // Bootstrap processor
#define MSR_APIC_BASE_X2APIC        0x00000400  // x2APIC mode enable
#define MSR_APIC_BASE_ENABLE        0x00000800  // Global APIC enable
#define MSR_APIC_BASE_ADDR_MASK     0xFFFFF000  // Physical base address mask

/**
 * @brief Read a Model-Specific Register, returning EAX and EDX directly.
 *
 * @param msr  The MSR address (loaded into ECX).
 * @param low  Out: the low 32 bits (EAX).
 * @param high Out: the high 32 bits (EDX).
 */
static inline void rdmsr(uint32_t msr, uint32_t *low, uint32_t *high) {
    asm volatile ("rdmsr" : "=a"(*low), "=d"(*high) : "c"(msr));
}

/**
 * @brief Write a Model-Specific Register from raw EAX and EDX values.
 *
 * @param msr  The MSR address (loaded into ECX).
 * @param low  The low 32 bits (EAX).
 * @param high The high 32 bits (EDX).
 */
static inline void wrmsr(uint32_t msr, uint32_t low, uint32_t high) {
    asm volatile ("wrmsr" : : "c"(msr), "a"(low), "d"(high));
}

#endif
