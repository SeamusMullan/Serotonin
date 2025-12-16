#include <stdio.h>
#include "../syscall/lib5ht/lib5ht.h"

int main(void) {

    proc_5ht_t procs[10];
    sys_5ht_list_processes(procs, 128);


    printf("%-5s | %-20s | %-8s | %-12s\n", "PID", "NAME", "PRIORITY", "PRIVILEGE");
    printf("-----------------------------------------------------------\n");

    for (int i = 0; i < 128; i++) {
        if (procs[i].name[0] == '\0')
            break;
        printf("%s%-5d | %-20s | %-8d | %-12s\033[39m\033[49m\n",(procs[i].priv == 0) ? "\033[38;2;255;200;140m" : "\033[38;2;170;210;255m", procs[i].pid, procs[i].name, procs[i].priority, (procs[i].priv == 0) ? "kernel mode" : "user mode");
    }

    return 0;
}