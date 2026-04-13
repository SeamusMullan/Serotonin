#ifndef _DEVICE_RTL8139
#define _DEVICE_RTL8139

#include <stdint.h>

/* RTL8139 PCI IDs */
#define RTL8139_VENDOR_ID   0x10EC
#define RTL8139_DEVICE_ID   0x8139

/* RTL8139 register offsets (from I/O base) */
#define RTL_IDR0            0x00    /* MAC address bytes 0-3 */
#define RTL_IDR4            0x04    /* MAC address bytes 4-5 */
#define RTL_MAR0            0x08    /* Multicast filter 0-3 */
#define RTL_MAR4            0x0C    /* Multicast filter 4-7 */
#define RTL_TSD0            0x10    /* Transmit Status Descriptor 0 */
#define RTL_TSAD0           0x20    /* Transmit Start Address Descriptor 0 */
#define RTL_RBSTART         0x30    /* Receive Buffer Start Address */
#define RTL_ERBCR           0x34    /* Early Receive Byte Count */
#define RTL_ERSR            0x36    /* Early Receive Status */
#define RTL_CR              0x37    /* Command Register */
#define RTL_CAPR            0x38    /* Current Address of Packet Read */
#define RTL_CBR             0x3A    /* Current Buffer Address */
#define RTL_IMR             0x3C    /* Interrupt Mask Register */
#define RTL_ISR             0x3E    /* Interrupt Status Register */
#define RTL_TCR             0x40    /* Transmit Configuration Register */
#define RTL_RCR             0x44    /* Receive Configuration Register */
#define RTL_TCTR            0x48    /* Timer Count Register */
#define RTL_MPC             0x4C    /* Missed Packet Counter */
#define RTL_9346CR          0x50    /* 93C46 Command Register */
#define RTL_CONFIG0         0x51    /* Configuration Register 0 */
#define RTL_CONFIG1         0x52    /* Configuration Register 1 */
#define RTL_MULINT          0x5C    /* Multiple Interrupt Select */
#define RTL_RERID           0x5E    /* PCI Revision ID */
#define RTL_TSAD            0x60    /* Transmit Status of All Descriptors */
#define RTL_BMCR            0x62    /* Basic Mode Control Register */
#define RTL_BMSR            0x64    /* Basic Mode Status Register */
#define RTL_ANAR            0x66    /* Auto-Negotiation Advertisement */
#define RTL_ANLPAR          0x68    /* Auto-Negotiation Link Partner */
#define RTL_ANER            0x6A    /* Auto-Negotiation Expansion */
#define RTL_DIS             0x6C    /* Disconnect Counter */
#define RTL_FCSC            0x6E    /* False Carrier Sense Counter */
#define RTL_NWAYTR          0x70    /* N-Way Test Register */

/* Command Register bits */
#define RTL_CR_RST          0x10    /* Reset */
#define RTL_CR_RE           0x08    /* Receiver Enable */
#define RTL_CR_TE           0x04    /* Transmitter Enable */
#define RTL_CR_BUFE         0x01    /* Buffer Empty */

/* Interrupt Status/Mask bits */
#define RTL_INT_ROK         0x0001  /* Receive OK */
#define RTL_INT_RER         0x0002  /* Receive Error */
#define RTL_INT_TOK         0x0004  /* Transmit OK */
#define RTL_INT_TER         0x0008  /* Transmit Error */
#define RTL_INT_RXOVW       0x0010  /* Rx Buffer Overflow */
#define RTL_INT_PUN         0x0020  /* Packet Underrun / Link Change */
#define RTL_INT_FOVW        0x0040  /* Rx FIFO Overflow */
#define RTL_INT_LENCHG      0x2000  /* Cable Length Change */
#define RTL_INT_TIMEOUT     0x4000  /* Time Out */
#define RTL_INT_SERR        0x8000  /* System Error */

/* Transmit Status Register bits */
#define RTL_TSD_OWN         (1 << 13)   /* DMA completed */
#define RTL_TSD_TUN         (1 << 14)   /* Transmit FIFO Underrun */
#define RTL_TSD_TOK         (1 << 15)   /* Transmit OK */
#define RTL_TSD_SIZE_MASK   0x1FFF      /* Size field (bits 0-12) */

/* Receive Configuration Register bits */
#define RTL_RCR_AAP         (1 << 0)    /* Accept All Packets (promisc) */
#define RTL_RCR_APM         (1 << 1)    /* Accept Physical Match */
#define RTL_RCR_AM          (1 << 2)    /* Accept Multicast */
#define RTL_RCR_AB          (1 << 3)    /* Accept Broadcast */
#define RTL_RCR_AR          (1 << 4)    /* Accept Runt */
#define RTL_RCR_AER         (1 << 5)    /* Accept Error Packets */
#define RTL_RCR_WRAP        (1 << 7)    /* Wrap around (ring buffer) */
#define RTL_RCR_RBLEN_8K    (0 << 11)   /* 8K + 16 bytes */
#define RTL_RCR_RBLEN_16K   (1 << 11)   /* 16K + 16 bytes */
#define RTL_RCR_RBLEN_32K   (2 << 11)   /* 32K + 16 bytes */
#define RTL_RCR_RBLEN_64K   (3 << 11)   /* 64K + 16 bytes */

/* Transmit Configuration Register bits */
#define RTL_TCR_IFG_STD     (3 << 24)   /* Standard Inter-Frame Gap */
#define RTL_TCR_MXDMA_2048  (7 << 8)    /* Max DMA burst 2048 bytes */

/* Receive packet header status bits */
#define RTL_RX_ROK          (1 << 0)    /* Receive OK */
#define RTL_RX_FAE          (1 << 1)    /* Frame Alignment Error */
#define RTL_RX_CRC          (1 << 2)    /* CRC Error */
#define RTL_RX_LONG         (1 << 3)    /* Long Packet */
#define RTL_RX_RUNT         (1 << 4)    /* Runt Packet */
#define RTL_RX_ISE          (1 << 5)    /* Invalid Symbol Error */
#define RTL_RX_BAR          (1 << 13)   /* Broadcast */
#define RTL_RX_PAM          (1 << 14)   /* Physical Address Matched */
#define RTL_RX_MAR          (1 << 15)   /* Multicast Address Matched */

/* Buffer sizes */
#define RTL_RX_RING_SIZE    65536
#define RTL_RX_BUF_SIZE     (RTL_RX_RING_SIZE + 16 + 1500)  /* ring + pad + wrap pad */
#define RTL_TX_BUF_SIZE     1536
#define RTL_NUM_TX_DESC      4

/* Ethernet constants */
#define ETH_FRAME_MAX       1514
#define ETH_ALEN            6

typedef struct {
    uint16_t io_base;
    uint8_t  irq;
    uint8_t  mac[ETH_ALEN];

    uint8_t *rx_buffer;
    uint32_t rx_buffer_phys;
    uint32_t rx_offset;

    uint8_t *tx_buffers[RTL_NUM_TX_DESC];
    uint32_t tx_buffers_phys[RTL_NUM_TX_DESC];
    uint8_t  tx_cur;

    uint32_t rx_packets;
    uint32_t tx_packets;
    uint32_t rx_errors;
    uint32_t tx_errors;
    uint32_t rx_dropped;
    uint32_t tx_busy_drops;
} rtl8139_dev_t;

typedef void (*rtl8139_rx_callback_t)(const uint8_t *data, uint16_t length);

void rtl8139_init(void);
int  rtl8139_send(const uint8_t *data, uint16_t length);
void rtl8139_get_mac(uint8_t *buf);
int  rtl8139_is_up(void);
void rtl8139_set_rx_callback(rtl8139_rx_callback_t cb);

#endif
