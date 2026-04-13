#!/bin/bash
# Rebuild just the OS-specific sysroot components (crt0, libsyscall, libcxxrt, headers).
# Use this after modifying syscall stubs, C++ runtime, or OS headers.
# For a full toolchain rebuild, use build-tools/build-toolchain.sh instead.

set -e

export DIR=$(pwd)
export PREFIX="$DIR/../build-tools/cross"
export TARGET=i686-serotonin
export PATH="$PREFIX/bin:$PATH"

SYSROOT="$PREFIX/$TARGET/sys-root"
USER_DIR="$DIR/../user"

CFLAGS="-m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse"
CXXFLAGS="-m32 -std=c++11 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse -fno-exceptions -fno-rtti -fno-threadsafe-statics"

echo "=== Rebuilding sysroot ==="

# --- Headers ---

mkdir -p "$SYSROOT/usr/include/serotonin"

echo "Installing Serotonin headers..."
cp "$USER_DIR/syscall/syscall_table.h" "$SYSROOT/usr/include/serotonin/"
cp "$USER_DIR/syscall/lib5ht/lib5ht.h" "$SYSROOT/usr/include/"

# --- CRT ---

echo "Building crt0.o..."
$TARGET-as "$USER_DIR/crt0.s" -o "$SYSROOT/usr/lib/crt0.o"

# --- libsyscall.a ---

echo "Building libsyscall.a..."
TMPDIR=$(mktemp -d)
$TARGET-gcc -c "$USER_DIR/syscall/syscall.c" -o "$TMPDIR/syscall.o" $CFLAGS \
    -I"$USER_DIR/syscall" -I"$SYSROOT/usr/include"
$TARGET-gcc -c "$USER_DIR/syscall/lib5ht/lib5ht.c" -o "$TMPDIR/lib5ht.o" $CFLAGS \
    -I"$USER_DIR/syscall" -I"$SYSROOT/usr/include"
$TARGET-ar rcs "$SYSROOT/usr/lib/libsyscall.a" "$TMPDIR/syscall.o" "$TMPDIR/lib5ht.o"
rm -rf "$TMPDIR"

# --- libcxxrt.a ---

echo "Building libcxxrt.a..."
TMPDIR=$(mktemp -d)
$TARGET-gcc -c "$USER_DIR/cxx/cxx_init.c" -o "$TMPDIR/cxx_init.o" $CFLAGS
$TARGET-g++ -c "$USER_DIR/cxx/cxx_runtime.cpp" -o "$TMPDIR/cxx_runtime.o" $CXXFLAGS
$TARGET-g++ -c "$USER_DIR/cxx/cxx_new_delete.cpp" -o "$TMPDIR/cxx_new_delete.o" $CXXFLAGS
$TARGET-ar rcs "$SYSROOT/usr/lib/libcxxrt.a" "$TMPDIR/cxx_init.o" "$TMPDIR/cxx_runtime.o" "$TMPDIR/cxx_new_delete.o"
rm -rf "$TMPDIR"

# --- Linker script ---

echo "Installing linker script..."
cp "$USER_DIR/user.ld" "$SYSROOT/usr/lib/user.ld"

# --- Summary ---

echo ""
echo "=== Sysroot updated at $SYSROOT ==="
echo "Contents:"
echo "  usr/include/  - newlib + Serotonin headers"
echo "  usr/lib/       - libraries and CRT objects"
