/**
 * @file tr.c
 * @brief Translate or delete characters for Serotonin OS
 *
 * Reads from stdin, translates or deletes characters based on SET1/SET2.
 * Supports -d (delete) flag and ranges like a-z, A-Z, 0-9.
 */

#include <stdio.h>
#include <string.h>

static int expand_set(const char *spec, char *out, int *len) {
    int n = 0;
    for (int i = 0; spec[i]; i++) {
        if (spec[i + 1] == '-' && spec[i + 2]) {
            unsigned char start = (unsigned char)spec[i];
            unsigned char end = (unsigned char)spec[i + 2];
            if (start <= end) {
                for (unsigned int c = start; c <= end; c++)
                    out[n++] = (char)c;
            } else {
                for (unsigned int c = start; c >= end; c--)
                    out[n++] = (char)c;
            }
            i += 2;
        } else {
            out[n++] = spec[i];
        }
    }
    *len = n;
    return 0;
}

int main(int argc, char **argv) {
    int flag_delete = 0;
    int arg_start = 1;

    if (arg_start < argc && strcmp(argv[arg_start], "-d") == 0) {
        flag_delete = 1;
        arg_start++;
    }

    if (flag_delete) {
        if (arg_start >= argc) {
            printf("tr: missing operand\n");
            printf("Usage: tr [-d] SET1 [SET2]\n");
            return 1;
        }
    } else {
        if (arg_start + 1 >= argc) {
            printf("tr: missing operand\n");
            printf("Usage: tr [-d] SET1 [SET2]\n");
            return 1;
        }
    }

    char set1[256];
    int set1_len = 0;
    expand_set(argv[arg_start], set1, &set1_len);

    if (flag_delete) {
        /* Build delete lookup table */
        unsigned char del[256] = {0};
        for (int i = 0; i < set1_len; i++)
            del[(unsigned char)set1[i]] = 1;

        int c;
        while ((c = getchar()) != EOF) {
            if (!del[c])
                putchar(c);
        }
    } else {
        char set2[256];
        int set2_len = 0;
        expand_set(argv[arg_start + 1], set2, &set2_len);

        /* Build translation table: identity by default */
        unsigned char table[256];
        for (int i = 0; i < 256; i++)
            table[i] = (unsigned char)i;

        for (int i = 0; i < set1_len; i++) {
            unsigned char from = (unsigned char)set1[i];
            /* If set2 is shorter, repeat last char of set2 */
            unsigned char to;
            if (i < set2_len)
                to = (unsigned char)set2[i];
            else if (set2_len > 0)
                to = (unsigned char)set2[set2_len - 1];
            else
                to = from;
            table[from] = to;
        }

        int c;
        while ((c = getchar()) != EOF)
            putchar(table[c]);
    }

    return 0;
}
