#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

extern int shm_create(uint32_t size);
extern void* shm_map(int id);

void handler(int sig) {
    printf("[receiver] got signal %d!\n", sig);
}

int main() {
    printf("[receiver] PID = %d, handler:%p\n", getpid(), handler);
    signal(1, handler);

    int id = shm_create(4096);
    char* buf = (char*)shm_map(id);
    strcpy(buf, "hello from A");
    printf("[receiver] id=%d shm says: %s (%p)\n", id, buf, buf);

    while (1) {
        pause();
    }
}