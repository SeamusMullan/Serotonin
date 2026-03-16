/**
 * @file getty.c
 * @brief Getty - login/shell spawner for virtual terminals
 *
 * Receives a PTY slave path as argv[1] (e.g., "/dev/pts/0"),
 * opens it as stdin/stdout/stderr, and loops running a shell.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include "../syscall/lib5ht/lib5ht.h"

int snprintf(char *str, size_t size, const char *fmt, ...);
int waitpid(pid_t pid, int *status);

int main(int argc, char **argv, char **envp) {
    const char *pts_path = "/dev/pts/0";

    if (argc >= 2)
        pts_path = argv[1];

    /* Close inherited fds 0, 1, 2 */
    close(0);
    close(1);
    close(2);

    /* Open PTY slave as fd 0 (stdin) */
    int fd = open(pts_path, O_RDWR);
    if (fd < 0) {
        /* Can't print - no stdout. Just exit. */
        _exit(1);
    }

    /* dup to stdout and stderr */
    dup2(fd, 1);
    dup2(fd, 2);
    if (fd > 2)
        close(fd);

    /* Set this process as foreground for the PTY */
    sys_5ht_pty_setpgrp(0);

    /* Print welcome banner */
    printf("\033[1m\033[38;2;122;152;255mSerotonin\033[0m on %s\n\n", pts_path);

    /* Loop: run login, wait, repeat */
    while (1) {
        pid_t pid = fork();
        if (pid == 0) {
            /* Child: exec login */
            char *login_argv[] = { "/bin/login", (char *)pts_path, NULL };
            execve("/bin/login", login_argv, envp);
            /* Fallback to shell if login not found */
            char *shell_argv[] = { "/bin/sh", NULL };
            execve("/bin/sh", shell_argv, envp);
            _exit(127);
        } else if (pid > 0) {
            /* Parent: wait for login/shell to exit */
            int status;
            waitpid(pid, &status);
        } else {
            /* fork failed */
            printf("getty: fork failed\n");
            _exit(1);
        }
    }

    return 0;
}
