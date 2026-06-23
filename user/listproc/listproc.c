/**
 * @file listproc.c
 * @brief Process listing utility for Serotonin OS
 *
 * Displays a formatted table of all running processes: PID, owning UID,
 * name, priority, privilege level, user-mode CPU ticks, kernel-mode CPU
 * ticks (syscalls etc. on this task's behalf), approximate memory usage,
 * and disk I/O.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <lib5ht.h>

static void format_bytes(char *dst, size_t dstsz, uint32_t bytes) {
    if (bytes >= (1u << 20))
        snprintf(dst, dstsz, "%lu.%luM",
                 (unsigned long)(bytes >> 20),
                 (unsigned long)(((bytes & ((1u << 20) - 1)) * 10u) >> 20));
    else if (bytes >= (1u << 10))
        snprintf(dst, dstsz, "%lu.%luK",
                 (unsigned long)(bytes >> 10),
                 (unsigned long)(((bytes & ((1u << 10) - 1)) * 10u) >> 10));
    else
        snprintf(dst, dstsz, "%luB", (unsigned long)bytes);
}

/**
 * Resolve a numeric uid to a username via /etc/passwd. Falls back to the
 * decimal uid if the lookup fails.
 */
static void resolve_username(unsigned uid, char *out, size_t outsize) {
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd < 0) {
        snprintf(out, outsize, "%u", uid);
        return;
    }

    char buf[1024];
    int n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        snprintf(out, outsize, "%u", uid);
        return;
    }
    buf[n] = '\0';

    // cppcheck-suppress constVariablePointer
    char *line = buf;
    while (line < buf + n) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line[0] != '\0' && line[0] != '#') {
            // cppcheck-suppress constVariablePointer
            char *c1 = strchr(line, ':');
            if (c1) {
                char *c2 = strchr(c1 + 1, ':');
                if (c2) {
                    unsigned entry_uid = 0;
                    for (char *p = c2 + 1; *p >= '0' && *p <= '9'; ++p)
                        entry_uid = entry_uid * 10u + (unsigned)(*p - '0');
                    if (entry_uid == uid) {
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
    snprintf(out, outsize, "%u", uid);
}

int main(void) {
    proc_5ht_t procs[128];
    memset(procs, 0, sizeof(procs));
    sys_5ht_list_processes(procs, 128);

    printf("%-5s | %-10s | %-20s | %-4s | %-6s | %-8s | %-8s | %-8s | %-8s\n",
           "PID", "USER", "NAME", "PRIO", "PRIV", "USR", "SYS", "MEM", "DISK");
    printf("-----------------------------------------------------------------------------------------------\n");

    for (int i = 0; i < 128; i++) {
        if (procs[i].name[0] == '\0')
            break;

        char mem_buf[16];
        char disk_buf[16];
        char user_buf[16];
        format_bytes(mem_buf,  sizeof(mem_buf),  procs[i].mem_bytes);
        format_bytes(disk_buf, sizeof(disk_buf), procs[i].disk_bytes);
        resolve_username(procs[i].uid, user_buf, sizeof(user_buf));

        printf("%s%-5d | %-10s | %-20s | %-4d | %-6s | %-8lu | %-8lu | %-8s | %-8s\033[39m\033[49m\n",
               (procs[i].priv == 0) ? "\033[38;2;255;200;140m" : "\033[38;2;170;210;255m",
               procs[i].pid,
               user_buf,
               procs[i].name,
               procs[i].priority,
               (procs[i].priv == 0) ? "kernel" : "user",
               (unsigned long)procs[i].cpu_user_ticks,
               (unsigned long)procs[i].cpu_kernel_ticks,
               mem_buf,
               disk_buf);
    }

    return 0;
}
