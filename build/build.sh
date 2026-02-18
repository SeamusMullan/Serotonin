export DIR=$(pwd)
export PREFIX="$DIR/../build-tools/bin"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"
OS_TYPE="$(uname)"

# parse flags
DEBUG=0
while getopts "g" opt; do
  case ${opt} in
    g ) DEBUG=1 ;;
    \? ) echo "Usage: $0 [-g]" && exit 1 ;;
  esac
done

# compiler flags
CFLAGS="-std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse"
[ "$DEBUG" -eq 1 ] && CFLAGS="$CFLAGS -g"
[ -n "$TEST_MODE" ] && CFLAGS="$CFLAGS -DKERNEL_TEST_MODE"

cd ../kernel

# da assembler
i686-elf-as boot.s -o boot.o
i686-elf-as isr.s -o isr.o
i686-elf-as -c schedule/switch_task.s -o schedule/switch_task.o
i686-elf-as -c schedule/kernel_yield.s -o schedule/kernel_yield.o
i686-elf-as -c schedule/signal_trampoline.s -o schedule/signal_trampoline.o
i686-elf-as -c syscall/isr_syscall.s -o syscall/isr_syscall.o

# da compiler
i686-elf-gcc -c audio/pcspeaker/pcspeaker.c -o audio/pcspeaker/pcspeaker.o $CFLAGS
i686-elf-gcc -c audio/opl2/opl2.c -o audio/opl2/opl2.o $CFLAGS
i686-elf-gcc -c audio/startup/opl2_sound/opl2_startup.c -o audio/startup/opl2_sound/opl2_startup.o $CFLAGS

i686-elf-gcc -c io/irq.c -o io/irq.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c io/pic.c -o io/pic.o $CFLAGS
i686-elf-gcc -c io/keyboard.c -o io/keyboard.o $CFLAGS
i686-elf-gcc -c io/rtc.c -o io/rtc.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c io/serial.c -o io/serial.o $CFLAGS

i686-elf-gcc -c stdlib/stdlib.c -o stdlib/stdlib.o $CFLAGS
i686-elf-gcc -c stdlib/mem.c -o stdlib/mem.o $CFLAGS
i686-elf-gcc -c stdio/stdio.c -o stdio/stdio.o $CFLAGS

i686-elf-gcc -c schedule/schedule.c -o schedule/schedule.o $CFLAGS
i686-elf-gcc -c syscall/syscall.c -o syscall/syscall.o $CFLAGS

i686-elf-gcc -c gdt.c -o gdt.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c idt.c -o idt.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c kernel.c -o kernel.o $CFLAGS
i686-elf-gcc -c tty.c -o tty.o $CFLAGS
i686-elf-gcc -c fault.c -o fault.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c string.c -o string.o $CFLAGS

i686-elf-gcc -c vmm/paging_init.c -o vmm/paging_init.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c vmm/vmm.c -o vmm/vmm.o -std=gnu99 -ffreestanding -O0 -Wall -Wextra

i686-elf-gcc -c video/font.c -o video/font.o $CFLAGS
i686-elf-gcc -c video/vbe/vbe.c -o video/vbe/vbe.o $CFLAGS -mstackrealign

i686-elf-gcc -c filesystem/vfs.c -o filesystem/vfs.o $CFLAGS
i686-elf-gcc -c filesystem/ide.c -o filesystem/ide.o -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c filesystem/devfs/devfs.c -o filesystem/devfs/devfs.o $CFLAGS
i686-elf-gcc -c filesystem/tmpfs/tmpfs.c -o filesystem/tmpfs/tmpfs.o $CFLAGS
i686-elf-gcc -c filesystem/fat32/fat32.c -o filesystem/fat32/fat32.o $CFLAGS
i686-elf-gcc -c filesystem/user_fs/user_fs.c -o filesystem/user_fs/user_fs.o $CFLAGS

i686-elf-gcc -c device/devfs_example.c -o device/devfs_example.o $CFLAGS
i686-elf-gcc -c device/mouse/dev_mouse.c -o device/mouse/dev_mouse.o $CFLAGS
i686-elf-gcc -c device/keyboard/dev_keyboard.c -o device/keyboard/dev_keyboard.o $CFLAGS

# Conditionally build test objects if TEST_MODE is enabled
TEST_OBJS=""
if [ -n "$TEST_MODE" ]; then
    echo "TEST_MODE enabled - including test objects..."
    TEST_OBJS="test/ktest.o test/test_main.o test/test_string.o test/test_mem.o test/test_stdlib.o test/test_vfs.o test/test_paging.o test/test_scheduler.o test/test_io.o"
fi

# da linker
i686-elf-gcc -T linker.ld -o ../build/serotonin.bin -ffreestanding -O2 -nostdlib boot.o kernel.o tty.o string.o stdlib/stdlib.o stdio/stdio.o stdlib/mem.o gdt.o idt.o isr.o fault.o io/irq.o io/pic.o io/keyboard.o vmm/paging_init.o vmm/vmm.o video/vbe/vbe.o video/font.o filesystem/vfs.o filesystem/devfs/devfs.o filesystem/tmpfs/tmpfs.o filesystem/ide.o filesystem/fat32/fat32.o schedule/schedule.o schedule/switch_task.o audio/pcspeaker/pcspeaker.o audio/opl2/opl2.o syscall/isr_syscall.o syscall/syscall.o audio/startup/opl2_sound/opl2_startup.o schedule/kernel_yield.o schedule/signal_trampoline.o filesystem/user_fs/user_fs.o device/devfs_example.o io/rtc.o io/serial.o device/mouse/dev_mouse.o device/keyboard/dev_keyboard.o $TEST_OBJS

cd ../build

rm -f serotonin.iso
cp serotonin.bin iso/boot/serotonin.bin
cp grub.cfg iso/boot/grub/grub.cfg
if [[ "$OS_TYPE" == "Darwin" ]]; then
    /opt/homebrew/Cellar/i686-elf-grub/2.12/bin/i686-elf-grub-mkrescue -o serotonin.iso iso
elif [[ "$OS_TYPE" == "Linux" ]]; then
    grub-mkrescue -o serotonin.iso iso
fi
