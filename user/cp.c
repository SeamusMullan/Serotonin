/**
 * @file cp.c
 * @brief File and directory copy utility for Serotonin OS
 *
 * Copies files, or recursively copies directories with -r.
 * If DEST is an existing directory, the source basename is
 * appended automatically.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief List directory contents (Serotonin OS system call)
 */
int listdir(const char *path, char *buf, size_t size);

/**
 * @brief Extract the basename component from a path
 *
 * @param path Full path
 * @return Pointer into path at the final component
 */
static const char *path_basename(const char *path) {
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

/**
 * @brief Build a full path from directory and name
 */
static int build_path(const char *dir, const char *name,
                      char *out, size_t out_size) {
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    if (dlen + 1 + nlen + 1 > out_size)
        return -1;
    strcpy(out, dir);
    if (dlen > 0 && dir[dlen - 1] != '/')
        out[dlen++] = '/';
    strcpy(out + dlen, name);
    return 0;
}

/**
 * @brief Check if a path is a directory
 *
 * @param path Path to check
 * @return 1 if directory, 0 otherwise
 */
static int is_dir(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0)
        return 0;
    return (st.st_mode & 0170000) == 0040000;
}

/**
 * @brief Copy a single regular file
 *
 * @param src Source file path
 * @param dst Destination file path
 * @return 0 on success, 1 on error
 */
static int copy_file(const char *src, const char *dst) {
    int fd_in = open(src, O_RDONLY);
    if (fd_in < 0) {
        printf("cp: cannot open '%s' (errno=%d)\n", src, errno);
        return 1;
    }

    int fd_out = open(dst, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd_out < 0) {
        printf("cp: cannot create '%s' (errno=%d)\n", dst, errno);
        close(fd_in);
        return 1;
    }

    char buf[4096];
    int status = 0;
    for (;;) {
        ssize_t n = read(fd_in, buf, sizeof(buf));
        if (n == 0)
            break;
        if (n < 0) {
            printf("cp: read error on '%s' (errno=%d)\n", src, errno);
            status = 1;
            break;
        }
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(fd_out, buf + written, (size_t)(n - written));
            if (w < 0) {
                printf("cp: write error on '%s' (errno=%d)\n", dst, errno);
                status = 1;
                break;
            }
            written += w;
        }
        if (status)
            break;
    }

    close(fd_in);
    close(fd_out);
    return status;
}

/**
 * @brief Recursively copy a directory tree
 *
 * @param src Source directory path
 * @param dst Destination directory path
 * @return 0 on success, 1 on error
 */
static int copy_recursive(const char *src, const char *dst) {
    struct stat st;
    if (stat(src, &st) != 0) {
        printf("cp: cannot stat '%s' (errno=%d)\n", src, errno);
        return 1;
    }

    /* Regular file: just copy */
    if ((st.st_mode & 0170000) != 0040000)
        return copy_file(src, dst);

    /* Directory: create destination, then enumerate and recurse */
    if (mkdir(dst, 0755) != 0 && errno != EEXIST) {
        printf("cp: cannot create directory '%s' (errno=%d)\n", dst, errno);
        return 1;
    }

    char listing[8192];
    int ret = listdir(src, listing, sizeof(listing));
    if (ret < 0) {
        printf("cp: cannot list '%s' (errno=%d)\n", src, errno);
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

        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            p = nl ? nl + 1 : end;
            continue;
        }

        char child_src[1024], child_dst[1024];
        if (build_path(src, name, child_src, sizeof(child_src)) < 0 ||
            build_path(dst, name, child_dst, sizeof(child_dst)) < 0) {
            printf("cp: path too long\n");
            status = 1;
        } else {
            status |= copy_recursive(child_src, child_dst);
        }

        p = nl ? nl + 1 : end;
    }

    return status;
}

/**
 * @brief Main entry point for cp command
 *
 * Usage: cp [-r] SOURCE DEST
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return 0 on success, 1 on error
 */
int main(int argc, char **argv) {
    int recursive = 0;
    int i = 1;

    if (i < argc && strcmp(argv[i], "-r") == 0) {
        recursive = 1;
        i++;
    }

    if (argc - i < 2) {
        printf("cp: missing operand\n");
        printf("Usage: cp [-r] SOURCE DEST\n");
        return 1;
    }

    const char *src = argv[i];
    const char *dst = argv[i + 1];
    char dest_path[1024];

    /* If destination is an existing directory, append source basename */
    if (is_dir(dst)) {
        if (build_path(dst, path_basename(src),
                       dest_path, sizeof(dest_path)) < 0) {
            printf("cp: destination path too long\n");
            return 1;
        }
        dst = dest_path;
    }

    if (recursive) {
        return copy_recursive(src, dst);
    }

    /* Non-recursive: source must not be a directory */
    if (is_dir(src)) {
        printf("cp: -r not specified; omitting directory '%s'\n", src);
        return 1;
    }

    return copy_file(src, dst);
}
