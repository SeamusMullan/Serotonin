/**
 * @file touch.c
 * @brief File creation utility for Serotonin OS
 *
 * Creates files if they do not exist. Since no utime syscall is available,
 * this only creates new files rather than updating timestamps on existing ones.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: touch FILE...\n");
        return 1;
    }

    int status = 0;

    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_CREAT | O_WRONLY, 0644);
        if (fd < 0) {
            printf("touch: %s: cannot create (errno=%d)\n", argv[i], errno);
            status = 1;
            continue;
        }
        close(fd);
    }

    return status;
}
