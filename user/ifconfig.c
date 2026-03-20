/*
 * ifconfig.c – Query network interface configuration from lwipd
 */

#include <stdio.h>
#include <stdint.h>
#include "lwip/serotonin/lwip_client.h"

static void print_ip(const char *label, uint32_t addr) {
    printf("%s%u.%u.%u.%u\n", label,
           (unsigned)(addr & 0xff), (unsigned)((addr >> 8) & 0xff),
           (unsigned)((addr >> 16) & 0xff), (unsigned)((addr >> 24) & 0xff));
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    uint32_t ip, mask, gw;
    if (lwip_ifconfig(&ip, &mask, &gw) < 0) {
        printf("ifconfig: cannot reach lwipd\n");
        return 1;
    }

    print_ip("inet  ", ip);
    print_ip("mask  ", mask);
    print_ip("gw    ", gw);
    return 0;
}
