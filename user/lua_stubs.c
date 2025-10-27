#include <sys/times.h>
#include <sys/time.h>
#include <errno.h>

int gettimeofday(struct timeval *tv, void *tz) {
    (void)tv; (void)tz;
    errno = ENOSYS;
    return -1;
}

clock_t times(struct tms *buf) {
    (void)buf;
    errno = ENOSYS;
    return (clock_t)-1;
}

int unlink(const char *path) {
    (void)path;
    errno = ENOSYS;
    return -1;
}

int link(const char *old, const char *new) {
    (void)old; (void)new;
    errno = ENOSYS;
    return -1;
}