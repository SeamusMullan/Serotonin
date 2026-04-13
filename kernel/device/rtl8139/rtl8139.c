#include <kernel/device/rtl8139/rtl8139.h>
#include <kernel/device/rtl8139/dev_rtl8139.h>
#include <kernel/io/io.h>
#include <kernel/io/pci/pci.h>
#include <kernel/vmm/paging_init.h>
#include <kernel/kernel.h>
#include <kernel/stdio/stdio.h>
#include <kernel/stdlib/stdlib.h>

#define RTL_LOG(fmt, ...) printfs(PRINT_STATUS_INFO, "rtl8139: " fmt, ##__VA_ARGS__)

static rtl8139_dev_t rtl;
static int rtl_initialised = 0;
static rtl8139_rx_callback_t rx_callback = 0;

static inline uint8_t  rtl_r8(uint16_t reg)  { return inb(rtl.io_base + reg); }
static inline uint16_t rtl_r16(uint16_t reg) { return inw(rtl.io_base + reg); }
static inline uint32_t rtl_r32(uint16_t reg) { return inl(rtl.io_base + reg); }
static inline void rtl_w8(uint16_t reg, uint8_t v)   { outb(rtl.io_base + reg, v); }
static inline void rtl_w16(uint16_t reg, uint16_t v) { outw(rtl.io_base + reg, v); }
static inline void rtl_w32(uint16_t reg, uint32_t v) { outl(rtl.io_base + reg, v); }

static void rtl_rx_ring_copy(uint8_t *dst, uint32_t off, uint16_t len) {
    off %= RTL_RX_RING_SIZE;
    uint16_t first = (uint16_t)(RTL_RX_RING_SIZE - off);
    if (first > len) first = len;
    memcpy(dst, rtl.rx_buffer + off, first);
    if (len > first) {
        memcpy(dst + first, rtl.rx_buffer, (uint16_t)(len - first));
    }
}

static void rtl8139_receive(void) {
    while (!(rtl_r8(RTL_CR) & RTL_CR_BUFE)) {
        /* packet header: 4 bytes at current rx_offset
         *   [15:0]  status   [31:16] length (including 4-byte CRC) */
        uint8_t hdr[4];
        rtl_rx_ring_copy(hdr, rtl.rx_offset, sizeof(hdr));
        uint16_t status = (uint16_t)(hdr[0] | (hdr[1] << 8));
        uint16_t length = (uint16_t)(hdr[2] | (hdr[3] << 8));

        if (length < 4 || length > (ETH_FRAME_MAX + 4)) {
            rtl.rx_errors++;
            /* Skip this header-sized slot to resync ring cursor. */
            rtl.rx_offset = (rtl.rx_offset + 4 + 3) & ~3U;
            rtl.rx_offset %= RTL_RX_RING_SIZE;
            rtl_w16(RTL_CAPR, (uint16_t)((rtl.rx_offset - 16) & (RTL_RX_RING_SIZE - 1)));
            continue;
        }

        if (!(status & RTL_RX_ROK)) {
            rtl.rx_errors++;
            // advance past this bad packet
            rtl.rx_offset = (rtl.rx_offset + length + 4 + 3) & ~3;
            rtl.rx_offset %= RTL_RX_RING_SIZE;
            rtl_w16(RTL_CAPR, (uint16_t)((rtl.rx_offset - 16) & (RTL_RX_RING_SIZE - 1)));
            continue;
        }

        // strip the 4-byte CRC from the length
        uint16_t pkt_len = length - 4;
        if (pkt_len > 0 && pkt_len <= ETH_FRAME_MAX) {
            rtl.rx_packets++;
            if (rx_callback) {
                uint8_t pkt_data[ETH_FRAME_MAX];
                rtl_rx_ring_copy(pkt_data, rtl.rx_offset + 4, pkt_len);
                rx_callback(pkt_data, pkt_len);
            }
        } else {
            rtl.rx_dropped++;
        }

        // advance read pointer: header(4) + length, dword-aligned
        rtl.rx_offset = (rtl.rx_offset + length + 4 + 3) & ~3;
        rtl.rx_offset %= RTL_RX_RING_SIZE;
        rtl_w16(RTL_CAPR, (uint16_t)((rtl.rx_offset - 16) & (RTL_RX_RING_SIZE - 1)));
    }
}

static void rtl8139_irq_handler(int irq, processor_context_t *ctx) {
    (void)irq;
    (void)ctx;

    for (;;) {
        uint16_t status = rtl_r16(RTL_ISR);
        if (status == 0)
            break;

        /* Ack this ISR snapshot; loop to drain any latched follow-up causes. */
        rtl_w16(RTL_ISR, status);

        if (status & (RTL_INT_ROK | RTL_INT_RER | RTL_INT_RXOVW | RTL_INT_FOVW))
            rtl8139_receive();

        if (status & RTL_INT_TOK)
            rtl.tx_packets++;

        if (status & RTL_INT_TER)
            rtl.tx_errors++;

        if (status & RTL_INT_RXOVW) {
            rtl.rx_dropped++;
            RTL_LOG("rx buffer overflow\n");
        }
    }
}

int rtl8139_send(const uint8_t *data, uint16_t length) {
    if (!rtl_initialised)
        return -1;
    if (length > ETH_FRAME_MAX)
        return -1;

    uint8_t desc = rtl.tx_cur;
    uint32_t tsd = 0;
    int found = 0;

    /* Don't drop immediately when all descriptors are transiently busy.
     * This path is used by lwip ACK/data fast-path and packet drops here
     * amplify into duplicate ACK storms and retransmissions.
     */
    for (int tries = 0; tries < 256 && !found; tries++) {
        for (int i = 0; i < RTL_NUM_TX_DESC; i++) {
            uint8_t cand = (uint8_t)((rtl.tx_cur + i) % RTL_NUM_TX_DESC);
            tsd = rtl_r32(RTL_TSD0 + cand * 4);
            if ((tsd & RTL_TSD_OWN) || (tsd & RTL_TSD_TOK) || (tsd & RTL_TSD_TUN)) {
                desc = cand;
                found = 1;
                break;
            }
        }
        if (!found)
            io_wait();
    }

    if (!found) {
        rtl.tx_busy_drops++;
        rtl.tx_errors++;
        if ((rtl.tx_busy_drops & 0x3FFU) == 1) {
            RTL_LOG("tx descriptor starvation (drops=%u)\n", rtl.tx_busy_drops);
        }
        return -1;
    }

    // copy data into the tx buffer for this descriptor
    memcpy(rtl.tx_buffers[desc], data, length);

    // write the phys addr
    rtl_w32(RTL_TSAD0 + desc * 4, rtl.tx_buffers_phys[desc]);

    // write length to TSD to start transmission
    rtl_w32(RTL_TSD0 + desc * 4, length & RTL_TSD_SIZE_MASK);

    rtl.tx_cur = (desc + 1) % RTL_NUM_TX_DESC;
    return 0;
}

void rtl8139_get_mac(uint8_t *buf) {
    for (int i = 0; i < ETH_ALEN; i++)
        buf[i] = rtl.mac[i];
}

int rtl8139_is_up(void) {
    return rtl_initialised;
}

void rtl8139_set_rx_callback(rtl8139_rx_callback_t cb) {
    rx_callback = cb;
}

static int rtl8139_probe(pci_function_t *fn) {
    if (!pci_bar_is_io(fn, 0)) {
        RTL_LOG("BAR0 is not I/O space, unsupported\n");
        return -1;
    }

    memset(&rtl, 0, sizeof(rtl));
    rtl.io_base = (uint16_t)pci_bar_address(fn, 0);
    rtl.irq = fn->interrupt_line;

    RTL_LOG("found at I/O 0x%x, IRQ %d\n", rtl.io_base, rtl.irq);

    // enable PCI bus mastering and I/O space
    pci_func_enable_bus_mastering(fn);
    pci_func_enable_io_space(fn);

    // power on: write 0 to CONFIG1
    rtl_w8(RTL_CONFIG1, 0x00);

    // software reset
    rtl_w8(RTL_CR, RTL_CR_RST);
    // wait for reset to complete (RST bit clears itself)
    for (int i = 0; i < 100000; i++) {
        if (!(rtl_r8(RTL_CR) & RTL_CR_RST))
            break;
    }

    // read MAC address from IDR registers
    uint32_t mac_lo = rtl_r32(RTL_IDR0);
    uint16_t mac_hi = rtl_r16(RTL_IDR4);
    rtl.mac[0] = (mac_lo >>  0) & 0xFF;
    rtl.mac[1] = (mac_lo >>  8) & 0xFF;
    rtl.mac[2] = (mac_lo >> 16) & 0xFF;
    rtl.mac[3] = (mac_lo >> 24) & 0xFF;
    rtl.mac[4] = (mac_hi >>  0) & 0xFF;
    rtl.mac[5] = (mac_hi >>  8) & 0xFF;

    RTL_LOG("MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
        rtl.mac[0], rtl.mac[1], rtl.mac[2],
        rtl.mac[3], rtl.mac[4], rtl.mac[5]);

    // allocate receive buffer (page-aligned for DMA)
    rtl.rx_buffer = (uint8_t *)kernel_malloc_align(4096, RTL_RX_BUF_SIZE);
    if (!rtl.rx_buffer) {
        RTL_LOG("failed to allocate rx buffer\n");
        return -1;
    }
    rtl.rx_buffer_phys = virt_to_phys(rtl.rx_buffer);
    memset(rtl.rx_buffer, 0, RTL_RX_BUF_SIZE);
    rtl.rx_offset = 0;

    // allocate transmit buffers
    for (int i = 0; i < RTL_NUM_TX_DESC; i++) {
        rtl.tx_buffers[i] = (uint8_t *)kernel_malloc_align(4096, RTL_TX_BUF_SIZE);
        if (!rtl.tx_buffers[i]) {
            RTL_LOG("failed to allocate tx buffer %d\n", i);
            return -1;
        }
        rtl.tx_buffers_phys[i] = virt_to_phys(rtl.tx_buffers[i]);
        memset(rtl.tx_buffers[i], 0, RTL_TX_BUF_SIZE);
    }

    // register IRQ handler
    irq_register(rtl.irq, rtl8139_irq_handler);

    // set receive buffer address
    rtl_w32(RTL_RBSTART, rtl.rx_buffer_phys);

    // enable interrupts: ROK, TOK, RER, TER, RXOVW, FOVW
    rtl_w16(RTL_IMR, RTL_INT_ROK | RTL_INT_TOK | RTL_INT_RER |
                     RTL_INT_TER | RTL_INT_RXOVW | RTL_INT_FOVW);

    /* configure receive: accept broadcast + physical match + multicast,
     * 64K buffer, wrap mode, no FIFO threshold */
    rtl_w32(RTL_RCR, RTL_RCR_APM | RTL_RCR_AM | RTL_RCR_AB |
                     RTL_RCR_WRAP | RTL_RCR_RBLEN_64K);

    // configure transmit: standard IFG, max DMA burst 2048
    rtl_w32(RTL_TCR, RTL_TCR_IFG_STD | RTL_TCR_MXDMA_2048);

    // accept all multicast (set all bits in MAR0-MAR7)
    rtl_w32(RTL_MAR0, 0xFFFFFFFF);
    rtl_w32(RTL_MAR4, 0xFFFFFFFF);

    // enable receiver and transmitter
    rtl_w8(RTL_CR, RTL_CR_RE | RTL_CR_TE);

    rtl_initialised = 1;
    RTL_LOG("driver initialised\n");

    dev_rtl8139_init();

    return 0;
}

static const pci_driver_t rtl8139_driver = {
    .name      = "rtl8139",
    .vendor_id = RTL8139_VENDOR_ID,
    .device_id = RTL8139_DEVICE_ID,
    .probe     = rtl8139_probe,
};

void rtl8139_init(void) {
    pci_register_driver(&rtl8139_driver);
}
