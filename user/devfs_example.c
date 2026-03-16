/*
 * devfs_example.c
 * Simple devfs client to read /dev/example/number and write /dev/example/write.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char buf[32];
    int fd = open("/dev/example/number", O_RDONLY);
    printf("/dev/example/number fd:%d\n",fd);
    if (fd < 0) {
        printf("open number failed fd=%d errno=%d\n", fd, errno);
        return 1;
    }

    int n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        printf("read number failed errno=%d\n", errno);
        close(fd);
        return 1;
    }
    buf[n] = '\0';
    printf("read %d bytes, number read: %s\n", n, buf);
    close(fd);

    fd = open("/dev/example/write", O_WRONLY);
    printf("/dev/example/write fd:%d\n",fd);
    if (fd < 0) {
        printf("open write failed fd=%d errno=%d\n", fd, errno);
        return 1;
    }

    const char *msg = "hello from user\n";
    int w = write(fd, msg, (int)strlen(msg));
    if (w < 0) {
        printf("write failed errno=%d\n", errno);
        close(fd);
        return 1;
    }
    close(fd);

    return 0;
}
