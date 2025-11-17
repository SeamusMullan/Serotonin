/**
 * @file syscall.c
 * @brief System call interface for Serotonin OS
 * 
 * Provides user-space wrappers for kernel system calls using the
 * interrupt-based system call mechanism (int 0x80). Each function
 * invokes the appropriate system call and handles error conditions.
 */

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

/**
 * @brief Terminate the calling process
 * 
 * This function does not return.
 * 
 * @param status Exit status code
 */
void _exit(int status) {
    do_syscall(SYSTEM_CALL_EXIT, status, 0, 0);
    for (;;);
}

/**
 * @brief Write data to a file descriptor
 * 
 * @param fd File descriptor
 * @param buf Buffer containing data to write
 * @param count Number of bytes to write
 * @return Number of bytes written on success, -1 on error (errno set)
 */
int write(int fd, const void *buf, size_t count) {
    return do_syscall(SYSTEM_CALL_WRITE, fd, (uintptr_t)buf, count);
}

/**
 * @brief Read data from a file descriptor
 * 
 * @param fd File descriptor
 * @param buf Buffer to store read data
 * @param count Maximum number of bytes to read
 * @return Number of bytes read on success, -1 on error (errno set)
 */
int read(int fd, void *buf, size_t count) {
    return do_syscall(SYSTEM_CALL_READ, fd, (uintptr_t)buf, count);
}

/**
 * @brief Open a file
 * 
 * @param path Path to the file
 * @param flags Open flags (O_RDONLY, O_WRONLY, O_RDWR, etc.)
 * @param ... Optional mode argument for file creation
 * @return File descriptor on success, -1 on error (errno set)
 */
int open(const char *path, int flags, ...) {
    return do_syscall(SYSTEM_CALL_OPEN, (uintptr_t)path, flags, 0);
}

/**
 * @brief Close a file descriptor
 * 
 * @param fd File descriptor to close
 * @return 0 on success, -1 on error (errno set)
 */
int close(int fd) {
    return do_syscall(SYSTEM_CALL_CLOSE, fd, 0, 0);
}

/**
 * @brief Reposition file offset
 * 
 * @param fd File descriptor
 * @param offset Offset value
 * @param whence Reference point (SEEK_SET, SEEK_CUR, SEEK_END)
 * @return New offset on success, -1 on error (errno set)
 */
off_t lseek(int fd, off_t offset, int whence) {
    return do_syscall(SYSTEM_CALL_LSEEK, fd, offset, whence);
}

/**
 * @brief Get file status by file descriptor
 * 
 * @param fd File descriptor
 * @param st Pointer to stat structure to fill
 * @return 0 on success, -1 on error (errno set)
 */
int fstat(int fd, struct stat *st) {
    return do_syscall(SYSTEM_CALL_FSTAT, fd, (uintptr_t)st, 0);
}

/**
 * @brief Check if file descriptor refers to a terminal
 * 
 * @param fd File descriptor
 * @return 1 if terminal, 0 if not, -1 on error (errno set)
 */
int isatty(int fd) {
    return do_syscall(SYSTEM_CALL_TTY, fd, 0, 0);
}

/**
 * @brief Get current process ID
 * 
 * @return Process ID
 */
pid_t getpid(void) {
    return do_syscall(SYSTEM_CALL_GETPID, 0, 0, 0);
}

/**
 * @brief Send signal to a process
 * 
 * @param pid Process ID to send signal to
 * @param sig Signal number
 * @return 0 on success, -1 on error (errno set)
 */
int kill(pid_t pid, int sig) {
    return do_syscall(SYSTEM_CALL_KILL, pid, sig, 0);
}

/**
 * @brief Change data segment size (heap allocation)
 * 
 * @param incr Number of bytes to increment (positive) or decrement (negative)
 * @return Previous program break on success, (void *)-1 on error
 */
void *sbrk(ptrdiff_t incr) {
    int ret = do_syscall(SYSTEM_CALL_SBRK, incr, 0, 0);
    if (ret == -1)
        return (void *)-1;
    return (void *)ret;
}

/**
 * @brief Create a new process by duplicating the current process
 * 
 * @return 0 in child process, child PID in parent process, -1 on error
 */
pid_t fork() {
    return do_syscall(SYSTEM_CALL_FORK, 0, 0, 0);
}

/**
 * @brief Wait for process to change state
 * 
 * @param pid Process ID to wait for
 * @param status Pointer to store exit status
 * @return Process ID of terminated child, -1 on error (errno set)
 */
int waitpid(pid_t pid, int *status) {
    return do_syscall(SYSTEM_CALL_WAITPID, pid, (uint32_t)status, 0);
}

/**
 * @brief Execute a program
 * 
 * Replaces the current process image with a new program.
 * This function only returns on error.
 * 
 * @param name Path to the executable
 * @param argv Argument vector (NULL-terminated)
 * @param envp Environment variables (NULL-terminated)
 * @return -1 on error (errno set), does not return on success
 */
int execve(const char *name, char *const argv[], char *const envp[]) {
    return do_syscall(SYSTEM_CALL_EXECVE, (uint32_t)name, (uint32_t)argv, (uint32_t)envp);
}

int gettimeofday(struct timeval *tv, void *tz) {
    return do_syscall(SYSTEM_CALL_TOD, (uint32_t)tv, (uint32_t)tz, 0);
}

/**
 * @brief Initialize function (called before main)
 * 
 * Empty initialization function for runtime setup.
 */
void _init(void) {}