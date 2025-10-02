#!/bin/bash
export DIR=$(pwd)
export PREFIX="$DIR/bin"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"

echo "=== Cleaning up previous build attempts ==="

# Clean up binutils source directory
echo "Cleaning binutils-gdb..."
cd src/binutils-gdb
# Try make distclean first
make distclean 2>/dev/null || true
# Remove all generated files
find . -name "*.cache" -delete 2>/dev/null || true
find . -name "config.status" -delete 2>/dev/null || true
find . -name "config.log" -delete 2>/dev/null || true
find . -name "Makefile" -not -path "*/testsuite/*" -delete 2>/dev/null || true
rm -rf host-* build-* 2>/dev/null || true
# Clean up subdirectories that cause issues
for dir in zlib libiberty libsframe binutils gas libbacktrace libdecnumber etc gprof readline gnulib; do
    if [ -d "$dir" ]; then
        cd "$dir"
        rm -f config.cache config.status config.log Makefile 2>/dev/null || true
        cd ..
    fi
done
cd ..

# Clean up GCC source directory
echo "Cleaning gcc..."
cd gcc
# Try make distclean first
make distclean 2>/dev/null || true
# Remove the problematic directory
rm -rf host-x86_64-pc-linux-gnu 2>/dev/null || true
# Remove all generated files
find . -name "*.cache" -delete 2>/dev/null || true
find . -name "config.status" -delete 2>/dev/null || true
find . -name "config.log" -delete 2>/dev/null || true
rm -rf build-* 2>/dev/null || true

# Download prerequisites while we're in the source directory
echo "Downloading GCC prerequisites..."
./contrib/download_prerequisites
cd ..

# Remove old build directories
echo "Removing old build directories..."
rm -rf build-binutils build-gcc

cd .. # Back to main directory

echo "=== Starting fresh build ==="

# Build binutils in a separate directory inside src/
mkdir -p src/build-binutils
cd src/build-binutils
echo "Configuring binutils..."
../binutils-gdb/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror
echo "Building binutils..."
make -j$(nproc)
echo "Installing binutils..."
make install
cd ../..

# Verify binutils installation
echo "Checking binutils installation..."
which ${TARGET}-as || { echo "Error: ${TARGET}-as not found in PATH"; exit 1; }

# Build GCC in a separate directory inside src/
mkdir -p src/build-gcc
cd src/build-gcc
echo "Configuring GCC..."
../gcc/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers --disable-hosted-libstdcxx
echo "Building GCC..."
make all-gcc -j$(nproc)
echo "Installing GCC..."
make install-gcc
cd ../..

echo "=== Build complete! ==="
echo "Cross-compiler installed to: $PREFIX"