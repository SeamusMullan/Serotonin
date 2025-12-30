#!/bin/bash
# STLport build script for Serotonin OS
# Builds a static library (libstlport.a) for i686-elf bare-metal target

set -e

# Find the Serotonin root directory (should be 3 levels up from STLport-5.2.1)
STLPORT_DIR="$(cd "$(dirname "$0")" && pwd)"
SEROTONIN_ROOT="$(cd "$STLPORT_DIR/../../.." && pwd)"

# Add cross-compiler to PATH
export PATH="$SEROTONIN_ROOT/build-tools/bin/bin:$PATH"
SRC_DIR="$STLPORT_DIR/src"
STLPORT_INCLUDE="$STLPORT_DIR/stlport"

# Output directory
BUILD_DIR="$STLPORT_DIR/build-output"
OBJ_DIR="$BUILD_DIR/obj"

# Cross-compiler settings (will be overridden by environment if set)
CXX="${CXX:-i686-elf-g++}"
CC="${CC:-i686-elf-gcc}"
AR="${AR:-i686-elf-ar}"
RANLIB="${RANLIB:-i686-elf-ranlib}"

# Compiler flags - need to include src directory for internal headers
COMMON_FLAGS="-m32 -ffreestanding -O2 -Wall -D__SEROTONIN__ -I$STLPORT_INCLUDE -I$SRC_DIR"
CXXFLAGS="${CXXFLAGS:--std=c++11 -fno-exceptions -fno-rtti -fno-threadsafe-statics}"
CFLAGS="${CFLAGS:--std=gnu99}"

# C++ source files (from Makefile.inc)
CXX_SOURCES="
    dll_main.cpp
    fstream.cpp
    strstream.cpp
    sstream.cpp
    ios.cpp
    stdio_streambuf.cpp
    istream.cpp
    ostream.cpp
    iostream.cpp
    codecvt.cpp
    collate.cpp
    ctype.cpp
    monetary.cpp
    num_get.cpp
    num_put.cpp
    num_get_float.cpp
    num_put_float.cpp
    numpunct.cpp
    time_facets.cpp
    messages.cpp
    locale.cpp
    locale_impl.cpp
    locale_catalog.cpp
    facets_byname.cpp
    complex.cpp
    complex_io.cpp
    complex_trig.cpp
    string.cpp
    bitset.cpp
    allocators.cpp
"

# C source files - use dummy locale for bare-metal
C_SOURCES="
    c_locale.c
    cxa.c
"

# Create build directories
mkdir -p "$OBJ_DIR"

echo "Building STLport for Serotonin OS..."
echo "  CXX: $CXX"
echo "  CC: $CC"
echo "  CXXFLAGS: $COMMON_FLAGS $CXXFLAGS"
echo ""

# Track failed compilations
FAILED=""

# Compile C++ sources
OBJ_FILES=""
for src in $CXX_SOURCES; do
    obj="$OBJ_DIR/${src%.cpp}.o"
    echo "Compiling $src..."
    if $CXX $COMMON_FLAGS $CXXFLAGS -c "$SRC_DIR/$src" -o "$obj" 2>&1; then
        OBJ_FILES="$OBJ_FILES $obj"
    else
        echo "  FAILED: $src"
        FAILED="$FAILED $src"
    fi
done

# Compile C sources
for src in $C_SOURCES; do
    obj="$OBJ_DIR/${src%.c}.o"
    echo "Compiling $src..."
    if $CC $COMMON_FLAGS $CFLAGS -c "$SRC_DIR/$src" -o "$obj" 2>&1; then
        OBJ_FILES="$OBJ_FILES $obj"
    else
        echo "  FAILED: $src"
        FAILED="$FAILED $src"
    fi
done

if [ -n "$FAILED" ]; then
    echo ""
    echo "WARNING: Some files failed to compile:$FAILED"
    echo ""
fi

# Create static library
echo ""
echo "Creating libstlport.a..."
$AR rcu "$BUILD_DIR/libstlport.a" $OBJ_FILES
$RANLIB "$BUILD_DIR/libstlport.a"

echo ""
echo "Build complete!"
echo "  Library: $BUILD_DIR/libstlport.a"
echo "  Headers: $STLPORT_INCLUDE"
echo ""
echo "To use STLport in your programs:"
echo "  CXXFLAGS: -I$STLPORT_INCLUDE -D__SEROTONIN__"
echo "  LDFLAGS:  -L$BUILD_DIR -lstlport"
