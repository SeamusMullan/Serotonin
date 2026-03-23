#include "pci_drivers.h"
#include "../io/pci/pci.h"
#include "../stdio/stdio.h"
#include "rtl8139/rtl8139.h"
#include "ac97/ac97.h"

void pci_drivers_init(void) {
    // register drivers
    rtl8139_init();

    // probe all registered drivers against discovered PCI devices
    pci_probe_drivers();

    // class-based drivers (scanned manually, not via vendor/device match)
    ac97_init();
}
