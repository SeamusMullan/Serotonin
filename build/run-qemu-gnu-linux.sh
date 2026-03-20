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

# Set up tap interface for packet sniffing
TAP_IF="${SEROTONIN_TAP:-tap0}"
if ! ip link show "$TAP_IF" &>/dev/null; then
	echo "[INFO] Creating tap interface $TAP_IF (requires sudo)..."
	sudo ip tuntap add dev "$TAP_IF" mode tap user "$(whoami)"
fi
# Ensure tap always has its IP and is up (idempotent)
if ! ip addr show "$TAP_IF" 2>/dev/null | grep -q '10.0.2.1/24'; then
	sudo ip addr add 10.0.2.1/24 dev "$TAP_IF"
fi
sudo ip link set "$TAP_IF" up

# Kill stale serotonin dnsmasq, then (re)start it
sudo pkill -f 'serotonin-dnsmasq' 2>/dev/null || true
sudo rm -f /tmp/serotonin-dnsmasq.pid /tmp/serotonin-dnsmasq.log
echo "[INFO] Starting dnsmasq DHCP server on $TAP_IF..."
sudo dnsmasq --interface="$TAP_IF" --bind-dynamic \
	--dhcp-range=10.0.2.50,10.0.2.150,255.255.255.0,12h \
	--except-interface=lo --no-resolv --no-hosts \
	--pid-file=/tmp/serotonin-dnsmasq.pid \
	--log-facility=/tmp/serotonin-dnsmasq.log \
	--log-dhcp
echo "[INFO] dnsmasq DHCP server started on $TAP_IF (10.0.2.50-150)"

# NAT: masquerade tap traffic out through the host's default interface
HOST_IF="$(ip route show default | awk '{print $5; exit}')"
sudo sysctl -w net.ipv4.ip_forward=1 >/dev/null
sudo iptables -t nat -C POSTROUTING -s 10.0.2.0/24 -o "$HOST_IF" -j MASQUERADE 2>/dev/null \
	|| sudo iptables -t nat -A POSTROUTING -s 10.0.2.0/24 -o "$HOST_IF" -j MASQUERADE
sudo iptables -C FORWARD -i "$TAP_IF" -o "$HOST_IF" -j ACCEPT 2>/dev/null \
	|| sudo iptables -A FORWARD -i "$TAP_IF" -o "$HOST_IF" -j ACCEPT
sudo iptables -C FORWARD -i "$HOST_IF" -o "$TAP_IF" -m state --state RELATED,ESTABLISHED -j ACCEPT 2>/dev/null \
	|| sudo iptables -A FORWARD -i "$HOST_IF" -o "$TAP_IF" -m state --state RELATED,ESTABLISHED -j ACCEPT
echo "[INFO] NAT enabled: $TAP_IF -> $HOST_IF"

# RTL8139 NIC on the tap backend
QEMU_CMD+=(
	-netdev tap,id=net0,ifname="$TAP_IF",script=no,downscript=no
	-device rtl8139,netdev=net0
)

# Forward any extra args to QEMU (e.g., -serial mon:stdio, -display none, etc.)
QEMU_CMD+=( "$@" )

exec "${QEMU_CMD[@]}"
