/**
 * @file initctl.h
 * @brief Shared IPC protocol between init and initctl
 */
#ifndef INITCTL_H
#define INITCTL_H

#include <stdint.h>

#define INITCTL_SOCK_PATH  "/tmp/init.sock"

#define INITCTL_CMD_START   1
#define INITCTL_CMD_STOP    2
#define INITCTL_CMD_STATUS  3

struct initctl_req {
    uint8_t cmd;
    char    name[31];
} __attribute__((packed));

struct initctl_rsp {
    int8_t  status;
    uint8_t count;
} __attribute__((packed));

struct initctl_job_info {
    char    name[32];
    uint8_t state;
    uint8_t type;
    int16_t pid;
} __attribute__((packed));

#define INITCTL_STATE_PENDING  0
#define INITCTL_STATE_RUNNING  1
#define INITCTL_STATE_READY    2
#define INITCTL_STATE_FAILED   3
#define INITCTL_STATE_EXITED   4

#define INITCTL_TYPE_ONESHOT   0
#define INITCTL_TYPE_DAEMON    1

#endif
