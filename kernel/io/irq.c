#include "io.h"
#include "../stdio/stdio.h"
#include "../stdlib/stdlib.h"
#include "../schedule/schedule.h"
#include "../video/vbe/vbe.h"
#include "../kernel.h"

volatile uint64_t timer_ticks = 0;
volatile uint64_t last_quantum_tick = 0;
volatile int multitasking_ready = 0;
volatile int irq_disabled = 1;
volatile rtc_time_t last_rtc_time;

static uint8_t ps2_mouse_packet[3];
static int ps2_mouse_packet_index = 0;
static int mouse_x = 0;
static int mouse_y = 0;

/**
 * @brief Handle IRQ (Interrupt Request) signals.
 *
 * @param irq The IRQ number.
 */
void irq_handler(int irq, processor_context_t *ctx) {
    if (irq == IRQ_PIT) {
        timer_ticks++;
        if (multitasking_ready == 0)
            goto end_irq;

        if (preempt_count == 0 && current_task->priv == CPU_USER_MODE && current_task->state == PROCESS_STATE_RUNNING) {
            last_quantum_tick++;
            if (last_quantum_tick >= SCHEDULE_QUANTUM) {
                memcpy(current_task->processor_context, ctx, sizeof(processor_context_t));
                last_quantum_tick = 0;
                task_yield(1);
            }
        }
        goto end_irq;
    } else if (irq == IRQ_KEYBOARD) {
        // fires every keypress
        uint8_t scancode = inb(PS2_DATA_PORT);
        handle_scancode(scancode);
    } else if (irq == IRQ_MOUSE) {
        uint8_t mouse_data = inb(PS2_DATA_PORT);

        if (ps2_mouse_packet_index == 0) {
            if (!(mouse_data & 0x08)) {
                goto end_irq;
            }
        }

        ps2_mouse_packet[ps2_mouse_packet_index++] = mouse_data;

        if (ps2_mouse_packet_index == 3) {
            int left = ps2_mouse_packet[0] & 0x01;
            int right = ps2_mouse_packet[0] & 0x02;
            int middle = ps2_mouse_packet[0] & 0x04;

            int dx = (int8_t)ps2_mouse_packet[1];
            int dy = (int8_t)ps2_mouse_packet[2];

            mouse_x += dx;
            mouse_y -= dy;

            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x >= SCREEN_WIDTH)  mouse_x = SCREEN_WIDTH - 1;
            if (mouse_y >= SCREEN_HEIGHT) mouse_y = SCREEN_HEIGHT - 1;

            ps2_mouse_packet_index = 0;

            vbe_set_cursor(0,0);
            printf("Mouse abs: x=%d y=%d (dx=%d dy=%d) L=%d R=%d M=%d       \n", mouse_x, mouse_y, dx, dy, left, right, middle);

            vbe_clear_z_layer(1, 0x00000000);
            for (int x = 0; x < 50; x++)
                for (int y = 0; y < 50; y++)
                    vbe_z_putpixel(1, (uint32_t)mouse_x+x, (uint32_t)mouse_y+y, 0x443300FF);
            vbe_flip_all();
        }
        goto end_irq;
    } else if (irq == IRQ_RTC) {
        outb(CMOS_STATUS_REGISTER_A, CMOS_RTC_STATUS_C);
        inb(CMOS_STATUS_REGISTER_B);

        rtc_time_t t;
        uint8_t status_b = cmos_read(0x0B);
        uint8_t binary_mode = status_b & 0x04;
        uint8_t hour_24 = status_b & 0x02;

        t.second = cmos_read(CMOS_RTC_SECONDS);
        t.minute = cmos_read(CMOS_RTC_MINUTES);
        t.hour   = cmos_read(CMOS_RTC_HOURS);
        t.day    = cmos_read(CMOS_RTC_DAY);
        t.month  = cmos_read(CMOS_RTC_MONTH);
        t.year   = cmos_read(CMOS_RTC_YEAR);

        if (!binary_mode) {
            t.second = bcd_to_bin(t.second);
            t.minute = bcd_to_bin(t.minute);
            t.hour   = bcd_to_bin(t.hour);
            t.day    = bcd_to_bin(t.day);
            t.month  = bcd_to_bin(t.month);
            t.year   = bcd_to_bin(t.year);
        }

        t.century = 20; // sure look its fine until 2099

        if (!hour_24) {
            uint8_t pm = t.hour & 0x80;
            t.hour &= 0x7F;
            if (pm && t.hour != 12) t.hour += 12;
            else if (!pm && t.hour == 12) t.hour = 0;
        }

        // printf("time: %d:%d:%d %d/%d/%d%d \n", t.hour,t.minute,t.second,t.day,t.month,t.century,t.year);
    }

end_irq:
    if (irq >= 8)
        outb(0xA0, 0x20);  // EOI to slave PIC
    outb(0x20, 0x20);      // EOI to master PIC
}
