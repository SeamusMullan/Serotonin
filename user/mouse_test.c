/*
 * mouse_test.c
 * Test application for blocking and non-blocking mouse event reads.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "syscall/lib5ht/lib5ht.h"

static const char *button_name(uint8_t buttons, uint8_t event_type) {
    if (event_type == MOUSE_EVENT_BUTTON_DOWN) {
        if (buttons & MOUSE_BTN_LEFT) return "LEFT DOWN";
        if (buttons & MOUSE_BTN_RIGHT) return "RIGHT DOWN";
        if (buttons & MOUSE_BTN_MIDDLE) return "MIDDLE DOWN";
    } else if (event_type == MOUSE_EVENT_BUTTON_UP) {
        if (!(buttons & MOUSE_BTN_LEFT)) return "LEFT UP";
        if (!(buttons & MOUSE_BTN_RIGHT)) return "RIGHT UP";
        if (!(buttons & MOUSE_BTN_MIDDLE)) return "MIDDLE UP";
    }
    return "UNKNOWN";
}

static void test_blocking_read(void) {
    printf("=== Blocking Read Test ===\n");
    printf("Opening /dev/mouse/event (blocking)...\n");

    int fd = open("/dev/mouse/event", O_RDONLY);
    if (fd < 0) {
        printf("Failed to open: errno=%d\n", errno);
        return;
    }
    printf("Opened fd=%d\n", fd);

    printf("Waiting for 3 mouse clicks (blocking)...\n");
    for (int i = 0; i < 3; i++) {
        mouse_event_t ev;
        int n = read(fd, &ev, sizeof(ev));
        if (n < 0) {
            printf("Read failed: errno=%d\n", errno);
            break;
        }
        if (n == sizeof(ev)) {
            printf("Event %d: pos=(%d,%d) buttons=0x%02x type=%s\n",
                   i + 1, ev.x, ev.y, ev.buttons, button_name(ev.buttons, ev.event_type));
        } else {
            printf("Event %d: unexpected n=%d (expected %d)\n", i + 1, n, (int)sizeof(ev));
        }
    }

    close(fd);
    printf("Blocking test complete.\n\n");
}

static void test_nonblocking_read(void) {
    printf("=== Non-Blocking Read Test ===\n");
    printf("Opening /dev/mouse/event (non-blocking)...\n");

    int fd = open("/dev/mouse/event", O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("Failed to open: errno=%d\n", errno);
        return;
    }
    printf("Opened fd=%d\n", fd);

    printf("Polling for events (5 attempts)...\n");
    for (int i = 0; i < 5; i++) {
        mouse_event_t ev;
        int n = read(fd, &ev, sizeof(ev));
        if (n < 0) {
            if (errno == EAGAIN) {
                printf("Attempt %d: No event available (EAGAIN)\n", i + 1);
            } else {
                printf("Attempt %d: Read failed errno=%d\n", i + 1, errno);
            }
        } else if (n == sizeof(ev)) {
            printf("Attempt %d: Event! pos=(%d,%d) buttons=0x%02x type=%s\n",
                   i + 1, ev.x, ev.y, ev.buttons, button_name(ev.buttons, ev.event_type));
        } else {
            printf("Attempt %d: unexpected n=%d (expected %d)\n", i + 1, n, (int)sizeof(ev));
        }
    }

    close(fd);
    printf("Non-blocking test complete.\n\n");
}

int main(void) {
    printf("Mouse Event Test\n");
    printf("================\n\n");

    test_nonblocking_read();
    test_blocking_read();

    printf("All tests complete.\n");
    return 0;
}
