#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "lwip/serotonin/lwip_client.h"

static uint32_t parse_ip(const char *s) {
    unsigned a, b, c, d;
    if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
        return 0;
    if (a > 255 || b > 255 || c > 255 || d > 255)
        return 0;
    return a | (b << 8) | (c << 16) | (d << 24);
}

static const char *ip_to_str(uint32_t addr, char *buf) {
    unsigned a = addr & 0xff, b = (addr >> 8) & 0xff;
    unsigned c = (addr >> 16) & 0xff, d = (addr >> 24) & 0xff;
    sprintf(buf, "%u.%u.%u.%u", a, b, c, d);
    return buf;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    if (argc < 2) {
        printf("usage: ping <host> [-c count]\n");
        return 1;
    }

    int count = 4;
    if (argc >= 4 && strcmp(argv[2], "-c") == 0)
        count = atoi(argv[3]);

    uint32_t addr = parse_ip(argv[1]);
    if (!addr) {
        if (lwip_dns_resolve(argv[1], &addr) != 0 || !addr) {
            printf("ping: cannot resolve '%s'\n", argv[1]);
            return 1;
        }
        char ipbuf[16];
        printf("PING %s (%s): 32 data bytes\n", argv[1], ip_to_str(addr, ipbuf));
    } else {
        printf("PING %s: 32 data bytes\n", argv[1]);
    }

    lwip_session_t *s = lwip_session_open();
    if (!s) {
        printf("ping: cannot connect to lwipd\n");
        return 1;
    }

    int received = 0;
    for (int i = 0; i < count; i++) {
        uint32_t rtt;
        uint16_t seq;
        int ret = lwip_ping(s, addr, &rtt, &seq);

        if (ret == 0) {
            char ipbuf[16];
            printf("reply from %s: seq=%u time=%u ms\n",
                   ip_to_str(addr, ipbuf), (unsigned)seq, (unsigned)rtt);
            received++;
        } else if (ret == -2) {
            printf("request timeout for seq %d\n", i + 1);
        } else {
            printf("ping: error\n");
        }

        if (i < count - 1) {
            for (volatile int j = 0; j < 5000000; j++);
        }
    }

    printf("--- %s ping statistics ---\n", argv[1]);
    printf("%d transmitted, %d received, %d%% loss\n",
           count, received,
           count > 0 ? ((count - received) * 100) / count : 0);

    lwip_session_close(s);
    return (received > 0) ? 0 : 1;
}
