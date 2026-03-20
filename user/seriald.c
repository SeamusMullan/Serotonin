/**
 * @file seriald.c
 * @brief Serial console daemon
 *
 * Bridges /dev/ttyS0 to a PTY pair and spawns login on the slave side.
 * Parent process shuttles bytes between the serial port and the PTY
 * master using poll().
 */

#include "syscall/sys/poll.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include "syscall/lib5ht/lib5ht.h"

int snprintf(char *str, size_t size, const char *fmt, ...);
int waitpid(pid_t pid, int *status);

static void spawn_login(int slave_fd, char **envp) {
    pid_t pid = fork();
    if (pid < 0)
        return;

    if (pid == 0) {
        close(0);
        close(1);
        close(2);

        dup2(slave_fd, 0);
        dup2(slave_fd, 1);
        dup2(slave_fd, 2);
        if (slave_fd > 2)
            close(slave_fd);

        sys_5ht_pty_setpgrp(0);

        printf("\033[1m\033[38;2;122;152;255mSerotonin\033[0m on serial console\n\n");

        char *login_argv[] = { "/bin/login", NULL };
        execve("/bin/login", login_argv, envp);
        char *shell_argv[] = { "/bin/sh", NULL };
        execve("/bin/sh", shell_argv, envp);
        _exit(127);
    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;

    int serial_fd = open("/dev/ttyS0", O_RDWR);
    if (serial_fd < 0) {
        _exit(1);
    }

    while (1) {
        // alloc pty master slave pair
        int pty_fds[2];
        if (sys_5ht_pty_open(pty_fds) < 0) {
            _exit(1);
        }
        int master_fd = pty_fds[0];
        int slave_fd  = pty_fds[1];

        spawn_login(slave_fd, envp);

        // the parent doesn't need the slave
        close(slave_fd);

        // serial to/from PTY master bridge
        int running = 1;
        while (running) {
            struct pollfd pfds[2];
            pfds[0].fd = serial_fd;
            pfds[0].events = POLLIN;
            pfds[0].revents = 0;
            pfds[1].fd = master_fd;
            pfds[1].events = POLLIN;
            pfds[1].revents = 0;

            int ret = poll(pfds, 2, 5000);
            if (ret < 0)
                break;

            if (ret == 0) {
                // wait
                int status;
                pid_t w = waitpid(-1, &status);
                if (w > 0) {
                    running = 0;
                    break;
                }
                continue;
            }

            // serial to PTY master
            if (pfds[0].revents & POLLIN) {
                char buf[256];
                int n = read(serial_fd, buf, sizeof(buf));
                if (n > 0) {
                    write(master_fd, buf, n);
                }
            }

            // PTY master to serial
            if (pfds[1].revents & POLLIN) {
                char buf[256];
                int n = read(master_fd, buf, sizeof(buf));
                if (n > 0) {
                    write(serial_fd, buf, n);
                } else if (n == 0) {
                    // slave closed
                    running = 0;
                }
            }

            if (pfds[1].revents & POLLHUP) {
                running = 0;
            }
        }

        close(master_fd);

        // wait
        int status;
        waitpid(-1, &status);
    }

    return 0;
}
