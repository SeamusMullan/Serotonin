/**
 * @file yes.c
 * @brief Repeatedly output a string for Serotonin OS
 *
 * Outputs "y" or a user-specified string repeatedly until killed.
 */

#include <stdio.h>

int main(int argc, char **argv) {
    const char *str = "y";
    if (argc > 1)
        str = argv[1];

    for (;;)
        puts(str);

    return 0;
}
