#!/usr/bin/env bash
set -euo pipefail

# === CONFIGURATION ===
IMG_NAME="serotonin.img"
IMG_SIZE_MB=512
MOUNT_POINT="/mnt/img"
SRC_DIR="../user"
LOOPDEV=""

echo "[*] Creating ${IMG_SIZE_MB}MB image: ${IMG_NAME}"

# === CREATE BLANK IMAGE ===
dd if=/dev/zero of="$IMG_NAME" bs=1M count="$IMG_SIZE_MB" status=progress

# === PARTITION THE IMAGE ===
echo "[*] Creating single FAT32 partition..."
parted -s "$IMG_NAME" mklabel msdos
parted -s "$IMG_NAME" mkpart primary fat32 1MiB 100%

# === SET UP LOOP DEVICE ===
echo "[*] Attaching loop device..."
sudo losetup -Pf "$IMG_NAME"
LOOPDEV=$(losetup -a | grep "$IMG_NAME" | cut -d: -f1)

sleep 1  # wait for partition to appear

# === FORMAT PARTITION ===
echo "[*] Formatting partition as FAT32..."
sudo mkfs.vfat -F 32 "${LOOPDEV}p1"

# === MOUNT IMAGE ===
echo "[*] Mounting image at ${MOUNT_POINT}..."
sudo mkdir -p "$MOUNT_POINT"
sudo mount "${LOOPDEV}p1" "$MOUNT_POINT"

# === COPY ALL .elf FILES TO /bin ===
echo "[*] Copying all .elf files to /bin ..."
sudo mkdir -p "${MOUNT_POINT}/bin"
while IFS= read -r -d '' elf_file; do
    basename="${elf_file##*/}"
    dest_name="${basename%.elf}"
    echo "   → ${elf_file} -> /bin/${dest_name}"
    sudo cp "$elf_file" "${MOUNT_POINT}/bin/${dest_name}"
done < <(find "${SRC_DIR}" -name "*.elf" -type f -print0)

# === CLEAN UP ===
echo "[*] Unmounting and detaching..."
sudo umount "$MOUNT_POINT"
sudo losetup -d "$LOOPDEV"

echo "[✓] Done! Created image: ${IMG_NAME}"

