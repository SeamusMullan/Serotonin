#ifndef _KERNEL_IO_LAPIC
#define _KERNEL_IO_LAPIC

#include <stdint.h>

/* Local APIC register offsets */

#define LAPIC_ID                    0x020   // Local APIC ID
#define LAPIC_VERSION               0x030   // Local APIC Version
#define LAPIC_TPR                   0x080   // Task Priority Register
#define LAPIC_APR                   0x090   // Arbitration Priority Register
#define LAPIC_PPR                   0x0A0   // Processor Priority Register
#define LAPIC_EOI                   0x0B0   // End-Of-Interrupt
#define LAPIC_RRD                   0x0C0   // Remote Read Register
#define LAPIC_LDR                   0x0D0   // Logical Destination Register
#define LAPIC_DFR                   0x0E0   // Destination Format Register
#define LAPIC_SVR                   0x0F0   // Spurious Interrupt Vector Register
#define LAPIC_ISR                   0x100   // In-Service Register (8 x 16 bytes)
#define LAPIC_TMR                   0x180   // Trigger Mode Register (8 x 16 bytes)
#define LAPIC_IRR                   0x200   // Interrupt Request Register (8 x 16 bytes)
#define LAPIC_ESR                   0x280   // Error Status Register
#define LAPIC_LVT_CMCI              0x2F0   // LVT Corrected Machine Check Interrupt
#define LAPIC_ICR_LOW               0x300   // Interrupt Command Register (bits 0-31)
#define LAPIC_ICR_HIGH              0x310   // Interrupt Command Register (bits 32-63)
#define LAPIC_LVT_TIMER             0x320   // LVT Timer Register
#define LAPIC_LVT_THERMAL           0x330   // LVT Thermal Sensor Register
#define LAPIC_LVT_PERF              0x340   // LVT Performance Monitoring Counters
#define LAPIC_LVT_LINT0             0x350   // LVT LINT0 Register
#define LAPIC_LVT_LINT1             0x360   // LVT LINT1 Register
#define LAPIC_LVT_ERROR             0x370   // LVT Error Register
#define LAPIC_TIMER_INITIAL_COUNT   0x380   // Initial Count Register (Timer)
#define LAPIC_TIMER_CURRENT_COUNT   0x390   // Current Count Register (Timer)
#define LAPIC_TIMER_DIVIDE          0x3E0   // Divide Configuration Register (Timer)

/* Interrupt Command Register (ICR) bit fields */

// Delivery mode (bits 8-10)
#define LAPIC_ICR_DELIVERY_FIXED        0x00000000
#define LAPIC_ICR_DELIVERY_LOWEST       0x00000100
#define LAPIC_ICR_DELIVERY_SMI          0x00000200
#define LAPIC_ICR_DELIVERY_NMI          0x00000400
#define LAPIC_ICR_DELIVERY_INIT         0x00000500
#define LAPIC_ICR_DELIVERY_STARTUP      0x00000600

// Destination mode (bit 11)
#define LAPIC_ICR_DEST_PHYSICAL         0x00000000
#define LAPIC_ICR_DEST_LOGICAL          0x00000800

// Delivery status (bit 12, read-only)
#define LAPIC_ICR_STATUS_IDLE           0x00000000
#define LAPIC_ICR_STATUS_PENDING        0x00001000

// Level (bit 14)
#define LAPIC_ICR_LEVEL_DEASSERT        0x00000000
#define LAPIC_ICR_LEVEL_ASSERT          0x00004000

// Trigger mode (bit 15)
#define LAPIC_ICR_TRIGGER_EDGE          0x00000000
#define LAPIC_ICR_TRIGGER_LEVEL         0x00008000

// Destination shorthand (bits 18-19)
#define LAPIC_ICR_DEST_NONE             0x00000000
#define LAPIC_ICR_DEST_SELF             0x00040000
#define LAPIC_ICR_DEST_ALL              0x00080000
#define LAPIC_ICR_DEST_ALL_NOSELF       0x000C0000

/* LVT Timer */

#define LAPIC_TIMER_PERIODIC            0x00020000
#define LAPIC_TIMER_MASKED              0x00010000
#define LAPIC_TIMER_VECTOR              0x40
#define LAPIC_SPURIOUS_VECTOR           0xFF
#define LAPIC_ERROR_VECTOR              0xFE

/* LAPIC mapping */

#define LAPIC_DEFAULT_PHYS 0xFEE00000
#define LAPIC_VIRTUAL_BASE 0xFEE00000

void lapic_init(void);
void lapic_eoi(void);

#endif
