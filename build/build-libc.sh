if [ ! -d "$newlib"]; then
    mkdir newlib
fi
cd newlib

export DIR=$(pwd)
export PREFIX="$DIR/../../build-tools/bin"
export SYSROOT="$DIR/../../sysroot"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

ln -sf $PREFIX/bin/i686-elf-gcc $PREFIX/bin/i686-elf-cc

../../newlib/configure \
    --target=i686-elf \
    --prefix="$PREFIX" \
    --disable-newlib-supplied-syscalls \
    --disable-libgloss \
    --enable-newlib-reent-small \
    --disable-nls
make -j$(nproc)
make install

# Also install into sysroot if it exists
if [ -d "$SYSROOT" ]; then
    echo "Installing newlib into sysroot..."
    mkdir -p "$SYSROOT/usr/include" "$SYSROOT/usr/lib"
    cp -r "$PREFIX/$TARGET/include/"* "$SYSROOT/usr/include/"
    cp "$PREFIX/$TARGET/lib/libc.a" "$SYSROOT/usr/lib/"
    cp "$PREFIX/$TARGET/lib/libm.a" "$SYSROOT/usr/lib/"
    cp "$PREFIX/$TARGET/lib/libg.a" "$SYSROOT/usr/lib/" 2>/dev/null || true
    if [ -d "$PREFIX/$TARGET/lib/ldscripts" ]; then
        cp -r "$PREFIX/$TARGET/lib/ldscripts" "$SYSROOT/usr/lib/"
    fi
fi
