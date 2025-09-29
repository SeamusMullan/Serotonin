#include "../../kernel/syscall/sys/file.h" // :troll:
#include <stdint.h>

/**
 * @brief Makes a system call.
 *
 * @param arg1 The first argument.
 * @param arg2 The second argument.
 * @param arg3 The third argument.
 * @param arg4 The fourth argument.
 * @return uint32_t The return value of the system call.
 */
int system_call(uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4) {
    int ret;
    asm volatile (
        "int $0x80"
        : "=a" (ret)
        : "a" (arg1),  // eax
          "b" (arg2),  // ebx
          "c" (arg3),  // ecx
          "d" (arg4)   // edx
        : "memory"
    );
    return ret;
}

/**
 * @brief The entry point of the user program.
 */
int main(int argc, char **argv, char **envp) {
    char* strdbg = "== Serotonin System Call Unit Test ==\n";
    system_call(1,0,(uint32_t)strdbg,0);

    strdbg = "Test 1: Checking passing of arguments and environment variables via argc, argv and envp\n";
    system_call(1,0, (uint32_t)strdbg, 0);

    char newline[] = "\n";
    for (int i = 0; i < argc; i++) {
        system_call(1,0,(uint32_t)argv[i],0);
        system_call(1,0,(uint32_t)newline,0);
    }

    for (char **e=envp; *e; e++) {
        system_call(1,0,(uint32_t)*e,0);
        system_call(1,0,(uint32_t)newline,0);
    }

    strdbg = "Test 2.1: Filesystem test, trying open()";
    system_call(1, 0, (uint32_t)strdbg, 0);

    char file[] = "/home/test.txt";
    int fd = system_call(7, (uint32_t)file, 0, 0);
    if (fd > 0) {
        strdbg = ".. PASS";
        system_call(1, 0, (uint32_t)strdbg, 0); 

        strdbg = "\nTest 2.2: Filesystem test, trying read()";
        system_call(1, 0, (uint32_t)strdbg, 0);
        char buf[256];
        int bytes_read = system_call(2, fd, (uint32_t)buf, 256);
        if (bytes_read > 0) {
            strdbg = ".. PASS\nFile contents:\n";
            system_call(1, 0, (uint32_t)strdbg, 0); 
            system_call(1, 0, (uint32_t)buf, 0); 
        } else {
            strdbg = ".. FAIL\n";
            system_call(1, 0, (uint32_t)strdbg, 0); 
        }
        strdbg = "\nTest 2.3: Filesystem test, trying write() then read back";
        system_call(1, 0, (uint32_t)strdbg, 0);
        char write_buf[] = "TestingWorld";
        int bytes_written = system_call(1, fd, (uint32_t)write_buf, 12);
        if (bytes_written > 0) {
            strdbg = ".. PASS\nSeeking to beginning and reading back written data:\n";
            system_call(1, 0, (uint32_t)strdbg, 0); 
            char read_buf[13];
            system_call(14, fd, 0, 0);
            system_call(2, fd, (uint32_t)read_buf, 12);
            system_call(1, 0, (uint32_t)read_buf, 0); 

            strdbg = "\nTest 2.4: Filesystem test, trying fstat()";
            system_call(1, 0, (uint32_t)strdbg, 0);
            struct stat st = {0};
            int stat_res = system_call(16, fd, (uint32_t)&st, 0);
            if (stat_res == 0) {
                strdbg = ".. PASS\n";
                system_call(1, 0, (uint32_t)strdbg, 0); 
            } else {
                strdbg = ".. FAIL\n";
                system_call(1, 0, (uint32_t)strdbg, 0); 
            }

            strdbg = "\nClosing file...";
            system_call(1, 0, (uint32_t)strdbg, 0); 
            system_call(8, fd, 0, 0);
            strdbg = " done.\n";
            system_call(1, 0, (uint32_t)strdbg, 0);
        } else {
            strdbg = ".. FAIL\n";
            system_call(1, 0, (uint32_t)strdbg, 0); 
        }
    } else {
        strdbg = ".. FAIL\n";
        system_call(1, 0, (uint32_t)strdbg, 0); 
    }

    strdbg = "Test 3: Trying fork()\n";
    system_call(1,0,(uint32_t)strdbg,0);
    uint32_t pid = system_call(4,0,0,0);
    if (pid == 0) {
        strdbg = "In child process (pid=0)\n";
        system_call(1,0,(uint32_t)strdbg,0);

        strdbg = "Test 4 (in child): Kernel memory protection test, trying illegal memory access (should fault and kill child)...\n";
        system_call(1,0,(uint32_t)strdbg,0);
        volatile int *ptr = (int*)0xC0000000; // Invalid memory address
        int val = *ptr; // This should cause a segmentation fault in the child process
        (void)val; // Suppress unused variable warning

        strdbg = "If you see this message, the illegal memory access did NOT cause a fault! FAIL.\n";
        system_call(1,0,(uint32_t)strdbg,0);
        system_call(0,0,0,0);
    } else {
        strdbg = "In parent process, waiting for child to exit...\n";
        system_call(1,0,(uint32_t)strdbg,0);
        int status;
        system_call(9,pid,(uint32_t)&status,0);
        strdbg = "Child exited, continuing...\n";
        system_call(1,0,(uint32_t)strdbg,0);
    }

    strdbg = "Test 5: Expanding heap with sbrk()";
    system_call(1,0,(uint32_t)strdbg,0);
    uint32_t* somemem = (uint32_t*)system_call(11,1,0,0);
    *somemem = 0xDEADBEEF;
    if (*somemem == 0xDEADBEEF) {
        strdbg = ".. PASS\n";
    } else {
        strdbg = ".. FAIL\n";
    }
    system_call(1,0,(uint32_t)strdbg,0);

    strdbg = "Test 6: stdin read() test, please type something and press Enter:\n";
    system_call(1,0,(uint32_t)strdbg,0);
    char input_buf[128];
    int input_bytes = system_call(2, 0, (uint32_t)input_buf, 128);
    if (input_bytes > 0) {
        strdbg = "\nYou typed: ";
        system_call(1,0,(uint32_t)strdbg,0);
        system_call(1,0,(uint32_t)input_buf,128);
        strdbg = "\n";
        system_call(1,0,(uint32_t)strdbg,0);
    } else {
        strdbg = "Read from stdin failed!\n";
        system_call(1,0,(uint32_t)strdbg,0);
    }

    strdbg = "== All tests done, exiting. ==\n";
    system_call(1,0,(uint32_t)strdbg,0);

    return 0;
}
