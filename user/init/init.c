void _start() {
    asm volatile(
        "movl $0, %eax\n\t"
        "movl $15, %ebx\n\t"
        "movl $0, %ecx\n\t"
        "movl $0, %edx\n\t"
        "int $0x80\n\t"
    );
}