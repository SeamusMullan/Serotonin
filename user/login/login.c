/**
 * @file login.c
 * @brief Login program for Serotonin OS
 *
 * Reads /etc/passwd, prompts for a username, sets up credentials
 * (uid, gid, home directory, umask), and executes the user's shell.
 * Loops forever on failed login so init never exits.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

int listdir(const char *path, char *buf, size_t size);

/* Parsed /etc/passwd entry */
struct passwd_entry {
    char username[32];
    int uid;
    int gid;
    char home[64];
    char shell[64];
};

/**
 * @brief Parse a single /etc/passwd line
 *
 * Format: username:x:uid:gid:gecos:home:shell
 *
 * @return 0 on success, -1 on parse error
 */
static int parse_passwd_line(const char *line, struct passwd_entry *ent) {
    char buf[256];
    size_t len = strlen(line);
    if (len == 0 || len >= sizeof(buf)) return -1;
    memcpy(buf, line, len + 1);

    /* Tokenize by ':' manually (no strtok in freestanding) */
    char *fields[7];
    int nfields = 0;
    char *p = buf;
    fields[nfields++] = p;
    while (*p && nfields < 7) {
        if (*p == ':') {
            *p = '\0';
            fields[nfields++] = p + 1;
        }
        p++;
    }
    if (nfields < 7) return -1;

    /* username */
    if (strlen(fields[0]) == 0 || strlen(fields[0]) >= sizeof(ent->username))
        return -1;
    strcpy(ent->username, fields[0]);

    /* skip password field (fields[1]) */

    /* uid */
    ent->uid = atoi(fields[2]);

    /* gid */
    ent->gid = atoi(fields[3]);

    /* skip gecos (fields[4]) */

    /* home */
    if (strlen(fields[5]) >= sizeof(ent->home)) return -1;
    strcpy(ent->home, fields[5]);

    /* shell */
    if (strlen(fields[6]) >= sizeof(ent->shell)) return -1;
    strcpy(ent->shell, fields[6]);

    return 0;
}

/**
 * @brief Look up a username in /etc/passwd
 *
 * @return 0 if found and ent is filled, -1 if not found
 */
static int lookup_user(const char *username, struct passwd_entry *ent) {
    int fd = open("/etc/passwd", 0);
    if (fd < 0) return -1;

    char filebuf[1024];
    int n = read(fd, filebuf, sizeof(filebuf) - 1);
    close(fd);
    if (n <= 0) return -1;
    filebuf[n] = '\0';

    char *line_start = filebuf;
    while (line_start < filebuf + n) {
        char *nl = strchr(line_start, '\n');
        size_t line_len;
        if (nl) {
            *nl = '\0';
            line_len = (size_t)(nl - line_start);
        } else {
            line_len = strlen(line_start);
        }

        if (line_len > 0 && line_start[0] != '#') {
            struct passwd_entry tmp;
            if (parse_passwd_line(line_start, &tmp) == 0) {
                if (strcmp(tmp.username, username) == 0) {
                    *ent = tmp;
                    return 0;
                }
            }
        }

        if (!nl) break;
        line_start = nl + 1;
    }

    return -1;
}

/**
 * @brief Ensure a directory exists, creating it if needed
 */
static void ensure_dir(const char *path, int mode) {
    struct stat st;
    if (stat(path, &st) != 0) {
        mkdir(path, (mode_t)mode);
    }
    chmod(path, (mode_t)mode);
}

int main(int argc, char **argv, char **envp) {
    (void)argc;

    for (;;) {
        char username[64];
        printf("\033[2J\033[H\033[1mSerotonin\033[0m Operating System (0.4.0)\n");

        const char *prompt = "\nserotonin login: ";
        write(1, prompt, strlen(prompt));

        int n = read(0, username, sizeof(username) - 1);
        if (n <= 0) continue;
        username[n] = '\0';

        /* Strip trailing newline/whitespace */
        while (n > 0 && (username[n - 1] == '\n' || username[n - 1] == '\r' ||
                         username[n - 1] == ' ')) {
            username[--n] = '\0';
        }
        if (n == 0) continue;

        struct passwd_entry ent;
        if (lookup_user(username, &ent) != 0) {
            printf("Login incorrect\n");
            continue;
        }

        /* Set up the user's home directory (as root, before forking) */
        ensure_dir(ent.home, (ent.uid == 0) ? 0700 : 0750);
        chown(ent.home, (uid_t)ent.uid, (gid_t)ent.gid);

        printf("Logging into \033[1m%s\033[0m\n", ent.username);

        /* Build envp with HOME and USER set */
        char home_env[80];
        char user_env[80];
        snprintf(home_env, sizeof(home_env), "HOME=%s", ent.home);
        snprintf(user_env, sizeof(user_env), "USER=%s", ent.username);

        const char *new_envp[6];
        new_envp[0] = "PATH=/bin";
        new_envp[1] = "TERM=xterm-256color";
        new_envp[2] = "COLORTERM=truecolor";
        new_envp[3] = home_env;
        new_envp[4] = user_env;
        new_envp[5] = NULL;

        /* Fork + exec the shell; login parent stays root */
        pid_t pid = fork();
        if (pid == 0) {
            /* child: drop privileges, then exec the user's shell */
            setgid((gid_t)ent.gid);
            setuid((uid_t)ent.uid);
            umask(022);

            if (chdir(ent.home) != 0) {
                chdir("/");
            }

            const char *shell_argv[2];
            shell_argv[0] = ent.shell;
            shell_argv[1] = NULL;
            execve(ent.shell, (char *const *)shell_argv, (char *const *)new_envp);
            printf("login: failed to exec %s\n", ent.shell);
            _exit(1);
        } else if (pid > 0) {
            int status;
            waitpid(pid, &status, 0);
            /* Shell exited, parent is still root, loop back to login */
        } else {
            printf("login: fork failed\n");
        }
    }

    return 0;
}
