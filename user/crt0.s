.global _start
_start:
    movl (%esp), %eax
    lea  4(%esp), %ebx
    lea  8(%esp,%eax,4), %ecx

    pushl %ecx
    pushl %ebx
    pushl %eax
    call main

    movl %eax, %ebx
    movl $0, %eax
    movl $0, %ecx
    movl $0, %edx
    int $0x80
