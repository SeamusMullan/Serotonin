/**
 * @file grep.c
 * @brief Fixed-string search utility for Serotonin OS
 *
 * Searches for a fixed string pattern in files or stdin.
 * Supports -i (case-insensitive), -n (line numbers), -v (invert),
 * -c (count only), -l (filenames only) flags.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int opt_i;  /* case insensitive */
static int opt_n;  /* line numbers */
static int opt_v;  /* invert match */
static int opt_c;  /* count only */
static int opt_l;  /* filenames only */

static void str_tolower(char *dst, const char *src, size_t max) {
    size_t i;
    for (i = 0; i < max - 1 && src[i]; i++) {
        char c = src[i];
        if (c >= 'A' && c <= 'Z') c += 32;
        dst[i] = c;
    }
    dst[i] = '\0';
}

static int grep_fd(int fd, const char *name, const char *pattern,
                   int multi_file) {
    char line[4096];
    char lower_line[4096];
    char lower_pat[4096];
    int pos = 0;
    int lineno = 0;
    long count = 0;
    int found_any = 0;

    if (opt_i) {
        str_tolower(lower_pat, pattern, sizeof(lower_pat));
    }

    for (;;) {
        ssize_t n = read(fd, line + pos, 1);
        if (n <= 0) {
            /* Process remaining data in buffer */
            if (pos > 0) {
                line[pos] = '\0';
                lineno++;
                const char *haystack = line;
                if (opt_i) {
                    str_tolower(lower_line, line, sizeof(lower_line));
                    haystack = lower_line;
                }
                const char *pat = opt_i ? lower_pat : pattern;
                int match = (strstr(haystack, pat) != NULL);
                if (opt_v) match = !match;
                if (match) {
                    found_any = 1;
                    count++;
                    if (!opt_c && !opt_l) {
                        if (multi_file) printf("%s:", name);
                        if (opt_n) printf("%d:", lineno);
                        printf("%s\n", line);
                    }
                }
            }
            if (n < 0) {
                printf("grep: %s: read error (errno=%d)\n", name, errno);
                return -1;
            }
            break;
        }

        if (line[pos] == '\n') {
            line[pos] = '\0';
            lineno++;
            const char *haystack = line;
            if (opt_i) {
                str_tolower(lower_line, line, sizeof(lower_line));
                haystack = lower_line;
            }
            const char *pat = opt_i ? lower_pat : pattern;
            int match = (strstr(haystack, pat) != NULL);
            if (opt_v) match = !match;
            if (match) {
                found_any = 1;
                count++;
                if (opt_l) {
                    printf("%s\n", name);
                    return 1;
                }
                if (!opt_c) {
                    if (multi_file) printf("%s:", name);
                    if (opt_n) printf("%d:", lineno);
                    printf("%s\n", line);
                }
            }
            pos = 0;
        } else {
            pos++;
            if (pos >= (int)sizeof(line) - 1) {
                /* Line too long, process what we have */
                line[pos] = '\0';
                lineno++;
                pos = 0;
            }
        }
    }

    if (opt_c) {
        if (multi_file) printf("%s:", name);
        printf("%ld\n", count);
    }

    return found_any ? 1 : 0;
}

int main(int argc, char **argv) {
    int file_start = 1;

    /* Parse flags */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') break;
        if (strcmp(argv[i], "--") == 0) {
            file_start = i + 1;
            break;
        }
        for (int j = 1; argv[i][j]; j++) {
            switch (argv[i][j]) {
            case 'i': opt_i = 1; break;
            case 'n': opt_n = 1; break;
            case 'v': opt_v = 1; break;
            case 'c': opt_c = 1; break;
            case 'l': opt_l = 1; break;
            default:
                printf("grep: unknown option '-%c'\n", argv[i][j]);
                return 1;
            }
        }
        file_start = i + 1;
    }

    if (file_start >= argc) {
        printf("usage: grep [-invcl] PATTERN [FILE...]\n");
        return 1;
    }

    const char *pattern = argv[file_start];
    file_start++;

    int nfiles = argc - file_start;
    int multi_file = (nfiles > 1);
    int found = 0;
    int status = 0;

    if (nfiles == 0) {
        int ret = grep_fd(0, "(stdin)", pattern, 0);
        if (ret < 0) status = 1;
        if (ret > 0) found = 1;
    } else {
        for (int i = file_start; i < argc; i++) {
            int fd = open(argv[i], O_RDONLY);
            if (fd < 0) {
                printf("grep: %s: unable to open (errno=%d)\n",
                       argv[i], errno);
                status = 1;
                continue;
            }
            int ret = grep_fd(fd, argv[i], pattern, multi_file);
            close(fd);
            if (ret < 0) status = 1;
            if (ret > 0) found = 1;
        }
    }

    if (status) return 2;
    return found ? 0 : 1;
}
