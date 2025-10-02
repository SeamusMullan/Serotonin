#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

int errno;

enum {
    SYSTEM_CALL_EXIT     = 0,
    SYSTEM_CALL_WRITE    = 1,
    SYSTEM_CALL_READ     = 2,
    SYSTEM_CALL_EXECVE   = 3,
    SYSTEM_CALL_FORK     = 4,
    SYSTEM_CALL_GETPID   = 5,
    SYSTEM_CALL_TTY      = 6,
    SYSTEM_CALL_OPEN     = 7,
    SYSTEM_CALL_CLOSE    = 8,
    SYSTEM_CALL_WAITPID  = 9,
    SYSTEM_CALL_TOD      = 10,
    SYSTEM_CALL_SBRK     = 11,
    SYSTEM_CALL_ENVIRON  = 12,
    SYSTEM_CALL_LINK     = 13,
    SYSTEM_CALL_LSEEK    = 14,
    SYSTEM_CALL_STAT     = 15,
    SYSTEM_CALL_FSTAT    = 16,
    SYSTEM_CALL_KILL     = 17
};


static inline int do_syscall(uint32_t num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    register uint32_t eax asm("eax") = num;
    register uint32_t ebx asm("ebx") = arg1;
    register uint32_t ecx asm("ecx") = arg2;
    register uint32_t edx asm("edx") = arg3;

    asm volatile("int $0x80"
                 : "+a"(eax)
                 : "b"(ebx), "c"(ecx), "d"(edx)
                 : "memory");

    if ((int)eax < 0) {
        errno = -(int)eax;
        return -1;
    }
    return eax;
}

void _exit(int status) {
    do_syscall(SYSTEM_CALL_EXIT, status, 0, 0);
    for (;;);
}

int write(int fd, const void *buf, size_t count) {
    return do_syscall(SYSTEM_CALL_WRITE, fd, (uintptr_t)buf, count);
}

int read(int fd, void *buf, size_t count) {
    return do_syscall(SYSTEM_CALL_READ, fd, (uintptr_t)buf, count);
}

int open(const char *path, int flags, ...) {
    return do_syscall(SYSTEM_CALL_OPEN, (uintptr_t)path, flags, 0);
}

int close(int fd) {
    return do_syscall(SYSTEM_CALL_CLOSE, fd, 0, 0);
}

off_t lseek(int fd, off_t offset, int whence) {
    return do_syscall(SYSTEM_CALL_LSEEK, fd, offset, whence);
}

int fstat(int fd, struct stat *st) {
    return do_syscall(SYSTEM_CALL_FSTAT, fd, (uintptr_t)st, 0);
}

int isatty(int fd) {
    return do_syscall(SYSTEM_CALL_TTY, fd, 0, 0);
}

pid_t getpid(void) {
    return do_syscall(SYSTEM_CALL_GETPID, 0, 0, 0);
}

int kill(pid_t pid, int sig) {
    return do_syscall(SYSTEM_CALL_KILL, pid, sig, 0);
}

void *sbrk(ptrdiff_t incr) {
    int ret = do_syscall(SYSTEM_CALL_SBRK, incr, 0, 0);
    if (ret == -1)
        return (void *)-1;
    return (void *)ret;
}

void _init(void) {}