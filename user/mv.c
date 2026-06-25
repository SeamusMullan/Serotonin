/**
 * @file mv.c
 * @brief Move/rename files for Serotonin OS
 *
 * Implements move as copy + unlink since no rename() syscall exists.
 * If DEST is a directory, the source basename is appended.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int listdir(const char *path, char *buf, size_t size);

static const char *basename_of(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static int copy_and_unlink(const char *src, const char *dst) {
    int fd_src = open(src, O_RDONLY);
    if (fd_src < 0) {
        printf("mv: %s: cannot open source (errno=%d)\n", src, errno);
        return 1;
    }

    int fd_dst = open(dst, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd_dst < 0) {
        printf("mv: %s: cannot create destination (errno=%d)\n", dst, errno);
        close(fd_src);
        return 1;
    }

    char buf[4096];
    int status = 0;

    for (;;) {
        ssize_t n = read(fd_src, buf, sizeof(buf));
        if (n == 0) break;
        if (n < 0) {
            printf("mv: %s: read error (errno=%d)\n", src, errno);
            status = 1;
            break;
        }
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(fd_dst, buf + written, (size_t)(n - written));
            if (w < 0) {
                printf("mv: %s: write error (errno=%d)\n", dst, errno);
                status = 1;
                break;
            }
            written += w;
        }
        if (status) break;
    }

    close(fd_src);
    close(fd_dst);

    if (status) return status;

    if (unlink(src) < 0) {
        printf("mv: %s: cannot remove source (errno=%d)\n", src, errno);
        return 1;
    }

    return 0;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        printf("usage: mv SOURCE DEST\n");
        return 1;
    }

    const char *src = argv[1];
    const char *dst = argv[2];
    char dest_path[1024];

    /* Check if destination is a directory */
    struct stat st;
    if (stat(dst, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR) {
        snprintf(dest_path, sizeof(dest_path), "%s/%s", dst, basename_of(src));
        dst = dest_path;
    }

    return copy_and_unlink(src, dst);
}
