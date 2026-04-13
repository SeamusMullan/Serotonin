/**
 * @file tail.c
 * @brief Print last N lines of files for Serotonin OS
 *
 * Supports -n NUM flag (default 10), multiple files with headers,
 * and reading from stdin.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int print_last_n_lines(const char *data, size_t len, int n) {
    if (len == 0 || n <= 0) return 0;

    /* Scan backwards to find n newlines */
    int count = 0;
    size_t pos = len;

    /* Skip trailing newline */
    if (pos > 0 && data[pos - 1] == '\n') pos--;

    while (pos > 0 && count < n) {
        pos--;
        if (data[pos] == '\n') count++;
    }

    /* If we found enough newlines, skip past the last one we found */
    if (count >= n && data[pos] == '\n') pos++;

    /* If we hit the start, print from the beginning */
    const char *start = data + pos;
    size_t out_len = len - pos;
    if (out_len > 0) {
        ssize_t w = write(1, start, out_len);
        if (w < 0) {
            printf("tail: write error (errno=%d)\n", errno);
            return 1;
        }
    }
    return 0;
}

static int tail_file(const char *path, int n) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        printf("tail: %s: cannot open (errno=%d)\n", path, errno);
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        printf("tail: %s: cannot stat (errno=%d)\n", path, errno);
        close(fd);
        return 1;
    }

    size_t size = (size_t)st.st_size;
    if (size == 0) {
        close(fd);
        return 0;
    }

    char *buf = malloc(size);
    if (!buf) {
        printf("tail: %s: out of memory\n", path);
        close(fd);
        return 1;
    }

    size_t total = 0;
    while (total < size) {
        ssize_t r = read(fd, buf + total, size - total);
        if (r <= 0) break;
        total += (size_t)r;
    }
    close(fd);

    int status = print_last_n_lines(buf, total, n);
    free(buf);
    return status;
}

static int tail_stdin(int n) {
    size_t cap = 4096;
    size_t len = 0;
    char *buf = malloc(cap);
    if (!buf) {
        printf("tail: out of memory\n");
        return 1;
    }

    for (;;) {
        if (len >= cap) {
            cap *= 2;
            char *tmp = realloc(buf, cap);
            if (!tmp) {
                printf("tail: out of memory\n");
                free(buf);
                return 1;
            }
            buf = tmp;
        }
        ssize_t r = read(0, buf + len, cap - len);
        if (r <= 0) break;
        len += (size_t)r;
    }

    int status = print_last_n_lines(buf, len, n);
    free(buf);
    return status;
}

int main(int argc, char **argv) {
    int n = 10;
    int file_start = 1;

    /* Parse -n NUM */
    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        n = atoi(argv[2]);
        if (n <= 0) {
            printf("tail: invalid number of lines: %s\n", argv[2]);
            return 1;
        }
        file_start = 3;
    }

    int num_files = argc - file_start;

    if (num_files <= 0) {
        return tail_stdin(n);
    }

    int status = 0;
    for (int i = file_start; i < argc; i++) {
        if (num_files > 1) {
            printf("==> %s <==\n", argv[i]);
        }
        status |= tail_file(argv[i], n);
        if (num_files > 1 && i < argc - 1) {
            printf("\n");
        }
    }

    return status;
}
