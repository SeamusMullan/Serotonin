SKIP_TOOLCHAIN=0
for arg in "$@"; do
    case "$arg" in
        --skip-toolchain) SKIP_TOOLCHAIN=1 ;;
    esac
done

export DIR=$(pwd)
export PREFIX="$DIR/../build-tools/cross"
export TARGET=i686-serotonin
export PATH="$PREFIX/bin:$PATH"
OS_TYPE="$(uname)"

CC="$PREFIX/bin/$TARGET-gcc"
CXX="$PREFIX/bin/$TARGET-g++"
AS="$PREFIX/bin/$TARGET-as"
AR="$PREFIX/bin/$TARGET-ar"
RANLIB="$PREFIX/bin/$TARGET-ranlib"
STRIP="$PREFIX/bin/$TARGET-strip"

SYSROOT="$PREFIX/$TARGET/sys-root"

# compiler flags — the toolchain handles sysroot, CRT, and linking automatically.
CFLAGS="-std=gnu99 -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse"

# C++ flags: no exceptions, no RTTI (not supported by Serotonin).
# Supported C++ features: classes, templates, operator overloading,
# new/delete (via libcxxrt), global constructors/destructors (via crt0),
# static destructors (__cxa_atexit), STL containers (via STLport).
CXXFLAGS="-std=c++11 -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse -fno-exceptions -fno-rtti -fno-threadsafe-statics"

# STLport configuration
STLPORT_DIR="$DIR/../user/cxx/STLport-5.2.1/stlport"
STLPORT_FLAGS="-I$STLPORT_DIR -D__SEROTONIN__"

cd ../user

# --- C programs (compile + link in one step — toolchain links automatically) ---

$CC init/init.c -o init.elf $CFLAGS
$CC login/login.c -o login.elf $CFLAGS
$CC test.c -o test.elf $CFLAGS
$CC shell.c -o sh.elf $CFLAGS
$CC ls.c -o ls.elf $CFLAGS
$CC cat.c -o cat.elf $CFLAGS

# --- Coreutils ---
$CC echo.c -o echo.elf $CFLAGS
$CC pwd.c -o pwd.elf $CFLAGS
$CC mkdir_cmd.c -o mkdir.elf $CFLAGS
$CC rm.c -o rm.elf $CFLAGS
$CC cp.c -o cp.elf $CFLAGS
$CC mv.c -o mv.elf $CFLAGS
$CC head.c -o head.elf $CFLAGS
$CC tail.c -o tail.elf $CFLAGS
$CC touch.c -o touch.elf $CFLAGS
$CC wc.c -o wc.elf $CFLAGS
$CC grep.c -o grep.elf $CFLAGS
$CC sort.c -o sort.elf $CFLAGS
$CC uniq.c -o uniq.elf $CFLAGS
$CC tr.c -o tr.elf $CFLAGS
$CC tee.c -o tee.elf $CFLAGS
$CC chmod_cmd.c -o chmod.elf $CFLAGS
$CC chown_cmd.c -o chown.elf $CFLAGS
$CC stat_cmd.c -o stat.elf $CFLAGS
$CC uname_cmd.c -o uname.elf $CFLAGS
$CC date.c -o date.elf $CFLAGS
$CC sleep_cmd.c -o sleep.elf $CFLAGS
$CC kill_cmd.c -o kill.elf $CFLAGS
$CC whoami.c -o whoami.elf $CFLAGS
$CC id.c -o id.elf $CFLAGS
$CC env.c -o env.elf $CFLAGS
$CC true.c -o true.elf $CFLAGS
$CC false.c -o false.elf $CFLAGS
$CC yes.c -o yes.elf $CFLAGS
$CC basename_cmd.c -o basename.elf $CFLAGS
$CC dirname_cmd.c -o dirname.elf $CFLAGS
$CC rmdir_cmd.c -o rmdir.elf $CFLAGS
$CC pipe_test.c -o pipe_test.elf $CFLAGS
$CC fb_layer_test.c -o fb_layer_test.elf $CFLAGS
$CC receiver.c -o receiver.elf $CFLAGS
$CC sender.c -o sender.elf $CFLAGS
$CC fs_syscall_test.c -o fs_syscall_test.elf $CFLAGS
$CC devfs_example.c -o devfs_example.elf $CFLAGS
$CC mouse_test.c -o ps2tst.elf $CFLAGS
$CC kb_test.c -o kbtest.elf $CFLAGS
$CC mouse_cursor.c -o mouse.elf $CFLAGS
$CC listproc/listproc.c -o listproc.elf $CFLAGS
$CC games/sponk/sponk.c -o sponk.elf $CFLAGS
$CC opl2_editor/opl2_editor.c -o opl2edit.elf $CFLAGS
$CC getty/getty.c -o getty.elf $CFLAGS
$CC fetch.c -o fetch.elf $CFLAGS
$CC nettest.c -o nettest.elf $CFLAGS
$CC socket_test.c -o socktest.elf $CFLAGS
$CC seriald.c -o seriald.elf $CFLAGS
$CC initctl.c -o initctl.elf $CFLAGS
$CC wavplay.c -o wavplay.elf $CFLAGS -Iinit

# --- Window Manager ---
echo "Building window manager..."
$CC wm/wm.c wm/wm_terminal.c wm/wm_draw.c wm/wm_layout.c wm/wm_input.c \
    -o wm.elf $CFLAGS
echo "Window manager build complete: wm.elf"

# lwip client library (compiled separately, used by network tools)
$CC -c lwip/serotonin/lwip_client.c -o lwip/serotonin/lwip_client.o $CFLAGS

# ifconfig, ping, httpd (need lwip client + lwip headers)
$CC ifconfig.c lwip/serotonin/lwip_client.o -o ifconfig.elf $CFLAGS -Ilwip/serotonin
$CC ping.c lwip/serotonin/lwip_client.o -o ping.elf $CFLAGS -Ilwip/serotonin
$CC httpd.c lwip/serotonin/lwip_client.o -o httpd.elf $CFLAGS -Ilwip/serotonin

# --- lwIP network daemon ---

echo "Building lwIP network daemon..."

LWIP_DIR="lwip/src"
LWIP_PORT="lwip/serotonin"
LWIP_INCLUDES="-I${LWIP_PORT} -I${LWIP_PORT}/arch -I${LWIP_DIR}/include"

# lwIP core sources (NO_SYS=1: no api/ files except err.c)
LWIP_SRCS="
    ${LWIP_DIR}/core/init.c
    ${LWIP_DIR}/core/def.c
    ${LWIP_DIR}/core/dns.c
    ${LWIP_DIR}/core/inet_chksum.c
    ${LWIP_DIR}/core/ip.c
    ${LWIP_DIR}/core/mem.c
    ${LWIP_DIR}/core/memp.c
    ${LWIP_DIR}/core/netif.c
    ${LWIP_DIR}/core/pbuf.c
    ${LWIP_DIR}/core/raw.c
    ${LWIP_DIR}/core/stats.c
    ${LWIP_DIR}/core/sys.c
    ${LWIP_DIR}/core/altcp.c
    ${LWIP_DIR}/core/altcp_alloc.c
    ${LWIP_DIR}/core/altcp_tcp.c
    ${LWIP_DIR}/core/tcp.c
    ${LWIP_DIR}/core/tcp_in.c
    ${LWIP_DIR}/core/tcp_out.c
    ${LWIP_DIR}/core/timeouts.c
    ${LWIP_DIR}/core/udp.c
    ${LWIP_DIR}/core/ipv4/autoip.c
    ${LWIP_DIR}/core/ipv4/dhcp.c
    ${LWIP_DIR}/core/ipv4/etharp.c
    ${LWIP_DIR}/core/ipv4/icmp.c
    ${LWIP_DIR}/core/ipv4/igmp.c
    ${LWIP_DIR}/core/ipv4/ip4_frag.c
    ${LWIP_DIR}/core/ipv4/ip4.c
    ${LWIP_DIR}/core/ipv4/ip4_addr.c
    ${LWIP_DIR}/netif/ethernet.c
    ${LWIP_DIR}/api/err.c
"

# Serotonin port sources
LWIP_PORT_SRCS="
    ${LWIP_PORT}/serotonin_netif.c
    ${LWIP_PORT}/lwip_daemon.c
"

# Compile all lwIP source files
LWIP_OBJS=""
for src in $LWIP_SRCS $LWIP_PORT_SRCS; do
    obj="${src%.c}.o"
    $CC -c "$src" -o "$obj" $CFLAGS $LWIP_INCLUDES -Wno-address
    LWIP_OBJS="$LWIP_OBJS $obj"
done

# Link lwIP daemon
$CC $LWIP_OBJS -o lwipd.elf $CFLAGS

echo "lwIP daemon build complete: lwipd.elf"

# --- C++ programs ---

$CXX cxx_test.cpp -o cxxtest.elf $CXXFLAGS

# Build STLport stubs (provides range error functions for bare-metal)
echo "Building STLport stubs..."
$CXX -c cxx/stlport_stubs.cpp -o cxx/stlport_stubs.o $CXXFLAGS $STLPORT_FLAGS

# STLport library location
STLPORT_LIB="$DIR/../user/cxx/STLport-5.2.1/build-output"

# Build STL test program (uses STLport containers)
echo "Building STL test..."
$CXX stl_test.cpp cxx/stlport_stubs.o -o stltest.elf $CXXFLAGS $STLPORT_FLAGS
echo "STL test build complete: stltest.elf"

# Build iostream test program (uses STLport with full iostream)
echo "Building iostream test..."
$CXX iostream_test.cpp -L$STLPORT_LIB -lstlport -o iostr.elf $CXXFLAGS $STLPORT_FLAGS
echo "iostream test build complete: iostr.elf"

# Build enhanced C++ shell (shell+)
echo "Building shell+..."
$CXX shell_plus.cpp -L$STLPORT_LIB -lstlport -o shplus.elf $CXXFLAGS $STLPORT_FLAGS
echo "shell+ build complete: shplus.elf"

# Build Lua
echo "Building Lua..."
cd lua/lua-5.4.8/src

# Clean previous build artifacts
make clean || true

# Build Lua library (object files only) with 32-bit integers
make CC="$CC" \
     AR="$AR rcu" \
     RANLIB="$RANLIB" \
     MYCFLAGS="$CFLAGS" \
     a \
     -j $(nproc)

# Build Lua interpreter object file
$CC -O2 -Wall -Wextra -c lua.c -o lua.o

# Link Lua interpreter as ELF binary for Serotonin
cd ../../..

$CC -c lua_stubs.c -o lua_stubs.o $CFLAGS
$CC -c libgcc_stubs.c -o libgcc_stubs.o $CFLAGS
$CC -c binutils/posix_stubs.c -o binutils/posix_stubs.o $CFLAGS
$CC -I"$DIR/../build-tools/src/binutils-gdb/include" -c binutils/sframe_stubs.c -o binutils/sframe_stubs.o $CFLAGS

$CC lua/lua-5.4.8/src/lua.o lua/lua-5.4.8/src/liblua.a lua_stubs.o libgcc_stubs.o \
    -o lua.elf $CFLAGS

echo "Lua build complete: lua.elf"

if [ "$SKIP_TOOLCHAIN" = "1" ]; then
    echo "Skipping binutils and GCC (--skip-toolchain)"
    echo "=== Build complete ==="
    exit 0
fi

# ═══════════════════════════════════════════════════════════════
# Build binutils for userland (Canadian cross: runs on Serotonin)
# ═══════════════════════════════════════════════════════════════
echo "Building binutils for userland..."
BINUTILS_SRC="$DIR/../build-tools/src/binutils-gdb"
BINUTILS_BUILD="$DIR/binutils-user-build"
BINUTILS_STAGE="$DIR/binutils-user-stage"
BINUTILS_CFLAGS="$CFLAGS -Wno-error=incompatible-pointer-types -Wno-incompatible-pointer-types -mno-tls-direct-seg-refs -DBFD_NO_THREADS"

# CC wrapper: autotools configure for --host=i686-serotonin will try to
# compile-and-link test programs. The toolchain handles linking automatically,
# but some configure-generated programs need the posix/sframe stubs to link.
# The wrapper injects them when producing executables.
mkdir -p "$BINUTILS_BUILD"
STUBS_DIR="$DIR/../user"
CC_WRAPPER="$BINUTILS_BUILD/cc-wrapper.sh"

# Pre-compile stubs once (only posix_stubs needed — libgcc and libsframe are auto-linked)
$CC -c "$STUBS_DIR/binutils/posix_stubs.c" -o "$BINUTILS_BUILD/posix_stubs.o" $BINUTILS_CFLAGS

cat > "$CC_WRAPPER" <<WEOF
#!/bin/sh
# Wrapper: for compile-only (-c), pass through.
# For linking (producing executables), append stub objects.
for arg in "\$@"; do
    if [ "\$arg" = "-c" ] || [ "\$arg" = "-E" ] || [ "\$arg" = "-S" ]; then
        exec $CC "\$@"
    fi
done

# Check if output is a library/object (not an executable)
prev=""
for arg in "\$@"; do
    if [ "\$prev" = "-o" ]; then
        case "\$arg" in
            *.a|*.la|*.so|*.o) exec $CC "\$@" ;;
        esac
        break
    fi
    prev="\$arg"
done

# Linking an executable — prepend stubs so they override libiberty.a symbols
exec $CC $BINUTILS_BUILD/posix_stubs.o "\$@"
WEOF
chmod +x "$CC_WRAPPER"

mkdir -p "$BINUTILS_STAGE"
cd "$BINUTILS_BUILD"

if [ ! -f "config.status" ]; then
    echo "Configuring binutils for userland..."
    CC="$CC_WRAPPER" \
    AR="$AR" \
    RANLIB="$RANLIB" \
    CFLAGS="$BINUTILS_CFLAGS" \
    "$BINUTILS_SRC/configure" \
        --host="$TARGET" \
        --target="$TARGET" \
        --prefix=/usr \
        --program-prefix= \
        --disable-nls \
        --disable-werror \
        --disable-gdb \
        --disable-gdbserver \
        --disable-gprofng \
        --disable-gold \
        --disable-libbacktrace \
        --disable-libctf \
        --disable-readline \
        --disable-gprof \
        --disable-sim
fi

echo "Building binutils utilities..."
make -j$(nproc)

echo "Installing binutils to staging..."
make DESTDIR="$BINUTILS_STAGE" install

echo "Copying binutils to userland..."
BINUTILS_STAGE_BIN="$BINUTILS_STAGE/usr/bin"
for bin in "$BINUTILS_STAGE_BIN"/*; do
    if [ -f "$bin" ]; then
        bin_name="$(basename "$bin")"
        cp "$bin" "$DIR/../user/binutils/${bin_name}.elf"
    fi
done

# ═══════════════════════════════════════════════════════════════
# Build GCC for userland (Canadian cross: runs on Serotonin)
# ═══════════════════════════════════════════════════════════════
echo ""
echo "Building GCC for userland..."
GCC_SRC="$DIR/../build-tools/src/gcc"

for f in $(find "$GCC_SRC" -name 'config.sub' -o -name 'configfsf.sub' 2>/dev/null); do
    [ -f "$f" ] && grep -q 'serotonin' "$f" && continue
    if grep -q '| fiwix\* )' "$f"; then
        sed -i 's/| fiwix\* )/| serotonin* | fiwix* )/' "$f"
    elif grep -q '| fiwix\*' "$f"; then
        sed -i '/| fiwix\*/i\\t| serotonin* \\' "$f"
    elif grep -q '| skyos\*' "$f"; then
        sed -i 's/| skyos\*/| serotonin* | skyos*/' "$f"
    elif grep -q '| emx\*)' "$f"; then
        sed -i 's/| emx\*)/| emx* | serotonin*)/' "$f"
    fi
done
GCC_BUILD="$DIR/gcc-user-build"
GCC_STAGE="$DIR/gcc-user-stage"
GCC_CFLAGS="$CFLAGS -Wno-error=incompatible-pointer-types -Wno-incompatible-pointer-types -mno-tls-direct-seg-refs"
GCC_CXXFLAGS="-O2 -Wall -msse -msse2 -mfpmath=sse -Wno-error=incompatible-pointer-types -Wno-incompatible-pointer-types -mno-tls-direct-seg-refs"

mkdir -p "$GCC_BUILD"
GCC_CC_WRAPPER="$GCC_BUILD/cc-wrapper.sh"
GCC_CXX_WRAPPER="$GCC_BUILD/cxx-wrapper.sh"

cat > "$GCC_CC_WRAPPER" <<WEOF
#!/bin/sh
for arg in "\$@"; do
    if [ "\$arg" = "-c" ] || [ "\$arg" = "-E" ] || [ "\$arg" = "-S" ]; then
        exec $CC "\$@"
    fi
done
prev=""
for arg in "\$@"; do
    if [ "\$prev" = "-o" ]; then
        case "\$arg" in
            *.a|*.la|*.so|*.o) exec $CC "\$@" ;;
        esac
        break
    fi
    prev="\$arg"
done
exec $CC $BINUTILS_BUILD/posix_stubs.o "\$@"
WEOF
chmod +x "$GCC_CC_WRAPPER"

cat > "$GCC_CXX_WRAPPER" <<WEOF
#!/bin/sh
for arg in "\$@"; do
    if [ "\$arg" = "-c" ] || [ "\$arg" = "-E" ] || [ "\$arg" = "-S" ]; then
        exec $CXX "\$@"
    fi
done
prev=""
for arg in "\$@"; do
    if [ "\$prev" = "-o" ]; then
        case "\$arg" in
            *.a|*.la|*.so|*.o) exec $CXX "\$@" ;;
        esac
        break
    fi
    prev="\$arg"
done
exec $CXX $BINUTILS_BUILD/posix_stubs.o "\$@" -lstdc++
WEOF
chmod +x "$GCC_CXX_WRAPPER"

mkdir -p "$GCC_STAGE"
cd "$GCC_BUILD"

if [ ! -f "config.status" ]; then
    echo "Configuring GCC for userland..."

    # Download prerequisites if needed
    (cd "$GCC_SRC" && { [ -d "gmp" ] || ./contrib/download_prerequisites; })

    CC_FOR_BUILD="gcc" \
    CXX_FOR_BUILD="g++" \
    CC="$GCC_CC_WRAPPER" \
    CXX="$GCC_CXX_WRAPPER" \
    AR="$AR" \
    RANLIB="$RANLIB" \
    CFLAGS="$GCC_CFLAGS" \
    CXXFLAGS="$GCC_CXXFLAGS" \
    "$GCC_SRC/configure" \
        --host="$TARGET" \
        --target="$TARGET" \
        --build="$(gcc -dumpmachine)" \
        --prefix=/usr \
        --with-sysroot=/usr/sysroot \
        --with-newlib \
        --enable-languages=c,c++ \
        --disable-nls \
        --disable-shared \
        --disable-threads \
        --disable-libssp \
        --disable-libquadmath \
        --disable-libgomp \
        --disable-libatomic \
        --disable-hosted-libstdcxx \
        --disable-bootstrap \
        --disable-multilib \
        --disable-libstdcxx \
        --disable-gcov \
        --disable-plugin \
        --disable-decimal-float \
        --disable-libffi \
        --disable-libitm \
        --disable-libsanitizer \
        --disable-libvtv
fi

echo "Building GCC compiler..."
sed -i 's/^maybe-configure-gettext: configure-gettext$/maybe-configure-gettext:/' Makefile
sed -i 's/^maybe-all-gettext: all-gettext$/maybe-all-gettext:/' Makefile

make configure-isl -j$(nproc)
sed -i '/isl_test_cpp\$(EXEEXT)/d' isl/Makefile
sed -i '/isl_test_cpp\$(EXEEXT)/d' isl/Makefile.in

make configure-gcc -j$(nproc)
sed -i 's/^#define HAVE_DECL_\(.*\) 0$/#define HAVE_DECL_\1 1/' gcc/auto-host.h

make all-libiberty -j$(nproc)

mkdir -p gcc
touch gcc/s-selftest-c gcc/s-selftest-c++

"$PREFIX/bin/$TARGET-gcc" -dumpspecs > gcc/specs
sed -i 's|^\$(SPECS): xgcc\$(exeext)|$(SPECS):|' gcc/Makefile

export MAKEFLAGS="-j$(nproc)"
make all-gcc

echo "Installing GCC to staging..."
unset MAKEFLAGS
make DESTDIR="$GCC_STAGE" install-gcc

GCC_SPECS_DIR="$GCC_STAGE/usr/lib/gcc/$TARGET"
cat > "$GCC_SPECS_DIR/specs" <<'SPECS'
*cc1:
%(cc1_cpu) -isystem /usr/sysroot/usr/include

*lib:
%{!nostdlib:--start-group -lsyscall -lcxxrt -lc -lm -lgcc --end-group}

SPECS

# install-gcc doesn't install runtime libraries; copy libgcc.a from the cross toolchain
GCC_VER_DIR="$GCC_STAGE/usr/lib/gcc/$TARGET/$(cat "$GCC_SRC/gcc/BASE-VER")"
cp "$PREFIX/lib/gcc/$TARGET/$(cat "$GCC_SRC/gcc/BASE-VER")/libgcc.a" "$GCC_VER_DIR/libgcc.a"
"$STRIP" --strip-debug "$GCC_VER_DIR/libgcc.a"

echo "Copying GCC to userland..."
GCC_STAGE_BIN="$GCC_STAGE/usr/bin"
mkdir -p "$DIR/../user/gcc"
for bin in "$GCC_STAGE_BIN"/*; do
    if [ -f "$bin" ]; then
        bin_name="$(basename "$bin")"
        cp "$bin" "$DIR/../user/gcc/${bin_name}.elf"
    fi
done

# Also copy the GCC support files (specs, headers, etc.)
GCC_STAGE_LIBEXEC="$GCC_STAGE/usr/libexec/gcc/$TARGET"
if [ -d "$GCC_STAGE_LIBEXEC" ]; then
    mkdir -p "$DIR/../user/gcc/libexec"
    cp -r "$GCC_STAGE_LIBEXEC"/* "$DIR/../user/gcc/libexec/"
fi

cd "$DIR/../user"

echo ""
echo "=== Build complete ==="
echo "Userland binutils: user/binutils/"
echo "Userland GCC:      user/gcc/"
echo "STLport containers are available (header-only mode)"
