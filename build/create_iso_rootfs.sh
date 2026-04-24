#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OS_TYPE="$(uname)"

OUTPUT_ISO="${SEROTONIN_ISO_OUTPUT:-$SCRIPT_DIR/serotonin-rootfs.iso}"
ROOTFS_IMAGE="${SEROTONIN_ROOTFS_IMAGE:-$SCRIPT_DIR/serotonin.img}"
CUSTOM_GRUB_CFG="$SCRIPT_DIR/iso/boot/grub/grub.cfg"
KERNEL_BIN="${SEROTONIN_KERNEL_BIN:-$SCRIPT_DIR/serotonin.bin}"

if [[ ! -f "$KERNEL_BIN" ]]; then
	echo "[ERROR] Kernel binary not found: $KERNEL_BIN"
	echo "        Build it first (e.g. run build/build.sh)."
	exit 1
fi

if [[ ! -f "$ROOTFS_IMAGE" ]]; then
	echo "[ERROR] Rootfs image not found: $ROOTFS_IMAGE"
	exit 1
fi

echo "[*] Preparing ISO staging tree (no kernel/userspace build)..."
mkdir -p "$SCRIPT_DIR/iso/boot/grub"
cp "$KERNEL_BIN" "$SCRIPT_DIR/iso/boot/serotonin.bin"

echo "[*] Writing ISO-rootfs GRUB config..."
cat > "$CUSTOM_GRUB_CFG" <<'EOF'
set gfxpayload=1920x1080x32

menuentry "Serotonin (ISO rootfs)" {
	echo "Loading kernel..."
	multiboot /boot/serotonin.bin quiet rootfs=iso
	echo "Loading rootfs..."
	module /boot/rootfs.img rootfs
}

menuentry "Serotonin (ISO rootfs): info" {
	echo "Loading kernel..."
	multiboot /boot/serotonin.bin info rootfs=iso
	echo "Loading rootfs..."
	module /boot/rootfs.img rootfs
}

menuentry "Serotonin (ISO rootfs): debug" {
	echo "Loading kernel..."
	multiboot /boot/serotonin.bin debug rootfs=iso
	echo "Loading rootfs..."
	module /boot/rootfs.img rootfs
}
EOF

cp "$ROOTFS_IMAGE" "$SCRIPT_DIR/iso/boot/rootfs.img"

echo "[*] Creating ISO with default rootfs=iso..."
if [[ "$OS_TYPE" == "Darwin" ]]; then
    /opt/homebrew/Cellar/i686-elf-grub/2.12/bin/i686-elf-grub-mkrescue -o "$OUTPUT_ISO" "$SCRIPT_DIR/iso"
elif [[ "$OS_TYPE" == "Linux" ]]; then
    grub-mkrescue -o "$OUTPUT_ISO" "$SCRIPT_DIR/iso"
else
    echo "[ERROR] Unsupported OS type: $OS_TYPE"
    exit 1
fi

echo "[✓] Done! Created ISO: $OUTPUT_ISO"
