/**
 * @file fetch.c
 * @brief System information display utility for Serotonin OS
 *
 * Displays system info (hostname, OS, kernel, architecture, processes)
 * alongside an ASCII art logo, similar to neofetch/fastfetch.
 */

#include "syscall/lib5ht/lib5ht.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <lib5ht.h>

int uname(void *buf);
int gethostname(char *name, size_t len);
int snprintf(char *str, size_t size, const char *fmt, ...);

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

static int count_processes(void) {
    proc_5ht_t procs[128];
    memset(procs, 0, sizeof(procs));
    sys_5ht_list_processes(procs, 128);

    int count = 0;
    for (int i = 0; i < 128; i++) {
        if (procs[i].name[0] == '\0')
            break;
        count++;
    }
    return count;
}

static void get_username(uid_t uid, char *out, size_t outsize) {
    int fd = open("/etc/passwd", 0);
    if (fd < 0) { strncpy(out, "?", outsize); return; }

    char buf[1024];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) { strncpy(out, "?", outsize); return; }
    buf[n] = '\0';

    char *line = buf;
    while (line < buf + n) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line[0] != '\0' && line[0] != '#') {
            char *c1 = strchr(line, ':');
            if (c1) {
                char *c2 = strchr(c1 + 1, ':');
                if (c2) {
                    int entry_uid = 0;
                    char *p = c2 + 1;
                    while (*p >= '0' && *p <= '9')
                        entry_uid = entry_uid * 10 + (*p++ - '0');
                    if (entry_uid == (int)uid) {
                        size_t ulen = (size_t)(c1 - line);
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

/* Colors */
#define C_BOLD   "\033[1m"
#define C_BLUE   "\033[38;2;122;152;255m"
#define C_CYAN   "\033[38;2;130;210;230m"
#define C_WHITE  "\033[38;2;220;220;230m"
#define C_GREY   "\033[38;2;140;140;160m"
#define C_RESET  "\033[0m"

#define LOGO_WIDTH 22
#define INFO_LINES 9

int main(void) {
    struct utsname uts;
    memset(&uts, 0, sizeof(uts));
    uname(&uts);

    sysinfo_5ht_t sysinfo = {0};
    memset(&sysinfo, 0, sizeof(sysinfo));
    sys_5ht_sysinfo(&sysinfo);

    char hostname[65];
    if (gethostname(hostname, sizeof(hostname)) < 0)
        strcpy(hostname, "serotonin");

    char username[32];
    get_username(getuid(), username, sizeof(username));

    int nprocs = count_processes();

    /* Info lines */
    char info[INFO_LINES][256];

    snprintf(info[0], sizeof(info[0]),
        C_CYAN C_BOLD "%s" C_RESET "@" C_CYAN C_BOLD "%s" C_RESET,
        username, hostname);

    /* Separator: length of user@host */
    {
        int sep_len = strlen(username) + 1 + strlen(hostname);
        char sep[128];
        if (sep_len > (int)sizeof(sep) - 1) sep_len = (int)sizeof(sep) - 1;
        memset(sep, '-', sep_len);
        sep[sep_len] = '\0';
        snprintf(info[1], sizeof(info[1]), C_GREY "%s" C_RESET, sep);
    }

    snprintf(info[2], sizeof(info[2]),
        C_CYAN C_BOLD "OS" C_RESET C_WHITE ":        %s" C_RESET, uts.sysname);

    snprintf(info[3], sizeof(info[3]),
        C_CYAN C_BOLD "Kernel" C_RESET C_WHITE ":    %s" C_RESET, uts.release);

    snprintf(info[4], sizeof(info[4]),
        C_CYAN C_BOLD "Arch" C_RESET C_WHITE ":      %s" C_RESET, uts.machine);

    snprintf(info[5], sizeof(info[5]),
        C_CYAN C_BOLD "Host" C_RESET C_WHITE ":      %s" C_RESET, hostname);

    snprintf(info[6], sizeof(info[6]),
        C_CYAN C_BOLD "Procs" C_RESET C_WHITE ":     %d" C_RESET, nprocs);

    snprintf(info[7], sizeof(info[6]),
        C_CYAN C_BOLD "Memory" C_RESET C_WHITE ":    %d MB / %d MB" C_RESET, sysinfo.mem_total-sysinfo.mem_free, sysinfo.mem_total);

    /* Color palette */
    snprintf(info[8], sizeof(info[7]),
        "\033[40m  \033[41m  \033[42m  \033[43m  \033[44m  \033[45m  \033[46m  \033[47m  " C_RESET);

    printf("\n");
    for (int i = 0; i < INFO_LINES; i++) {
        printf("%s\n", info[i]);
    }
    printf("\n");

    return 0;
}
