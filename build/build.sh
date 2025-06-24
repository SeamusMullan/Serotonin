export DIR=$(pwd)
export PREFIX="$DIR/../build-tools/bin"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"
OS_TYPE="$(uname)"

cd ../kernel

i686-elf-as boot.s -o boot.o 
i686-elf-as isr.s -o isr.o 
i686-elf-gcc -c idt.c -o idt.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c tty.c -o tty.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c gdt.c -o gdt.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c fault.c -o fault.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c string.c -o string.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c stdlib/stdlib.c -o stdlib/stdlib.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c stdlib/mem.c -o stdlib/mem.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c stdio/stdio.c -o stdio/stdio.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c kernel.c -o kernel.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c paging.c -o paging.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c io/irq.c -o io/irq.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c io/pic.c -o io/pic.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c io/keyboard.c -o io/keyboard.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c video/font.c -o video/font.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c video/vbe/vbe.c -o video/vbe/vbe.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c video/splash.c -o video/splash.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c filesystem/vfs.c -o filesystem/vfs.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c filesystem/ide.c -o filesystem/ide.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c filesystem/tmpfs/tmpfs.c -o filesystem/tmpfs/tmpfs.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c filesystem/fat32/fat32.c -o filesystem/fat32/fat32.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-gcc -c schedule/schedule.c -o schedule/schedule.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -mfpmath=sse
i686-elf-as -c schedule/switch_task.s -o schedule/switch_task.o
i686-elf-gcc -T linker.ld -o ../build/serotonin.bin -ffreestanding -O2 -nostdlib boot.o kernel.o tty.o string.o stdlib/stdlib.o stdio/stdio.o stdlib/mem.o gdt.o idt.o isr.o fault.o io/irq.o io/pic.o io/keyboard.o paging.o video/vbe/vbe.o video/font.o video/splash.o filesystem/vfs.o filesystem/tmpfs/tmpfs.o filesystem/ide.o filesystem/fat32/fat32.o schedule/schedule.o schedule/switch_task.o

cd ../build

rm -f serotonin.iso
cp serotonin.bin iso/boot/serotonin.bin
cp grub.cfg iso/boot/grub/grub.cfg
if [[ "$OS_TYPE" == "Darwin" ]]; then
    /opt/homebrew/Cellar/i686-elf-grub/2.12/bin/i686-elf-grub-mkrescue -o serotonin.iso iso
elif [[ "$OS_TYPE" == "Linux" ]]; then
    grub-mkrescue -o serotonin.iso iso
fi