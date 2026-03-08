export DIR=$(pwd)
export PREFIX="$DIR/../build-tools/bin"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"
OS_TYPE="$(uname)"

# sysroot
SYSROOT="$DIR/../sysroot"

if [ ! -d "$SYSROOT/usr/lib" ]; then
    echo "Error: sysroot not found at $SYSROOT"
    echo "Run build-sysroot.sh first."
    exit 1
fi

# compiler flags
CFLAGS="-m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse --sysroot=$SYSROOT"
CXXFLAGS="-m32 -std=c++11 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse -fno-exceptions -fno-rtti -fno-threadsafe-statics --sysroot=$SYSROOT"

# linker flags: use sysroot CRT and libraries
CRT0="$SYSROOT/usr/lib/crt0.o"
LDFLAGS="-T $SYSROOT/usr/lib/user.ld -nostdlib -L$SYSROOT/usr/lib"
LDLIBS="-Wl,--start-group -lsyscall -lcxxrt -lc -lm -Wl,--end-group"

# STLport configuration
STLPORT_DIR="$DIR/../user/cxx/STLport-5.2.1/stlport"
STLPORT_FLAGS="-I$STLPORT_DIR -D__SEROTONIN__"

cd ../user

# --- C programs ---

i686-elf-gcc -c init/init.c -o init/init.o $CFLAGS
i686-elf-gcc -c login/login.c -o login/login.o $CFLAGS
i686-elf-gcc -c test.c -o test.o $CFLAGS
i686-elf-gcc -c shell.c -o shell.o $CFLAGS
i686-elf-gcc -c ls.c -o ls.o $CFLAGS
i686-elf-gcc -c cat.c -o cat.o $CFLAGS
i686-elf-gcc -c pipe_test.c -o pipe_test.o $CFLAGS
i686-elf-gcc -c fb_layer_test.c -o fb_layer_test.o $CFLAGS
i686-elf-gcc -c receiver.c -o receiver.o $CFLAGS
i686-elf-gcc -c sender.c -o sender.o $CFLAGS
i686-elf-gcc -c fs_syscall_test.c -o fs_syscall_test.o $CFLAGS
i686-elf-gcc -c devfs_example.c -o devfs_example.o $CFLAGS
i686-elf-gcc -c mouse_test.c -o mouse_test.o $CFLAGS
i686-elf-gcc -c kb_test.c -o kb_test.o $CFLAGS
i686-elf-gcc -c mouse_cursor.c -o mouse_cursor.o $CFLAGS
i686-elf-gcc -c listproc/listproc.c -o listproc/listproc.o $CFLAGS
i686-elf-gcc -c games/sponk/sponk.c -o games/sponk/sponk.o $CFLAGS
i686-elf-gcc -c opl2_editor/opl2_editor.c -o opl2_editor/opl2_editor.o $CFLAGS

# --- Link C programs ---

i686-elf-gcc $LDFLAGS $CRT0 init/init.o $LDLIBS -o init.elf
i686-elf-gcc $LDFLAGS $CRT0 login/login.o $LDLIBS -o login.elf
i686-elf-gcc $LDFLAGS $CRT0 test.o $LDLIBS -o test.elf
i686-elf-gcc $LDFLAGS $CRT0 listproc/listproc.o $LDLIBS -o listproc.elf
i686-elf-gcc $LDFLAGS $CRT0 shell.o $LDLIBS -o sh.elf
i686-elf-gcc $LDFLAGS $CRT0 ls.o $LDLIBS -o ls.elf
i686-elf-gcc $LDFLAGS $CRT0 cat.o $LDLIBS -o cat.elf
i686-elf-gcc $LDFLAGS $CRT0 pipe_test.o $LDLIBS -o pipe_test.elf
i686-elf-gcc $LDFLAGS $CRT0 fb_layer_test.o $LDLIBS -o fb_layer_test.elf
i686-elf-gcc $LDFLAGS $CRT0 fs_syscall_test.o $LDLIBS -o fs_syscall_test.elf
i686-elf-gcc $LDFLAGS $CRT0 games/sponk/sponk.o $LDLIBS -o sponk.elf
i686-elf-gcc $LDFLAGS $CRT0 receiver.o $LDLIBS -o receiver.elf
i686-elf-gcc $LDFLAGS $CRT0 sender.o $LDLIBS -o sender.elf
i686-elf-gcc $LDFLAGS $CRT0 devfs_example.o $LDLIBS -o devfs_example.elf
i686-elf-gcc $LDFLAGS $CRT0 mouse_test.o $LDLIBS -o ps2tst.elf
i686-elf-gcc $LDFLAGS $CRT0 mouse_cursor.o $LDLIBS -o mouse.elf
i686-elf-gcc $LDFLAGS $CRT0 kb_test.o $LDLIBS -o kbtest.elf
i686-elf-gcc $LDFLAGS $CRT0 opl2_editor/opl2_editor.o $LDLIBS -o opl2edit.elf

# --- C++ programs ---

i686-elf-g++ -c cxx_test.cpp -o cxx_test.o $CXXFLAGS
i686-elf-g++ $LDFLAGS $CRT0 cxx_test.o $LDLIBS -o cxxtest.elf

# Build STLport stubs (provides range error functions for bare-metal)
echo "Building STLport stubs..."
i686-elf-g++ -c cxx/stlport_stubs.cpp -o cxx/stlport_stubs.o $CXXFLAGS $STLPORT_FLAGS

# STLport library location
STLPORT_LIB="$DIR/../user/cxx/STLport-5.2.1/build-output"

# Build STL test program (uses STLport containers)
echo "Building STL test..."
i686-elf-g++ -c stl_test.cpp -o stl_test.o $CXXFLAGS $STLPORT_FLAGS
i686-elf-g++ $LDFLAGS $CRT0 cxx/stlport_stubs.o stl_test.o $LDLIBS -o stltest.elf
echo "STL test build complete: stltest.elf"

# Build iostream test program (uses STLport with full iostream)
# Note: don't link stlport_stubs.o when using libstlport.a (it provides those functions)
echo "Building iostream test..."
i686-elf-g++ -c iostream_test.cpp -o iostream_test.o $CXXFLAGS $STLPORT_FLAGS
i686-elf-g++ $LDFLAGS $CRT0 iostream_test.o -L$STLPORT_LIB -lstlport $LDLIBS -o iostr.elf
echo "iostream test build complete: iostreamtest.elf"

# Build enhanced C++ shell (shell+)
echo "Building shell+..."
i686-elf-g++ -c shell_plus.cpp -o shell_plus.o $CXXFLAGS $STLPORT_FLAGS
i686-elf-g++ $LDFLAGS $CRT0 shell_plus.o -L$STLPORT_LIB -lstlport $LDLIBS -o shplus.elf
echo "shell+ build complete: shplus.elf"

# Build Lua
echo "Building Lua..."
cd lua/lua-5.4.8/src

# Clean previous build artifacts
make clean

# Build Lua library (object files only) with 32-bit integers
make CC="i686-elf-gcc" \
     AR="i686-elf-ar rcu" \
     RANLIB="i686-elf-ranlib" \
     MYCFLAGS="$CFLAGS" \
     a \
     -j $(nproc)

# Build Lua interpreter object file
i686-elf-gcc -m32 -ffreestanding -O2 -Wall -Wextra --sysroot=$SYSROOT -c lua.c -o lua.o

# Link Lua interpreter as ELF binary for Serotonin OS
cd ../../..

i686-elf-gcc -c lua_stubs.c -o lua_stubs.o $CFLAGS
i686-elf-gcc -c libgcc_stubs.c -o libgcc_stubs.o $CFLAGS
i686-elf-gcc -c binutils/posix_stubs.c -o binutils/posix_stubs.o $CFLAGS
i686-elf-gcc -I"$DIR/../build-tools/src/binutils-gdb/include" -c binutils/sframe_stubs.c -o binutils/sframe_stubs.o $CFLAGS

# Link without -lgcc
i686-elf-gcc $LDFLAGS $CRT0 lua/lua-5.4.8/src/lua.o lua/lua-5.4.8/src/liblua.a lua_stubs.o libgcc_stubs.o $LDLIBS -o lua.elf

echo "Lua build complete: lua.elf"

# Build binutils for userland
echo "Building binutils..."
BINUTILS_SRC="$DIR/../build-tools/src/binutils-gdb"
BINUTILS_BUILD="$DIR/binutils-user-build"
BINUTILS_STAGE="$DIR/binutils-user-stage"
BINUTILS_CFLAGS="$CFLAGS -Wno-error=incompatible-pointer-types -Wno-incompatible-pointer-types -mno-tls-direct-seg-refs -DBFD_NO_THREADS"
BINUTILS_CC_WRAPPER="$BINUTILS_BUILD/cc-wrapper.sh"

mkdir -p "$BINUTILS_BUILD" "$BINUTILS_STAGE"
cd "$BINUTILS_BUILD"

cat > "$BINUTILS_CC_WRAPPER" <<EOF
#!/usr/bin/env sh
set -e

user_objs="$SYSROOT/usr/lib/crt0.o"
user_ldflags="-nostartfiles -T $SYSROOT/usr/lib/user.ld --sysroot=$SYSROOT -L$SYSROOT/usr/lib"
user_libs="-Wl,--start-group -lsyscall -lcxxrt -lc -lm -Wl,--end-group"

for arg in "\$@"; do
    if [ "\$arg" = "-c" ]; then
        exec i686-elf-gcc "\$@"
    fi
done

out=""
prev=""
for arg in "\$@"; do
    if [ "\$prev" = "-o" ]; then
        out="\$arg"
        break
    fi
    prev="\$arg"
done

case "\$out" in
    *.a|*.la|*.so|*.o)
        exec i686-elf-gcc "\$@"
        ;;
esac

exec i686-elf-gcc "\$@" \$user_ldflags \$user_objs \$user_libs
EOF
chmod +x "$BINUTILS_CC_WRAPPER"

if [ ! -f "config.status" ]; then
    echo "Configuring binutils..."
    CC="$BINUTILS_CC_WRAPPER"     AR="i686-elf-ar"     RANLIB="i686-elf-ranlib"     CFLAGS="$BINUTILS_CFLAGS"     "$BINUTILS_SRC/configure"         --host="$TARGET"         --target="$TARGET"         --prefix=/usr         --program-prefix=         --disable-nls         --disable-werror         --disable-gdb         --disable-gdbserver         --disable-gprofng         --disable-gold         --disable-libsframe         --disable-libbacktrace         --disable-libctf         --disable-readline         --disable-gprof         --disable-sim
fi

if [ ! -d "opcodes" ]; then
    echo "Configuring opcodes..."
    mkdir -p opcodes
    (cd opcodes && \
        CC="$BINUTILS_CC_WRAPPER" \
        AR="i686-elf-ar" \
        RANLIB="i686-elf-ranlib" \
        CFLAGS="$BINUTILS_CFLAGS" \
        "$BINUTILS_SRC/opcodes/configure" \
            --host="$TARGET" \
            --target="$TARGET" \
            --prefix=/usr \
            --disable-nls \
            --disable-werror \
    )
fi


echo "Building binutils utilities..."
make -j$(nproc)

echo "Building libiberty..."
make -C "$BINUTILS_BUILD/libiberty" all
make -C "$BINUTILS_BUILD/libiberty" libiberty.a || true
if [ -f "$BINUTILS_BUILD/libiberty/required-list" ]; then
    (cd "$BINUTILS_BUILD/libiberty" && \
        i686-elf-ar cru libiberty.a $(cat required-list) && \
        i686-elf-ranlib libiberty.a)
fi
if [ ! -f "$BINUTILS_BUILD/libiberty/libiberty.a" ] && [ -f "$BINUTILS_BUILD/libiberty/.libs/libiberty.a" ]; then
    cp "$BINUTILS_BUILD/libiberty/.libs/libiberty.a" "$BINUTILS_BUILD/libiberty/libiberty.a"
fi
if [ ! -f "$BINUTILS_BUILD/libiberty/libiberty.a" ]; then
    echo "libiberty.a missing after build"
    exit 1
fi

echo "Installing binutils utilities to staging..."
make DESTDIR="$BINUTILS_STAGE" install

echo "Copying binutils utilities to userland..."
BINUTILS_STAGE_BIN="$BINUTILS_STAGE/usr/bin"
for bin in "$BINUTILS_STAGE_BIN"/*; do
    if [ -f "$bin" ]; then
        bin_name="$(basename "$bin")"
        cp "$bin" "$DIR/../user/binutils/${bin_name}.elf"
    fi
done

cd "$DIR/../user"

echo ""
echo "=== Build complete ==="
echo "STLport containers are available (header-only mode)"
