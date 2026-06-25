/**
 * @file echo.c
 * @brief Echo utility for Serotonin OS
 *
 * Prints arguments to stdout, optionally interpreting escape sequences.
 * Supports -n (no trailing newline) and -e (interpret escapes) flags.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief Write a string interpreting C-style escape sequences
 *
 * Handles \n, \t, \\, and \0.
 *
 * @param s The string to interpret and write
 */
static void write_escaped(const char *s) {
    const char *p = s;
    while (*p) {
        if (*p == '\\' && p[1]) {
            switch (p[1]) {
            case 'n':  putchar('\n'); p += 2; break;
            case 't':  putchar('\t'); p += 2; break;
            case '\\': putchar('\\'); p += 2; break;
            case '0':  putchar('\0'); p += 2; break;
            default:   putchar(*p);   p += 1; break;
            }
        } else {
            putchar(*p);
            p++;
        }
    }
}

/**
 * @brief Main entry point for echo command
 *
 * Usage: echo [-n] [-e] [STRING...]
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success
 */
int main(int argc, char **argv) {
    int no_newline = 0;
    int escape = 0;
    int i = 1;

    /* Parse flags */
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        const char *f = argv[i];
        /* Validate that the flag contains only recognised option chars */
        int valid = 1;
        for (int j = 1; f[j]; j++) {
            if (f[j] != 'n' && f[j] != 'e') {
                valid = 0;
                break;
            }
        }
        if (!valid)
            break;
        for (int j = 1; f[j]; j++) {
            if (f[j] == 'n') no_newline = 1;
            if (f[j] == 'e') escape = 1;
        }
        i++;
    }

    /* Print arguments */
    for (; i < argc; i++) {
        if (escape) {
            write_escaped(argv[i]);
        } else {
            fputs(argv[i], stdout);
        }
        if (i + 1 < argc)
            putchar(' ');
    }

    if (!no_newline)
        putchar('\n');

    return 0;
}
