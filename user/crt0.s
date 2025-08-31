.global _start
_start:
    movl (%esp), %eax
    lea  4(%esp), %ebx
    lea  8(%esp,%eax,4), %ecx

    pushl %ecx
    pushl %ebx
    pushl %eax
    call main

    pushl $0
    pushl $15
    int $0x80
