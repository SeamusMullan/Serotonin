/**
 * @file initctl.c
 * @brief CLI utility for managing the Serotonin init system
 *
 * Usage: initctl <command> [args...]
 *
 * Job management (talks to init via /tmp/init.sock):
 *   start <job>            Start a job
 *   stop <job>             Stop a running job
 *   restart <job>          Restart a job
 *   status                 Show all job statuses
 *
 * Job configuration (direct filesystem ops on /etc/init/):
 *   enable <job>           Enable a disabled job
 *   disable <job>          Disable a job (skipped on boot)
 *
 * System:
 *   hostname [name]        Get or set the system hostname
 *   useradd <name> [uid]   Add a user
 *   userdel <name>         Remove a user
 *   users                  List all users
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>

#include "syscall/sys/socket.h"
#include "init/initctl.h"

#define MAX_TAIL 16
#define PASSWD_PATH "/etc/passwd"
#define MAX_PASSWD  4096

int listdir(const char *path, char *buf, size_t size);


static int connect_init(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        printf("initctl: socket() failed\n");
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, INITCTL_SOCK_PATH, UNIX_PATH_MAX - 1);

    if (connect(fd, &addr, sizeof(addr)) < 0) {
        printf("initctl: cannot connect to init\n");
        close(fd);
        return -1;
    }
    return fd;
}

static int send_cmd(uint8_t cmd, const char *name) {
    int fd = connect_init();
    if (fd < 0) return -1;

    struct initctl_req req;
    memset(&req, 0, sizeof(req));
    req.cmd = cmd;
    if (name)
        strncpy(req.name, name, sizeof(req.name) - 1);

    send(fd, &req, sizeof(req), 0);

    struct initctl_rsp rsp;
    int n = recv(fd, &rsp, sizeof(rsp), 0);
    close(fd);

    if (n < (int)sizeof(rsp))
        return -1;
    return rsp.status;
}

static int cmd_start(const char *name) {
    int r = send_cmd(INITCTL_CMD_START, name);
    if (r == 0)
        printf("Started %s\n", name);
    else
        printf("Failed to start %s\n", name);
    return r;
}

static int cmd_stop(const char *name) {
    int r = send_cmd(INITCTL_CMD_STOP, name);
    if (r == 0)
        printf("Stopped %s\n", name);
    else
        printf("Failed to stop %s\n", name);
    return r;
}

static int cmd_restart(const char *name) {
    send_cmd(INITCTL_CMD_STOP, name);
    int r = send_cmd(INITCTL_CMD_START, name);
    if (r == 0)
        printf("Restarted %s\n", name);
    else
        printf("Failed to restart %s\n", name);
    return r;
}

static const char *state_str(uint8_t state) {
    switch (state) {
    case INITCTL_STATE_PENDING: return "pending";
    case INITCTL_STATE_RUNNING: return "starting";
    case INITCTL_STATE_READY:   return "running";
    case INITCTL_STATE_FAILED:  return "failed";
    case INITCTL_STATE_EXITED:  return "exited";
    default:                    return "unknown";
    }
}

static const char *state_color(uint8_t state) {
    switch (state) {
    case INITCTL_STATE_PENDING: return "\033[38;2;100;110;140m";
    case INITCTL_STATE_RUNNING: return "\033[38;2;220;190;80m";
    case INITCTL_STATE_READY:   return "\033[38;2;80;200;120m";
    case INITCTL_STATE_FAILED:  return "\033[38;2;220;80;80m";
    case INITCTL_STATE_EXITED:  return "\033[38;2;100;110;140m";
    default:                    return "\033[0m";
    }
}

static int cmd_status(void) {
    int fd = connect_init();
    if (fd < 0) return -1;

    struct initctl_req req;
    memset(&req, 0, sizeof(req));
    req.cmd = INITCTL_CMD_STATUS;
    send(fd, &req, sizeof(req), 0);

    struct initctl_rsp rsp;
    int n = recv(fd, &rsp, sizeof(rsp), 0);
    if (n < (int)sizeof(rsp) || rsp.status != 0) {
        close(fd);
        printf("initctl: failed to get status\n");
        return -1;
    }

    printf("\033[1m  %-16s %-10s %-6s %s\033[0m\n",
           "JOB", "STATE", "TYPE", "PID");

    for (int i = 0; i < rsp.count; i++) {
        struct initctl_job_info info;
        n = recv(fd, &info, sizeof(info), 0);
        if (n < (int)sizeof(info)) break;

        const char *type = (info.type == INITCTL_TYPE_DAEMON)
                           ? "daemon" : "oneshot";
        const char *col = state_color(info.state);

        printf("  %s%-16s %-10s\033[0m %-6s ",
               col, info.name, state_str(info.state), type);

        if (info.pid > 0)
            printf("%d", info.pid);
        else
            printf("-");
        printf("\n");
    }

    close(fd);
    return 0;
}

static const char *state_dot(uint8_t state) {
    switch (state) {
    case INITCTL_STATE_PENDING: return "\033[38;2;100;110;140m\xe2\x97\x8b";
    case INITCTL_STATE_RUNNING: return "\033[38;2;220;190;80m\xe2\x97\x8b";
    case INITCTL_STATE_READY:   return "\033[38;2;80;200;120m\xe2\x97\x8f";
    case INITCTL_STATE_FAILED:  return "\033[38;2;220;80;80m\xe2\x97\x8f";
    case INITCTL_STATE_EXITED:  return "\033[38;2;100;110;140m\xe2\x97\x8b";
    default:                    return "\033[0m?";
    }
}

static int cmd_status_job(const char *name) {
    int fd = connect_init();
    if (fd < 0) return -1;

    struct initctl_req req;
    memset(&req, 0, sizeof(req));
    req.cmd = INITCTL_CMD_STATUS;
    send(fd, &req, sizeof(req), 0);

    struct initctl_rsp rsp;
    int n = recv(fd, &rsp, sizeof(rsp), 0);
    if (n < (int)sizeof(rsp) || rsp.status != 0) {
        close(fd);
        printf("initctl: failed to get status\n");
        return -1;
    }

    struct initctl_job_info found;
    int have = 0;

    for (int i = 0; i < rsp.count; i++) {
        struct initctl_job_info info;
        n = recv(fd, &info, sizeof(info), 0);
        if (n < (int)sizeof(info)) break;
        if (strcmp(info.name, name) == 0) {
            found = info;
            have = 1;
        }
    }
    close(fd);

    if (!have) {
        printf("initctl: job '%s' not found\n", name);
        return -1;
    }

    const char *col = state_color(found.state);
    const char *dot = state_dot(found.state);
    const char *type = (found.type == INITCTL_TYPE_DAEMON)
                       ? "daemon" : "oneshot";

    printf("\n  %s\033[0m %s%s\033[0m\n", dot, col, found.name);
    printf("  \033[38;2;55;60;75m");
    for (int i = 0; i < 40; i++) printf("\xe2\x94\x80");
    printf("\033[0m\n");

    printf("  \033[38;2;140;145;165mState    %s%s\033[0m\n",
           col, state_str(found.state));
    printf("  \033[38;2;140;145;165mType     \033[38;2;200;205;220m%s\033[0m\n",
           type);
    printf("  \033[38;2;140;145;165mPID      \033[38;2;200;205;220m");
    if (found.pid > 0)
        printf("%d", found.pid);
    else
        printf("-");
    printf("\033[0m\n");

    char log_path[128];
    snprintf(log_path, sizeof(log_path), "/var/log/%s.log", name);

    int lfd = open(log_path, O_RDONLY);
    if (lfd < 0) {
        printf("\n  \033[38;2;75;80;95m(no log)\033[0m\n\n");
        return 0;
    }

    char logbuf[4096];
    int total = 0;
    int r;
    while (total < (int)sizeof(logbuf) - 1 &&
           (r = read(lfd, logbuf + total, sizeof(logbuf) - 1 - total)) > 0)
        total += r;
    close(lfd);
    logbuf[total] = '\0';

    if (total == 0) {
        printf("\n  \033[38;2;75;80;95m(empty log)\033[0m\n\n");
        return 0;
    }


    const char *lines[MAX_TAIL];
    int nlines = 0;

    const char *p = logbuf;
    while (*p) {
        if (nlines < MAX_TAIL)
            lines[nlines] = p;
        else {
            for (int i = 0; i < MAX_TAIL - 1; i++)
                lines[i] = lines[i + 1];
            lines[MAX_TAIL - 1] = p;
        }
        nlines++;
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    if (nlines > MAX_TAIL) nlines = MAX_TAIL;

    printf("\n  \033[38;2;55;60;75m");
    for (int i = 0; i < 40; i++) printf("\xe2\x94\x80");
    printf("\033[0m\n");
    printf("  \033[1;38;2;140;145;165mLog\033[0m "
           "\033[38;2;75;80;95m%s\033[0m\n\n", log_path);

    for (int i = 0; i < nlines; i++) {
        const char *line = lines[i];
        const char *eol = line;
        while (*eol && *eol != '\n') eol++;
        int len = (int)(eol - line);
        if (len == 0) continue;

        int maxw = 68;
        printf("  \033[38;2;90;100;125m");
        if (len <= maxw)
            printf("%.*s", len, line);
        else
            printf("%.*s...", maxw, line);
        printf("\033[0m\n");
    }
    printf("\n");

    return 0;
}

static int copy_file(const char *src_path, const char *dst_path) {
    int src = open(src_path, O_RDONLY);
    if (src < 0) return -1;

    char buf[1024];
    int len = 0, n;
    while (len < (int)sizeof(buf) - 1 &&
           (n = read(src, buf + len, sizeof(buf) - 1 - len)) > 0)
        len += n;
    close(src);

    int dst = open(dst_path, O_CREAT | O_WRONLY);
    if (dst < 0) return -1;
    write(dst, buf, len);
    close(dst);
    return 0;
}

static int cmd_enable(const char *name) {
    char flag[128], path[128];
    snprintf(flag, sizeof(flag), "/etc/init/%s.disabled", name);
    snprintf(path, sizeof(path), "/etc/init/%s", name);

    struct stat st;
    if (stat(flag, &st) != 0) {
        printf("%s is already enabled\n", name);
        return 0;
    }

    if (copy_file(flag, path) < 0) {
        printf("initctl: cannot restore %s\n", path);
        return -1;
    }
    unlink(flag);

    printf("Enabled %s\n", name);
    return 0;
}

static int cmd_disable(const char *name) {
    char path[128], flag[128];
    snprintf(path, sizeof(path), "/etc/init/%s", name);
    snprintf(flag, sizeof(flag), "/etc/init/%s.disabled", name);

    struct stat st;
    if (stat(path, &st) != 0) {
        printf("initctl: job '%s' not found\n", name);
        return -1;
    }
    if (stat(flag, &st) == 0) {
        printf("%s is already disabled\n", name);
        return 0;
    }

    if (copy_file(path, flag) < 0) {
        printf("initctl: cannot create %s\n", flag);
        return -1;
    }
    unlink(path);

    printf("Disabled %s\n", name);
    return 0;
}

static int cmd_hostname(int argc, char **argv) {
    if (argc >= 3) {
        const char *name = argv[2];
        if (sethostname(name, strlen(name)) < 0) {
            printf("initctl: sethostname failed\n");
            return -1;
        }
        int fd = open("/etc/hostname", O_CREAT | O_WRONLY);
        if (fd >= 0) {
            write(fd, name, strlen(name));
            write(fd, "\n", 1);
            close(fd);
        }
        printf("%s\n", name);
    } else {
        char buf[65];
        if (gethostname(buf, sizeof(buf)) < 0) {
            printf("initctl: gethostname failed\n");
            return -1;
        }
        printf("%s\n", buf);
    }
    return 0;
}

static int read_passwd(char *buf, int bufsz) {
    int fd = open(PASSWD_PATH, O_RDONLY);
    if (fd < 0) return -1;

    int total = 0;
    int n;
    while (total < bufsz - 1 &&
           (n = read(fd, buf + total, bufsz - 1 - total)) > 0)
        total += n;
    close(fd);
    buf[total] = '\0';
    return total;
}

static int write_passwd(const char *buf, int len) {
    unlink(PASSWD_PATH);
    int fd = open(PASSWD_PATH, O_CREAT | O_WRONLY);
    if (fd < 0) return -1;
    write(fd, buf, len);
    close(fd);
    chmod(PASSWD_PATH, 0644);
    return 0;
}

static int find_user(const char *data, const char *name, int namelen) {
    const char *p = data;
    while (*p) {
        if (strncmp(p, name, namelen) == 0 && p[namelen] == ':')
            return 1;
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return 0;
}

static int find_max_uid(const char *data) {
    int max_uid = 999;
    const char *p = data;

    while (*p) {
        while (*p && *p != ':') p++;
        if (!*p) break; p++;
        while (*p && *p != ':') p++;
        if (!*p) break; p++;
        int uid = 0;
        while (*p >= '0' && *p <= '9')
            uid = uid * 10 + (*p++ - '0');
        if (uid > max_uid) max_uid = uid;
        while (*p && *p != '\n') p++;
        if (*p) p++;
    }
    return max_uid;
}

static int cmd_useradd(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: initctl useradd <name> [uid]\n");
        return -1;
    }
    const char *name = argv[2];

    char data[MAX_PASSWD];
    int len = read_passwd(data, sizeof(data));
    if (len < 0) {
        printf("initctl: cannot read %s\n", PASSWD_PATH);
        return -1;
    }

    if (find_user(data, name, strlen(name))) {
        printf("initctl: user '%s' already exists\n", name);
        return -1;
    }

    int uid;
    if (argc >= 4)
        uid = atoi(argv[3]);
    else
        uid = find_max_uid(data) + 1;

    char home[128];
    snprintf(home, sizeof(home), "/home/%s", name);
    mkdir(home, 0755);

    char line[256];
    int llen = snprintf(line, sizeof(line),
                        "%s:x:%d:%d::%s:/bin/sh\n",
                        name, uid, uid, home);

    int fd = open(PASSWD_PATH, O_WRONLY);
    if (fd < 0) {
        printf("initctl: cannot open %s\n", PASSWD_PATH);
        return -1;
    }
    lseek(fd, 0, 2);
    write(fd, line, llen);
    close(fd);

    printf("Added user %s (uid %d)\n", name, uid);
    return 0;
}

static int cmd_userdel(const char *name) {
    char data[MAX_PASSWD];
    int len = read_passwd(data, sizeof(data));
    if (len < 0) {
        printf("initctl: cannot read %s\n", PASSWD_PATH);
        return -1;
    }

    int namelen = strlen(name);
    if (!find_user(data, name, namelen)) {
        printf("initctl: user '%s' not found\n", name);
        return -1;
    }

    char out[MAX_PASSWD];
    int olen = 0;
    const char *p = data;

    while (*p) {
        const char *eol = p;
        while (*eol && *eol != '\n') eol++;
        int line_len = (int)(eol - p);

        if (!(strncmp(p, name, namelen) == 0 && p[namelen] == ':')) {
            memcpy(out + olen, p, line_len);
            olen += line_len;
            if (*eol == '\n') out[olen++] = '\n';
        }

        p = (*eol) ? eol + 1 : eol;
    }
    out[olen] = '\0';

    write_passwd(out, olen);
    printf("Removed user %s\n", name);
    return 0;
}

static int cmd_users(void) {
    char data[MAX_PASSWD];
    int len = read_passwd(data, sizeof(data));
    if (len < 0) {
        printf("initctl: cannot read %s\n", PASSWD_PATH);
        return -1;
    }

    printf("\033[1m  %-12s %-6s %-6s %-16s %s\033[0m\n",
           "USER", "UID", "GID", "HOME", "SHELL");

    const char *p = data;
    while (*p) {
        char uname[64] = {0}, pass[16] = {0};
        char uid_s[16] = {0}, gid_s[16] = {0};
        char gecos[64] = {0}, home[128] = {0}, shell[64] = {0};
        char *fields[] = { uname, pass, uid_s, gid_s, gecos, home, shell };
        int fi = 0;
        int ci = 0;

        while (*p && *p != '\n' && fi < 7) {
            if (*p == ':') {
                fields[fi][ci] = '\0';
                fi++;
                ci = 0;
            } else if (ci < 63) {
                fields[fi][ci++] = *p;
            }
            p++;
        }
        if (fi < 7) fields[fi][ci] = '\0';
        while (*p && *p != '\n') p++;
        if (*p) p++;

        if (uname[0])
            printf("  %-12s %-6s %-6s %-16s %s\n",
                   uname, uid_s, gid_s, home, shell);
    }
    return 0;
}

static void usage(void) {
    printf("Usage: initctl <command> [args...]\n\n"
           "Job control:\n"
           "  start <job>          Start a job\n"
           "  stop <job>           Stop a running job\n"
           "  restart <job>        Restart a job\n"
           "  status               Show all job statuses\n"
           "  status <job>         Show job details and log\n\n"
           "Configuration:\n"
           "  enable <job>         Enable a disabled job\n"
           "  disable <job>        Disable a job (skipped on boot)\n\n"
           "System:\n"
           "  hostname [name]      Get or set hostname\n"
           "  useradd <name> [uid] Add a user\n"
           "  userdel <name>       Remove a user\n"
           "  users                List all users\n");
}

int main(int argc, char **argv) {
    if (argc < 2) {
        usage();
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "start") == 0) {
        if (argc < 3) { printf("Usage: initctl start <job>\n"); return 1; }
        return cmd_start(argv[2]);
    }
    if (strcmp(cmd, "stop") == 0) {
        if (argc < 3) { printf("Usage: initctl stop <job>\n"); return 1; }
        return cmd_stop(argv[2]);
    }
    if (strcmp(cmd, "restart") == 0) {
        if (argc < 3) { printf("Usage: initctl restart <job>\n"); return 1; }
        return cmd_restart(argv[2]);
    }
    if (strcmp(cmd, "status") == 0)
        return (argc >= 3) ? cmd_status_job(argv[2]) : cmd_status();
    if (strcmp(cmd, "enable") == 0) {
        if (argc < 3) { printf("Usage: initctl enable <file>\n"); return 1; }
        return cmd_enable(argv[2]);
    }
    if (strcmp(cmd, "disable") == 0) {
        if (argc < 3) { printf("Usage: initctl disable <job>\n"); return 1; }
        return cmd_disable(argv[2]);
    }
    if (strcmp(cmd, "hostname") == 0)
        return cmd_hostname(argc, argv);
    if (strcmp(cmd, "useradd") == 0)
        return cmd_useradd(argc, argv);
    if (strcmp(cmd, "userdel") == 0) {
        if (argc < 3) { printf("Usage: initctl userdel <name>\n"); return 1; }
        return cmd_userdel(argv[2]);
    }
    if (strcmp(cmd, "users") == 0)
        return cmd_users();

    printf("initctl: unknown command '%s'\n", cmd);
    usage();
    return 1;
}
