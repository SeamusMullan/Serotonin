/**
 * @file init.c
 * @brief System initialization program for Serotonin OS
 *
 * This is the first user-space program executed by the kernel.
 * It sets up filesystem permissions, creates home directories,
 * ensures /etc/passwd exists, and launches the login program.
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

int listdir(const char *path, char *buf, size_t size);

/**
 * @brief Set all files in /bin to executable (0755)
 */
static void setup_bin_permissions(void) {
    char buf[4096];
    int ret = listdir("/bin", buf, sizeof(buf));
    if (ret <= 0) return;

    char *p = buf;
    char *end = buf + ret;
    while (p < end) {
        char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len > 0 && len < 256) {
            char path[270];
            memcpy(path, "/bin/", 5);
            memcpy(path + 5, p, len);
            path[5 + len] = '\0';
            chmod(path, 0755);
        }
        if (!nl) break;
        p = nl + 1;
    }
}

/**
 * @brief Create a directory if it doesn't exist
 * @return 0 on success or if it already exists
 */
static int ensure_dir(const char *path, mode_t mode) {
    struct stat st;
    if (stat(path, &st) == 0) {
        chmod(path, mode);
        return 0;
    }
    if (mkdir(path, mode) != 0) {
        return -1;
    }
    chmod(path, mode);
    return 0;
}

/**
 * @brief Set up home directories
 */
static void setup_home_directories(void) {
    // /root for the root user
    if (ensure_dir("/root", 0700) != 0) {
        printf("init: warning: could not create /root\n");
    }

    // /home for regular users
    if (ensure_dir("/home", 0755) != 0) {
        printf("init: warning: could not create /home\n");
    }
}

static const char default_passwd[] =
    "root:x:0:0:root:/root:/bin/sh\n"
    "user:x:1000:1000::/home/user:/bin/sh\n";

/**
 * @brief Ensure /etc and /etc/passwd exist
 *
 * If /etc/passwd is missing, create it with default entries.
 * Since the rootfs is FAT32, /etc must be created as a directory.
 */
static void setup_etc_passwd(void) {
    ensure_dir("/etc", 0755);

    struct stat st;
    if (stat("/etc/passwd", &st) == 0) {
        // Already exists, just fix permissions
        chmod("/etc/passwd", 0644);
        return;
    }

    // Create /etc/passwd with default contents
    int fd = open("/etc/passwd", O_CREAT | O_WRONLY);
    if (fd < 0) {
        printf("init: warning: could not create /etc/passwd\n");
        return;
    }
    write(fd, default_passwd, sizeof(default_passwd) - 1);
    close(fd);
    chmod("/etc/passwd", 0644);
}

/**
 * @brief Init process entry point
 *
 * Sets up filesystem permissions, creates home directories,
 * ensures /etc/passwd exists, and executes the login program.
 *
 * @param argc Argument count (unused)
 * @param argv Argument vector (passed to login)
 * @param envp Environment variables (passed to login)
 * @return 1 on error (login failed to load)
 */
int main(int argc, char **argv, char **envp) {
    (void)argc;

    printf("Welcome to \033[1m\033[38;2;122;152;255mSerotonin\033[0m!\n");

    setup_bin_permissions();
    setup_home_directories();
    setup_etc_passwd();

    execve("/bin/login", argv, envp);

    // Fallback to shell if login is not available
    printf("init: /bin/login not found, falling back to shell\n");
    execve("/bin/sh", argv, envp);

    printf("init: failed to load shell!\n");

    return 1;
}
