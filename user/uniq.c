/**
 * @file uniq.c
 * @brief Filter adjacent duplicate lines for Serotonin OS
 *
 * Reads from stdin or a file and filters adjacent duplicate lines.
 * Supports -c (count) and -d (duplicates only) flags.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int flag_count = 0;
    int flag_dupes = 0;
    int file_start = 1;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-')
            break;
        if (strcmp(argv[i], "--") == 0) {
            file_start = i + 1;
            break;
        }
        for (const char *p = argv[i] + 1; *p; p++) {
            if (*p == 'c')
                flag_count = 1;
            else if (*p == 'd')
                flag_dupes = 1;
            else {
                printf("uniq: invalid option -- '%c'\n", *p);
                return 1;
            }
        }
        file_start = i + 1;
    }

    FILE *fp = stdin;
    if (file_start < argc && strcmp(argv[file_start], "-") != 0) {
        fp = fopen(argv[file_start], "r");
        if (!fp) {
            printf("uniq: %s: unable to open (errno=%d)\n", argv[file_start], errno);
            return 1;
        }
    }

    char line[4096];
    char prev[4096];
    int count = 0;
    int have_prev = 0;

    while (fgets(line, sizeof(line), fp)) {
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        if (!have_prev) {
            strcpy(prev, line);
            count = 1;
            have_prev = 1;
            continue;
        }

        if (strcmp(line, prev) == 0) {
            count++;
        } else {
            if (!flag_dupes || count > 1) {
                if (flag_count)
                    printf("%7d %s\n", count, prev);
                else
                    puts(prev);
            }
            strcpy(prev, line);
            count = 1;
        }
    }

    if (have_prev && (!flag_dupes || count > 1)) {
        if (flag_count)
            printf("%7d %s\n", count, prev);
        else
            puts(prev);
    }

    if (fp != stdin)
        fclose(fp);

    return 0;
}
