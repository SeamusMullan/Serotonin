#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int expect_zero(int rc, const char *label) {
    if (rc != 0) {
        printf("%s failed rc=%d errno=%d\n", label, rc, errno);
        return -1;
    }
    return 0;
}

static int expect_len(int rc, int expected, const char *label) {
    if (rc != expected) {
        printf("%s failed rc=%d expected=%d errno=%d\n", label, rc, expected, errno);
        return -1;
    }
    return 0;
}

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
