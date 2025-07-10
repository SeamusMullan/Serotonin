#include "io.h"
#include "../stdio/stdio.h"
#include "../stdlib/stdlib.h"
#include "../schedule/schedule.h"
#include "../kernel.h"

volatile uint64_t timer_ticks = 0;
volatile uint64_t last_quantum_tick = 0;
volatile int multitasking_ready = 0;
volatile int irq_disabled = 1;

/**
 * @brief Handle IRQ (Interrupt Request) signals.
 *
 * @param irq The IRQ number.
 */
void irq_handler(int irq, processor_context_t *ctx) {
    if (irq == 0) {
        timer_ticks++;
        if (multitasking_ready == 0)
            goto end_irq;

        if (preempt_count == 0 && current_task->priv == CPU_USER_MODE && current_task->state == PROCESS_STATE_RUNNING) {
            last_quantum_tick++;
            memcpy(current_task->processor_context, ctx, sizeof(processor_context_t));
            int schedule_quantum = MILLISECONDS_TO_TICKS(100);
            if (last_quantum_tick == schedule_quantum) {
                last_quantum_tick = 1;
                task_yield(1);
            }
        }
        goto end_irq;
    } else if (irq == 1) {
        // fires every keypress
        uint8_t scancode = inb(0x60);
        handle_scancode(scancode);
    }

end_irq:
    if (irq >= 8)
        outb(0xA0, 0x20);  // EOI to slave PIC
    outb(0x20, 0x20);      // EOI to master PIC
}
