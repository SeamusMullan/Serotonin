/**
 * @file env.c
 * @brief Print environment variables for Serotonin OS
 *
 * Prints all environment variables to stdout, one per line.
 */

#include <stdio.h>

extern char **environ;

int main(void) {
    if (environ) {
        for (char **ep = environ; *ep; ep++)
            puts(*ep);
    }
    return 0;
}
