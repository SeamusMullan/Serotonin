export DIR=$(pwd)
export PREFIX="$DIR/../build-tools/bin"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"
OS_TYPE="$(uname)"

# compiler flags
CFLAGS="-m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse"

cd ../user

i686-elf-as crt0.s -o crt0.o

i686-elf-gcc -c init/init.c -o init/init.o $CFLAGS
i686-elf-gcc -c init/test.c -o init/test.o $CFLAGS

i686-elf-ld -Ttext=0x400100 -o init/test init/test.o crt0.o
i686-elf-ld -Ttext=0x400100 -o init/init init/init.o crt0.o
