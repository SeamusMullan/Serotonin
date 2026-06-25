/**
 * @file basename_cmd.c
 * @brief Strip directory from path for Serotonin OS
 *
 * Prints the last component of a pathname, optionally removing a suffix.
 */

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: basename PATH [SUFFIX]\n");
        return 1;
    }

    char path[4096];
    strncpy(path, argv[1], sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    /* Strip trailing slashes (but not if path is just "/") */
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
        path[--len] = '\0';

    /* If path is all slashes, result is "/" */
    if (len == 1 && path[0] == '/') {
        puts("/");
        return 0;
    }

    /* Find last slash */
    char *base = strrchr(path, '/');
    if (base)
        base++;
    else
        base = path;

    /* Strip suffix if provided */
    if (argc > 2) {
        const char *suffix = argv[2];
        size_t blen = strlen(base);
        size_t slen = strlen(suffix);
        if (slen > 0 && slen < blen &&
            strcmp(base + blen - slen, suffix) == 0) {
            base[blen - slen] = '\0';
        }
    }

    puts(base);
    return 0;
}
