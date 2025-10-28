#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

int errno;

/** System call number for process exit */
enum {
    SYSTEM_CALL_EXIT     = 0,
    SYSTEM_CALL_WRITE    = 1,   /**< Write to file descriptor */
    SYSTEM_CALL_READ     = 2,   /**< Read from file descriptor */
    SYSTEM_CALL_EXECVE   = 3,   /**< Execute program */
    SYSTEM_CALL_FORK     = 4,   /**< Fork process */
    SYSTEM_CALL_GETPID   = 5,   /**< Get process ID */
    SYSTEM_CALL_TTY      = 6,   /**< Check if file descriptor is a TTY */
    SYSTEM_CALL_OPEN     = 7,   /**< Open file */
    SYSTEM_CALL_CLOSE    = 8,   /**< Close file descriptor */
    SYSTEM_CALL_WAITPID  = 9,   /**< Wait for process to change state */
    SYSTEM_CALL_TOD      = 10,  /**< Get time of day */
    SYSTEM_CALL_SBRK     = 11,  /**< Change data segment size */
    SYSTEM_CALL_ENVIRON  = 12,  /**< Get environment variables */
    SYSTEM_CALL_LINK     = 13,  /**< Create hard link */
    SYSTEM_CALL_LSEEK    = 14,  /**< Reposition file offset */
    SYSTEM_CALL_STAT     = 15,  /**< Get file status */
    SYSTEM_CALL_FSTAT    = 16,  /**< Get file status by descriptor */
    SYSTEM_CALL_KILL     = 17   /**< Send signal to process */
};


/**
 * @brief Execute a system call via interrupt 0x80
 * 
 * Low-level function that performs the actual system call by triggering
 * interrupt 0x80 with the appropriate register values.
 * 
 * @param num System call number
 * @param arg1 First argument
 * @param arg2 Second argument
 * @param arg3 Third argument
 * @return System call return value, or sets errno and returns error code on failure
 */
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
        return errno;
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

pid_t fork() {
    return do_syscall(SYSTEM_CALL_FORK, 0, 0, 0);
}

int waitpid(pid_t pid, int *status) {
    return do_syscall(SYSTEM_CALL_WAITPID, pid, (uint32_t)status, 0);
}

int execve(const char *name, char *const argv[], char *const envp[]) {
    return do_syscall(SYSTEM_CALL_EXECVE, (uint32_t)name, (uint32_t)argv, (uint32_t)envp);
}

void _init(void) {}