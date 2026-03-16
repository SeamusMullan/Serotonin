/**
 * @file posix_stubs.c
 * @brief Minimal POSIX stubs for binutils userland ports on Serotonin OS
 *
 * These stubs satisfy link-time dependencies for utilities that do not
 * require the full POSIX semantics at runtime.
 */

#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../syscall/syscall_table.h"
#include "../syscall/lib5ht/lib5ht.h"

int access(const char *path, int mode) {
    (void)path;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

int fcntl(int fd, int cmd, ...) {
    (void)fd;
    (void)cmd;
    errno = ENOSYS;
    return -1;
}

mode_t umask(mode_t mask) {
    (void)mask;
    return 0;
}

int chmod(const char *path, mode_t mode) {
    (void)path;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

int fchmod(int fd, mode_t mode) {
    (void)fd;
    (void)mode;
    errno = ENOSYS;
    return -1;
}

long sysconf(int name) {
    (void)name;
    errno = ENOSYS;
    return -1;
}

int utime(const char *path, const void *times) {
    (void)path;
    (void)times;
    errno = ENOSYS;
    return -1;
}

unsigned int sleep(unsigned int seconds) {
    (void)seconds;
    return 0;
}

int execvp(const char *file, char *const argv[]) {
    (void)file;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

int execv(const char *path, char *const argv[]) {
    (void)path;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

int link(const char *oldpath, const char *newpath) {
    (void)oldpath;
    (void)newpath;
    errno = ENOSYS;
    return -1;
}
