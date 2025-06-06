#include "stdio/stdio.h"
#include "kernel.h"
#include "stdlib/stdlib.h"
#include "fault.h"

void fault_handler(int vector) {
    printf("INT #%d RAISED\n", vector);
    switch (vector) {
        case ISR_DIVIDE_ERROR: 
            kernel_panic("exception - divide by zero");
            break;
        case ISR_INVALID_OPCODE: 
            kernel_panic("exception - invalid opcode"); 
            break;
        case ISR_GENERAL_PROTECTION: 
            kernel_panic("exception - general protection fault"); 
            break;
        case ISR_PAGE_FAULT: 
            kernel_panic("exception - page fault"); 
            break;
        default: 
            kernel_panic("exception - pnknown exception"); 
            break;
    }

    abort();
}
