#include <stdio.h>
#include <signal.h>
#include <unistd.h>

void handler(int sig) {
    printf("[receiver] got signal %d!\n", sig);
}

int main() {
    printf("[receiver] PID = %d, handler:%p\n", getpid(), handler);
    signal(1, handler);

    while (1) {
        pause();
    }
}