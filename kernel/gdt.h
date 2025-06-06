// gdt.h
#ifndef _GDT_H
#define _GDT_H

void gdt_flush(uint32_t); 

void init_gdt(void);

#endif