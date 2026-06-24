# ---------------------------------------------------------------------------
# ISO + disk image creation
# ---------------------------------------------------------------------------

ISO_DIR       := $(BUILD_DIR)/iso
ISO_BOOT      := $(ISO_DIR)/boot
SEROTONIN_ISO := $(BUILD_DIR)/serotonin.iso
SEROTONIN_IMG := $(BUILD_DIR)/serotonin.img
IMG_SIZE_MB   := 512
MOUNT_POINT   := /mnt/serotonin-img

.PHONY: iso
iso: $(SEROTONIN_ISO)

$(SEROTONIN_ISO): $(KERNEL_BIN)
	@mkdir -p $(ISO_BOOT)/grub
	cp $(KERNEL_BIN) $(ISO_BOOT)/serotonin.bin
	cp $(BUILD_DIR)/grub.cfg $(ISO_BOOT)/grub/grub.cfg
	@echo "[iso] Running grub-mkrescue..."
	$(GRUB_MKRESCUE) -o $@ $(ISO_DIR)
	@echo "[iso] $(SEROTONIN_ISO)"

.PHONY: img
img: $(SEROTONIN_IMG)

$(SEROTONIN_IMG): user
	@echo "[img] Creating $(IMG_SIZE_MB)MB FAT32 image..."
	@set -e; \
	dd if=/dev/zero of=$@ bs=1M count=$(IMG_SIZE_MB) status=progress; \
	parted -s $@ mklabel msdos; \
	parted -s $@ mkpart primary fat32 1MiB 100%; \
	LOOPDEV=$$(sudo losetup --show -Pf $@); \
	echo "[img] Loop device: $$LOOPDEV"; \
	sudo partprobe $$LOOPDEV; \
	sleep 2; \
	sudo mkfs.vfat -F 32 $${LOOPDEV}p1; \
	sudo mkdir -p $(MOUNT_POINT); \
	sudo mount $${LOOPDEV}p1 $(MOUNT_POINT); \
	sudo mkdir -p $(MOUNT_POINT)/bin; \
	for elf in $(USER_ELFS); do \
	  base=$$(basename $$elf .elf); \
	  echo "  -> /bin/$$base"; \
	  sudo cp $$elf $(MOUNT_POINT)/bin/$$base; \
	done; \
	sudo mkdir -p $(MOUNT_POINT)/etc; \
	echo "nameserver 1.1.1.1" | sudo tee $(MOUNT_POINT)/etc/resolv.conf > /dev/null; \
	echo "serotonin"          | sudo tee $(MOUNT_POINT)/etc/hostname     > /dev/null; \
	printf 'root:x:0:0:root:/root:/bin/sh\n' | sudo tee $(MOUNT_POINT)/etc/passwd > /dev/null; \
	sudo mkdir -p $(MOUNT_POINT)/etc/init; \
	if [ -d $(USER_DIR)/init/jobs ]; then \
	  for job in $(USER_DIR)/init/jobs/*; do \
	    [ -f "$$job" ] && sudo cp "$$job" $(MOUNT_POINT)/etc/init/; \
	  done; fi; \
	sudo mkdir -p $(MOUNT_POINT)/var/log $(MOUNT_POINT)/srv; \
	printf '<html><body><h1>Serotonin</h1></body></html>\n' | \
	  sudo tee $(MOUNT_POINT)/srv/index.html > /dev/null; \
	if [ -d $(SYSROOT) ]; then \
	  sudo mkdir -p $(MOUNT_POINT)/usr; \
	  sudo cp -r $(SYSROOT)/usr/lib     $(MOUNT_POINT)/usr/lib; \
	  sudo cp -r $(SYSROOT)/usr/include $(MOUNT_POINT)/usr/include; fi; \
	sync; \
	sudo umount $(MOUNT_POINT); \
	sudo losetup -d $$LOOPDEV; \
	echo "[img] $@"
