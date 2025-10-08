.extern __libc_init_array
.extern _exit

.global _start
_start:
    movl (%esp), %eax
    lea  4(%esp), %ebx
    lea  8(%esp,%eax,4), %ecx

    pushl %ecx
    pushl %ebx
    pushl %eax
    call main

    pushl %eax
    call _exit
