/**
 * @file rm.c
 * @brief File and directory removal utility for Serotonin OS
 *
 * Removes files and directories. Supports -r for recursive removal
 * and -f for force (ignore nonexistent entries, suppress errors).
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief List directory contents (Serotonin OS system call)
 *
 * Fills buf with newline-separated entry names.
 *
 * @param path Directory path to list
 * @param buf Buffer to store listing
 * @param size Buffer size
 * @return Number of bytes written on success, -1 on error
 */
int listdir(const char *path, char *buf, size_t size);

static int force_flag;
static int recursive_flag;

/**
 * @brief Build a full path from directory and entry name
 *
 * @param dir  Parent directory path
 * @param name Entry name
 * @param out  Output buffer
 * @param out_size Size of output buffer
 * @return 0 on success, -1 if path would be too long
 */
static int build_path(const char *dir, const char *name,
                      char *out, size_t out_size) {
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    /* +1 for '/' +1 for '\0' */
    if (dlen + 1 + nlen + 1 > out_size)
        return -1;
    strcpy(out, dir);
    if (dlen > 0 && dir[dlen - 1] != '/')
        out[dlen++] = '/';
    strcpy(out + dlen, name);
    return 0;
}

/**
 * @brief Recursively remove a directory and all its contents
 *
 * @param path Directory path to remove
 * @return 0 on success, 1 on error
 */
static int remove_recursive(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        if (force_flag)
            return 0;
        printf("rm: cannot stat '%s' (errno=%d)\n", path, errno);
        return 1;
    }

    /* If it's not a directory, just unlink */
    if ((st.st_mode & 0170000) != 0040000) {
        if (unlink(path) != 0) {
            if (force_flag)
                return 0;
            printf("rm: cannot remove '%s' (errno=%d)\n", path, errno);
            return 1;
        }
        return 0;
    }

    /* It's a directory: enumerate and recurse */
    char listing[8192];
    int ret = listdir(path, listing, sizeof(listing));
    if (ret < 0) {
        if (force_flag)
            return 0;
        printf("rm: cannot list '%s' (errno=%d)\n", path, errno);
        return 1;
    }

    int status = 0;
    char *p = listing;
    char *end = listing + ret;
    while (p < end) {
        char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len == 0) { p = nl ? nl + 1 : end; continue; }

        char name[256];
        if (len >= sizeof(name)) { p = nl ? nl + 1 : end; continue; }
        memcpy(name, p, len);
        name[len] = '\0';

        /* Skip . and .. */
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            p = nl ? nl + 1 : end;
            continue;
        }

        char child[1024];
        if (build_path(path, name, child, sizeof(child)) < 0) {
            printf("rm: path too long for '%s/%s'\n", path, name);
            status = 1;
        } else {
            status |= remove_recursive(child);
        }

        p = nl ? nl + 1 : end;
    }

    /* Remove the now-empty directory */
    if (rmdir(path) != 0) {
        if (!force_flag) {
            printf("rm: cannot remove directory '%s' (errno=%d)\n",
                   path, errno);
            status = 1;
        }
    }
    return status;
}

/**
 * @brief Main entry point for rm command
 *
 * Usage: rm [-rf] FILE...
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int main(int argc, char **argv) {
    force_flag = 0;
    recursive_flag = 0;
    int i = 1;
    int status = 0;

    /* Parse flags */
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        const char *f = argv[i];
        for (int j = 1; f[j]; j++) {
            if (f[j] == 'r')      recursive_flag = 1;
            else if (f[j] == 'f') force_flag = 1;
            else {
                printf("rm: unknown option '-%c'\n", f[j]);
                return 1;
            }
        }
        i++;
    }

    if (i >= argc) {
        if (force_flag)
            return 0;
        printf("rm: missing operand\n");
        printf("Usage: rm [-rf] FILE...\n");
        return 1;
    }

    for (; i < argc; i++) {
        if (recursive_flag) {
            status |= remove_recursive(argv[i]);
        } else {
            struct stat st;
            if (stat(argv[i], &st) != 0) {
                if (!force_flag) {
                    printf("rm: cannot stat '%s' (errno=%d)\n",
                           argv[i], errno);
                    status = 1;
                }
                continue;
            }
            if ((st.st_mode & 0170000) == 0040000) {
                printf("rm: cannot remove '%s': Is a directory\n", argv[i]);
                status = 1;
                continue;
            }
            if (unlink(argv[i]) != 0) {
                if (!force_flag) {
                    printf("rm: cannot remove '%s' (errno=%d)\n",
                           argv[i], errno);
                    status = 1;
                }
            }
        }
    }

    return status;
}
