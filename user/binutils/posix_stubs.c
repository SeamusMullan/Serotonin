/**
 * @file posix_stubs.c
 * @brief Minimal POSIX stubs for binutils userland ports on Serotonin OS
 *
 * These stubs satisfy link-time dependencies for utilities that do not
 * require the full POSIX semantics at runtime.
 */

#include <errno.h>
typedef struct _dirdesc DIR;
// cppcheck-suppress unusedStructMember
struct dirent { char d_name[256]; };  /* minimal — stubs never return a real entry */
#include <netdb.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../syscall/syscall_table.h"
#include <lib5ht.h>

extern char **environ;

pid_t wait(int *status) {
    return waitpid(-1, status, 0);
}

int access(const char *path, int mode) {
    (void)mode;
    struct stat st;
    if (stat(path, &st) < 0)
        return -1;
    return 0;
}

int fcntl(int fd, int cmd, ...) {
    (void)fd;
    (void)cmd;
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
    switch (name) {
    case _SC_PAGESIZE:
        return 4096;
    case _SC_PHYS_PAGES:
        return (128 * 1024 * 1024) / 4096; /* report 128MB */
    case _SC_AVPHYS_PAGES:
        return (64 * 1024 * 1024) / 4096; /* report 64MB available */
    default:
        errno = EINVAL;
        return -1;
    }
}

/* Override libiberty's lrealpath which has no return statement when
   HAVE_REALPATH is unset (falls off the end → UB/crash).  We can't
   set HAVE_REALPATH because that makes make_relative_prefix() in the
   GCC driver compute wrong paths (gcc at /bin/ vs compiled-in /usr/bin/).
   This simple identity version satisfies canonical_filename_eq() in cc1
   while the driver's wrong relative prefix harmlessly fails access()
   and falls back to the compiled-in standard prefix. */
char *lrealpath(const char *filename) {
    return strdup(filename);
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

static const char *_getenv(const char *name) {
    if (!environ) return NULL;
    size_t len = strlen(name);
    for (char **e = environ; *e; e++) {
        if (strncmp(*e, name, len) == 0 && (*e)[len] == '=')
            return *e + len + 1;
    }
    return NULL;
}

int execvp(const char *file, char *const argv[]) {
    /* If file contains a slash, use it directly */
    if (strchr(file, '/')) {
        execve(file, argv, environ);
        return -1;
    }

    /* Search PATH */
    const char *path = _getenv("PATH");
    if (!path) path = "/usr/bin:/bin";

    size_t flen = strlen(file);
    char buf[1024];

    while (*path) {
        const char *sep = strchr(path, ':');
        size_t dlen = sep ? (size_t)(sep - path) : strlen(path);

        if (dlen + 1 + flen + 1 <= sizeof(buf)) {
            memcpy(buf, path, dlen);
            buf[dlen] = '/';
            memcpy(buf + dlen + 1, file, flen + 1);
            execve(buf, argv, environ);
            /* execve only returns on error — try next dir unless fatal */
            if (errno != ENOENT && errno != EACCES) return -1;
        }

        if (!sep) break;
        path = sep + 1;
    }

    errno = ENOENT;
    return -1;
}

int execv(const char *path, char *const argv[]) {
    return execve(path, argv, environ);
}

int link(const char *oldpath, const char *newpath) {
    (void)oldpath;
    (void)newpath;
    errno = ENOSYS;
    return -1;
}

DIR *opendir(const char *name) {
    (void)name;
    errno = ENOSYS;
    return NULL;
}

struct dirent *readdir(DIR *dirp) {
    (void)dirp;
    errno = ENOSYS;
    return NULL;
}

int closedir(DIR *dirp) {
    (void)dirp;
    errno = ENOSYS;
    return -1;
}

char *realpath(const char *path, char *resolved) {
    if (!path) {
        errno = EINVAL;
        return NULL;
    }
    if (!resolved) {
        resolved = malloc(strlen(path) + 1);
        if (!resolved) return NULL;
    }
    strcpy(resolved, path);
    return resolved;
}

int truncate(const char *path, off_t length) {
    (void)path;
    (void)length;
    errno = ENOSYS;
    return -1;
}

const char *gai_strerror(int errcode) {
    (void)errcode;
    return "gai_strerror not implemented";
}

int fstatat(int dirfd, const char *pathname, struct stat *buf, int flags) {
    (void)dirfd;
    (void)pathname;
    (void)buf;
    (void)flags;
    errno = ENOSYS;
    return -1;
}

int getaddrinfo(const char *node, const char *service,
                const struct addrinfo *hints, struct addrinfo **res) {
    (void)node;
    (void)service;
    (void)hints;
    (void)res;
    return -1;
}

void freeaddrinfo(struct addrinfo *res) {
    (void)res;
}

uint16_t htons(uint16_t hostshort) {
    return ((hostshort & 0xff) << 8) | ((hostshort >> 8) & 0xff);
}

uint16_t ntohs(uint16_t netshort) {
    return htons(netshort);
}

uint32_t htonl(uint32_t hostlong) {
    return ((hostlong & 0xff) << 24) | ((hostlong & 0xff00) << 8) |
           ((hostlong >> 8) & 0xff00) | ((hostlong >> 24) & 0xff);
}

uint32_t ntohl(uint32_t netlong) {
    return htonl(netlong);
}

/* socket, bind, listen, accept, connect, shutdown are in libsyscall.a */

int _gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (tv) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
    }
    return 0;
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen) {
    (void)sockfd;
    (void)level;
    (void)optname;
    (void)optval;
    (void)optlen;
    errno = ENOSYS;
    return -1;
}
