/**
 * @file crt0.s
 * @brief C runtime startup code for Serotonin OS
 *
 * This is the entry point for all user-space programs. It:
 * 1. Runs C++ global constructors via __run_init_array
 * 2. Sets up argc, argv, and envp from the stack
 * 3. Calls main(argc, argv, envp)
 * 4. Runs C++ global destructors via __run_fini_array
 * 5. Calls __cxa_finalize for static destructor support
 * 6. Calls exit (flushes stdio) with main's return value
 */

.extern main
.extern exit
.extern __run_init_array
.extern __run_fini_array
.extern __cxa_finalize
.extern environ

/**
 * @brief Program entry point
 *
 * Called by the kernel after loading the program. The stack
 * contains argc at [esp], argv at [esp+4], and envp follows.
 */
.global _start
_start:
    call __run_init_array

    # argc/argv/envp
    movl (%esp), %eax
    lea  4(%esp), %ebx
    lea  8(%esp,%eax,4), %ecx

    # environ = envp
    movl %ecx, environ

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
    call exit
