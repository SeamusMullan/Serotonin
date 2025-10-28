export DIR=$(pwd)
export PREFIX="$DIR/../build-tools/bin"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"
OS_TYPE="$(uname)"

# compiler flags
CFLAGS="-m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse"

cd ../user

i686-elf-as crt0.s -o crt0.o

i686-elf-gcc -c init/init.c -o init/init.o
i686-elf-gcc -c syscall.c -o syscall.o
i686-elf-gcc -c shell.c -o shell.o
i686-elf-gcc -c games/sponk/sponk.c -o games/sponk/sponk.o

i686-elf-gcc -Ttext=0x400100 -nostdlib crt0.o init/init.o syscall.o -Wl,--start-group -lc -lm -Wl,--end-group -o init.elf
i686-elf-gcc -Ttext=0x400100 -nostdlib crt0.o shell.o syscall.o -Wl,--start-group -lc -lm -Wl,--end-group -o sh.elf
i686-elf-gcc -Ttext=0x400100 -nostdlib crt0.o games/sponk/sponk.o syscall.o -Wl,--start-group -lc -lm -Wl,--end-group -o sponk.elf
