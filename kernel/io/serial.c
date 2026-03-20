#include "serial.h"
#include "io.h"
#include "../stdio/stdio.h"

void serial_init(uint16_t port) {
    outb(port + SERIAL_INT_ENABLE, 0x00);    // Disable all interrupts
    outb(port + SERIAL_LINE_CTRL, 0x80);     // Enable DLAB (set baud rate divisor)
    outb(port + SERIAL_LSB_DIVISOR, 0x03);   // Set divisor to 3 (lo byte) 38400 baud
    outb(port + SERIAL_MSB_DIVISOR, 0x00);   //                  (hi byte)
    outb(port + SERIAL_LINE_CTRL, 0x03);     // 8 bits, no parity, one stop bit
    outb(port + SERIAL_FIFO_CTRL, 0xC7);     // Enable FIFO, clear them, with 14-byte threshold
    outb(port + SERIAL_MODEM_CTRL, 0x0B);    // IRQs enabled, RTS/DSR set

    // Test serial chip (send byte 0xAE and check if serial returns same byte)
    outb(port + SERIAL_MODEM_CTRL, 0x1E);    // Set
    outb(port + SERIAL_TRAN_BUFFER, 0xAE);   // Send test byte 0xAE
    if (inb(port + SERIAL_RECV_BUFFER) != 0xAE) {
        printfs(PRINT_STATUS_ERROR,"Serial port 0x%x not functioning\n", port);
        return;
    }

    printf("its working??\n");

    outb(port + SERIAL_MODEM_CTRL, 0x0F);    // Set normal operation mode

    // Enable receive data available interrupt
    outb(port + SERIAL_INT_ENABLE, 0x01);
}

void serial_putchar(uint16_t port, char c) {
    io_wait();
    outb(port, c);
}

void serial_puts(uint16_t port, const char* str) {
    while (*str) {
        serial_putchar(port, (char)*str++);
    }
}

int serial_data_ready(uint16_t port) {
    return inb(port + SERIAL_LINE_STATUS) & 0x01;
}

char serial_getchar(uint16_t port) {
    return (char)inb(port + SERIAL_RECV_BUFFER);
}