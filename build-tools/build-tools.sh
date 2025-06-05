export DIR=$(pwd)
export PREFIX="$DIR/bin"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

cd src/

cd binutils-gdb
./configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
make -j$(nproc)
make install
cd ..

cd gcc
./contrib/download_prerequisites
./configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers --disable-hosted-libstdcxx
make all-gcc -j$(nproc)
make all-target-libgcc -j$(nproc)
make all-target-libstdc++-v3 -j$(nproc)