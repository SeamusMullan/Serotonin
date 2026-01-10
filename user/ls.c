/**
 * @file ls.c
 * @brief Directory listing utility for Serotonin OS
 *
 * A simple implementation of the ls command that displays
 * the contents of a directory using the listdir system call.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/**
 * @brief List directory contents
 *
 * System call wrapper to read directory entries into a buffer.
 *
 * @param path Directory path to list
 * @param buf Buffer to store directory listing
 * @param size Buffer size
 * @return Number of bytes written to buffer on success, -1 on error
 */
int listdir(const char *path, char *buf, size_t size);

/**
 * @brief Main entry point for ls command
 *
 * Lists the contents of the specified directory, or current
 * directory if no argument is provided.
 *
 * @param argc Argument count
 * @param argv Argument vector (optional directory path)
 * @return 0 on success, 1 on error
 */
int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : ".";
    char buf[4096];

    errno = 0;
    int ret = listdir(path, buf, sizeof(buf));
    if (errno != 0) {
        printf("ls: failed to list directory (errno=%d)\n", errno);
        return 1;
    }

    if (ret > 0) {
        write(1, buf, (size_t)ret);
    }

    return 0;
}
