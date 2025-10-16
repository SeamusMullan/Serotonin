#!/bin/bash
# build/build-tests.sh
# Build kernel with test support enabled

export DIR=$(pwd)
export PREFIX="$DIR/../build-tools/bin"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"
OS_TYPE="$(uname)"

echo "Building Serotonin Kernel with TEST MODE enabled..."

# Enable test mode
CFLAGS="-std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse -DKERNEL_TEST_MODE"

cd ../kernel

# Build test framework
echo "Building test framework..."
i686-elf-gcc -c test/ktest.c -o test/ktest.o $CFLAGS
i686-elf-gcc -c test/test_main.c -o test/test_main.o $CFLAGS

# Build individual test suites
echo "Building test suites..."
i686-elf-gcc -c test/test_string.c -o test/test_string.o $CFLAGS
i686-elf-gcc -c test/test_mem.c -o test/test_mem.o $CFLAGS
i686-elf-gcc -c test/test_stdlib.c -o test/test_stdlib.o $CFLAGS
i686-elf-gcc -c test/test_vfs.c -o test/test_vfs.o $CFLAGS
i686-elf-gcc -c test/test_paging.c -o test/test_paging.o $CFLAGS
i686-elf-gcc -c test/test_scheduler.c -o test/test_scheduler.o $CFLAGS
i686-elf-gcc -c test/test_io.c -o test/test_io.o $CFLAGS

echo "Test objects built successfully!"
echo ""
echo "Now building full kernel with test support..."
echo ""

cd ../build
export TEST_MODE=1
./build.sh
