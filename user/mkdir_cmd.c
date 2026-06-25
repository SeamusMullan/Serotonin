/**
 * @file mkdir_cmd.c
 * @brief Directory creation utility for Serotonin OS
 *
 * Creates one or more directories. With -p, creates parent directories
 * as needed and does not error if the directory already exists.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief Create a directory and all missing parent components
 *
 * Walks the path component by component, calling mkdir() for each.
 * Ignores EEXIST so that existing intermediate directories are fine.
 *
 * @param path The full directory path to create
 * @return 0 on success, 1 on error
 */
static int mkdir_parents(const char *path) {
    char tmp[1024];
    size_t len = strlen(path);

    if (len == 0 || len >= sizeof(tmp)) {
        printf("mkdir: path too long or empty\n");
        return 1;
    }

    strcpy(tmp, path);

    /* Strip trailing slash */
    if (len > 1 && tmp[len - 1] == '/')
        tmp[--len] = '\0';

    for (size_t i = 1; i <= len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\0') {
            char saved = tmp[i];
            tmp[i] = '\0';

            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                printf("mkdir: cannot create '%s' (errno=%d)\n", tmp, errno);
                return 1;
            }

            tmp[i] = saved;
        }
    }
    return 0;
}

/**
 * @brief Main entry point for mkdir command
 *
 * Usage: mkdir [-p] DIR...
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int main(int argc, char **argv) {
    int parents = 0;
    int i = 1;
    int status = 0;

    if (i < argc && strcmp(argv[i], "-p") == 0) {
        parents = 1;
        i++;
    }

    if (i >= argc) {
        printf("mkdir: missing operand\n");
        printf("Usage: mkdir [-p] DIR...\n");
        return 1;
    }

    for (; i < argc; i++) {
        if (parents) {
            status |= mkdir_parents(argv[i]);
        } else {
            if (mkdir(argv[i], 0755) != 0) {
                printf("mkdir: cannot create '%s' (errno=%d)\n",
                       argv[i], errno);
                status = 1;
            }
        }
    }

    return status;
}
