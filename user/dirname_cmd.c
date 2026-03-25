/**
 * @file dirname_cmd.c
 * @brief Strip last component from path for Serotonin OS
 *
 * Prints the directory portion of a pathname.
 */

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: dirname PATH\n");
        return 1;
    }

    char path[4096];
    strncpy(path, argv[1], sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    /* Strip trailing slashes (but not if path is just "/") */
    size_t len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
        path[--len] = '\0';

    /* Find last slash */
    char *last = strrchr(path, '/');

    if (!last) {
        /* No slash at all: directory is "." */
        puts(".");
        return 0;
    }

    if (last == path) {
        /* Slash is the first character: directory is "/" */
        puts("/");
        return 0;
    }

    /* Strip trailing slashes from directory part */
    *last = '\0';
    len = strlen(path);
    while (len > 1 && path[len - 1] == '/')
        path[--len] = '\0';

    puts(path);
    return 0;
}
