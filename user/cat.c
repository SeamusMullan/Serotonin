/**
 * @file cat.c
 * @brief File concatenation utility for Serotonin OS
 *
 * A simple implementation of the cat command that reads one or more files
 * and writes their contents to stdout. If no files are provided, it reads
 * from stdin.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int copy_fd(int fd, const char *label) {
    char buf[4096];

    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n == 0) {
            return 0;
        }
        if (n < 0) {
            printf("cat: %s: read error (errno=%d)\n", label, errno);
            return 1;
        }

        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(1, buf + written, (size_t)(n - written));
            if (w < 0) {
                printf("cat: write error (errno=%d)\n", errno);
                return 1;
            }
            written += w;
        }
    }
}

int main(int argc, char **argv) {
    int status = 0;

    if (argc == 1) {
        return copy_fd(0, "stdin");
    }

    for (int i = 1; i < argc; i++) {
        const char *path = argv[i];
        if (strcmp(path, "-") == 0) {
            status |= copy_fd(0, "stdin");
            continue;
        }

        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            printf("cat: %s: unable to open (errno=%d)\n", path, errno);
            status = 1;
            continue;
        }

        status |= copy_fd(fd, path);
        close(fd);
    }

    return status;
}
