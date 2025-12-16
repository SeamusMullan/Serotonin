#include "lib5ht.h"
#include "../syscall_table.h"

int sys_5ht_list_processes(proc_5ht_t *buf, size_t max) {
    return do_syscall(SYSTEM_CALL_5HT_LIST_PROC, (uint32_t)buf, (uint32_t)max, 0);
}