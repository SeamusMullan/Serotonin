/**
 * @file whoami.c
 * @brief Print effective username for Serotonin OS
 *
 * Looks up the effective user ID in /etc/passwd and prints the username.
 * Falls back to printing the numeric UID if lookup fails.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    uid_t uid = getuid();

    FILE *fp = fopen("/etc/passwd", "r");
    if (fp) {
        char line[1024];
        while (fgets(line, sizeof(line), fp)) {
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\n')
                line[len - 1] = '\0';

            /* Format: name:password:uid:gid:... */
            // cppcheck-suppress variableScope
            // cppcheck-suppress constVariablePointer
            char *name = line;
            char *p = strchr(line, ':');
            if (!p) continue;
            *p++ = '\0';

            /* Skip password field */
            p = strchr(p, ':');
            if (!p) continue;
            p++;

            /* Parse uid field */
            long file_uid = strtol(p, NULL, 10);
            if (file_uid == (long)uid) {
                puts(name);
                fclose(fp);
                return 0;
            }
        }
        fclose(fp);
    }

    /* Fallback: print numeric uid */
    printf("%u\n", (unsigned)uid);
    return 0;
}
