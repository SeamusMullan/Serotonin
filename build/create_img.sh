#!/usr/bin/env bash
set -euo pipefail

# === CONFIGURATION ===
IMG_NAME="serotonin.img"
IMG_SIZE_MB=512
MOUNT_POINT="/mnt/img"
SRC_DIR="../user"
FILE_LIST="${SRC_DIR}/filelist.txt"   # contains lines like: "test.elf bin/init"
LOOPDEV=""

# === SANITY CHECKS ===
if [[ ! -f "$FILE_LIST" ]]; then
    echo "[!] Missing file list: $FILE_LIST"
    echo "    Each line should be: <source> <destination>"
    echo "    Example: test.elf bin/init"
    exit 1
fi

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

# === COPY FILES FROM FILELIST ===
echo "[*] Copying files according to ${FILE_LIST} ..."
while read -r src dst; do
    [[ -z "${src:-}" || -z "${dst:-}" ]] && continue  # skip blanks

    src_path="${SRC_DIR}/${src}"
    dest_path="${MOUNT_POINT}/${dst}"
    dest_dir="$(dirname "$dest_path")"

    if [[ -f "$src_path" ]]; then
        echo "   → ${src} -> ${dst}"
        sudo mkdir -p "$dest_dir"
        sudo cp "$src_path" "$dest_path"
    else
        echo "   [!] Missing: $src"
    fi
done < "$FILE_LIST"

# === CLEAN UP ===
echo "[*] Unmounting and detaching..."
sudo umount "$MOUNT_POINT"
sudo losetup -d "$LOOPDEV"

echo "[✓] Done! Created image: ${IMG_NAME}"

