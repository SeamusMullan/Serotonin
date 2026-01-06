#include "lib5ht.h"
#include "../syscall_table.h"

int sys_5ht_list_processes(proc_5ht_t *buf, size_t max) {
    return do_syscall(SYSTEM_CALL_5HT_LIST_PROC, (uint32_t)buf, (uint32_t)max, 0);
}

int sys_5ht_req_buf(uint16_t id, const fb_layer_config_t *cfg, fb_layer_info_t *out) {
    return do_syscall(SYSTEM_CALL_5HT_REQ_BUF, (uint32_t)id, (uint32_t)cfg, (uint32_t)out);
}

int sys_5ht_rel_buf(uint16_t id) {
    return do_syscall(SYSTEM_CALL_5HT_REL_BUF, (uint32_t)id, 0, 0);
}

int sys_5ht_rcfg_layer(uint16_t id, const fb_layer_config_t *cfg, fb_layer_info_t *out) {
    return do_syscall(SYSTEM_CALL_5HT_RCFG_LAYER, (uint32_t)id, (uint32_t)cfg, (uint32_t)out);
}

int sys_5ht_query_info(fb_info_t *out) {
    return do_syscall(SYSTEM_CALL_5HT_QUERY_INFO, (uint32_t)out, 0, 0);
}

int sys_5ht_query_layer(uint16_t id, fb_layer_info_t *out) {
    return do_syscall(SYSTEM_CALL_5HT_QUERY_LAYER, (uint32_t)id, (uint32_t)out, 0);
}
