#!/usr/bin/env bash
set -euo pipefail

# Resolve script directory to ensure relative paths work regardless of CWD
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ISO="$SCRIPT_DIR/serotonin.iso"

# Allow override via SEROTONIN_DISK env var; otherwise use local serotonin.img,
# or try common qcow2 paths used by cp-user-elfs-to-disk.sh
DISK_IMAGE="${SEROTONIN_DISK:-}"
if [[ -z "${DISK_IMAGE}" ]]; then
	if [[ -f "$SCRIPT_DIR/serotonin.img" ]]; then
		DISK_IMAGE="$SCRIPT_DIR/serotonin.img"
	elif [[ -f "$SCRIPT_DIR/serotonin.qcow2" ]]; then
		DISK_IMAGE="$SCRIPT_DIR/serotonin.qcow2"
	elif [[ -f "/var/lib/libvirt/images/serotonin-1.qcow2" ]]; then
		DISK_IMAGE="/var/lib/libvirt/images/serotonin-1.qcow2"
	else
		DISK_IMAGE=""
	fi
fi

# Basic sanity checks with friendly messages
if [[ ! -f "$ISO" ]]; then
	echo "[WARN] ISO not found at $ISO. Did the build complete?"
fi

if [[ -z "$DISK_IMAGE" ]]; then
	echo "[WARN] No disk image found. The guest HDD will be missing."
	echo "       Set SEROTONIN_DISK to a path, or place serotonin.img/serotonin.qcow2 in $SCRIPT_DIR."
fi

# Compose QEMU command
QEMU_CMD=(
	qemu-system-x86_64
	-m 2048
	-boot d                     # boot from CD first
	-cdrom "$ISO"
	-vga std
)

# Attach user HDD if present. Use primary slave (index=1) to match IDE probing.
if [[ -n "$DISK_IMAGE" ]]; then
	fmt="raw"
	case "$DISK_IMAGE" in
		*.qcow2) fmt="qcow2" ;;
		*.img) fmt="raw" ;;
	esac
	QEMU_CMD+=( -drive file="$DISK_IMAGE",if=ide,index=1,media=disk,format="$fmt" )
fi

# Forward any extra args to QEMU (e.g., -serial mon:stdio, -display none, etc.)
QEMU_CMD+=( "$@" )

exec "${QEMU_CMD[@]}"
