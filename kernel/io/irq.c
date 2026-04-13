#include <kernel/io/io.h>
#include <kernel/io/serial.h>
#include <kernel/stdio/stdio.h>
#include <kernel/stdlib/stdlib.h>
#include <kernel/schedule/schedule.h>
#include <kernel/video/vbe/vbe.h>
#include <kernel/kernel.h>
#include <kernel/device/mouse/dev_mouse.h>
#include <kernel/device/serial/dev_serial.h>
#include <kernel/syscall/syscall.h>

volatile uint64_t timer_ticks = 0;
volatile uint64_t last_quantum_tick = 0;
volatile int multitasking_ready = 0;
volatile int irq_disabled = 1;
volatile rtc_time_t last_rtc_time;
volatile uint32_t unix_timestamp = 0;
volatile uint32_t vbe_ticks = 0;
volatile int mouse_x = 0;
volatile int mouse_y = 0;

static uint8_t ps2_mouse_packet[3];
static int ps2_mouse_packet_index = 0;
static uint8_t prev_mouse_buttons = 0;
static uint64_t ps2_mouse_last_byte_tick = 0;

static irq_handler_fn irq_handlers[IRQ_MAX][IRQ_CHAIN_MAX] = {{0}};

static void pic_unmask_irq(int irq) {
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    uint8_t line = (irq < 8) ? irq : irq - 8;
    outb(port, inb(port) & ~(1 << line));
    /* if slave PIC, also unmask IRQ2 (cascade) on master */
    if (irq >= 8)
        outb(0x21, inb(0x21) & ~(1 << 2));
}

static void pic_mask_irq(int irq) {
    uint16_t port = (irq < 8) ? 0x21 : 0xA1;
    uint8_t line = (irq < 8) ? irq : irq - 8;
    outb(port, inb(port) | (1 << line));
}

void irq_register(int irq, irq_handler_fn handler) {
    if (irq < 0 || irq >= IRQ_MAX)
        return;
    for (int i = 0; i < IRQ_CHAIN_MAX; i++) {
        if (!irq_handlers[irq][i]) {
            irq_handlers[irq][i] = handler;
            pic_unmask_irq(irq);
            return;
        }
    }
}

void irq_unregister(int irq) {
    if (irq < 0 || irq >= IRQ_MAX)
        return;
    for (int i = 0; i < IRQ_CHAIN_MAX; i++)
        irq_handlers[irq][i] = 0;
    pic_mask_irq(irq);
}

static void irq_pit_handler(int irq, processor_context_t *ctx) {
    (void)irq;
    timer_ticks++;
    vbe_ticks++;

    if (multitasking_ready == 0)
        return;

    /* decrement alarm timers for all tasks */
    for (process_control_block_t *t = task_list; t; t = t->next) {
        if (t->alarm_ticks > 0) {
            t->alarm_ticks--;
            if (t->alarm_ticks == 0)
                task_ipc_signal_raise(t, EXIT_SIGALRM);
        }
    }

    /* check blocked select()/poll() waiters */
    poll_waiter_tick();

    if (vbe_ticks >= VBE_TICKS_PER_FRAME) {
        vbe_ticks = 0;
        enqueue(vbe_worker_task);
    }

    if (preempt_count == 0 && current_task->priv == CPU_USER_MODE && current_task->state == PROCESS_STATE_RUNNING) {
        last_quantum_tick++;
        if (last_quantum_tick >= SCHEDULE_QUANTUM) {
            if (ctx->cs != USER_MODE_CODE_SEGMENT)
                return;
            current_task->quanta_used++;
            current_task->priority = task_priority_decay(current_task);
            memcpy(current_task->processor_context, ctx, sizeof(processor_context_t));
            last_quantum_tick = 0;
            task_yield(1);
        }
    }
}

static void irq_keyboard_handler(int irq, processor_context_t *ctx) {
    (void)irq;
    (void)ctx;
    uint8_t scancode = inb(PS2_DATA_PORT);
    handle_scancode(scancode);
}

static void irq_mouse_handler(int irq, processor_context_t *ctx) {
    (void)irq;
    (void)ctx;
    uint8_t mouse_data = inb(PS2_DATA_PORT);

    // Timeout-based resync: if too long between bytes, restart packet
    if (ps2_mouse_packet_index > 0 && (timer_ticks - ps2_mouse_last_byte_tick) > 2) {
        ps2_mouse_packet_index = 0;
    }

    if (ps2_mouse_packet_index == 0) {
        // Byte 0 must have sync bit (bit 3) set
        if (!(mouse_data & 0x08))
            return;
        // If both overflow bits (6,7) are set, likely garbage - discard
        if ((mouse_data & 0xC0) == 0xC0)
            return;
    }

    ps2_mouse_packet[ps2_mouse_packet_index++] = mouse_data;
    ps2_mouse_last_byte_tick = timer_ticks;

    if (ps2_mouse_packet_index == 3) {
        uint8_t buttons = ps2_mouse_packet[0] & 0x07;

        int rel_x = ps2_mouse_packet[1];
        if (ps2_mouse_packet[0] & 0x10) { // x sign bit
            rel_x -= 256;
        }

        int rel_y = ps2_mouse_packet[2];
        if (ps2_mouse_packet[0] & 0x20) { // y sign bit
            rel_y -= 256;
        }

        mouse_x += rel_x;
        mouse_y -= rel_y;  // PS/2 Y is inverted: positive = up, screen Y = down

        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= (int32_t)vbe_info.width)  mouse_x = vbe_info.width - 1;
        if (mouse_y >= (int32_t)vbe_info.height) mouse_y = vbe_info.height - 1;

        uint8_t changed = buttons ^ prev_mouse_buttons;
        if (changed) {
            for (int i = 0; i < 3; i++) {
                uint8_t mask = (1 << i);
                if (changed & mask) {
                    mouse_event_t ev;
                    ev.x = (int16_t)mouse_x;
                    ev.y = (int16_t)mouse_y;
                    ev.buttons = buttons;
                    ev.event_type = (buttons & mask) ? MOUSE_EVENT_BUTTON_DOWN : MOUSE_EVENT_BUTTON_UP;
                    dev_mouse_push_event(&ev);
                }
            }
            prev_mouse_buttons = buttons;
        }

        // Always emit a move event so userspace can track cursor position
        if (rel_x != 0 || rel_y != 0) {
            mouse_event_t ev;
            ev.x = (int16_t)mouse_x;
            ev.y = (int16_t)mouse_y;
            ev.buttons = buttons;
            ev.event_type = MOUSE_EVENT_MOVE;
            dev_mouse_push_event(&ev);
        }

        ps2_mouse_packet_index = 0;
    }
}

static void irq_rtc_handler(int irq, processor_context_t *ctx) {
    (void)irq;
    (void)ctx;
    outb(CMOS_STATUS_REGISTER_A, CMOS_RTC_STATUS_C);
    inb(CMOS_STATUS_REGISTER_B);

    rtc_time_t t = {0};
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

    //printf("time: %d:%d:%d %d/%d/%d%d \n", t.hour,t.minute,t.second,t.day,t.month,t.century,t.year);
    unix_timestamp = rtc_to_unix_timestamp(&t);
}

void irq_handler(int irq, processor_context_t *ctx) {
    if (irq >= 0 && irq < IRQ_MAX) {
        for (int i = 0; i < IRQ_CHAIN_MAX; i++) {
            if (irq_handlers[irq][i])
                irq_handlers[irq][i](irq, ctx);
        }
    }

    if (irq >= 8)
        outb(0xA0, 0x20);  // EOI to slave PIC
    outb(0x20, 0x20);      // EOI to master PIC
}

static void irq_serial_handler(int irq, processor_context_t *ctx) {
    (void)irq;
    (void)ctx;
    dev_serial_irq_handler();
}

void irq_install_defaults(void) {
    irq_register(IRQ_PIT, irq_pit_handler);
    irq_register(IRQ_KEYBOARD, irq_keyboard_handler);
    irq_register(IRQ_MOUSE, irq_mouse_handler);
    irq_register(IRQ_RTC, irq_rtc_handler);
    irq_register(IRQ_SERIAL, irq_serial_handler);
}
