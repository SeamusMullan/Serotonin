if [ ! -d "$newlib"]; then
    mkdir newlib
fi
cd newlib

export DIR=$(pwd)
export PREFIX="$DIR/../../build-tools/bin"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

ln -s $PREFIX/bin/i686-elf-gcc $PREFIX/bin/i686-elf-cc

../../newlib/configure \
    --target=i686-elf \
    --prefix="$PREFIX" \
    --disable-newlib-supplied-syscalls \
    --disable-libgloss \
    --enable-newlib-reent-small \
    --disable-nls
make -j$(nproc)
make install