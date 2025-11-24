.global signal_trampoline
.global signal_trampoline_end
signal_trampoline:
    movl $19, %eax
    int $0x80
signal_trampoline_end: