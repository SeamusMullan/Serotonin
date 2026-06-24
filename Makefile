# =============================================================================
# Serotonin OS — root Makefile
# =============================================================================
# Targets:
#   make toolchain   — download + build i686-elf cross-compiler (once)
#   make sysroot     — build newlib sysroot + Serotonin runtime libs
#   make kernel      — compile kernel → build/serotonin.bin
#   make user        — compile all userspace programs → build/user/*.elf
#   make iso         — package kernel into bootable ISO
#   make img         — create FAT32 disk image with userspace binaries
#   make all         — toolchain → sysroot → kernel → user → iso
#   make run         — launch in QEMU
#   make run-debug   — launch in QEMU, wait for GDB on :1234
#   make test        — build kernel in TEST_MODE, run in QEMU (headless)
#   make clean       — remove build artifacts (keeps toolchain + sysroot)
#   make distclean   — remove everything including toolchain + sysroot
# =============================================================================

ROOT_DIR := $(realpath $(dir $(abspath $(lastword $(MAKEFILE_LIST)))))

include make/platform.mk
include make/toolchain.mk
include make/sysroot.mk
include make/kernel.mk
include make/user.mk
include make/iso.mk

# ---------------------------------------------------------------------------
# Top-level composite targets
# ---------------------------------------------------------------------------

.PHONY: all
all: toolchain sysroot kernel user iso

.PHONY: run
run: iso img
	$(QEMU) \
	  -m 2048 -boot d \
	  -cdrom $(SEROTONIN_ISO) \
	  -drive file=$(SEROTONIN_IMG),if=ide,index=1,media=disk,format=raw \
	  -vga std \
	  -netdev user,id=net0 \
	  -device rtl8139,netdev=net0

# Boot from ISO only — no disk image required (no sudo needed)
.PHONY: run-iso
run-iso: iso
	$(QEMU) \
	  -m 2048 -boot d \
	  -cdrom $(SEROTONIN_ISO) \
	  -vga std

.PHONY: run-debug
run-debug: iso img
	$(QEMU) \
	  -m 2048 -boot d \
	  -cdrom $(SEROTONIN_ISO) \
	  -drive file=$(SEROTONIN_IMG),if=ide,index=1,media=disk,format=raw \
	  -vga std \
	  -s -S

.PHONY: test
test:
	$(MAKE) kernel TEST_MODE=1
	$(QEMU) \
	  -m 512 -boot d \
	  -cdrom $(SEROTONIN_ISO) \
	  -display none -serial stdio \
	  -no-reboot

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)/kernel $(BUILD_DIR)/user \
	       $(BUILD_DIR)/serotonin.bin $(BUILD_DIR)/serotonin.iso \
	       $(BUILD_DIR)/serotonin.img

.PHONY: distclean
distclean: clean
	rm -rf $(SYSROOT) $(TOOLS_DIR)/bin $(TOOLS_DIR)/src \
	       $(TOOLS_DIR)/.built-*
