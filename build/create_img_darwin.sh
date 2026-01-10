#!/usr/bin/env bash
set -euo pipefail

# === CONFIGURATION ===
IMG_NAME="serotonin.img"
IMG_SIZE_MB=512
MOUNT_POINT="/Volumes/SEROTONIN"
SRC_DIR="../user"
FILE_LIST="${SRC_DIR}/filelist.txt"   # contains lines like: "test.elf bin/init"
DISKDEV=""
TEMP_DMG="serotonin_temp.dmg"

# === SANITY CHECKS ===
if [[ ! -f "$FILE_LIST" ]]; then
    echo "[!] Missing file list: $FILE_LIST"
    echo "    Each line should be: <source> <destination>"
    echo "    Example: test.elf bin/init"
    exit 1
fi

echo "[*] Creating ${IMG_SIZE_MB}MB image: ${IMG_NAME}"

# === CREATE AND FORMAT IMAGE ===
echo "[*] Creating FAT32 disk image..."
hdiutil create -size "${IMG_SIZE_MB}m" -fs "MS-DOS FAT32" -volname "SEROTONIN" -layout MBRSPUD -ov "$TEMP_DMG"

# === ATTACH DISK IMAGE ===
echo "[*] Attaching disk image..."
DISKDEV=$(hdiutil attach "$TEMP_DMG" | grep "^/dev/" | tail -n1 | awk '{print $1}')

echo "[*] Mounted at ${MOUNT_POINT}"
sleep 1  # wait for mount to complete

# === COPY ALL ELF FILES ===
echo "[*] Copying all ELF files from ${SRC_DIR} ..."
sudo mkdir -p "${MOUNT_POINT}/bin"
find "$SRC_DIR" -name "*.elf" -type f | while read -r elf_file; do
    elf_name="$(basename "$elf_file")"
    echo "   → ${elf_file} -> bin/${elf_name}"
    sudo cp "$elf_file" "${MOUNT_POINT}/bin/${elf_name}"
done

# === CLEAN UP ===
echo "[*] Ejecting disk image..."
hdiutil eject "$DISKDEV"

# === CONVERT TO RAW IMAGE ===
echo "[*] Converting to raw disk image..."
hdiutil convert "$TEMP_DMG" -format UDRO -o "${IMG_NAME%.img}"
rm "$TEMP_DMG"

# Rename .dmg to .img
mv "${IMG_NAME%.img}.dmg" "$IMG_NAME"

echo "[✓] Done! Created image: ${IMG_NAME}"