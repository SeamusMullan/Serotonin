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
#include "syscall_table.h"
#include "lib5ht/lib5ht.h"

int errno;

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

int signal(int sig, void* handler) {
    return do_syscall(SYSTEM_CALL_SIGNAL, (uint32_t)sig, (uint32_t)handler, 0);
}

int raise(int sig) {
    return kill(getpid(), sig);
}

int pause(void) {
    return do_syscall(SYSTEM_CALL_PAUSE, 0, 0, 0);
}

int shm_create(size_t size) {
    return do_syscall(SYSTEM_CALL_SHM_CREATE, (uint32_t)size, 0, 0);
}

void *shm_map(int shm_id) {
    return (void*)do_syscall(SYSTEM_CALL_SHM_MAP, (uint32_t)shm_id, 0, 0);
}

int shm_unmap(void* addr) {
    return do_syscall(SYSTEM_CALL_SHM_UNMAP, (uint32_t)addr, 0, 0);
}

int mkdir(const char *path, mode_t mode) {
    (void)mode;
    return do_syscall(SYSTEM_CALL_MKDIR, (uint32_t)path, 0, 0);
}

int rmdir(const char *path) {
    return do_syscall(SYSTEM_CALL_RMDIR, (uint32_t)path, 0, 0);
}

int chdir(const char *path) {
    return do_syscall(SYSTEM_CALL_CHDIR, (uint32_t)path, 0, 0);
}

char *getcwd(char *buf, size_t size) {
    int ret = do_syscall(SYSTEM_CALL_GETCWD, (uint32_t)buf, (uint32_t)size, 0);
    if (ret != 0) {
        return NULL;
    }
    return buf;
}

int unlink(const char *path) {
    return do_syscall(SYSTEM_CALL_UNLINK, (uint32_t)path, 0, 0);
}

/**
 * @brief Initialize function (called before main)
 * 
 * Empty initialization function for runtime setup.
 */
void _init(void) {}
