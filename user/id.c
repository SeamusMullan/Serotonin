/**
 * @file id.c
 * @brief Print user and group IDs for Serotonin OS
 *
 * Prints the real and effective user and group IDs in numeric form.
 */

#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("uid=%u gid=%u euid=%u egid=%u\n",
           (unsigned)getuid(),
           (unsigned)getgid(),
           (unsigned)geteuid(),
           (unsigned)getegid());
    return 0;
}
