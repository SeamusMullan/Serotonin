#!/bin/bash
# Rebuild just the OS-specific sysroot components (crt0, libsyscall, libcxxrt, headers).
# Use this after modifying syscall stubs, C++ runtime, or OS headers.
# For a full toolchain rebuild, use build-tools/build-toolchain.sh instead.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

export DIR="$SCRIPT_DIR"
export PREFIX="$ROOT_DIR/build-tools/cross"
export TARGET=i686-serotonin
export PATH="$PREFIX/bin:$PATH"

SYSROOT="$ROOT_DIR/sysroot"
USER_DIR="$ROOT_DIR/user"
SEROTONIN_HEADERS_DIR="$ROOT_DIR/build-tools/serotonin-headers"

CFLAGS="-m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse"
CXXFLAGS="-m32 -std=c++11 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse -fno-exceptions -fno-rtti -fno-threadsafe-statics"

echo "=== Rebuilding sysroot ==="

# --- Headers ---

mkdir -p "$SYSROOT/usr/include/serotonin" "$SYSROOT/usr/lib"

echo "Installing Serotonin headers..."
# Keep serotonin namespace headers in sync on every run.
rm -rf "$SYSROOT/usr/include/serotonin"
mkdir -p "$SYSROOT/usr/include/serotonin"
cp "$USER_DIR/syscall/syscall_table.h" "$SYSROOT/usr/include/serotonin/"
cp "$USER_DIR/syscall/lib5ht/lib5ht.h" "$SYSROOT/usr/include/serotonin/"
cp "$USER_DIR/syscall/lib5ht/lib5ht.h" "$SYSROOT/usr/include/"

# Keep auxiliary Serotonin networking stubs in sync as well.
if [ -d "$SEROTONIN_HEADERS_DIR" ]; then
    mkdir -p "$SYSROOT/usr/include/sys" "$SYSROOT/usr/include/netinet" "$SYSROOT/usr/include/arpa"
    cp "$SEROTONIN_HEADERS_DIR/sys/socket.h" "$SYSROOT/usr/include/sys/" 2>/dev/null || true
    cp "$SEROTONIN_HEADERS_DIR/sys/un.h" "$SYSROOT/usr/include/sys/" 2>/dev/null || true
    cp "$SEROTONIN_HEADERS_DIR/netinet/in.h" "$SYSROOT/usr/include/netinet/" 2>/dev/null || true
    cp "$SEROTONIN_HEADERS_DIR/arpa/inet.h" "$SYSROOT/usr/include/arpa/" 2>/dev/null || true
    cp "$SEROTONIN_HEADERS_DIR/netdb.h" "$SYSROOT/usr/include/" 2>/dev/null || true
fi

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
