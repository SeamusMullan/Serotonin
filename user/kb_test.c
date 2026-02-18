/*
 * kb_test.c
 * Test application for blocking and non-blocking keyboard event reads.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "syscall/lib5ht/lib5ht.h"

static const char *flag_str(uint8_t flags) {
    static char buf[32];
    buf[0] = '\0';
    if (flags & KEY_FLAG_RELEASED) strcat(buf, "REL ");
    if (flags & KEY_FLAG_SHIFT)    strcat(buf, "SHIFT ");
    if (flags & KEY_FLAG_CTRL)     strcat(buf, "CTRL ");
    if (buf[0] == '\0') strcat(buf, "PRESS");
    return buf;
}

static void print_ascii(uint8_t ascii) {
    if (ascii >= 0x20 && ascii < 0x7F) {
        printf("'%c'", ascii);
    } else if (ascii == '\n') {
        printf("'\\n'");
    } else if (ascii == '\b') {
        printf("'\\b'");
    } else if (ascii == '\t') {
        printf("'\\t'");
    } else if (ascii == 27) {
        printf("ESC");
    } else {
        printf("0x%02x", ascii);
    }
}

static void test_nonblocking_read(void) {
    printf("=== Non-Blocking Read Test ===\n");
    printf("Opening /dev/keyboard/event (non-blocking)...\n");

    int fd = open("/dev/keyboard/event", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("Failed to open: errno=%d\n", errno);
        return;
    }
    printf("Opened fd=%d\n", fd);

    printf("Polling for events (15 attempts)...\n");
    for (int i = 0; i < 15; i++) {
        keyboard_event_t ev;
        int n = read(fd, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EAGAIN) {
                printf("  Attempt %d: No event available (EAGAIN)\n", i + 1);
            } else {
                printf("  Attempt %d: Read failed errno=%d\n", i + 1, errno);
            }
        } else if (n == sizeof(ev)) {
            printf("  Attempt %d: scancode=0x%02x ascii=", i + 1, ev.scancode);
            print_ascii(ev.ascii);
            printf(" [%s]\n", flag_str(ev.flags));
        } else {
            printf("  Attempt %d: unexpected n=%d\n", i + 1, n);
        }
    }

    close(fd);
    printf("Non-blocking test complete.\n\n");
}

static void test_blocking_read(void) {
    printf("=== Blocking Read Test ===\n");
    printf("Opening /dev/keyboard/event (blocking)...\n");

    int fd = open("/dev/keyboard/event", O_RDONLY);
    if (fd < 0) {
        printf("Failed to open: errno=%d\n", errno);
        return;
    }
    printf("Opened fd=%d\n", fd);

    printf("Press 10 keys (blocking read)...\n");
    for (int i = 0; i < 10; i++) {
        keyboard_event_t ev;
        int n = read(fd, &ev, sizeof(ev));
        if (n < 0) {
            printf("  Read failed: errno=%d\n", errno);
            break;
        }
        if (n == sizeof(ev)) {
            printf("  [%2d] scancode=0x%02x ascii=", i + 1, ev.scancode);
            print_ascii(ev.ascii);
            printf(" [%s]\n", flag_str(ev.flags));
        } else {
            printf("  [%2d] unexpected n=%d\n", i + 1, n);
        }
    }

    close(fd);
    printf("Blocking test complete.\n\n");
}

int main(void) {
    printf("Keyboard Event Test\n");
    printf("====================\n\n");

    test_nonblocking_read();
    test_blocking_read();

    printf("All tests complete.\n");
    return 0;
}
