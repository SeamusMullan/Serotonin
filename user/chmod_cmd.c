/**
 * @file chmod_cmd.c
 * @brief Change file permissions for Serotonin OS
 *
 * Supports octal mode only (e.g., 755, 644).
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: chmod MODE FILE...\n");
        return 1;
    }

    char *endptr;
    long mode = strtol(argv[1], &endptr, 8);
    if (*endptr != '\0' || mode < 0 || mode > 07777) {
        printf("chmod: invalid mode: %s\n", argv[1]);
        return 1;
    }

    int status = 0;

    for (int i = 2; i < argc; i++) {
        if (chmod(argv[i], (mode_t)mode) < 0) {
            printf("chmod: %s: cannot change mode (errno=%d)\n",
                   argv[i], errno);
            status = 1;
        }
    }

    return status;
}
