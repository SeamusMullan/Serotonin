void _start() {
    volatile int someint = 0;
    asm volatile("int $0x80");
    while (1) {
        someint++;
    };
}