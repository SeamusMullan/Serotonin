/*
 * nettest.c
 * RTL8139 NIC test program for Serotonin OS
 *
 * Opens /dev/rtl8139/tun and exercises send/receive.
 * Tests:
 *   1. Open device and read MAC address
 *   2. Send a raw Ethernet broadcast frame
 *   3. Non-blocking receive
 *   4. Send ARP who-has 10.0.2.1 (host tap)
 *   5. Blocking receive with alarm timeout
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* must match kernel's rtl8139_packet_t / dev_rtl8139.h */
#define ETH_ALEN      6
#define ETH_FRAME_MAX 1514

typedef struct {
    unsigned char src_mac[ETH_ALEN];
    unsigned char dst_mac[ETH_ALEN];
    unsigned short length;
    unsigned char data[ETH_FRAME_MAX];
} __attribute__((packed)) rtl8139_packet_t;

static const char *TUN_PATH = "/dev/rtl8139/tun";
static const char *MAC_PATH = "/dev/rtl8139/mac";

static const unsigned char BROADCAST[ETH_ALEN] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};

static unsigned char our_mac[ETH_ALEN];

static void print_mac(const char *label, const unsigned char *mac) {
    printf("%s: %02x:%02x:%02x:%02x:%02x:%02x\n",
        label, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void print_hex(const unsigned char *buf, int len) {
    for (int i = 0; i < len && i < 64; i++) {
        printf("%02x ", buf[i]);
        if ((i & 0xF) == 0xF)
            printf("\n");
    }
    if (len > 64)
        printf("... (%d bytes total)", len);
    printf("\n");
}

/* ---- Test 1: open device + read MAC ---- */
static int test_open(void) {
    printf("[test 1] opening %s ... ", TUN_PATH);

    int fd = open(TUN_PATH, O_RDWR);
    if (fd < 0) {
        printf("FAIL (errno=%d)\n", errno);
        return -1;
    }
    printf("OK (fd=%d)\n", fd);

    /* read MAC from /dev/rtl8139/mac */
    int mac_fd = open(MAC_PATH, O_RDONLY);
    if (mac_fd < 0) {
        printf("  could not open %s (errno=%d)\n", MAC_PATH, errno);
        close(fd);
        return -1;
    }
    int n = read(mac_fd, our_mac, ETH_ALEN);
    close(mac_fd);
    if (n != ETH_ALEN) {
        printf("  failed to read MAC (got %d bytes)\n", n);
        close(fd);
        return -1;
    }
    print_mac("  MAC", our_mac);
    return fd;
}

/* ---- Test 2: send a broadcast frame ---- */
static int test_send(int fd) {
    printf("[test 2] sending broadcast frame ... ");

    rtl8139_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    memcpy(pkt.dst_mac, BROADCAST, ETH_ALEN);
    /* src_mac = zero → kernel fills from card */

    /* EtherType 0x8899 (Realtek test) + message */
    const char *msg = "Serotonin RTL8139 test frame";
    int msg_len = (int)strlen(msg);
    pkt.data[0] = 0x88;
    pkt.data[1] = 0x99;
    memcpy(pkt.data + 2, msg, msg_len);
    pkt.length = 2 + msg_len;

    int n = write(fd, &pkt, sizeof(pkt));
    if (n < 0) {
        printf("FAIL (errno=%d)\n", errno);
        return -1;
    }
    printf("OK (%d bytes written)\n", n);
    return 0;
}

/* ---- Test 3: non-blocking receive ---- */
static int test_recv_nonblock(void) {
    printf("[test 3] non-blocking receive ... ");

    int fd = open(TUN_PATH, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        printf("FAIL open (errno=%d)\n", errno);
        return -1;
    }

    rtl8139_packet_t pkt;
    int n = read(fd, &pkt, sizeof(pkt));

    if (n < 0 && errno == EAGAIN) {
        printf("OK (no packets waiting, EAGAIN as expected)\n");
        close(fd);
        return 0;
    } else if (n > 0) {
        printf("OK (got packet, %d bytes)\n", n);
        print_mac("  src", pkt.src_mac);
        print_mac("  dst", pkt.dst_mac);
        printf("  payload length: %d\n", pkt.length);
        print_hex(pkt.data, pkt.length > 64 ? 64 : pkt.length);
        close(fd);
        return 0;
    } else {
        printf("FAIL (n=%d errno=%d)\n", n, errno);
        close(fd);
        return -1;
    }
}

/* ---- Test 4: send ARP who-has 10.0.2.1 ---- */
static int test_arp(int fd) {
    printf("[test 4] sending ARP who-has 10.0.2.1 ... ");

    rtl8139_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));

    memcpy(pkt.dst_mac, BROADCAST, ETH_ALEN);
    /* src_mac = zero → kernel fills from card */

    unsigned char *d = pkt.data;

    /* EtherType: ARP */
    d[0] = 0x08; d[1] = 0x06;

    /* ARP header */
    d[2] = 0x00; d[3] = 0x01;   /* hardware type: Ethernet */
    d[4] = 0x08; d[5] = 0x00;   /* protocol type: IPv4 */
    d[6] = 0x06;                 /* hardware addr len */
    d[7] = 0x04;                 /* protocol addr len */
    d[8] = 0x00; d[9] = 0x01;   /* opcode: request */

    /* sender hardware address: our MAC */
    memcpy(d + 10, our_mac, ETH_ALEN);
    /* sender protocol address: 10.0.2.15 */
    d[16] = 10; d[17] = 0; d[18] = 2; d[19] = 15;
    /* target hardware address: zero (unknown) */
    memset(d + 20, 0, 6);
    /* target protocol address: 10.0.2.1 (host tap) */
    d[26] = 10; d[27] = 0; d[28] = 2; d[29] = 1;

    pkt.length = 30; /* EtherType(2) + ARP(28) */

    int n = write(fd, &pkt, sizeof(pkt));
    if (n < 0) {
        printf("FAIL (errno=%d)\n", errno);
        return -1;
    }
    printf("OK (%d bytes written)\n", n);
    return 0;
}

/* ---- Test 5: blocking receive (ARP reply or timeout) ---- */
static volatile int got_alarm = 0;

static void alarm_handler(int sig) {
    (void)sig;
    got_alarm = 1;
}

static int test_recv_blocking(int fd) {
    printf("[test 5] blocking receive (3s timeout) ... ");

    signal(14, alarm_handler);  /* 14 = SIGALRM */
    alarm(3);

    rtl8139_packet_t pkt;
    int n = read(fd, &pkt, sizeof(pkt));

    if (n > 0) {
        printf("OK (got packet)\n");
        print_mac("  src", pkt.src_mac);
        print_mac("  dst", pkt.dst_mac);
        printf("  payload length: %d\n", pkt.length);
        if (pkt.length >= 2)
            printf("  ethertype: %02x%02x\n", pkt.data[0], pkt.data[1]);
        print_hex(pkt.data, pkt.length > 64 ? 64 : pkt.length);
        return 0;
    } else if (got_alarm) {
        printf("TIMEOUT (no reply in 3s - normal if no peer)\n");
        return 0;
    } else {
        printf("FAIL (n=%d errno=%d)\n", n, errno);
        return -1;
    }
}

int main(void) {
    int failures = 0;

    printf("=== RTL8139 NIC Test ===\n\n");

    int fd = test_open();
    if (fd < 0) {
        printf("\nDevice not available. Is the RTL8139 present?\n");
        return 1;
    }

    if (test_send(fd) < 0)
        failures++;

    if (test_recv_nonblock() < 0)
        failures++;

    if (test_arp(fd) < 0)
        failures++;

    if (test_recv_blocking(fd) < 0)
        failures++;

    close(fd);

    printf("\n=== Results: %d failure(s) ===\n", failures);
    return failures ? 1 : 0;
}
