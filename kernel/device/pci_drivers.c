#include <kernel/device/pci_drivers.h>
#include <kernel/io/pci/pci.h>
#include <kernel/stdio/stdio.h>
#include <kernel/device/rtl8139/rtl8139.h>
#include <kernel/device/ac97/ac97.h>

void pci_drivers_init(void) {
    // register drivers
    rtl8139_init();

    // probe all registered drivers against discovered PCI devices
    pci_probe_drivers();

    // class-based drivers (scanned manually, not via vendor/device match)
    ac97_init();
}
