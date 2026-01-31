/**
 * @file pipe_test.c
 * @brief Pipe IPC test app for Serotonin OS
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>

int waitpid(pid_t pid, int* status);

#define SIGPIPE_NUM 13

static volatile int sigpipe_seen = 0;

static void sigpipe_handler(int sig) {
    (void)sig;
    printf("sigpipe_seen\n");
    sigpipe_seen = 1;
}

int main(void) {
    int fds[2] = { -1, -1 };
    if (pipe(fds) != 0) {
        printf("pipe_test: pipe failed (errno=%d)\n", errno);
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        printf("pipe_test: fork failed (errno=%d)\n", errno);
        return 1;
    }

    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], 1) < 0) {
            printf("pipe_test: dup2 failed (errno=%d)\n", errno);
            _exit(1);
        }
        const char *msg = "pipe: hello from child\n";
        write(1, msg, strlen(msg));
        close(fds[1]);
        close(1);
        _exit(0);
    }

    close(fds[1]);
    char buf[128];
    while (1) {
        int n = read(fds[0], buf, sizeof(buf));
        if (n < 0) {
            printf("pipe_test: read failed (errno=%d)\n", errno);
            close(fds[0]);
            return 1;
        }
        if (n == 0) {
            break;
        }
        write(1, buf, (size_t)n);
    }
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status);

    // SIGPIPE test: write to pipe with no readers
    int pfds[2] = { -1, -1 };
    if (pipe(pfds) != 0) {
        printf("pipe_test: pipe2 failed (errno=%d)\n", errno);
        return 1;
    }

    signal(SIGPIPE_NUM, sigpipe_handler);
    sigpipe_seen = 0;
    close(pfds[0]);

    errno = 0;
    int wret = write(pfds[1], "X", 1);
    if (errno != EPIPE || !sigpipe_seen) {
        printf("pipe_test: sigpipe failed (ret=%d errno=%d sig=%d)\n", wret, errno, sigpipe_seen);
        close(pfds[1]);
        return 1;
    }

    close(pfds[1]);
    printf("pipe_test: sigpipe ok\n");

    return 0;
}
