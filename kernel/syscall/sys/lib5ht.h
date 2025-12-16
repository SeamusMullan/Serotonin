#ifndef _KERNEL_LIB5HT
#define _KERNEL_LIB5HT

typedef struct proc_5ht {
    int pid;
    char name[32];
    int priority;
    int priv;
} proc_5ht_t;

#endif