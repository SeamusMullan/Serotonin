#ifndef _IO_SERIAL
#define _IO_SERIAL

#include "io.h"

#define COM1_BASE 0x3F8
#define COM2_BASE 0x2F8
#define COM3_BASE 0x3E8
#define COM4_BASE 0x2E8
#define COM5_BASE 0x5F8
#define COM6_BASE 0x4F8
#define COM7_BASE 0x5E8
#define COM8_BASE 0x4E8

#define SERIAL_RECV_BUFFER  0
#define SERIAL_TRAN_BUFFER  0
#define SERIAL_INT_ENABLE   1
#define SERIAL_LSB_DIVISOR  0
#define SERIAL_MSB_DIVISOR  1
#define SERIAL_INT_ID       2
#define SERIAL_FIFO_CTRL    2
#define SERIAL_LINE_CTRL    3
#define SERIAL_MODEM_CTRL   4
#define SERIAL_LINE_STATUS  5
#define SERIAL_SCRATCH      7

void serial_init(uint16_t port);
void serial_putchar(uint16_t port, char c);
void serial_puts(uint16_t port, const char* str);
int  serial_data_ready(uint16_t port);
char serial_getchar(uint16_t port);

#endif