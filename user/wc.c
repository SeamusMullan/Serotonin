/**
 * @file wc.c
 * @brief Word, line, and byte counting utility for Serotonin OS
 *
 * Counts lines, words, and bytes in files or stdin.
 * Supports -l (lines), -w (words), -c (bytes) flags.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int flag_lines;
static int flag_words;
static int flag_bytes;

static int is_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r'
        || c == '\v' || c == '\f';
}

static void print_counts(long lines, long words, long bytes, const char *name) {
    if (flag_lines) printf("%7ld", lines);
    if (flag_words) printf("%7ld", words);
    if (flag_bytes) printf("%7ld", bytes);
    if (name) {
        printf(" %s", name);
    }
    printf("\n");
}

static int count_fd(int fd, const char *name, long *tl, long *tw, long *tb) {
    char buf[4096];
    long lines = 0, words = 0, bytes = 0;
    int in_word = 0;

    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) {
            printf("wc: %s: read error (errno=%d)\n", name, errno);
            return 1;
        }
        bytes += n;
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] == '\n') lines++;
            if (is_space(buf[i])) {
                in_word = 0;
            } else {
                if (!in_word) words++;
                in_word = 1;
            }
        }
    }

    print_counts(lines, words, bytes, name);
    *tl += lines;
    *tw += words;
    *tb += bytes;
    return 0;
}

int main(int argc, char **argv) {
    int file_start = 1;

    /* Parse flags */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') break;
        for (int j = 1; argv[i][j]; j++) {
            switch (argv[i][j]) {
            case 'l': flag_lines = 1; break;
            case 'w': flag_words = 1; break;
            case 'c': flag_bytes = 1; break;
            default:
                printf("wc: unknown option '-%c'\n", argv[i][j]);
                return 1;
            }
        }
        file_start = i + 1;
    }

    /* Default: show all */
    if (!flag_lines && !flag_words && !flag_bytes) {
        flag_lines = flag_words = flag_bytes = 1;
    }

    long tl = 0, tw = 0, tb = 0;
    int status = 0;
    int nfiles = argc - file_start;

    if (nfiles == 0) {
        /* Read from stdin */
        status = count_fd(0, NULL, &tl, &tw, &tb);
    } else {
        for (int i = file_start; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                printf("wc: %s: unable to open (errno=%d)\n", argv[i], errno);
                status = 1;
                continue;
            }
            status |= count_fd(fd, argv[i], &tl, &tw, &tb);
            close(fd);
        }
        if (nfiles > 1) {
            print_counts(tl, tw, tb, "total");
        }
    }

    return status;
}
