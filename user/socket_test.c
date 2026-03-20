/**
 * @file socket_test.c
 * @brief Unix domain socket test app for Serotonin OS
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include "syscall/sys/socket.h"

int waitpid(pid_t pid, int *status);

static int test_socketpair(void) {
    int sv[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        printf("socketpair failed (errno=%d)\n", errno);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        printf("fork failed (errno=%d)\n", errno);
        return 1;
    }

    if (pid == 0) {
        close(sv[0]);
        const char *msg = "hello from child";
        send(sv[1], msg, strlen(msg), 0);
        close(sv[1]);
        _exit(0);
    }

    close(sv[1]);
    char buf[64];
    memset(buf, 0, sizeof(buf));
    int n = recv(sv[0], buf, sizeof(buf) - 1, 0);
    close(sv[0]);

    int status = 0;
    waitpid(pid, &status);

    if (n <= 0 || strcmp(buf, "hello from child") != 0) {
        printf("socketpair: bad data (n=%d buf='%s')\n", n, buf);
        return 1;
    }
    printf("socketpair: ok\n");
    return 0;
}

static int test_bind_connect(void) {
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd < 0) {
        printf("socket failed (errno=%d)\n", errno);
        return 1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, "/tmp/test.sock", UNIX_PATH_MAX);

    if (bind(server_fd, &addr, sizeof(addr)) != 0) {
        printf("bind failed (errno=%d)\n", errno);
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 4) != 0) {
        printf("listen failed (errno=%d)\n", errno);
        close(server_fd);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        printf("fork failed (errno=%d)\n", errno);
        close(server_fd);
        return 1;
    }

    if (pid == 0) {
        close(server_fd);
        int cli = socket(AF_UNIX, SOCK_STREAM, 0);
        if (cli < 0)
            _exit(1);

        if (connect(cli, &addr, sizeof(addr)) != 0)
            _exit(2);

        const char *msg = "hello via connect";
        send(cli, msg, strlen(msg), 0);
        close(cli);
        _exit(0);
    }

    int cli_fd = accept(server_fd, NULL, NULL);
    if (cli_fd < 0) {
        printf("accept failed (errno=%d)\n", errno);
        close(server_fd);
        return 1;
    }

    char buf[64];
    memset(buf, 0, sizeof(buf));
    int n = recv(cli_fd, buf, sizeof(buf) - 1, 0);
    close(cli_fd);
    close(server_fd);

    int status = 0;
    waitpid(pid, &status);

    if (n <= 0 || strcmp(buf, "hello via connect") != 0) {
        printf("bind_connect: bad data (n=%d buf='%s')\n", n, buf);
        return 1;
    }
    printf("bind_connect: ok\n");
    return 0;
}

static int test_read_write(void) {
    int sv[2] = { -1, -1 };
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
        printf("socketpair failed (errno=%d)\n", errno);
        return 1;
    }

    const char *msg = "read_write test";
    int w = write(sv[0], msg, strlen(msg));
    if (w <= 0) {
        printf("read_write: write failed (ret=%d errno=%d)\n", w, errno);
        close(sv[0]);
        close(sv[1]);
        return 1;
    }

    char buf[64];
    memset(buf, 0, sizeof(buf));
    int r = read(sv[1], buf, sizeof(buf) - 1);
    close(sv[0]);
    close(sv[1]);

    if (r <= 0 || strcmp(buf, "read_write test") != 0) {
        printf("read_write: bad data (r=%d buf='%s')\n", r, buf);
        return 1;
    }
    printf("read_write: ok\n");
    return 0;
}

int main(void) {
    printf("=== Unix socket tests ===\n");

    int fails = 0;
    fails += test_socketpair();
    fails += test_bind_connect();
    fails += test_read_write();

    if (fails == 0)
        printf("all socket tests passed\n");
    else
        printf("%d socket test(s) failed\n", fails);

    return fails;
}
