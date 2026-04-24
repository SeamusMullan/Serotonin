#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

export DIR="$SCRIPT_DIR"
OS_TYPE="$(uname)"

# Detect toolchain: prefer i686-serotonin, fall back to i686-elf
if [ -x "$ROOT_DIR/build-tools/cross/bin/i686-serotonin-gcc" ]; then
    export PREFIX="$ROOT_DIR/build-tools/cross"
    export TARGET=i686-serotonin
elif [ -x "$ROOT_DIR/build-tools/bin/bin/i686-elf-gcc" ]; then
    export PREFIX="$ROOT_DIR/build-tools/bin"
    export TARGET=i686-elf
else
    echo "Error: no cross-compiler found. Build the toolchain first."
    exit 1
fi
export PATH="$PREFIX/bin:$PATH"

# parse flags
DEBUG=0
while getopts "g" opt; do
  case ${opt} in
    g ) DEBUG=1 ;;
    \? ) echo "Usage: $0 [-g]" && exit 1 ;;
  esac
done

# kernel include root: -I.. so that <kernel/...> resolves from the repo root
KERNEL_INCLUDES="-I.."

# compiler flags (freestanding kernel — -nostdlib suppresses serotonin default linking)
CFLAGS="-std=gnu99 -ffreestanding -nostdlib -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse -fstack-protector-strong $KERNEL_INCLUDES"
[ "$DEBUG" -eq 1 ] && CFLAGS="$CFLAGS -g"
[ -n "${TEST_MODE:-}" ] && CFLAGS="$CFLAGS -DKERNEL_TEST_MODE"

CC="$TARGET-gcc"
AS="$TARGET-as"

cd "$ROOT_DIR/kernel"

# da assembler
$AS boot.s -o boot.o
$AS isr.s -o isr.o
$AS -c schedule/switch_task.s -o schedule/switch_task.o
$AS -c schedule/kernel_yield.s -o schedule/kernel_yield.o
$AS -c schedule/signal_trampoline.s -o schedule/signal_trampoline.o
$AS -c syscall/isr_syscall.s -o syscall/isr_syscall.o

# da compiler

$CC -c io/irq.c -o io/irq.o -std=gnu99 -ffreestanding -nostdlib -O2 -Wall -Wextra $KERNEL_INCLUDES
$CC -c io/pic.c -o io/pic.o $CFLAGS
$CC -c io/keyboard.c -o io/keyboard.o $CFLAGS
$CC -c io/rtc.c -o io/rtc.o -std=gnu99 -ffreestanding -nostdlib -O2 -Wall -Wextra $KERNEL_INCLUDES
$CC -c io/serial.c -o io/serial.o $CFLAGS
$CC -c io/pci/pci.c -o io/pci/pci.o $CFLAGS

$CC -c stdlib/stdlib.c -o stdlib/stdlib.o $CFLAGS
$CC -c stdlib/mem.c -o stdlib/mem.o $CFLAGS
$CC -c stdio/stdio.c -o stdio/stdio.o $CFLAGS

$CC -c schedule/schedule.c -o schedule/schedule.o $CFLAGS
$CC -c syscall/syscall.c -o syscall/syscall.o $CFLAGS -mno-sse -mno-sse2

$CC -c gdt.c -o gdt.o -std=gnu99 -ffreestanding -nostdlib -O2 -Wall -Wextra $KERNEL_INCLUDES
$CC -c idt.c -o idt.o -std=gnu99 -ffreestanding -nostdlib -O2 -Wall -Wextra $KERNEL_INCLUDES
$CC -c kernel.c -o kernel.o -std=gnu99 -ffreestanding -nostdlib -O0 -Wall -Wextra $KERNEL_INCLUDES
$CC -c tty.c -o tty.o $CFLAGS
$CC -c fault.c -o fault.o -std=gnu99 -ffreestanding -nostdlib -O2 -Wall -Wextra $KERNEL_INCLUDES
$CC -c string.c -o string.o $CFLAGS

$CC -c vmm/paging_init.c -o vmm/paging_init.o -std=gnu99 -ffreestanding -nostdlib -O2 -Wall -Wextra $KERNEL_INCLUDES
$CC -c vmm/vmm.c -o vmm/vmm.o -std=gnu99 -ffreestanding -nostdlib -O0 -Wall -Wextra $KERNEL_INCLUDES

$CC -c video/font.c -o video/font.o $CFLAGS
$CC -c video/vbe/vbe.c -o video/vbe/vbe.o $CFLAGS -mstackrealign

$CC -c filesystem/vfs.c -o filesystem/vfs.o $CFLAGS
$CC -c device/ide/ide_pci.c -o device/ide/ide_pci.o $CFLAGS
$CC -c filesystem/devfs/devfs.c -o filesystem/devfs/devfs.o $CFLAGS
$CC -c filesystem/tmpfs/tmpfs.c -o filesystem/tmpfs/tmpfs.o $CFLAGS
$CC -c filesystem/fat32/fat32.c -o filesystem/fat32/fat32.o $CFLAGS
$CC -c filesystem/blkcache.c -o filesystem/blkcache.o $CFLAGS
$CC -c filesystem/vfs_perm.c -o filesystem/vfs_perm.o $CFLAGS
$CC -c filesystem/user_fs/user_fs.c -o filesystem/user_fs/user_fs.o $CFLAGS

$CC -c device/devfs_example.c -o device/devfs_example.o $CFLAGS
$CC -c device/mouse/dev_mouse.c -o device/mouse/dev_mouse.o $CFLAGS
$CC -c device/keyboard/dev_keyboard.c -o device/keyboard/dev_keyboard.o $CFLAGS
$CC -c device/serial/dev_serial.c -o device/serial/dev_serial.o $CFLAGS
$CC -c device/rtl8139/rtl8139.c -o device/rtl8139/rtl8139.o $CFLAGS
$CC -c device/rtl8139/dev_rtl8139.c -o device/rtl8139/dev_rtl8139.o $CFLAGS
$CC -c device/ac97/ac97.c -o device/ac97/ac97.o $CFLAGS
$CC -c device/ac97/dev_ac97.c -o device/ac97/dev_ac97.o $CFLAGS
$CC -c device/pci_drivers.c -o device/pci_drivers.o $CFLAGS

$CC -c pty/pty.c -o pty/pty.o $CFLAGS

# Conditionally build test objects if TEST_MODE is enabled
TEST_OBJS=""
if [ -n "${TEST_MODE:-}" ]; then
    echo "TEST_MODE enabled - including test objects..."
    TEST_OBJS="test/ktest.o test/test_main.o test/test_string.o test/test_mem.o test/test_stdlib.o test/test_vfs.o test/test_paging.o test/test_scheduler.o test/test_io.o"
fi

# da linker (freestanding, own linker script, link with -lgcc for compiler builtins)
$CC -T linker.ld -o ../build/serotonin.bin -ffreestanding -O2 -nostdlib boot.o kernel.o tty.o string.o stdlib/stdlib.o stdio/stdio.o stdlib/mem.o gdt.o idt.o isr.o fault.o io/irq.o io/pic.o io/keyboard.o vmm/paging_init.o vmm/vmm.o video/vbe/vbe.o video/font.o filesystem/vfs.o filesystem/devfs/devfs.o filesystem/tmpfs/tmpfs.o device/ide/ide_pci.o filesystem/fat32/fat32.o filesystem/blkcache.o schedule/schedule.o schedule/switch_task.o syscall/isr_syscall.o syscall/syscall.o schedule/kernel_yield.o schedule/signal_trampoline.o filesystem/vfs_perm.o filesystem/user_fs/user_fs.o device/devfs_example.o io/rtc.o io/serial.o io/pci/pci.o device/mouse/dev_mouse.o device/keyboard/dev_keyboard.o device/serial/dev_serial.o device/rtl8139/rtl8139.o device/rtl8139/dev_rtl8139.o device/ac97/ac97.o device/ac97/dev_ac97.o device/pci_drivers.o pty/pty.o $TEST_OBJS -lgcc

cd "$SCRIPT_DIR"

rm -f serotonin.iso
cp serotonin.bin iso/boot/serotonin.bin
cp grub.cfg iso/boot/grub/grub.cfg

if [[ "${SEROTONIN_EMBED_ROOTFS:-0}" == "1" ]]; then
    ROOTFS_IMAGE="${SEROTONIN_ROOTFS_IMAGE:-$SCRIPT_DIR/serotonin.img}"
    if [[ -f "$ROOTFS_IMAGE" ]]; then
        echo "[*] Embedding rootfs image into ISO: $ROOTFS_IMAGE"
        cp "$ROOTFS_IMAGE" iso/boot/rootfs.img
    else
        rm -f iso/boot/rootfs.img
        echo "[WARN] rootfs image not found at $ROOTFS_IMAGE; continuing without embedded rootfs."
    fi
else
    rm -f iso/boot/rootfs.img
    echo "[*] Skipping embedded rootfs (set SEROTONIN_EMBED_ROOTFS=1 to include /boot/rootfs.img)."
fi

echo "[*] Running grub-mkrescue..."
if [[ "$OS_TYPE" == "Darwin" ]]; then
    /opt/homebrew/Cellar/i686-elf-grub/2.12/bin/i686-elf-grub-mkrescue -o serotonin.iso iso
elif [[ "$OS_TYPE" == "Linux" ]]; then
    grub-mkrescue -o serotonin.iso iso
fi
