#!/bin/bash
# Rebuild newlib for i686-serotonin.
# Only needed if you want to rebuild newlib independently of the full toolchain.
# For a full build, use build-tools/build-toolchain.sh instead.

set -e

if [ -d "newlib" ]; then
    # Clean stale configure cache — target alias may have changed
    rm -f newlib/config.cache newlib/etc/config.cache
else
    mkdir -p newlib
fi
cd newlib

export DIR=$(pwd)
export PREFIX="$DIR/../../build-tools/cross"
export SYSROOT="$PREFIX/i686-serotonin/sys-root"
export TARGET=i686-serotonin
export PATH="$PREFIX/bin:$PATH"

ln -sf $PREFIX/bin/i686-serotonin-gcc $PREFIX/bin/i686-serotonin-cc

export CFLAGS_FOR_TARGET="-O2 -msse -msse2 -g"
../../newlib/configure \
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
