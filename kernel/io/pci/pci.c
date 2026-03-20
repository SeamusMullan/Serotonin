#include "pci.h"
#include "../io.h"
#include "../../kernel.h"
#include "../../stdio/stdio.h"

#define PCI_LOG(fmt, ...) printfs(PRINT_STATUS_INFO, "pci: " fmt, ##__VA_ARGS__)

static const char *pci_class_name(uint8_t class_code) {
    switch (class_code) {
        case PCI_CLASS_UNCLASSIFIED:      return "Unclassified";
        case PCI_CLASS_STORAGE:           return "Mass Storage";
        case PCI_CLASS_NETWORK:           return "Network";
        case PCI_CLASS_DISPLAY:           return "Display";
        case PCI_CLASS_MULTIMEDIA:        return "Multimedia";
        case PCI_CLASS_MEMORY:            return "Memory";
        case PCI_CLASS_BRIDGE:            return "Bridge";
        case PCI_CLASS_COMMUNICATION:     return "Communication";
        case PCI_CLASS_SYSTEM:            return "System";
        case PCI_CLASS_INPUT:             return "Input";
        case PCI_CLASS_DOCKING:           return "Docking Station";
        case PCI_CLASS_PROCESSOR:         return "Processor";
        case PCI_CLASS_SERIAL_BUS:        return "Serial Bus";
        case PCI_CLASS_WIRELESS:          return "Wireless";
        case PCI_CLASS_INTELLIGENT_IO:    return "Intelligent I/O";
        case PCI_CLASS_SATELLITE:         return "Satellite";
        case PCI_CLASS_ENCRYPTION:        return "Encryption";
        case PCI_CLASS_SIGNAL_PROCESSING: return "Signal Processing";
        default:                          return "Unknown";
    }
}

static const char *pci_vendor_name(uint16_t vendor_id) {
    switch (vendor_id) {
        case PCI_VENDOR_INTEL:         return "Intel";
        case PCI_VENDOR_QEMU:          return "QEMU";
        case PCI_VENDOR_REDHAT_VIRTIO: return "Red Hat (Virtio)";
        case PCI_VENDOR_REDHAT_QEMU:   return "Red Hat (QEMU)";
        case PCI_VENDOR_REALTEK:       return "Realtek";
        case PCI_VENDOR_CIRRUS:        return "Cirrus Logic";
        case PCI_VENDOR_NEC:           return "NEC";
        case PCI_VENDOR_AMD:           return "AMD";
        case PCI_VENDOR_AMD_ATI:       return "AMD/ATI";
        case PCI_VENDOR_VMWARE:        return "VMware";
        case PCI_VENDOR_NVIDIA:        return "NVIDIA";
        case PCI_VENDOR_S3:            return "S3";
        default:                       return "Unknown";
    }
}

#define PCI_MAX_DRIVERS 32

static pci_root_t pci_root;
static const pci_driver_t *pci_driver_table[PCI_MAX_DRIVERS];
static uint32_t pci_driver_count = 0;

// get the 32-bit CONFIG_ADDRESS value for a given bus/slot/func/offset
static inline uint32_t pci_address(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)(
        ((uint32_t)1       << 31) |  // enable bit
        ((uint32_t)bus     << 16) |
        ((uint32_t)slot    << 11) |
        ((uint32_t)func    <<  8) |
        ((uint32_t)offset & 0xFC)    // dword-aligned
    );
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

void pci_config_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_address(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_config_read32(bus, slot, func, offset);
    return (uint16_t)(dword >> ((offset & 2) * 8));
}

void pci_config_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t dword = pci_config_read32(bus, slot, func, offset);
    int shift = (offset & 2) * 8;
    dword &= ~(0xFFFF << shift);
    dword |= ((uint32_t)value << shift);
    pci_config_write32(bus, slot, func, offset, dword);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_config_read32(bus, slot, func, offset);
    return (uint8_t)(dword >> ((offset & 3) * 8));
}

void pci_config_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value) {
    uint32_t dword = pci_config_read32(bus, slot, func, offset);
    int shift = (offset & 3) * 8;
    dword &= ~(0xFF << shift);
    dword |= ((uint32_t)value << shift);
    pci_config_write32(bus, slot, func, offset, dword);
}

static pci_bus_t *pci_get_or_create_bus(uint8_t bus_number) {
    pci_bus_t *bus = pci_root.buses;
    while (bus) {
        if (bus->bus_number == bus_number)
            return bus;
        bus = bus->next;
    }
    bus = (pci_bus_t *)kernel_malloc(sizeof(pci_bus_t));
    if (!bus) return 0;
    bus->bus_number = bus_number;
    bus->devices = 0;
    bus->next = pci_root.buses;
    pci_root.buses = bus;
    return bus;
}

static pci_device_t *pci_get_or_create_device(pci_bus_t *bus, uint8_t slot) {
    pci_device_t *dev = bus->devices;
    while (dev) {
        if (dev->slot == slot)
            return dev;
        dev = dev->next;
    }
    dev = (pci_device_t *)kernel_malloc(sizeof(pci_device_t));
    if (!dev) return 0;
    dev->slot = slot;
    dev->functions = 0;
    dev->next = bus->devices;
    bus->devices = dev;
    return dev;
}

static void pci_add_function(pci_device_t *dev, uint8_t bus_num, uint8_t slot, uint8_t func_num) {
    pci_function_t *fn = (pci_function_t *)kernel_malloc(sizeof(pci_function_t));
    if (!fn) return;

    fn->bus           = bus_num;
    fn->slot          = slot;
    fn->function      = func_num;
    fn->vendor_id     = pci_config_read16(bus_num, slot, func_num, PCI_REG_VENDOR_ID);
    fn->device_id     = pci_config_read16(bus_num, slot, func_num, PCI_REG_DEVICE_ID);
    fn->revision      = pci_config_read8(bus_num, slot, func_num, PCI_REG_REVISION_ID);
    fn->prog_if       = pci_config_read8(bus_num, slot, func_num, PCI_REG_PROG_IF);
    fn->subclass      = pci_config_read8(bus_num, slot, func_num, PCI_REG_SUBCLASS);
    fn->class_code    = pci_config_read8(bus_num, slot, func_num, PCI_REG_CLASS_CODE);
    fn->header_type   = pci_config_read8(bus_num, slot, func_num, PCI_REG_HEADER_TYPE);
    fn->interrupt_line = pci_config_read8(bus_num, slot, func_num, PCI_REG_INTERRUPT_LINE);
    fn->interrupt_pin  = pci_config_read8(bus_num, slot, func_num, PCI_REG_INTERRUPT_PIN);

    // read BARs (only for standard device headers)
    int max_bars = ((fn->header_type & PCI_HEADER_TYPE_MASK) == PCI_HEADER_TYPE_DEVICE) ? 6 : 2;
    for (int i = 0; i < PCI_MAX_BAR; i++) {
        fn->bar[i] = (i < max_bars)
            ? pci_config_read32(bus_num, slot, func_num, PCI_REG_BAR0 + (i * 4))
            : 0;
    }

    fn->next = dev->functions;
    dev->functions = fn;
    pci_root.device_count++;
}

static void pci_scan_bus(uint8_t bus_num);

static void pci_check_function(uint8_t bus_num, uint8_t slot, uint8_t func) {
    uint8_t class_code = pci_config_read8(bus_num, slot, func, PCI_REG_CLASS_CODE);
    uint8_t subclass   = pci_config_read8(bus_num, slot, func, PCI_REG_SUBCLASS);

    // if this is a PCI-to-PCI bridge, scan the secondary bus
    if (class_code == PCI_CLASS_BRIDGE && subclass == PCI_SUBCLASS_PCI_TO_PCI) {
        uint8_t secondary = pci_config_read8(bus_num, slot, func, PCI_REG_SECONDARY_BUS);
        pci_scan_bus(secondary);
    }
}

static void pci_scan_slot(uint8_t bus_num, uint8_t slot) {
    uint16_t vendor = pci_config_read16(bus_num, slot, 0, PCI_REG_VENDOR_ID);
    if (vendor == PCI_VENDOR_NONE)
        return;

    pci_bus_t *bus = pci_get_or_create_bus(bus_num);
    if (!bus) return;

    pci_device_t *dev = pci_get_or_create_device(bus, slot);
    if (!dev) return;

    // add function 0
    pci_add_function(dev, bus_num, slot, 0);
    pci_check_function(bus_num, slot, 0);

    // check for multifunction device
    uint8_t header_type = pci_config_read8(bus_num, slot, 0, PCI_REG_HEADER_TYPE);
    if (header_type & PCI_HEADER_MULTIFUNCTION) {
        for (uint8_t func = 1; func < PCI_MAX_FUNC; func++) {
            vendor = pci_config_read16(bus_num, slot, func, PCI_REG_VENDOR_ID);
            if (vendor == PCI_VENDOR_NONE)
                continue;
            pci_add_function(dev, bus_num, slot, func);
            pci_check_function(bus_num, slot, func);
        }
    }
}

static void pci_scan_bus(uint8_t bus_num) {
    for (uint8_t slot = 0; slot < PCI_MAX_SLOT; slot++) {
        pci_scan_slot(bus_num, slot);
    }
}

void pci_init(void) {
    pci_root.buses = 0;
    pci_root.device_count = 0;

    // check if host controller is multifunction (multiple PCI buses from root)
    uint8_t header_type = pci_config_read8(0, 0, 0, PCI_REG_HEADER_TYPE);
    if (header_type & PCI_HEADER_MULTIFUNCTION) {
        for (uint8_t func = 0; func < PCI_MAX_FUNC; func++) {
            if (pci_config_read16(0, 0, func, PCI_REG_VENDOR_ID) == PCI_VENDOR_NONE)
                continue;
            pci_scan_bus(func);
        }
    } else {
        pci_scan_bus(0);
    }

    PCI_LOG("enumeration complete: %d device(s) found\n", pci_root.device_count);

    for (pci_bus_t *bus = pci_root.buses; bus; bus = bus->next) {
        for (pci_device_t *dev = bus->devices; dev; dev = dev->next) {
            for (pci_function_t *fn = dev->functions; fn; fn = fn->next) {
                PCI_LOG("%02x:%02x.%d %s %s [%02x%02x] %04x:%04x (rev %02x) IRQ %d\n", bus->bus_number, dev->slot, fn->function,
                    pci_vendor_name(fn->vendor_id), pci_class_name(fn->class_code),fn->class_code,fn->subclass, fn->vendor_id, fn->device_id,
                    fn->revision, fn->interrupt_line);
            }
        }
    }
}

pci_root_t *pci_get_root(void) {
    return &pci_root;
}

pci_function_t *pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (pci_bus_t *bus = pci_root.buses; bus; bus = bus->next) {
        for (pci_device_t *dev = bus->devices; dev; dev = dev->next) {
            for (pci_function_t *fn = dev->functions; fn; fn = fn->next) {
                if (fn->vendor_id == vendor_id && fn->device_id == device_id)
                    return fn;
            }
        }
    }
    return 0;
}

pci_function_t *pci_find_class(uint8_t class_code, uint8_t subclass) {
    for (pci_bus_t *bus = pci_root.buses; bus; bus = bus->next) {
        for (pci_device_t *dev = bus->devices; dev; dev = dev->next) {
            for (pci_function_t *fn = dev->functions; fn; fn = fn->next) {
                if (fn->class_code == class_code && fn->subclass == subclass)
                    return fn;
            }
        }
    }
    return 0;
}

uint32_t pci_bar_address(pci_function_t *func, uint8_t bar_index) {
    if (bar_index >= PCI_MAX_BAR) return 0;
    uint32_t bar = func->bar[bar_index];
    if (bar & PCI_BAR_IO_SPACE)
        return bar & PCI_BAR_IO_MASK;
    return bar & PCI_BAR_MEMORY_MASK;
}

uint32_t pci_bar_size(uint8_t bus, uint8_t slot, uint8_t function, uint8_t bar_index) {
    if (bar_index >= PCI_MAX_BAR) return 0;
    uint8_t reg = PCI_REG_BAR0 + (bar_index * 4);

    uint32_t original = pci_config_read32(bus, slot, function, reg);
    pci_config_write32(bus, slot, function, reg, 0xFFFFFFFF);
    uint32_t size_mask = pci_config_read32(bus, slot, function, reg);
    pci_config_write32(bus, slot, function, reg, original);

    if (original & PCI_BAR_IO_SPACE)
        size_mask &= PCI_BAR_IO_MASK;
    else
        size_mask &= PCI_BAR_MEMORY_MASK;

    if (size_mask == 0) return 0;
    return ~size_mask + 1;
}

int pci_bar_is_io(pci_function_t *func, uint8_t bar_index) {
    if (bar_index >= PCI_MAX_BAR) return 0;
    return (func->bar[bar_index] & PCI_BAR_IO_SPACE) != 0;
}

void pci_enable_bus_mastering(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t cmd = pci_config_read16(bus, slot, func, PCI_REG_COMMAND);
    cmd |= PCI_CMD_BUS_MASTER;
    pci_config_write16(bus, slot, func, PCI_REG_COMMAND, cmd);
}

void pci_enable_io_space(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t cmd = pci_config_read16(bus, slot, func, PCI_REG_COMMAND);
    cmd |= PCI_CMD_IO_SPACE;
    pci_config_write16(bus, slot, func, PCI_REG_COMMAND, cmd);
}

void pci_enable_memory_space(uint8_t bus, uint8_t slot, uint8_t func) {
    uint16_t cmd = pci_config_read16(bus, slot, func, PCI_REG_COMMAND);
    cmd |= PCI_CMD_MEMORY_SPACE;
    pci_config_write16(bus, slot, func, PCI_REG_COMMAND, cmd);
}

void pci_func_enable_bus_mastering(pci_function_t *fn) {
    pci_enable_bus_mastering(fn->bus, fn->slot, fn->function);
}

void pci_func_enable_io_space(pci_function_t *fn) {
    pci_enable_io_space(fn->bus, fn->slot, fn->function);
}

void pci_func_enable_memory_space(pci_function_t *fn) {
    pci_enable_memory_space(fn->bus, fn->slot, fn->function);
}

/* ---- Driver registration and probing ---- */

void pci_register_driver(const pci_driver_t *driver) {
    if (pci_driver_count < PCI_MAX_DRIVERS)
        pci_driver_table[pci_driver_count++] = driver;
}

static int pci_driver_matches(const pci_driver_t *drv, pci_function_t *fn) {
    if (drv->vendor_id != PCI_DRIVER_MATCH_ANY && drv->vendor_id != fn->vendor_id)
        return 0;
    if (drv->device_id != PCI_DRIVER_MATCH_ANY && drv->device_id != fn->device_id)
        return 0;
    return 1;
}

void pci_probe_drivers(void) {
    for (pci_bus_t *bus = pci_root.buses; bus; bus = bus->next) {
        for (pci_device_t *dev = bus->devices; dev; dev = dev->next) {
            for (pci_function_t *fn = dev->functions; fn; fn = fn->next) {
                for (uint32_t i = 0; i < pci_driver_count; i++) {
                    const pci_driver_t *drv = pci_driver_table[i];
                    if (pci_driver_matches(drv, fn)) {
                        PCI_LOG("probing driver \"%s\" for %02x:%02x.%d (%04x:%04x)\n",
                            drv->name, fn->bus, fn->slot, fn->function,
                            fn->vendor_id, fn->device_id);
                        int ret = drv->probe(fn);
                        if (ret == 0) {
                            PCI_LOG("driver \"%s\" claimed %02x:%02x.%d\n",
                                drv->name, fn->bus, fn->slot, fn->function);
                            break;
                        }
                    }
                }
            }
        }
    }
}
