/**
 * @file shell.c
 * @brief Simple command-line shell for Serotonin OS
 *
 * Provides a basic interactive shell that reads commands from stdin,
 * forks child processes to execute them, and waits for completion.
 * The shell runs in a loop until EOF (Ctrl+D) is received.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include "syscall/lib5ht/lib5ht.h"

int gethostname(char *name, size_t len);
int snprintf(char *str, size_t size, const char *fmt, ...);

void sigint_handle(int sig) {
    return;
}

/**
 * @brief Look up a username by uid from /etc/passwd
 *
 * Reads /etc/passwd and finds the entry matching the given uid.
 * Falls back to "?" if the file can't be read or uid isn't found.
 */
static void get_username(uid_t uid, char *out, size_t outsize) {
    int fd = open("/etc/passwd", 0);
    if (fd < 0) {
        strncpy(out, "?", outsize);
        return;
    }

    char buf[1024];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        strncpy(out, "?", outsize);
        return;
    }
    buf[n] = '\0';

    // Parse each line: username:x:uid:gid:gecos:home:shell
    char *line = buf;
    while (line < buf + n) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        if (line[0] != '\0' && line[0] != '#') {
            // Find first ':' -> username
            char *colon1 = strchr(line, ':');
            if (colon1) {
                // Skip password field
                char *colon2 = strchr(colon1 + 1, ':');
                if (colon2) {
                    // Parse uid field
                    int entry_uid = 0;
                    char *p = colon2 + 1;
                    while (*p >= '0' && *p <= '9') {
                        entry_uid = entry_uid * 10 + (*p - '0');
                        p++;
                    }
                    if (entry_uid == (int)uid) {
                        size_t ulen = (size_t)(colon1 - line);
                        if (ulen >= outsize) ulen = outsize - 1;
                        memcpy(out, line, ulen);
                        out[ulen] = '\0';
                        return;
                    }
                }
            }
        }

        if (!nl) break;
        line = nl + 1;
    }

    strncpy(out, "?", outsize);
}

/**
 * @brief Main shell loop
 *
 * Implements a read-eval-execute loop that:
 * 1. Displays a prompt
 * 2. Reads user input
 * 3. Forks a child process
 * 4. Executes the command in the child
 * 5. Waits for the child to complete
 *
 * @return 0 on normal exit, 1 on error
 */
int main(int argc, char **argv, char **envp)
{
    pid_t sh_pid = getpid();
    sys_5ht_set_fid(sh_pid);

    signal(SIGINT, sigint_handle);

	char command[256];
	const char *prompt = "serotonin# ";
	const char *read_error = "Error: failed to read input\n";
	const char *fork_error = "Error: failed to fork process\n";
	const char *exec_error = "Error: failed to execute command\n";
	const char *empty_cmd = "Error: empty command\n";

	char username[32];
	get_username(getuid(), username, sizeof(username));

	char hostname[65];
	if (gethostname(hostname, sizeof(hostname)) < 0)
		strcpy(hostname, "serotonin");

	for (;;) {
    	char cwd[256];
    	char prompt_buf[512];
    	if (getcwd(cwd, sizeof(cwd))) {
    		int len = snprintf(prompt_buf, sizeof(prompt_buf),
    			"\033[1;32m%s@%s\033[0m \033[1;34m%s\033[0m # ", username, hostname, cwd);
    		if (write(1, prompt_buf, len) < 0)
    			_exit(1);
    	} else {
    		if (write(1, prompt, strlen(prompt)) < 0)
    			_exit(1);
    	}

    	errno = 0;
    	int count = read(0, command, sizeof(command) - 1);

		if (count < 0) {
			write(2, read_error, 28);
			continue;
		}

		// Handle EOF (Ctrl+D)
		if (count == 0) {
			break;
		}

		// Null-terminate the command
		command[count] = '\0';

		// Remove trailing newline if present
		if (count > 0 && command[count - 1] == '\n') {
			command[count - 1] = '\0';
			count--;
		}

		// Skip empty commands
		if (count == 0 || command[0] == '\0') {
			continue;
		}

		// Check for buffer overflow (command too long)
		if (count >= (int)(sizeof(command) - 1)) {
			write(2, "Error: command too long\n", 24);
			// Drain remaining input
			char drain;
			while (read(0, &drain, 1) > 0 && drain != '\n');
			continue;
		}

		int background = 0;

        // Trim trailing spaces/tabs
        int end = strlen(command) - 1;
        while (end >= 0 &&
               (command[end] == ' ' || command[end] == '\t')) {
            command[end] = '\0';
            end--;
        }

        // Check for trailing '&'
        if (end >= 0 && command[end] == '&') {
            background = 1;
            command[end] = '\0';
            end--;

            // Trim spaces before '&'
            while (end >= 0 &&
                   (command[end] == ' ' || command[end] == '\t')) {
                command[end] = '\0';
                end--;
            }
        }

		char *args[64];
        int arg_count = 0;
        char *p = command;

        while (*p != '\0') {
            // Skip leading spaces
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p == '\0') {
                break;
            }

            if (arg_count < (int)(sizeof(args) / sizeof(args[0])) - 1) {
                args[arg_count++] = p;
            }

            // Move to next delimiter
            while (*p != '\0' && *p != ' ' && *p != '\t') {
                p++;
            }
            if (*p == '\0') {
                break;
            }

            // Terminate this token
            *p = '\0';
            p++;
        }

        args[arg_count] = NULL;

		if (arg_count == 0) {
			continue;
		}

		if (strcmp(args[0], "cd") == 0) {
			const char *target = (arg_count > 1) ? args[1] : "/";
			if (chdir(target) != 0) {
				printf("cd: failed to change directory\n");
			}
			continue;
		}

		if (strcmp(args[0], "clear") == 0) {
			write(1, "\033[2J\033[H", 7);
			continue;
		}

		if (strcmp(args[0], "exit") == 0) {
			int code = 0;
			if (arg_count > 1) {
				code = atoi(args[1]);
			}
			_exit(code);
		}

		// Fork the process
		pid_t fork_result = fork();

		if (fork_result < 0) {
			// Fork failed
			write(2, fork_error, 28);
			continue;
		} else if (fork_result == 0) {
			// Child process: execute the command
			if (strchr(args[0], '/')) {
				execve(args[0], args, envp);
			} else {
				const char *path_env = NULL;
				for (char **e = envp; e && *e; e++) {
					if (strncmp(*e, "PATH=", 5) == 0) {
						path_env = *e + 5;
						break;
					}
				}
				if (!path_env || path_env[0] == '\0') {
					path_env = "/bin";
				}

				char candidate[256];
				const char *p = path_env;
				while (*p) {
					const char *start = p;
					while (*p && *p != ':') {
						p++;
					}
					size_t dir_len = (size_t)(p - start);
					size_t cmd_len = strlen(args[0]);
					if (dir_len + 1 + cmd_len + 1 < sizeof(candidate)) {
						memcpy(candidate, start, dir_len);
						if (dir_len > 0 && candidate[dir_len - 1] != '/') {
							candidate[dir_len] = '/';
							memcpy(candidate + dir_len + 1, args[0], cmd_len);
							candidate[dir_len + 1 + cmd_len] = '\0';
						} else {
							memcpy(candidate + dir_len, args[0], cmd_len);
							candidate[dir_len + cmd_len] = '\0';
						}
						execve(candidate, args, envp);
					}

					if (*p == ':') {
						p++;
					}
				}

				execve(args[0], args, envp);
			}

			// If execve returns, it failed
			write(2, exec_error, 30);
			_exit(1);
		} else {
			// Parent process: wait for child
			sys_5ht_set_fid(fork_result); // not a race since parent runs first in kernel
			int status;
			if (!background) {
				pid_t wait_result = waitpid(fork_result, &status, 0);
				sys_5ht_set_fid(sh_pid);

				if (wait_result < 0) {
					write(2, "Error: failed to wait for child process\n", 41);
				}
			} else {
				printf("[%d]\n",fork_result);
			}
			// Optionally check exit status
			// if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
			//     write(2, "Command exited with non-zero status\n", 37);
			// }
		}
	}

	_exit(0);
}
