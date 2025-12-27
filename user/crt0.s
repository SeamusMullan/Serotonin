.extern main
.extern _exit
.extern __run_init_array
.extern __run_fini_array
.extern __cxa_finalize

.global _start
_start:
    call __run_init_array

    # argc/argv/envp
    movl (%esp), %eax
    lea  4(%esp), %ebx
    lea  8(%esp,%eax,4), %ecx

    # main(argc,argv,envp)
    pushl %ecx
    pushl %ebx
    pushl %eax
    call main

    # 7 grand dead
    movl %eax, %ebx   
    call __run_fini_array
    pushl $0
    call __cxa_finalize
    pushl %ebx
    call _exit
