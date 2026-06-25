/**
 * @file sort.c
 * @brief Sort lines of text for Serotonin OS
 *
 * Reads lines from files or stdin, sorts them, and prints to stdout.
 * Supports -r (reverse) and -n (numeric) flags.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int flag_reverse;
static int flag_numeric;

static int cmp_lines(const void *a, const void *b) {
    const char *la = *(const char **)a;
    const char *lb = *(const char **)b;
    int result;

    if (flag_numeric) {
        long na = atol(la);
        long nb = atol(lb);
        result = (na > nb) - (na < nb);
    } else {
        result = strcmp(la, lb);
    }

    return flag_reverse ? -result : result;
}

static char **lines;
static size_t nlines;
static size_t cap;

static int add_line(const char *line) {
    if (nlines >= cap) {
        size_t newcap = cap ? cap * 2 : 256;
        char **tmp = realloc(lines, newcap * sizeof(char *));
        if (!tmp) {
            printf("sort: out of memory (errno=%d)\n", errno);
            return 1;
        }
        lines = tmp;
        cap = newcap;
    }
    lines[nlines] = strdup(line);
    if (!lines[nlines]) {
        printf("sort: out of memory (errno=%d)\n", errno);
        return 1;
    }
    nlines++;
    return 0;
}

static int read_file(FILE *fp) {
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n')
            buf[len - 1] = '\0';
        if (add_line(buf))
            return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    int status = 0;
    int file_start = 1;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-')
            break;
        if (strcmp(argv[i], "--") == 0) {
            file_start = i + 1;
            break;
        }
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'r')
                flag_reverse = 1;
            else if (*p == 'n')
                flag_numeric = 1;
            else {
                printf("sort: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
        file_start = i + 1;
    }

    if (file_start >= argc) {
        status = read_file(stdin);
    } else {
        for (int i = file_start; i < argc; i++) {
            if (strcmp(argv[i], "-") == 0) {
                status |= read_file(stdin);
                continue;
            }
            FILE *fp = fopen(argv[i], "r");
            if (!fp) {
                printf("sort: %s: unable to open (errno=%d)\n", argv[i], errno);
                status = 1;
                continue;
            }
            status |= read_file(fp);
            fclose(fp);
        }
    }

    if (nlines > 0) {
        qsort(lines, nlines, sizeof(char *), cmp_lines);
        for (size_t i = 0; i < nlines; i++) {
            puts(lines[i]);
            free(lines[i]);
        }
    }
    free(lines);

    return status;
}
