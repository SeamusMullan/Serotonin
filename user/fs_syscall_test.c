/**
 * @file fs_syscall_test.c
 * @brief Filesystem system call test program
 *
 * Comprehensive test suite for filesystem operations including
 * mkdir, chdir, getcwd, open, read, write, lseek, close,
 * unlink, and rmdir system calls.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/**
 * @brief Assert that a return code is zero
 *
 * @param rc Return code to check
 * @param label Descriptive label for error messages
 * @return 0 if rc is 0, -1 otherwise (prints error)
 */
static int expect_zero(int rc, const char *label) {
    if (rc != 0) {
        printf("%s failed rc=%d errno=%d\n", label, rc, errno);
        return -1;
    }
    return 0;
}

/**
 * @brief Assert that a return code matches expected value
 *
 * @param rc Return code to check
 * @param expected Expected value
 * @param label Descriptive label for error messages
 * @return 0 if rc equals expected, -1 otherwise (prints error)
 */
static int expect_len(int rc, int expected, const char *label) {
    if (rc != expected) {
        printf("%s failed rc=%d expected=%d errno=%d\n", label, rc, expected, errno);
        return -1;
    }
    return 0;
}

/**
 * @brief Main entry point for filesystem test
 *
 * Runs through a series of filesystem operations:
 * 1. Creates a test directory
 * 2. Changes into the directory
 * 3. Creates, writes, reads, and deletes a file
 * 4. Cleans up by removing the test directory
 *
 * @return 0 on success, 1 on any test failure
 */
int main(void) {
    char cwd[256];
    const char *dir = "fstest";
    const char *file = "hello.txt";
    const char *data = "serotonin-fs\n";
    char buf[32];

    printf("fs syscalls test start\n");

    if (expect_zero(mkdir(dir, 0755), "mkdir")) return 1;
    if (expect_zero(chdir(dir), "chdir")) return 1;

    if (!getcwd(cwd, sizeof(cwd))) {
        printf("getcwd failed errno=%d\n", errno);
        return 1;
    }
    printf("cwd=%s\n", cwd);

    int fd = open(file, O_CREAT | O_RDWR | O_TRUNC, 0644);
    if (fd < 3) {
        printf("open failed fd=%d errno=%d\n", fd, errno);
        return 1;
    }

    if (expect_len(write(fd, data, strlen(data)), (int)strlen(data), "write")) return 1;
    if (expect_zero(lseek(fd, 0, SEEK_SET), "lseek")) return 1;
    if (expect_len(read(fd, buf, sizeof(buf)), (int)strlen(data), "read")) return 1;

    buf[strlen(data)] = '\0';
    printf("read back: %s", buf);

    if (expect_zero(close(fd), "close")) return 1;
    if (expect_zero(unlink(file), "unlink")) return 1;

    if (expect_zero(chdir(".."), "chdir ..")) return 1;
    if (expect_zero(rmdir(dir), "rmdir")) return 1;

    printf("fs syscalls test ok\n");
    return 0;
}
