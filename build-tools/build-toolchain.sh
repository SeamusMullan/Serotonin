#!/bin/bash
# build-toolchain.sh — Build the i686-serotonin cross-toolchain.
#
# Stages:
#   1. Patch source trees (binutils, GCC, newlib)
#   2. Build & install binutils
#   3. Build & install GCC stage-1 (C only, no libc)
#   4. Build & install newlib
#   5. Build & install libgcc (full, with newlib headers)
#   6. Install CRT, linker script, and OS libraries into the sysroot
#
# After running this script:
#   i686-serotonin-gcc test.c -o test     (userspace, auto-linked)
#   i686-serotonin-gcc -ffreestanding -nostdlib -c kernel.c  (kernel)

set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$DIR/.." && pwd)"

export TARGET=i686-serotonin
export PREFIX="$DIR/cross"
export SYSROOT="$PREFIX/$TARGET/sys-root"
export PATH="$PREFIX/bin:$PATH"

NEWLIB_SRC="$ROOT/newlib"
USER_DIR="$ROOT/user"

NPROC=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
OS_TYPE="$(uname)"

# macOS: point at Homebrew GMP/MPFR if available
EXTRA_CONFIGURE_FLAGS=""
if [ "$OS_TYPE" = "Darwin" ]; then
    if [ -d "/opt/homebrew/opt/gmp" ]; then
        EXTRA_CONFIGURE_FLAGS="--with-gmp=/opt/homebrew/opt/gmp --with-mpfr=/opt/homebrew/opt/mpfr"
    fi
fi

echo "============================================="
echo " Serotonin Cross-Toolchain Builder"
echo "============================================="
echo " Target:  $TARGET"
echo " Prefix:  $PREFIX"
echo " Sysroot: $SYSROOT"
echo " Jobs:    $NPROC"
echo "============================================="
echo ""

# ── Stage 0: Patch sources ─────────────────────────────
echo "=== Stage 0: Patching source trees ==="
bash "$DIR/patch-sources.sh"
echo ""

# ── Stage 1: Binutils ──────────────────────────────────
echo "=== Stage 1: Building binutils ==="

# Restore any damaged autotools-generated files in binutils
(cd "$DIR/src/binutils-gdb" && git checkout -- '*.in' 2>/dev/null || true)

rm -rf "$DIR/src/build-binutils-serotonin"
mkdir -p "$DIR/src/build-binutils-serotonin"
cd "$DIR/src/build-binutils-serotonin"

../binutils-gdb/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --disable-nls \
    --disable-werror \
    --disable-gdb \
    --disable-gdbserver \
    $EXTRA_CONFIGURE_FLAGS

make -j"$NPROC"
make install

echo ""

# Verify binutils
if ! command -v "$TARGET-as" >/dev/null 2>&1; then
    echo "ERROR: $TARGET-as not found after installation"
    exit 1
fi
echo "binutils installed: $(command -v $TARGET-ld)"

# ── Stage 2: GCC stage-1 (C only, freestanding) ───────
echo ""
echo "=== Stage 2: Building GCC stage-1 (C only) ==="

# Need sysroot dirs to exist for configure
mkdir -p "$SYSROOT/usr/include"
mkdir -p "$SYSROOT/usr/lib"

# Download GCC prerequisites if not already present
cd "$DIR/src/gcc"
if [ ! -d "gmp" ] && [ ! -d "mpfr" ]; then
    echo "Downloading GCC prerequisites..."
    ./contrib/download_prerequisites
fi
cd "$DIR"

rm -rf "$DIR/src/build-gcc-serotonin"
mkdir -p "$DIR/src/build-gcc-serotonin"
cd "$DIR/src/build-gcc-serotonin"

../gcc/configure \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --enable-languages=c,c++ \
    --with-newlib \
    --disable-nls \
    --disable-shared \
    --disable-threads \
    --disable-libssp \
    --disable-libquadmath \
    --disable-libgomp \
    --disable-libatomic \
    $EXTRA_CONFIGURE_FLAGS

# Build just the compiler (no runtime libraries yet)
make all-gcc -j"$NPROC"
make install-gcc

# Build and install libgcc (needed by newlib)
make all-target-libgcc -j"$NPROC"
make install-target-libgcc

echo ""
echo "GCC stage-1 installed: $(command -v $TARGET-gcc)"

# ── Stage 3: Install system headers into sysroot ──────
echo ""
echo "=== Stage 3: Installing headers into sysroot ==="

mkdir -p "$SYSROOT/usr/include/serotonin"

# Copy Serotonin-specific headers
cp "$USER_DIR/syscall/syscall_table.h" "$SYSROOT/usr/include/serotonin/"
cp "$USER_DIR/syscall/lib5ht/lib5ht.h" "$SYSROOT/usr/include/"

echo "System headers installed."

# ── Stage 4: Build newlib ──────────────────────────────
echo ""
echo "=== Stage 4: Building newlib ==="

# Ensure i686-serotonin-cc exists (newlib wants it)
ln -sf "$PREFIX/bin/$TARGET-gcc" "$PREFIX/bin/$TARGET-cc"

rm -rf "$DIR/src/build-newlib-serotonin"
mkdir -p "$DIR/src/build-newlib-serotonin"
cd "$DIR/src/build-newlib-serotonin"

export CFLAGS_FOR_TARGET="-O2 -msse -msse2 -g"

"$NEWLIB_SRC/configure" \
    --target="$TARGET" \
    --prefix="$PREFIX" \
    --with-sysroot="$SYSROOT" \
    --disable-newlib-supplied-syscalls \
    --disable-libgloss \
    --enable-newlib-reent-small \
    --enable-newlib-io-long-long \
    --disable-nls

make -j"$NPROC"
make install

# Install newlib into the sysroot
echo "Installing newlib into sysroot..."
cp -r "$PREFIX/$TARGET/include/"* "$SYSROOT/usr/include/"
cp "$PREFIX/$TARGET/lib/libc.a" "$SYSROOT/usr/lib/"
# Remove newlib's signal/raise from libc.a — libsyscall provides them via
# the kernel syscall interface and the linker would otherwise see duplicates.
"$PREFIX/bin/$TARGET-ar" d "$SYSROOT/usr/lib/libc.a" libc_a-signal.o 2>/dev/null || true
cp "$PREFIX/$TARGET/lib/libm.a" "$SYSROOT/usr/lib/"
cp "$PREFIX/$TARGET/lib/libg.a" "$SYSROOT/usr/lib/" 2>/dev/null || true

unset CFLAGS_FOR_TARGET

# Patch newlib headers to expose POSIX functions guarded behind other OS checks
for hdr in "$SYSROOT/usr/include/sys/stat.h" "$PREFIX/$TARGET/include/sys/stat.h"; do
    [ -f "$hdr" ] && sed -i 's/#if defined (__SPU__) || defined(__rtems__) || defined(__CYGWIN__)/#if defined (__SPU__) || defined(__rtems__) || defined(__CYGWIN__) || defined(__serotonin__)/' "$hdr"
done

# Install minimal dlfcn.h stub (Serotonin has no dynamic loading;
# binutils ld/plugin.c includes it and needs RTLD_NOW defined)
for dest in "$SYSROOT/usr/include/dlfcn.h" "$PREFIX/$TARGET/include/dlfcn.h"; do
    cat > "$dest" << 'DLFCN_EOF'
/* dlfcn.h — minimal stub for Serotonin (no dynamic loading support) */
#ifndef _DLFCN_H
#define _DLFCN_H
#define RTLD_NOW    0x2
#define RTLD_LAZY   0x1
#define RTLD_GLOBAL 0x100
#define RTLD_LOCAL  0x000
static inline void *dlopen(const char *f, int m)  { (void)f; (void)m; return 0; }
static inline void *dlsym(void *h, const char *s) { (void)h; (void)s; return 0; }
static inline int   dlclose(void *h)              { (void)h; return 0; }
static inline char *dlerror(void)                 { return "dlopen not supported"; }
#endif /* _DLFCN_H */
DLFCN_EOF
done

# Install Serotonin sys/dirent.h (newlib's default is a #error stub)
for dest in "$SYSROOT/usr/include/sys/dirent.h" "$PREFIX/$TARGET/include/sys/dirent.h"; do
    cat > "$dest" << 'DIRENT_EOF'
/* sys/dirent.h — Serotonin directory entry definitions. */
#ifndef _SYS_DIRENT_H_
#define _SYS_DIRENT_H_
#include <sys/types.h>
#define MAXNAMLEN 255
struct dirent {
    ino_t  d_ino;
    char   d_name[MAXNAMLEN + 1];
};
typedef struct {
    int    dd_fd;
    int    dd_loc;
    int    dd_size;
    char  *dd_buf;
} DIR;
#endif /* _SYS_DIRENT_H_ */
DIRENT_EOF
done

# Install networking stub headers (needed for building GCC Canadian cross)
for dest_base in "$SYSROOT/usr/include" "$PREFIX/$TARGET/include"; do
    mkdir -p "$dest_base/sys" "$dest_base/netinet" "$dest_base/arpa"
    cp "$DIR/serotonin-headers/sys/socket.h"  "$dest_base/sys/"  2>/dev/null || true
    cp "$DIR/serotonin-headers/sys/un.h"       "$dest_base/sys/"  2>/dev/null || true
    cp "$DIR/serotonin-headers/netinet/in.h"   "$dest_base/netinet/" 2>/dev/null || true
    cp "$DIR/serotonin-headers/arpa/inet.h"    "$dest_base/arpa/" 2>/dev/null || true
    cp "$DIR/serotonin-headers/netdb.h"        "$dest_base/"      2>/dev/null || true
done

echo "newlib installed."

# ── Stage 4b: Build libstdc++-v3 (libsupc++ for C++ exceptions) ─
echo ""
echo "=== Stage 4b: Building libstdc++-v3 (libsupc++ / exception support) ==="

cd "$DIR/src/build-gcc-serotonin"
make all-target-libstdc++-v3 -j"$NPROC"
make install-target-libstdc++-v3

# Copy the full hosted libstdc++/libsupc++ to the sysroot
for lib in libstdc++.a libsupc++.a; do
    if [ -f "$PREFIX/$TARGET/lib/$lib" ]; then
        cp "$PREFIX/$TARGET/lib/$lib" "$SYSROOT/usr/lib/"
    fi
done

echo "libstdc++-v3 installed."

# ── Stage 5: Build OS libraries (libsyscall, libcxxrt, crt0) ─
echo ""
echo "=== Stage 5: Building OS libraries ==="

CFLAGS="-m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse"
CXXFLAGS="-m32 -std=c++11 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse -fno-exceptions -fno-rtti -fno-threadsafe-statics"

# crt0.o
echo "  Building crt0.o..."
"$TARGET-as" "$USER_DIR/crt0.s" -o "$SYSROOT/usr/lib/crt0.o"

# libsyscall.a
echo "  Building libsyscall.a..."
TMPDIR=$(mktemp -d)
"$TARGET-gcc" -c "$USER_DIR/syscall/syscall.c" -o "$TMPDIR/syscall.o" $CFLAGS \
    -I"$USER_DIR/syscall" -I"$SYSROOT/usr/include"
"$TARGET-gcc" -c "$USER_DIR/syscall/lib5ht/lib5ht.c" -o "$TMPDIR/lib5ht.o" $CFLAGS \
    -I"$USER_DIR/syscall" -I"$SYSROOT/usr/include"
"$TARGET-ar" rcs "$SYSROOT/usr/lib/libsyscall.a" "$TMPDIR/syscall.o" "$TMPDIR/lib5ht.o"
rm -rf "$TMPDIR"

# libcxxrt.a
echo "  Building libcxxrt.a..."
TMPDIR=$(mktemp -d)
"$TARGET-gcc" -c "$USER_DIR/cxx/cxx_init.c" -o "$TMPDIR/cxx_init.o" $CFLAGS
"$TARGET-g++" -c "$USER_DIR/cxx/cxx_runtime.cpp" -o "$TMPDIR/cxx_runtime.o" $CXXFLAGS
"$TARGET-g++" -c "$USER_DIR/cxx/cxx_new_delete.cpp" -o "$TMPDIR/cxx_new_delete.o" $CXXFLAGS
"$TARGET-ar" rcs "$SYSROOT/usr/lib/libcxxrt.a" "$TMPDIR/cxx_init.o" "$TMPDIR/cxx_runtime.o" "$TMPDIR/cxx_new_delete.o"
rm -rf "$TMPDIR"

# Install linker script
echo "  Installing user.ld..."
cp "$USER_DIR/user.ld" "$SYSROOT/usr/lib/user.ld"

# Install C++ support libraries if available
for lib in libstdc++.a libsupc++.a; do
    if [ -f "$PREFIX/$TARGET/lib/$lib" ]; then
        cp "$PREFIX/$TARGET/lib/$lib" "$SYSROOT/usr/lib/"
    fi
done

echo "OS libraries installed."

# ── Summary ────────────────────────────────────────────
echo ""
echo "============================================="
echo " Toolchain build complete!"
echo "============================================="
echo ""
echo " Binaries: $PREFIX/bin/$TARGET-*"
echo " Sysroot:  $SYSROOT"
echo ""
echo " Userspace compilation:"
echo "   $TARGET-gcc test.c -o test"
echo ""
echo " Kernel compilation (freestanding):"
echo "   $TARGET-gcc -ffreestanding -nostdlib -c kernel.c"
echo "   $TARGET-gcc -ffreestanding -nostdlib -T linker.ld -o kernel.bin *.o -lgcc"
echo ""
echo " Add to PATH:"
echo "   export PATH=\"$PREFIX/bin:\$PATH\""
echo "============================================="
