#!/bin/bash
# Rebuild newlib for i686-serotonin.
# Only needed if you want to rebuild newlib independently of the full toolchain.
# For a full build, use build-tools/build-toolchain.sh instead.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
NEWLIB_BUILD_DIR="$SCRIPT_DIR/newlib"
NEWLIB_SRC_DIR="$ROOT_DIR/newlib"

if [ ! -d "$NEWLIB_SRC_DIR" ]; then
    echo "Error: newlib source not found at $NEWLIB_SRC_DIR"
    exit 1
fi

mkdir -p "$NEWLIB_BUILD_DIR"
rm -f "$NEWLIB_BUILD_DIR/config.cache" "$NEWLIB_BUILD_DIR/etc/config.cache"
cd "$NEWLIB_BUILD_DIR"

export DIR="$NEWLIB_BUILD_DIR"
export PREFIX="$ROOT_DIR/build-tools/cross"
export SYSROOT="$ROOT_DIR/sysroot"
export TARGET=i686-serotonin
export PATH="$PREFIX/bin:$PATH"

ln -sf $PREFIX/bin/i686-serotonin-gcc $PREFIX/bin/i686-serotonin-cc

export CFLAGS_FOR_TARGET="-O2 -msse -msse2 -g"
"$NEWLIB_SRC_DIR/configure" \
    --target=i686-serotonin \
    --prefix="$PREFIX" \
    --disable-newlib-supplied-syscalls \
    --disable-libgloss \
    --enable-newlib-reent-small \
    --disable-nls
make -j$(nproc)
make install

# Also install into sysroot
if [ -d "$SYSROOT" ]; then
    echo "Installing newlib into sysroot..."
    mkdir -p "$SYSROOT/usr/include" "$SYSROOT/usr/lib"
    cp -r "$PREFIX/$TARGET/include/"* "$SYSROOT/usr/include/"
    cp "$PREFIX/$TARGET/lib/libc.a" "$SYSROOT/usr/lib/"
    # Remove newlib's signal/raise from libc.a — libsyscall provides them via
    # the kernel syscall interface and the linker would otherwise see duplicates.
    "$PREFIX/bin/$TARGET-ar" d "$SYSROOT/usr/lib/libc.a" libc_a-signal.o 2>/dev/null || true
    cp "$PREFIX/$TARGET/lib/libm.a" "$SYSROOT/usr/lib/"
    cp "$PREFIX/$TARGET/lib/libg.a" "$SYSROOT/usr/lib/" 2>/dev/null || true
    if [ -d "$PREFIX/$TARGET/lib/ldscripts" ]; then
        cp -r "$PREFIX/$TARGET/lib/ldscripts" "$SYSROOT/usr/lib/"
    fi
fi
