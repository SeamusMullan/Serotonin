/**
 * @file tee.c
 * @brief Tee utility for Serotonin OS
 *
 * Reads from stdin and writes to stdout and zero or more files.
 * Supports -a flag for append mode.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_FILES 16

static int write_all(int fd, const char *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t w = write(fd, buf + written, len - written);
        if (w < 0) return -1;
        written += (size_t)w;
    }
    return 0;
}

int main(int argc, char **argv) {
    int append = 0;
    int file_start = 1;
    int fds[MAX_FILES];
    int nfds = 0;
    int status = 0;

    /* Parse flags */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') break;
        if (strcmp(argv[i], "--") == 0) {
            file_start = i + 1;
            break;
        }
        for (int j = 1; argv[i][j]; j++) {
            switch (argv[i][j]) {
            case 'a': append = 1; break;
            default:
                printf("tee: unknown option '-%c'\n", argv[i][j]);
                return 1;
            }
        }
        file_start = i + 1;
    }

    /* Open output files */
    int flags = O_CREAT | O_WRONLY | (append ? O_APPEND : O_TRUNC);
    for (int i = file_start; i < argc && nfds < MAX_FILES; i++) {
        int fd = open(argv[i], flags, 0644);
        if (fd < 0) {
            printf("tee: %s: unable to open (errno=%d)\n", argv[i], errno);
            status = 1;
            continue;
        }
        fds[nfds++] = fd;
    }

    /* Copy stdin to stdout and all files */
    char buf[4096];
    for (;;) {
        ssize_t n = read(0, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) {
            printf("tee: read error (errno=%d)\n", errno);
            status = 1;
            break;
        }

        if (write_all(1, buf, (size_t)n) < 0) {
            printf("tee: stdout write error (errno=%d)\n", errno);
            status = 1;
        }

        for (int i = 0; i < nfds; i++) {
            if (write_all(fds[i], buf, (size_t)n) < 0) {
                printf("tee: write error (errno=%d)\n", errno);
                status = 1;
            }
        }
    }

    for (int i = 0; i < nfds; i++) {
        close(fds[i]);
    }

    return status;
}
