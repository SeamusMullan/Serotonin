/**
 * @file lua_stubs.c
 * @brief Stub implementations for unimplemented system functions
 * 
 * Provides placeholder implementations for POSIX functions that are
 * not yet implemented in Serotonin OS. These stubs set errno to ENOSYS
 * and return appropriate error values.
 */

#include <sys/times.h>
#include <sys/time.h>
#include <errno.h>

/**
 * @brief Get current time (stub implementation)
 * 
 * @param tv Pointer to timeval structure (unused)
 * @param tz Pointer to timezone structure (unused)
 * @return -1 (always fails with ENOSYS)
 */
int gettimeofday(struct timeval *tv, void *tz) {
    (void)tv; (void)tz;
    errno = ENOSYS;
    return -1;
}

/**
 * @brief Get process times (stub implementation)
 * 
 * @param buf Pointer to tms structure (unused)
 * @return (clock_t)-1 (always fails with ENOSYS)
 */
clock_t times(struct tms *buf) {
    (void)buf;
    errno = ENOSYS;
    return (clock_t)-1;
}

/**
 * @brief Delete a file (stub implementation)
 * 
 * @param path Path to file (unused)
 * @return -1 (always fails with ENOSYS)
 */
int unlink(const char *path) {
    (void)path;
    errno = ENOSYS;
    return -1;
}

/**
 * @brief Create a hard link (stub implementation)
 * 
 * @param old Existing file path (unused)
 * @param new New link path (unused)
 * @return -1 (always fails with ENOSYS)
 */
int link(const char *old, const char *new) {
    (void)old; (void)new;
    errno = ENOSYS;
    return -1;
}