#!/usr/bin/env bash
set -euo pipefail

# Resolve paths relative to this script's directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# === CONFIGURATION ===
IMG_NAME="${SCRIPT_DIR}/serotonin.img"
IMG_SIZE_MB=512
MOUNT_POINT="/mnt/img"
SRC_DIR="${SCRIPT_DIR}/../user"
SYSROOT_DIR="${SCRIPT_DIR}/../build-tools/cross/i686-serotonin/sys-root"
LOOPDEV="/dev/nbd0"

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
GCC_ELF_SKIP="gcc.elf g++.elf cpp.elf c++.elf gcc-ar.elf gcc-nm.elf gcc-ranlib.elf lto-dump.elf"
GCC_ELF_SKIP="$GCC_ELF_SKIP i686-serotonin-gcc.elf i686-serotonin-g++.elf i686-serotonin-gcc-16.0.0.elf"
GCC_ELF_SKIP="$GCC_ELF_SKIP i686-serotonin-c++.elf i686-serotonin-gcc-ar.elf i686-serotonin-gcc-nm.elf i686-serotonin-gcc-ranlib.elf"
echo "[*] Copying all .elf files to /bin ..."
sudo mkdir -p "${MOUNT_POINT}/bin"
while IFS= read -r -d '' elf_file; do
    basename="${elf_file##*/}"
    dest_name="${basename%.elf}"
    skip=0
    for s in $GCC_ELF_SKIP; do [ "$basename" = "$s" ] && skip=1 && break; done
    if [ $skip -eq 0 ]; then
        echo "   → ${elf_file} -> /bin/${dest_name}"
        sudo cp "$elf_file" "${MOUNT_POINT}/bin/${dest_name}"
    fi
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

# === CREATE /tmp ===
echo "[*] Creating /tmp ..."
sudo mkdir -p "${MOUNT_POINT}/tmp"

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
    echo "[*] Creating sysroot at /usr/sysroot ..."
    sudo mkdir -p "${MOUNT_POINT}/usr/sysroot/usr"
    sudo cp -r "${SYSROOT_DIR}/usr/include" "${MOUNT_POINT}/usr/sysroot/usr/include"
    sudo cp -r "${SYSROOT_DIR}/usr/lib" "${MOUNT_POINT}/usr/sysroot/usr/lib"
else
    echo "[!] Sysroot not found at ${SYSROOT_DIR}, skipping"
fi

# === INSTALL GCC/BINUTILS INTO /usr (matching --prefix=/usr) ===
GCC_STAGE="${SCRIPT_DIR}/gcc-user-stage"
BINUTILS_STAGE="${SCRIPT_DIR}/binutils-user-stage"

if [ -d "$GCC_STAGE/usr" ]; then
    echo "[*] Installing GCC into /usr ..."
    sudo mkdir -p "${MOUNT_POINT}/usr/bin" "${MOUNT_POINT}/usr/libexec" "${MOUNT_POINT}/usr/lib"
    sudo cp -r "$GCC_STAGE/usr/bin/."     "${MOUNT_POINT}/usr/bin/"     2>/dev/null || true
    sudo cp -r "$GCC_STAGE/usr/libexec/." "${MOUNT_POINT}/usr/libexec/" 2>/dev/null || true
    sudo cp -r "$GCC_STAGE/usr/lib/."     "${MOUNT_POINT}/usr/lib/"     2>/dev/null || true
fi
if [ -d "$BINUTILS_STAGE/usr" ]; then
    echo "[*] Installing binutils into /usr ..."
    sudo cp -r "$BINUTILS_STAGE/usr/bin/." "${MOUNT_POINT}/usr/bin/" 2>/dev/null || true
    sudo cp -r "$BINUTILS_STAGE/usr/lib/." "${MOUNT_POINT}/usr/lib/" 2>/dev/null || true
    echo "[*] Creating /usr/i686-serotonin/bin ..."
    sudo mkdir -p "${MOUNT_POINT}/usr/i686-serotonin/bin"
    sudo cp "${MOUNT_POINT}/usr/bin/as" "${MOUNT_POINT}/usr/i686-serotonin/bin/as" 2>/dev/null || true
    sudo cp "${MOUNT_POINT}/usr/bin/ld" "${MOUNT_POINT}/usr/i686-serotonin/bin/ld" 2>/dev/null || true
fi

GCC_SKIP="gcc g++ cpp c++ gcc-ar gcc-nm gcc-ranlib lto-dump"
GCC_SKIP="$GCC_SKIP i686-serotonin-gcc i686-serotonin-g++ i686-serotonin-gcc-16.0.0"
GCC_SKIP="$GCC_SKIP i686-serotonin-gcc-ar i686-serotonin-gcc-nm i686-serotonin-gcc-ranlib"
if [ -d "${MOUNT_POINT}/usr/bin" ]; then
    for f in "${MOUNT_POINT}"/usr/bin/*; do
        name="$(basename "$f")"
        skip=0
        for s in $GCC_SKIP; do [ "$name" = "$s" ] && skip=1 && break; done
        [ $skip -eq 0 ] && [ ! -e "${MOUNT_POINT}/bin/${name}" ] && sudo cp -r "$f" "${MOUNT_POINT}/bin/${name}"
    done
fi

# === CLEAN UP ===
echo "[*] Syncing and unmounting..."
sync
sudo umount "$MOUNT_POINT"
sudo losetup -d "$LOOPDEV"

echo "[✓] Done! Created image: ${IMG_NAME}"
