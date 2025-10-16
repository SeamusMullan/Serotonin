// kernel/test/test_io.c
#include "ktest.h"
#include "../io/io.h"
#include "../stdio/stdio.h"

// 32-bit port I/O functions
static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile ("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outl(uint16_t port, uint32_t val) {
    asm volatile ("outl %0, %1" : : "a"(val), "Nd"(port));
}

// Test port I/O functions exist and don't crash
KTEST_DEFINE(inb_test) {
    // Reading from port should not crash
    // Using a safe port (typically 0x80 is used for delays)
    uint8_t value = inb(0x80);
    
    // Just verify it returns something
    (void)value; // Suppress unused warning
    KTEST_ASSERT(1, "inb completes without crash");
    
    return 1;
}

KTEST_DEFINE(outb_test) {
    // Writing to port should not crash
    // Using port 0x80 which is typically safe
    outb(0x80, 0x00);
    
    KTEST_ASSERT(1, "outb completes without crash");
    
    return 1;
}

KTEST_DEFINE(inw_test) {
    // Read 16-bit from port
    uint16_t value = inw(0x80);
    
    (void)value;
    KTEST_ASSERT(1, "inw completes without crash");
    
    return 1;
}

KTEST_DEFINE(outw_test) {
    // Write 16-bit to port
    outw(0x80, 0x0000);
    
    KTEST_ASSERT(1, "outw completes without crash");
    
    return 1;
}

KTEST_DEFINE(inl_test) {
    // Read 32-bit from port
    uint32_t value = inl(0x80);
    
    (void)value;
    KTEST_ASSERT(1, "inl completes without crash");
    
    return 1;
}

KTEST_DEFINE(outl_test) {
    // Write 32-bit to port
    outl(0x80, 0x00000000);
    
    KTEST_ASSERT(1, "outl completes without crash");
    
    return 1;
}

// Test io_wait function
KTEST_DEFINE(io_wait_test) {
    // Should complete without hanging
    io_wait();
    io_wait();
    io_wait();
    
    KTEST_ASSERT(1, "io_wait completes");
    
    return 1;
}

// Test interrupt enable/disable
KTEST_DEFINE(interrupt_test) {
    // Get current interrupt state
    uint32_t eflags_before;
    asm volatile("pushf; pop %0" : "=r"(eflags_before));
    
    // Disable interrupts
    asm volatile("cli");
    
    uint32_t eflags_disabled;
    asm volatile("pushf; pop %0" : "=r"(eflags_disabled));
    
    // Re-enable interrupts
    asm volatile("sti");
    
    uint32_t eflags_enabled;
    asm volatile("pushf; pop %0" : "=r"(eflags_enabled));
    
    // Check IF flag (bit 9)
    KTEST_ASSERT(!(eflags_disabled & 0x200), "interrupts disabled");
    KTEST_ASSERT(eflags_enabled & 0x200, "interrupts enabled");
    
    return 1;
}

// Test simple delay
KTEST_DEFINE(delay_test) {
    // Simple busy-wait delay
    for (volatile int i = 0; i < 1000; i++) {
        io_wait();
    }
    
    KTEST_ASSERT(1, "delay completes");
    
    return 1;
}

// Test multiple I/O operations
KTEST_DEFINE(multiple_io_test) {
    for (int i = 0; i < 10; i++) {
        outb(0x80, (uint8_t)i);
        io_wait();
        inb(0x80);
    }
    
    KTEST_ASSERT(1, "multiple I/O operations complete");
    
    return 1;
}

// Run all I/O tests
void test_io_suite(void) {
    ktest_t tests[] = {
        KTEST_RUN(inb_test),
        KTEST_RUN(outb_test),
        KTEST_RUN(inw_test),
        KTEST_RUN(outw_test),
        KTEST_RUN(inl_test),
        KTEST_RUN(outl_test),
        KTEST_RUN(io_wait_test),
        KTEST_RUN(interrupt_test),
        KTEST_RUN(delay_test),
        KTEST_RUN(multiple_io_test),
    };
    
    ktest_run_suite("I/O Operations", tests, sizeof(tests) / sizeof(tests[0]));
}
