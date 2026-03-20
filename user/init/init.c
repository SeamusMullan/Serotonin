/**
 * @file init.c
 * @brief Init system for Serotonin
 *
 * Reads job definitions from /etc/init/, resolves dependencies,
 * starts jobs in dependency order, captures stdout for readiness
 * detection, and logs output to /var/log/.
 *
 * Config format (/etc/init/<jobname>):
 *   exec=/bin/program [args...]
 *   type=daemon|oneshot         (default: oneshot)
 *   after=job1,job2             (comma-separated dependency names)
 *   ready=stdout:<pattern>      (ready when stdout line contains pattern)
 *   ready=start                 (ready immediately on exec, daemon default)
 *   ready=exit                  (ready on exit 0, oneshot default)
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "../syscall/sys/poll.h"
#include "../syscall/sys/socket.h"
#include "initctl.h"

int listdir(const char *path, char *buf, size_t size);
int waitpid(pid_t pid, int *status);
int kill(pid_t pid, int sig);
int sethostname(const char *name, size_t len);

#define MAX_JOBS        16
#define MAX_ARGS        8
#define MAX_NAME        32
#define MAX_PATH        128
#define MAX_LINE        256
#define MAX_PATTERN     64
#define MAX_DEPS_STR    256
#define LINE_BUF_SIZE   512
#define NUM_KERNEL_VTYS 4

#define JOB_DIR         "/etc/init"
#define LOG_DIR         "/var/log"

#define TYPE_ONESHOT    0
#define TYPE_DAEMON     1

#define READY_EXIT      0
#define READY_START     1
#define READY_STDOUT    2
#define READY_UNSET     (-1)

#define STATE_PENDING   0
#define STATE_RUNNING   1
#define STATE_READY     2
#define STATE_FAILED    3
#define STATE_EXITED    4

struct job {
    char    name[MAX_NAME];
    char    exec_line[MAX_PATH];
    int     type;
    int     ready_mode;
    char    ready_pattern[MAX_PATTERN];
    char    after[MAX_DEPS_STR];

    int     state;
    pid_t   pid;
    int     pipe_fd;
    int     log_fd;
    char    line_buf[LINE_BUF_SIZE];
    int     line_len;
    char    last_log[LINE_BUF_SIZE];

    int     display_row;
    int     has_log_row;
    int     anim_tick;
};

static struct job jobs[MAX_JOBS];
static int num_jobs;
static int display_order[MAX_JOBS];
static int total_display_rows;
static int ctl_fd = -1;
static char **saved_envp;

static void setup_bin_permissions(void) {
    char buf[4096];
    int ret = listdir("/bin", buf, sizeof(buf));
    if (ret <= 0) return;

    char *p = buf, *end = buf + ret;
    while (p < end) {
        char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
        if (len > 0 && len < 256) {
            char path[270];
            memcpy(path, "/bin/", 5);
            memcpy(path + 5, p, len);
            path[5 + len] = '\0';
            chmod(path, 0755);
        }
        if (!nl) break;
        p = nl + 1;
    }
}

static int ensure_dir(const char *path, mode_t mode) {
    struct stat st;
    if (stat(path, &st) == 0) { chmod(path, mode); return 0; }
    if (mkdir(path, mode) != 0) return -1;
    chmod(path, mode);
    return 0;
}

static void bootstrap_filesystem(void) {
    setup_bin_permissions();
    ensure_dir("/root",     0700);
    ensure_dir("/home",     0755);
    ensure_dir("/etc",      0755);
    ensure_dir("/etc/init", 0755);
    ensure_dir("/var",      0755);
    ensure_dir("/var/log",  0755);
    ensure_dir("/tmp",      0777);

    int fd = open("/etc/hostname", O_RDONLY);
    if (fd >= 0) {
        char hbuf[65];
        int n = read(fd, hbuf, sizeof(hbuf) - 1);
        close(fd);
        if (n > 0) {
            while (n > 0 && (hbuf[n-1] == '\n' || hbuf[n-1] == '\r'
                                || hbuf[n-1] == ' '))
                n--;
            hbuf[n] = '\0';
            if (n > 0)
                sethostname(hbuf, n);
        }
    }

    struct stat st;
    if (stat("/etc/passwd", &st) != 0) {
        int fd = open("/etc/passwd", O_CREAT | O_WRONLY);
        if (fd >= 0) {
            static const char pw[] =
                "root:x:0:0:root:/root:/bin/sh\n"
                "user:x:1000:1000::/home/user:/bin/sh\n";
            write(fd, pw, sizeof(pw) - 1);
            close(fd);
            chmod("/etc/passwd", 0644);
        }
    }
}

static void strip_trailing(char *s) {
    char *p = s + strlen(s);
    while (p > s && (p[-1] == '\n' || p[-1] == '\r' || p[-1] == ' '))
        *--p = '\0';
}

static int parse_job_file(const char *path, struct job *j) {
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    memset(j, 0, sizeof(*j));
    j->state      = STATE_PENDING;
    j->pipe_fd    = -1;
    j->log_fd     = -1;
    j->pid        = -1;
    j->ready_mode = READY_UNSET;

    char line[MAX_LINE];
    while (fgets(line, sizeof(line), fp)) {
        strip_trailing(line);
        if (line[0] == '#' || line[0] == '\0') continue;

        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        char *key = line, *val = eq + 1;

        if (strcmp(key, "exec") == 0)
            strncpy(j->exec_line, val, MAX_PATH - 1);
        else if (strcmp(key, "type") == 0)
            j->type = (strcmp(val, "daemon") == 0) ? TYPE_DAEMON
                                                   : TYPE_ONESHOT;
        else if (strcmp(key, "after") == 0)
            strncpy(j->after, val, MAX_DEPS_STR - 1);
        else if (strcmp(key, "ready") == 0) {
            if (strncmp(val, "stdout:", 7) == 0) {
                j->ready_mode = READY_STDOUT;
                strncpy(j->ready_pattern, val + 7, MAX_PATTERN - 1);
            } else if (strcmp(val, "start") == 0) {
                j->ready_mode = READY_START;
            } else {
                j->ready_mode = READY_EXIT;
            }
        }
    }
    fclose(fp);

    if (j->ready_mode == READY_UNSET)
        j->ready_mode = (j->type == TYPE_DAEMON) ? READY_START
                                                  : READY_EXIT;
    return (j->exec_line[0]) ? 0 : -1;
}

static void load_jobs(void) {
    char buf[2048];
    int ret = listdir(JOB_DIR, buf, sizeof(buf));
    if (ret <= 0) return;

    char *p = buf, *end = buf + ret;
    while (p < end && num_jobs < MAX_JOBS) {
        char *nl = memchr(p, '\n', (size_t)(end - p));
        size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);

        if (len > 0 && len < MAX_NAME && p[0] != '.'
            && !memchr(p, '.', len)) {
            char name[MAX_NAME], path[MAX_PATH];
            memcpy(name, p, len);
            name[len] = '\0';
            snprintf(path, sizeof(path), "%s/%s", JOB_DIR, name);

            struct job *j = &jobs[num_jobs];
            if (parse_job_file(path, j) == 0) {
                strncpy(j->name, name, MAX_NAME - 1);
                num_jobs++;
            }
        }
        if (!nl) break;
        p = nl + 1;
    }
}

static struct job *find_job(const char *name) {
    for (int i = 0; i < num_jobs; i++)
        if (strcmp(jobs[i].name, name) == 0)
            return &jobs[i];
    return NULL;
}

static int deps_satisfied(const struct job *j) {
    if (j->after[0] == '\0') return 1;

    char tmp[MAX_DEPS_STR];
    strncpy(tmp, j->after, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *tok = tmp;
    while (*tok) {
        while (*tok == ' ' || *tok == ',') tok++;
        if (!*tok) break;

        char *e = tok;
        while (*e && *e != ',' && *e != ' ') e++;
        char saved = *e;
        *e = '\0';

        struct job *dep = find_job(tok);
        if (dep && dep->state != STATE_READY && dep->state != STATE_EXITED)
            return 0;

        if (!saved) break;
        tok = e + 1;
    }
    return 1;
}

static int job_depth(int idx, int recurse) {
    if (recurse > MAX_JOBS) return 0;
    if (jobs[idx].after[0] == '\0') return 0;

    int max_d = 0;
    char tmp[MAX_DEPS_STR];
    strncpy(tmp, jobs[idx].after, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    char *tok = tmp;
    while (*tok) {
        while (*tok == ' ' || *tok == ',') tok++;
        if (!*tok) break;
        char *e = tok;
        while (*e && *e != ',' && *e != ' ') e++;
        char saved = *e;
        *e = '\0';

        for (int i = 0; i < num_jobs; i++) {
            if (strcmp(jobs[i].name, tok) == 0) {
                int d = job_depth(i, recurse + 1) + 1;
                if (d > max_d) max_d = d;
                break;
            }
        }

        if (!saved) break;
        tok = e + 1;
    }
    return max_d;
}

static void compute_display_order(void) {
    int depths[MAX_JOBS];
    for (int i = 0; i < num_jobs; i++) {
        display_order[i] = i;
        depths[i] = job_depth(i, 0);
    }
    for (int i = 0; i < num_jobs - 1; i++) {
        for (int j = i + 1; j < num_jobs; j++) {
            int a = display_order[i], b = display_order[j];
            if (depths[a] > depths[b] ||
                (depths[a] == depths[b] &&
                 strcmp(jobs[a].name, jobs[b].name) > 0)) {
                int t = display_order[i];
                display_order[i] = display_order[j];
                display_order[j] = t;
            }
        }
    }
}

static void compute_display_layout(void) {
    int row = 0;
    for (int di = 0; di < num_jobs; di++) {
        int i = display_order[di];
        jobs[i].display_row = row;
        jobs[i].has_log_row = (jobs[i].ready_mode == READY_STDOUT);
        row++;
        if (jobs[i].has_log_row) row++;
    }
    total_display_rows = row;
}

static void draw_name_animated(const char *name, int tick) {
    int len = (int)strlen(name);
    int cycle = len + 4;

    for (int i = 0; i < len; i++) {
        int phase = ((tick - i) % cycle + cycle) % cycle;
        if (phase == 0)
            printf("\033[38;2;122;152;255m");
        else if (phase == 1)
            printf("\033[38;2;170;185;245m");
        else if (phase == 2)
            printf("\033[38;2;200;210;240m");
        else
            printf("\033[38;2;100;110;140m");
        printf("%c", name[i]);
    }
    printf("\033[0m");
}

static void get_unmet_deps(const struct job *j, char *buf, int bufsz) {
    buf[0] = '\0';
    if (j->after[0] == '\0') return;

    char tmp[MAX_DEPS_STR];
    strncpy(tmp, j->after, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    int first = 1;
    char *tok = tmp;
    while (*tok) {
        while (*tok == ' ' || *tok == ',') tok++;
        if (!*tok) break;
        char *e = tok;
        while (*e && *e != ',' && *e != ' ') e++;
        char saved = *e;
        *e = '\0';

        struct job *dep = find_job(tok);
        if (dep && dep->state != STATE_READY && dep->state != STATE_EXITED) {
            if (!first && (int)strlen(buf) + 2 < bufsz)
                strncat(buf, ", ", bufsz - strlen(buf) - 1);
            strncat(buf, tok, bufsz - strlen(buf) - 1);
            first = 0;
        }

        if (!saved) break;
        tok = e + 1;
    }
}

static void render_display(void) {
    printf("\033[u");

    for (int di = 0; di < num_jobs; di++) {
        int i = display_order[di];
        struct job *j = &jobs[i];

        printf("\r\033[K");

        switch (j->state) {
        case STATE_PENDING:
            printf("  \033[38;2;55;60;75m[ \033[38;2;75;80;95m.."
                   " \033[38;2;55;60;75m]\033[0m ");
            break;
        case STATE_RUNNING:
            printf("  \033[38;2;220;190;80m[ "
                   "\033[1m..\033[22;38;2;220;190;80m"
                   " ]\033[0m ");
            break;
        case STATE_READY: case STATE_EXITED:
            printf("  \033[38;2;80;200;120m[ "
                   "\033[1mOK\033[22;38;2;80;200;120m"
                   " ]\033[0m ");
            break;
        case STATE_FAILED:
            printf("  \033[38;2;220;80;80m[ "
                   "\033[1m!!\033[22;38;2;220;80;80m"
                   " ]\033[0m ");
            break;
        }

        switch (j->state) {
        case STATE_PENDING:
            printf("\033[38;2;75;80;95mWaiting  \033[0m");
            break;
        case STATE_RUNNING:
            printf("\033[38;2;180;185;200mStarting \033[0m");
            break;
        case STATE_READY: case STATE_EXITED:
            printf("\033[38;2;180;185;200mStarted  \033[0m");
            break;
        case STATE_FAILED:
            printf("\033[38;2;220;80;80mFailed   \033[0m");
            break;
        }

        switch (j->state) {
        case STATE_PENDING: {
            printf("\033[38;2;75;80;95m%s\033[0m", j->name);
            char unmet[128];
            get_unmet_deps(j, unmet, sizeof(unmet));
            if (unmet[0])
                printf("  \033[38;2;55;60;75mfor %s\033[0m", unmet);
            break;
        }
        case STATE_RUNNING:
            draw_name_animated(j->name, j->anim_tick);
            break;
        case STATE_READY: case STATE_EXITED:
            printf("\033[1;38;2;230;235;255m%s\033[0m", j->name);
            break;
        case STATE_FAILED:
            printf("\033[38;2;180;60;60m%s\033[0m", j->name);
            break;
        }

        printf("\n");

        if (j->has_log_row) {
            printf("\r\033[K");
            if (j->last_log[0])
                printf("           \033[38;2;80;90;110m%s\033[0m",
                       j->last_log);
            printf("\n");
        }
    }

    fflush(stdout);
}

static int start_job(struct job *j, char **envp) {
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        j->state = STATE_FAILED;
        return -1;
    }

    char log_path[MAX_PATH];
    snprintf(log_path, sizeof(log_path), "%s/%s.log", LOG_DIR, j->name);
    j->log_fd = open(log_path, O_CREAT | O_WRONLY);

    char exec_buf[MAX_PATH];
    strncpy(exec_buf, j->exec_line, sizeof(exec_buf) - 1);
    exec_buf[sizeof(exec_buf) - 1] = '\0';

    char *argv[MAX_ARGS + 1];
    int argc = 0;
    char *s = exec_buf;
    while (*s && argc < MAX_ARGS) {
        while (*s == ' ') s++;
        if (!*s) break;
        argv[argc++] = s;
        while (*s && *s != ' ') s++;
        if (*s) *s++ = '\0';
    }
    argv[argc] = NULL;

    if (argc == 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        j->state = STATE_FAILED;
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        j->state = STATE_FAILED;
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        close(pipefd[1]);
        if (ctl_fd >= 0) close(ctl_fd);
        signal(15, (void *)0);
        signal(13, (void *)0);
        execve(argv[0], argv, envp);
        _exit(127);
    }

    close(pipefd[1]);
    j->pipe_fd = pipefd[0];
    j->pid     = pid;
    j->state   = STATE_RUNNING;

    if (j->ready_mode == READY_START)
        j->state = STATE_READY;

    return 0;
}

static void process_pipe(struct job *j) {
    char buf[256];
    int n = read(j->pipe_fd, buf, sizeof(buf));

    if (n <= 0) {
        close(j->pipe_fd);
        j->pipe_fd = -1;

        if (j->state == STATE_RUNNING) {
            int status = 0;
            if (j->pid > 0)
                waitpid(j->pid, &status);

            if (j->ready_mode == READY_EXIT) {
                j->state = (status == 0) ? STATE_EXITED : STATE_FAILED;
            } else if (j->ready_mode == READY_STDOUT) {
                j->state = STATE_FAILED;
                strncpy(j->last_log, "exited before ready",
                        sizeof(j->last_log) - 1);
            }
            j->pid = -1;
        }
        return;
    }

    if (j->log_fd >= 0)
        write(j->log_fd, buf, n);

    for (int i = 0; i < n; i++) {
        if (buf[i] == '\n' || j->line_len >= LINE_BUF_SIZE - 1) {
            j->line_buf[j->line_len] = '\0';

            if (j->line_buf[0]) {
                int maxw = 65;
                if (j->line_len <= maxw) {
                    strncpy(j->last_log, j->line_buf,
                            sizeof(j->last_log) - 1);
                } else {
                    memcpy(j->last_log, j->line_buf, maxw);
                    j->last_log[maxw] = '\0';
                    strncat(j->last_log, "...",
                            sizeof(j->last_log) - strlen(j->last_log) - 1);
                }
            }

            if (j->state == STATE_RUNNING &&
                j->ready_mode == READY_STDOUT &&
                strstr(j->line_buf, j->ready_pattern)) {
                j->state = STATE_READY;
            }

            j->line_len = 0;
        } else {
            j->line_buf[j->line_len++] = buf[i];
        }
    }
}

static void drain_pipes(void) {
    struct pollfd pfds[MAX_JOBS];
    int map[MAX_JOBS];
    int np = 0;

    for (int i = 0; i < num_jobs; i++) {
        if (jobs[i].pipe_fd >= 0) {
            map[np] = i;
            pfds[np].fd      = jobs[i].pipe_fd;
            pfds[np].events  = POLLIN;
            pfds[np].revents = 0;
            np++;
        }
    }
    if (np == 0) return;

    poll(pfds, np, 0);
    for (int i = 0; i < np; i++)
        if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR))
            process_pipe(&jobs[map[i]]);
}

static int init_control_socket(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, INITCTL_SOCK_PATH, UNIX_PATH_MAX - 1);

    unlink(INITCTL_SOCK_PATH);

    if (bind(fd, &addr, sizeof(addr)) < 0) { close(fd); return -1; }
    if (listen(fd, 4) < 0)                  { close(fd); return -1; }

    return fd;
}

static void handle_ctl_client(int client_fd) {
    struct initctl_req req;
    int n = recv(client_fd, &req, sizeof(req), 0);
    if (n < (int)sizeof(req)) { close(client_fd); return; }

    struct initctl_rsp rsp;
    memset(&rsp, 0, sizeof(rsp));

    switch (req.cmd) {
    case INITCTL_CMD_START: {
        struct job *j = find_job(req.name);

        if (!j && num_jobs < MAX_JOBS) {
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "%s/%s", JOB_DIR, req.name);
            j = &jobs[num_jobs];
            if (parse_job_file(path, j) == 0) {
                strncpy(j->name, req.name, MAX_NAME - 1);
                num_jobs++;
            } else {
                j = NULL;
            }
        }

        if (!j) {
            rsp.status = -1;
            send(client_fd, &rsp, sizeof(rsp), 0);
            break;
        }

        if (j->state == STATE_RUNNING || j->state == STATE_READY) {
            rsp.status = -1;
            send(client_fd, &rsp, sizeof(rsp), 0);
            break;
        }

        j->state = STATE_PENDING;
        j->pid = -1;
        if (j->pipe_fd >= 0) { close(j->pipe_fd); j->pipe_fd = -1; }
        if (j->log_fd >= 0)  { close(j->log_fd);  j->log_fd  = -1; }
        j->line_len = 0;
        j->last_log[0] = '\0';

        start_job(j, saved_envp);
        rsp.status = 0;
        send(client_fd, &rsp, sizeof(rsp), 0);
        break;
    }
    case INITCTL_CMD_STOP: {
        struct job *j = find_job(req.name);
        if (j && j->pid > 0) {
            kill(j->pid, 15); /* SIGTERM */
            j->state = STATE_EXITED;
            j->pid = -1;
            if (j->pipe_fd >= 0) { close(j->pipe_fd); j->pipe_fd = -1; }
            if (j->log_fd >= 0)  { close(j->log_fd);  j->log_fd  = -1; }
            rsp.status = 0;
        } else {
            rsp.status = -1;
        }
        send(client_fd, &rsp, sizeof(rsp), 0);
        break;
    }
    case INITCTL_CMD_STATUS: {
        rsp.status = 0;
        rsp.count  = (uint8_t)num_jobs;
        send(client_fd, &rsp, sizeof(rsp), 0);

        for (int i = 0; i < num_jobs; i++) {
            struct initctl_job_info info;
            memset(&info, 0, sizeof(info));
            strncpy(info.name, jobs[i].name, sizeof(info.name) - 1);
            info.state = (uint8_t)jobs[i].state;
            info.type  = (uint8_t)jobs[i].type;
            info.pid   = (int16_t)jobs[i].pid;
            send(client_fd, &info, sizeof(info), 0);
        }
        break;
    }
    default:
        rsp.status = -1;
        send(client_fd, &rsp, sizeof(rsp), 0);
        break;
    }

    close(client_fd);
}

static void poll_ctl(void) {
    if (ctl_fd < 0) return;

    struct pollfd pfd = { .fd = ctl_fd, .events = POLLIN, .revents = 0 };
    if (poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        int cfd = accept(ctl_fd, NULL, NULL);
        if (cfd >= 0)
            handle_ctl_client(cfd);
    }
}

static int count_pending(void) {
    int n = 0;
    for (int i = 0; i < num_jobs; i++) {
        int s = jobs[i].state;
        if (s == STATE_PENDING || s == STATE_RUNNING)
            n++;
    }
    return n;
}

static void startup_loop(char **envp) {
    compute_display_order();
    compute_display_layout();

    printf("\033[?25l");
    printf("\033[s");

    for (int i = 0; i < total_display_rows; i++)
        printf("\n");

    render_display();

    int stall = 0, prev_pending = -1;

    while (count_pending() > 0) {
        int started = 0;

        for (int i = 0; i < num_jobs; i++) {
            if (jobs[i].state == STATE_PENDING && deps_satisfied(&jobs[i])) {
                start_job(&jobs[i], envp);
                started = 1;
            }
        }

        struct pollfd pfds[MAX_JOBS + 1];
        int map[MAX_JOBS + 1];
        int np = 0;
        int ctl_idx = -1;

        if (ctl_fd >= 0) {
            ctl_idx = np;
            pfds[np].fd      = ctl_fd;
            pfds[np].events  = POLLIN;
            pfds[np].revents = 0;
            np++;
        }

        for (int i = 0; i < num_jobs; i++) {
            if (jobs[i].pipe_fd >= 0) {
                map[np] = i;
                pfds[np].fd      = jobs[i].pipe_fd;
                pfds[np].events  = POLLIN;
                pfds[np].revents = 0;
                np++;
            }
        }

        if (np == 0 && !started) break;

        int need_anim = 0;
        for (int i = 0; i < num_jobs; i++)
            if (jobs[i].state == STATE_RUNNING)
                need_anim = 1;

        if (np > 0)
            poll(pfds, np, need_anim ? 120 : 5000);

        if (ctl_idx >= 0 && (pfds[ctl_idx].revents & POLLIN)) {
            int cfd = accept(ctl_fd, NULL, NULL);
            if (cfd >= 0)
                handle_ctl_client(cfd);
        }

        for (int i = (ctl_idx >= 0 ? 1 : 0); i < np; i++)
            if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR))
                process_pipe(&jobs[map[i]]);

        if (need_anim) {
            for (int i = 0; i < num_jobs; i++)
                if (jobs[i].state == STATE_RUNNING)
                    jobs[i].anim_tick++;
        }

        render_display();

        int any_running = 0;
        for (int i = 0; i < num_jobs; i++)
            if (jobs[i].state == STATE_RUNNING)
                any_running = 1;

        if (!any_running && !started && count_pending() > 0)
            stall++;
        else
            stall = 0;

        if (stall >= 6) {
            for (int i = 0; i < num_jobs; i++)
                if (jobs[i].state == STATE_PENDING)
                    jobs[i].state = STATE_FAILED;
            render_display();
            break;
        }
    }

    printf("\033[u");
    for (int i = 0; i < total_display_rows; i++)
        printf("\n");
    printf("\033[?25h");
    fflush(stdout);
}

static void spawn_daemon(const char *path, char **envp) {
    pid_t pid = fork();
    if (pid == 0) {
        char *argv[] = { (char *)path, NULL };
        execve(path, argv, envp);
        _exit(127);
    }
}

static void spawn_getty(int vty, char **envp) {
    pid_t pid = fork();
    if (pid == 0) {
        char pts[32];
        snprintf(pts, sizeof(pts), "/dev/pts/%d", vty);
        char *argv[] = { "/bin/getty", pts, NULL };
        execve("/bin/getty", argv, envp);
        _exit(127);
    }
}

static void sig_ignore(int sig) { (void)sig; }

static void service_loop(void) {
    for (;;) {
        struct pollfd pfds[MAX_JOBS + 1];
        int map[MAX_JOBS + 1];
        int np = 0;
        int ctl_idx = -1;

        if (ctl_fd >= 0) {
            ctl_idx = np;
            pfds[np].fd      = ctl_fd;
            pfds[np].events  = POLLIN;
            pfds[np].revents = 0;
            np++;
        }

        int pipe_base = np;
        for (int i = 0; i < num_jobs; i++) {
            if (jobs[i].pipe_fd >= 0) {
                map[np] = i;
                pfds[np].fd      = jobs[i].pipe_fd;
                pfds[np].events  = POLLIN;
                pfds[np].revents = 0;
                np++;
            }
        }

        if (np == 0) {
            int status;
            waitpid(-1, &status);
            continue;
        }

        poll(pfds, np, -1);

        if (ctl_idx >= 0 && (pfds[ctl_idx].revents & POLLIN)) {
            int cfd = accept(ctl_fd, NULL, NULL);
            if (cfd >= 0)
                handle_ctl_client(cfd);
        }

        for (int i = pipe_base; i < np; i++)
            if (pfds[i].revents & (POLLIN | POLLHUP | POLLERR))
                process_pipe(&jobs[map[i]]);

    }
}

int main(int argc, char **argv, char **envp) {
    (void)argc;
    (void)argv;

    setvbuf(stdout, NULL, _IONBF, 0);
    saved_envp = envp;

    signal(15, (void *)sig_ignore);
    signal(13, (void *)sig_ignore);

    printf("\nWelcome to \033[1m\033[38;2;122;152;255mSerotonin\033[0m!\n\n");

    bootstrap_filesystem();
    load_jobs();

    ctl_fd = init_control_socket();

    if (num_jobs == 0) {
        printf("init: no jobs in %s, using built-in defaults\n", JOB_DIR);
        spawn_daemon("/bin/lwipd", envp);
        for (int i = 0; i < NUM_KERNEL_VTYS; i++)
            spawn_getty(i, envp);
    } else {
        startup_loop(envp);
        printf("\n");
    }

    service_loop();
    return 0;
}
