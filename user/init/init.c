void _start() {
    volatile int someint = 0;
    asm volatile(
        "movl $69, %eax\n\t"
        "movl $420, %ebx\n\t"
        "movl $1337, %ecx\n\t"
        "movl $9001, %edx\n\t"
        "int $0x80\n\t"
    );
    while (1) {
        someint++;
    };
}