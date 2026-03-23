#!/usr/bin/env bash
set -euo pipefail

# Resolve paths relative to this script's directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# === CONFIGURATION ===
IMG_NAME="${SCRIPT_DIR}/serotonin.img"
IMG_SIZE_MB=512
MOUNT_POINT="/mnt/img"
SRC_DIR="${SCRIPT_DIR}/../user"
SYSROOT_DIR="${SCRIPT_DIR}/../sysroot"
LOOPDEV=""

if [ "$LOOPDEV" == "" ]; then

echo "[*] Creating ${IMG_SIZE_MB}MB image: ${IMG_NAME}"

# === CREATE BLANK IMAGE ===
dd if=/dev/zero of="$IMG_NAME" bs=1M count="$IMG_SIZE_MB" status=progress

# === PARTITION THE IMAGE ===
echo "[*] Creating single FAT32 partition..."
parted -s "$IMG_NAME" mklabel msdos
parted -s "$IMG_NAME" mkpart primary fat32 1MiB 100%

# === SET UP LOOP DEVICE ===
echo "[*] Attaching loop device..."
LOOPDEV=$(sudo losetup --show -Pf "$IMG_NAME")
fi
echo "[*] Loop device: ${LOOPDEV}"

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

# === CREATE /etc ===
echo "[*] Creating /etc ..."
sudo mkdir -p "${MOUNT_POINT}/etc"
echo "nameserver 1.1.1.1" | sudo tee "${MOUNT_POINT}/etc/resolv.conf" > /dev/null
echo "serotonin" | sudo tee "${MOUNT_POINT}/etc/hostname" > /dev/null
sudo tee "${MOUNT_POINT}/etc/passwd" > /dev/null <<'PASSWD'
root:x:0:0:root:/root:/bin/sh
PASSWD

# === CREATE /etc/init (startup jobs) ===
echo "[*] Creating /etc/init ..."
sudo mkdir -p "${MOUNT_POINT}/etc/init"
if [ -d "${SRC_DIR}/init/jobs" ]; then
    for job in "${SRC_DIR}/init/jobs/"*; do
        [ -f "$job" ] && sudo cp "$job" "${MOUNT_POINT}/etc/init/"
    done
fi

# === CREATE /var/log ===
echo "[*] Creating /var/log ..."
sudo mkdir -p "${MOUNT_POINT}/var/log"

# === CREATE /srv ===
echo "[*] Creating /srv ..."
sudo mkdir -p "${MOUNT_POINT}/srv"
sudo tee "${MOUNT_POINT}/srv/index.html" > /dev/null <<'HTML'
<html>
<head><title>Serotonin HTTP Server</title></head>
<body>
<h1>Serotonin HTTP Server</h1>
<p>:troll:</p>
</body>
</html>
HTML

# === COPY HOME FILES ===
if [ -d "${SRC_DIR}/home" ]; then
    echo "[*] Copying home files to /home ..."
    sudo mkdir -p "${MOUNT_POINT}/home"
    sudo cp -r "${SRC_DIR}/home/." "${MOUNT_POINT}/home/"
else
    echo "[!] No home directory found at ${SRC_DIR}/home, skipping"
fi

# === COPY SYSROOT ===
if [ -d "$SYSROOT_DIR" ]; then
    echo "[*] Copying sysroot to /usr ..."
    sudo mkdir -p "${MOUNT_POINT}/usr"
    sudo cp -r "${SYSROOT_DIR}/usr/lib" "${MOUNT_POINT}/usr/lib"
    sudo cp -r "${SYSROOT_DIR}/usr/include" "${MOUNT_POINT}/usr/include"
else
    echo "[!] Sysroot not found at ${SYSROOT_DIR}, skipping"
fi

# === CLEAN UP ===
echo "[*] Syncing and unmounting..."
sync
sudo umount "$MOUNT_POINT"
sudo losetup -d "$LOOPDEV"

echo "[✓] Done! Created image: ${IMG_NAME}"
