/**
 * @file chown_cmd.c
 * @brief Change file owner and group for Serotonin OS
 *
 * Supports numeric OWNER[:GROUP] syntax only.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: chown OWNER[:GROUP] FILE...\n");
        return 1;
    }

    const char *spec = argv[1];
    // cppcheck-suppress constVariablePointer
    char *colon = strchr(spec, ':');

    long uid, gid;
    char *endptr;

    if (colon) {
        /* Temporarily null-terminate for uid parsing */
        size_t uid_len = (size_t)(colon - spec);
        char uid_str[32];
        if (uid_len >= sizeof(uid_str)) {
            printf("chown: invalid owner: %s\n", spec);
            return 1;
        }
        memcpy(uid_str, spec, uid_len);
        uid_str[uid_len] = '\0';

        uid = strtol(uid_str, &endptr, 10);
        if (*endptr != '\0') {
            printf("chown: invalid owner: %s\n", uid_str);
            return 1;
        }

        gid = strtol(colon + 1, &endptr, 10);
        if (*endptr != '\0') {
            printf("chown: invalid group: %s\n", colon + 1);
            return 1;
        }
    } else {
        uid = strtol(spec, &endptr, 10);
        if (*endptr != '\0') {
            printf("chown: invalid owner: %s\n", spec);
            return 1;
        }
        gid = -1; /* Leave group unchanged */
    }

    int status = 0;

    for (int i = 2; i < argc; i++) {
        if (chown(argv[i], (int)uid, (int)gid) < 0) {
            printf("chown: %s: cannot change owner (errno=%d)\n",
                   argv[i], errno);
            status = 1;
        }
    }

    return status;
}
