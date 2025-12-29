#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int listdir(const char *path, char *buf, size_t size);

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";
    char buf[4096];

    errno = 0;
    int ret = listdir(path, buf, sizeof(buf));
    if (errno != 0) {
        printf("ls: failed to list directory (errno=%d)\n", errno);
        return 1;
    }

    if (ret > 0) {
        write(1, buf, (size_t)ret);
    }

    return 0;
}
