export DIR=$(pwd)
export PREFIX="$DIR/../build-tools/bin"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

cd ../kernel

i686-elf-as boot.s -o boot.o
i686-elf-as gdt_flush.s -o gdt_flush.o
i686-elf-as isr.s -o isr.o
i686-elf-gcc -c idt.c -o idt.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c tty.c -o tty.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c gdt.c -o gdt.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c fault.c -o fault.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c string.c -o string.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c stdlib/stdlib.c -o stdlib/stdlib.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c stdlib/mem.c -o stdlib/mem.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c stdio/stdio.c -o stdio/stdio.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c kernel.c -o kernel.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c paging.c -o paging.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c io/irq.c -o io/irq.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c io/pic.c -o io/pic.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c io/keyboard.c -o io/keyboard.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c video/font.c -o video/font.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -c video/vbe/vbe.c -o video/vbe/vbe.o -std=gnu99 -ffreestanding -O2 -Wall -Wextra
i686-elf-gcc -T linker.ld -o ../build/serotonin.bin -ffreestanding -O2 -nostdlib boot.o kernel.o tty.o string.o stdlib/stdlib.o stdio/stdio.o stdlib/mem.o gdt_flush.o gdt.o idt.o isr.o fault.o io/irq.o io/pic.o io/keyboard.o paging.o video/vbe/vbe.o video/font.o 

cd ../build

rm -f serotonin.iso
cp serotonin.bin iso/boot/serotonin.bin -v
cp grub.cfg iso/boot/grub/grub.cfg -v
grub-mkrescue -o serotonin.iso iso