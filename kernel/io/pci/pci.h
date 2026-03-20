#ifndef _KERNEL_PCI
#define _KERNEL_PCI

#include <stdint.h>

#define PCI_CONFIG_ADDRESS          0xCF8
#define PCI_CONFIG_DATA             0xCFC

#define PCI_REG_VENDOR_ID           0x00
#define PCI_REG_DEVICE_ID           0x02
#define PCI_REG_COMMAND             0x04
#define PCI_REG_STATUS              0x06
#define PCI_REG_REVISION_ID         0x08
#define PCI_REG_PROG_IF             0x09
#define PCI_REG_SUBCLASS            0x0A
#define PCI_REG_CLASS_CODE          0x0B
#define PCI_REG_CACHE_LINE_SIZE     0x0C
#define PCI_REG_LATENCY_TIMER       0x0D
#define PCI_REG_HEADER_TYPE         0x0E
#define PCI_REG_BIST                0x0F
#define PCI_REG_BAR0                0x10
#define PCI_REG_BAR1                0x14
#define PCI_REG_BAR2                0x18
#define PCI_REG_BAR3                0x1C
#define PCI_REG_BAR4                0x20
#define PCI_REG_BAR5                0x24
#define PCI_REG_CARDBUS_CIS         0x28
#define PCI_REG_SUBSYS_VENDOR_ID    0x2C
#define PCI_REG_SUBSYS_ID           0x2E
#define PCI_REG_EXPANSION_ROM       0x30
#define PCI_REG_CAPABILITIES        0x34
#define PCI_REG_INTERRUPT_LINE      0x3C
#define PCI_REG_INTERRUPT_PIN       0x3D
#define PCI_REG_MIN_GRANT           0x3E
#define PCI_REG_MAX_LATENCY         0x3F

#define PCI_REG_PRIMARY_BUS         0x18
#define PCI_REG_SECONDARY_BUS       0x19
#define PCI_REG_SUBORDINATE_BUS     0x1A

#define PCI_HEADER_TYPE_MASK        0x7F
#define PCI_HEADER_MULTIFUNCTION    0x80
#define PCI_HEADER_TYPE_DEVICE      0x00
#define PCI_HEADER_TYPE_BRIDGE      0x01
#define PCI_HEADER_TYPE_CARDBUS     0x02

#define PCI_CMD_IO_SPACE            0x0001
#define PCI_CMD_MEMORY_SPACE        0x0002
#define PCI_CMD_BUS_MASTER          0x0004
#define PCI_CMD_SPECIAL_CYCLES      0x0008
#define PCI_CMD_MWI_ENABLE          0x0010
#define PCI_CMD_VGA_PALETTE_SNOOP   0x0020
#define PCI_CMD_PARITY_ERROR        0x0040
#define PCI_CMD_SERR_ENABLE         0x0100
#define PCI_CMD_FAST_B2B_ENABLE     0x0200
#define PCI_CMD_INTERRUPT_DISABLE   0x0400

#define PCI_STATUS_CAPABILITIES     0x0010
#define PCI_STATUS_66MHZ            0x0020
#define PCI_STATUS_FAST_B2B         0x0080
#define PCI_STATUS_PARITY_ERROR     0x0100
#define PCI_STATUS_DEVSEL_MASK      0x0600
#define PCI_STATUS_SIG_TARGET_ABORT 0x0800
#define PCI_STATUS_RCV_TARGET_ABORT 0x1000
#define PCI_STATUS_RCV_MASTER_ABORT 0x2000
#define PCI_STATUS_SIG_SYSTEM_ERROR 0x4000
#define PCI_STATUS_PARITY_DETECT    0x8000

#define PCI_BAR_IO_SPACE            0x01
#define PCI_BAR_MEMORY_TYPE_MASK    0x06
#define PCI_BAR_MEMORY_32BIT        0x00
#define PCI_BAR_MEMORY_1MB          0x02
#define PCI_BAR_MEMORY_64BIT        0x04
#define PCI_BAR_PREFETCHABLE        0x08
#define PCI_BAR_IO_MASK             0xFFFFFFFC
#define PCI_BAR_MEMORY_MASK         0xFFFFFFF0

#define PCI_CLASS_UNCLASSIFIED      0x00
#define PCI_CLASS_STORAGE           0x01
#define PCI_CLASS_NETWORK           0x02
#define PCI_CLASS_DISPLAY           0x03
#define PCI_CLASS_MULTIMEDIA        0x04
#define PCI_CLASS_MEMORY            0x05
#define PCI_CLASS_BRIDGE            0x06
#define PCI_CLASS_COMMUNICATION     0x07
#define PCI_CLASS_SYSTEM            0x08
#define PCI_CLASS_INPUT             0x09
#define PCI_CLASS_DOCKING           0x0A
#define PCI_CLASS_PROCESSOR         0x0B
#define PCI_CLASS_SERIAL_BUS        0x0C
#define PCI_CLASS_WIRELESS          0x0D
#define PCI_CLASS_INTELLIGENT_IO    0x0E
#define PCI_CLASS_SATELLITE         0x0F
#define PCI_CLASS_ENCRYPTION        0x10
#define PCI_CLASS_SIGNAL_PROCESSING 0x11
#define PCI_CLASS_UNDEFINED         0xFF

#define PCI_SUBCLASS_PCI_TO_PCI     0x04

#define PCI_SUBCLASS_IDE            0x01
#define PCI_SUBCLASS_FLOPPY         0x02
#define PCI_SUBCLASS_ATA            0x05
#define PCI_SUBCLASS_SATA           0x06
#define PCI_SUBCLASS_NVM            0x08

#define PCI_VENDOR_INTEL            0x8086
#define PCI_VENDOR_QEMU             0x1234
#define PCI_VENDOR_REDHAT_VIRTIO    0x1AF4
#define PCI_VENDOR_REDHAT_QEMU      0x1B36
#define PCI_VENDOR_REALTEK          0x10EC
#define PCI_VENDOR_CIRRUS           0x1013
#define PCI_VENDOR_NEC              0x1033
#define PCI_VENDOR_AMD              0x1022
#define PCI_VENDOR_AMD_ATI          0x1002
#define PCI_VENDOR_VMWARE           0x15AD
#define PCI_VENDOR_NVIDIA           0x10DE
#define PCI_VENDOR_S3               0x5333

#define PCI_VENDOR_NONE             0xFFFF
#define PCI_MAX_BUS                 256
#define PCI_MAX_SLOT                32
#define PCI_MAX_FUNC                8
#define PCI_MAX_BAR                 6

#define PCI_DRIVER_MATCH_ANY 0xFFFF

/*
 * PCI device tree hierarchy:
 *
 *   pci_root_t
 *     └── pci_bus_t (linked list of buses)
 *           └── pci_device_t (linked list of devices per bus)
 *                 └── pci_function_t (linked list of functions per device)
 */

typedef struct pci_function {
    uint8_t  bus;
    uint8_t  slot;
    uint8_t  function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  revision;
    uint8_t  prog_if;
    uint8_t  subclass;
    uint8_t  class_code;
    uint8_t  header_type;
    uint8_t  interrupt_line;
    uint8_t  interrupt_pin;
    uint32_t bar[PCI_MAX_BAR];
    struct pci_function *next;
} pci_function_t;

typedef struct pci_driver {
    const char *name;
    uint16_t vendor_id;
    uint16_t device_id;
    int (*probe)(pci_function_t *fn);
} pci_driver_t;

typedef struct pci_device {
    uint8_t          slot;
    pci_function_t  *functions;
    struct pci_device *next;
} pci_device_t;

typedef struct pci_bus {
    uint8_t        bus_number;
    pci_device_t  *devices;
    struct pci_bus *next;
} pci_bus_t;

typedef struct pci_root {
    pci_bus_t *buses;
    uint32_t   device_count;
} pci_root_t;

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value);
void pci_init(void);
pci_root_t *pci_get_root(void);
pci_function_t *pci_find_device(uint16_t vendor_id, uint16_t device_id);
pci_function_t *pci_find_class(uint8_t class_code, uint8_t subclass);
uint32_t pci_bar_address(pci_function_t *func, uint8_t bar_index);
uint32_t pci_bar_size(uint8_t bus, uint8_t slot, uint8_t function, uint8_t bar_index);
int pci_bar_is_io(pci_function_t *func, uint8_t bar_index);
void pci_enable_bus_mastering(uint8_t bus, uint8_t slot, uint8_t func);
void pci_enable_io_space(uint8_t bus, uint8_t slot, uint8_t func);
void pci_enable_memory_space(uint8_t bus, uint8_t slot, uint8_t func);
void pci_func_enable_bus_mastering(pci_function_t *fn);
void pci_func_enable_io_space(pci_function_t *fn);
void pci_func_enable_memory_space(pci_function_t *fn);
void pci_register_driver(const pci_driver_t *driver);
void pci_probe_drivers(void);

#endif
