#!/bin/bash
set -e

export DIR=$(pwd)
export PREFIX="$DIR/../build-tools/bin"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

SYSROOT="$DIR/../sysroot"
USER_DIR="$DIR/../user"

CFLAGS="-m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse"
CXXFLAGS="-m32 -std=c++11 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse -fno-exceptions -fno-rtti -fno-threadsafe-statics"

echo "=== Building sysroot ==="

# Clean previous sysroot
rm -rf "$SYSROOT"
mkdir -p "$SYSROOT/usr/include/serotonin"
mkdir -p "$SYSROOT/usr/lib"

# --- Headers ---

# Copy newlib headers
echo "Installing newlib headers..."
cp -r "$PREFIX/$TARGET/include/"* "$SYSROOT/usr/include/"

# Copy OS-specific headers
echo "Installing Serotonin headers..."
cp "$USER_DIR/syscall/syscall_table.h" "$SYSROOT/usr/include/serotonin/"
cp "$USER_DIR/syscall/lib5ht/lib5ht.h" "$SYSROOT/usr/include/serotonin/"

# --- Libraries ---

# Copy newlib libraries
echo "Installing newlib libraries..."
cp "$PREFIX/$TARGET/lib/libc.a" "$SYSROOT/usr/lib/"
cp "$PREFIX/$TARGET/lib/libm.a" "$SYSROOT/usr/lib/"
cp "$PREFIX/$TARGET/lib/libg.a" "$SYSROOT/usr/lib/" 2>/dev/null || true
if [ -d "$PREFIX/$TARGET/lib/ldscripts" ]; then
    cp -r "$PREFIX/$TARGET/lib/ldscripts" "$SYSROOT/usr/lib/"
fi

# Copy C++ standard libraries if they exist
cp "$PREFIX/$TARGET/lib/libstdc++.a" "$SYSROOT/usr/lib/" 2>/dev/null || true
cp "$PREFIX/$TARGET/lib/libsupc++.a" "$SYSROOT/usr/lib/" 2>/dev/null || true

# --- CRT ---

# Build and install crt0.o
echo "Building crt0.o..."
i686-elf-as "$USER_DIR/crt0.s" -o "$SYSROOT/usr/lib/crt0.o"

# --- libsyscall.a (syscall stubs + lib5ht) ---

echo "Building libsyscall.a..."
TMPDIR=$(mktemp -d)
i686-elf-gcc -c "$USER_DIR/syscall/syscall.c" -o "$TMPDIR/syscall.o" $CFLAGS \
    -I"$USER_DIR/syscall" -I"$SYSROOT/usr/include"
i686-elf-gcc -c "$USER_DIR/syscall/lib5ht/lib5ht.c" -o "$TMPDIR/lib5ht.o" $CFLAGS \
    -I"$USER_DIR/syscall" -I"$SYSROOT/usr/include"
i686-elf-ar rcs "$SYSROOT/usr/lib/libsyscall.a" "$TMPDIR/syscall.o" "$TMPDIR/lib5ht.o"
rm -rf "$TMPDIR"

# --- libcxxrt.a (C++ runtime) ---

echo "Building libcxxrt.a..."
TMPDIR=$(mktemp -d)
i686-elf-gcc -c "$USER_DIR/cxx/cxx_init.c" -o "$TMPDIR/cxx_init.o" $CFLAGS
i686-elf-g++ -c "$USER_DIR/cxx/cxx_runtime.cpp" -o "$TMPDIR/cxx_runtime.o" $CXXFLAGS
i686-elf-g++ -c "$USER_DIR/cxx/cxx_new_delete.cpp" -o "$TMPDIR/cxx_new_delete.o" $CXXFLAGS
i686-elf-ar rcs "$SYSROOT/usr/lib/libcxxrt.a" "$TMPDIR/cxx_init.o" "$TMPDIR/cxx_runtime.o" "$TMPDIR/cxx_new_delete.o"
rm -rf "$TMPDIR"

# --- Linker script ---

echo "Installing linker script..."
cp "$USER_DIR/user.ld" "$SYSROOT/usr/lib/user.ld"

# --- Summary ---

echo ""
echo "=== Sysroot built at $SYSROOT ==="
echo "Contents:"
echo "  usr/include/  - newlib + Serotonin headers"
echo "  usr/lib/       - libraries and CRT objects"
echo ""
echo "Usage: i686-elf-gcc --sysroot=$SYSROOT ..."
