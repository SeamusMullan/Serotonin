/**
 * @file head.c
 * @brief Print first N lines utility for Serotonin OS
 *
 * Prints the first N lines (default 10) of each file.
 * Supports multiple files with headers, and stdin if no files given.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Print the first max_lines lines from a file descriptor
 *
 * @param fd File descriptor to read from
 * @param label File name for error messages
 * @param max_lines Number of lines to output
 * @return 0 on success, 1 on error
 */
static int head_fd(int fd, const char *label, int max_lines) {
    char buf[4096];
    int lines = 0;

    while (lines < max_lines) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n == 0)
            break;
        if (n < 0) {
            printf("head: %s: read error (errno=%d)\n", label, errno);
            return 1;
        }

        for (ssize_t i = 0; i < n && lines < max_lines; i++) {
            putchar(buf[i]);
            if (buf[i] == '\n')
                lines++;
        }
    }
    return 0;
}

/**
 * @brief Main entry point for head command
 *
 * Usage: head [-n NUM] [FILE...]
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int main(int argc, char **argv) {
    int max_lines = 10;
    int i = 1;
    int status = 0;

    /* Parse -n NUM */
    if (i < argc && strcmp(argv[i], "-n") == 0) {
        i++;
        if (i >= argc) {
            printf("head: option '-n' requires an argument\n");
            return 1;
        }
        max_lines = atoi(argv[i]);
        if (max_lines <= 0) {
            printf("head: invalid number of lines '%s'\n", argv[i]);
            return 1;
        }
        i++;
    }

    int num_files = argc - i;

    /* No files: read from stdin */
    if (num_files == 0)
        return head_fd(0, "stdin", max_lines);

    for (int f = 0; f < num_files; f++) {
        const char *path = argv[i + f];

        /* Print header when there are multiple files */
        if (num_files > 1) {
            if (f > 0)
                putchar('\n');
            printf("==> %s <==\n", path);
        }

        if (strcmp(path, "-") == 0) {
            status |= head_fd(0, "stdin", max_lines);
            continue;
        }

        int fd = open(path, O_RDONLY);
        if (fd < 0) {
            printf("head: %s: unable to open (errno=%d)\n", path, errno);
            status = 1;
            continue;
        }

        status |= head_fd(fd, path, max_lines);
        close(fd);
    }

    return status;
}
