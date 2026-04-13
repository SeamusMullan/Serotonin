/**
 * @file uname_cmd.c
 * @brief System information utility for Serotonin OS
 *
 * Prints system information using the uname() syscall.
 * Supports -s (sysname), -n (nodename), -r (release),
 * -v (version), -m (machine), -a (all).
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

int uname(struct utsname *buf);

int main(int argc, char **argv) {
    struct utsname uts;
    int opt_s = 0, opt_n = 0, opt_r = 0, opt_v = 0, opt_m = 0;

    /* Parse flags */
    for (int i = 1; i < argc; i++) {
        if (argv[i][0] != '-') {
            printf("uname: unexpected argument '%s'\n", argv[i]);
            return 1;
        }
        for (int j = 1; argv[i][j]; j++) {
            switch (argv[i][j]) {
            case 's': opt_s = 1; break;
            case 'n': opt_n = 1; break;
            case 'r': opt_r = 1; break;
            case 'v': opt_v = 1; break;
            case 'm': opt_m = 1; break;
            case 'a': opt_s = opt_n = opt_r = opt_v = opt_m = 1; break;
            default:
                printf("uname: unknown option '-%c'\n", argv[i][j]);
                return 1;
            }
        }
    }

    /* Default: -s */
    if (!opt_s && !opt_n && !opt_r && !opt_v && !opt_m) {
        opt_s = 1;
    }

    if (uname(&uts) < 0) {
        printf("uname: failed (errno=%d)\n", errno);
        return 1;
    }

    int first = 1;

    if (opt_s) { printf("%s%s", first ? "" : " ", uts.sysname); first = 0; }
    if (opt_n) { printf("%s%s", first ? "" : " ", uts.nodename); first = 0; }
    if (opt_r) { printf("%s%s", first ? "" : " ", uts.release); first = 0; }
    if (opt_v) { printf("%s%s", first ? "" : " ", uts.version); first = 0; }
    if (opt_m) { printf("%s%s", first ? "" : " ", uts.machine); first = 0; }

    printf("\n");
    return 0;
}
