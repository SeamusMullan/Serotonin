export DIR=$(pwd)
export PREFIX="$DIR/../build-tools/bin"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"
OS_TYPE="$(uname)"

# compiler flags
CFLAGS="-m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse"
CXXFLAGS="-m32 -std=c++11 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse -fno-exceptions -fno-rtti -fno-threadsafe-statics"

# STLport configuration
STLPORT_DIR="$DIR/../user/cxx/STLport-5.2.1/stlport"
STLPORT_FLAGS="-I$STLPORT_DIR -D__SEROTONIN__"

cd ../user

i686-elf-as crt0.s -o crt0.o

i686-elf-gcc -c cxx/cxx_init.c -o cxx/cxx_init.o $CFLAGS
i686-elf-g++ -c cxx/cxx_runtime.cpp -o cxx/cxx_runtime.o $CXXFLAGS
i686-elf-g++ -c cxx/cxx_new_delete.cpp  -o cxx/cxx_new_delete.o $CXXFLAGS

i686-elf-gcc -c init/init.c -o init/init.o $CFLAGS
i686-elf-gcc -c init/init.c -o init/init.o $CFLAGS
i686-elf-gcc -c syscall/syscall.c -o syscall/syscall.o $CFLAGS
i686-elf-gcc -c syscall/lib5ht/lib5ht.c -o syscall/lib5ht/lib5ht.o $CFLAGS
i686-elf-gcc -c shell.c -o shell.o $CFLAGS
i686-elf-gcc -c ls.c -o ls.o $CFLAGS
i686-elf-gcc -c receiver.c -o receiver.o $CFLAGS
i686-elf-gcc -c sender.c -o sender.o $CFLAGS
i686-elf-gcc -c fs_syscall_test.c -o fs_syscall_test.o $CFLAGS
i686-elf-gcc -c listproc/listproc.c -o listproc/listproc.o $CFLAGS
i686-elf-gcc -c games/sponk/sponk.c -o games/sponk/sponk.o $CFLAGS

i686-elf-gcc -Ttext=0x400100 -nostdlib cxx/cxx_init.o cxx/cxx_new_delete.o cxx/cxx_runtime.o crt0.o init/init.o syscall/syscall.o syscall/lib5ht/lib5ht.o -Wl,--start-group -lc -lm -Wl,--end-group -o init.elf
i686-elf-gcc -Ttext=0x400100 -nostdlib cxx/cxx_init.o cxx/cxx_new_delete.o cxx/cxx_runtime.o crt0.o listproc/listproc.o syscall/syscall.o syscall/lib5ht/lib5ht.o -Wl,--start-group -lc -lm -Wl,--end-group -o listproc.elf
i686-elf-gcc -Ttext=0x400100 -nostdlib cxx/cxx_init.o cxx/cxx_new_delete.o cxx/cxx_runtime.o crt0.o shell.o syscall/syscall.o syscall/lib5ht/lib5ht.o -Wl,--start-group -lc -lm -Wl,--end-group -o sh.elf
i686-elf-gcc -Ttext=0x400100 -nostdlib cxx/cxx_init.o cxx/cxx_new_delete.o cxx/cxx_runtime.o crt0.o ls.o syscall/syscall.o syscall/lib5ht/lib5ht.o -Wl,--start-group -lc -lm -Wl,--end-group -o ls.elf
i686-elf-gcc -Ttext=0x400100 -nostdlib cxx/cxx_init.o cxx/cxx_new_delete.o cxx/cxx_runtime.o crt0.o fs_syscall_test.o syscall/syscall.o syscall/lib5ht/lib5ht.o -Wl,--start-group -lc -lm -Wl,--end-group -o fs_syscall_test.elf
i686-elf-gcc -Ttext=0x400100 -nostdlib cxx/cxx_init.o cxx/cxx_new_delete.o cxx/cxx_runtime.o crt0.o games/sponk/sponk.o syscall/syscall.o syscall/lib5ht/lib5ht.o -Wl,--start-group -lc -lm -Wl,--end-group -o sponk.elf
i686-elf-gcc -Ttext=0x400100 -nostdlib cxx/cxx_init.o cxx/cxx_new_delete.o cxx/cxx_runtime.o crt0.o receiver.o syscall/syscall.o syscall/lib5ht/lib5ht.o -Wl,--start-group -lc -lm -Wl,--end-group -o receiver.elf
i686-elf-gcc -Ttext=0x400100 -nostdlib cxx/cxx_init.o cxx/cxx_new_delete.o cxx/cxx_runtime.o crt0.o sender.o syscall/syscall.o syscall/lib5ht/lib5ht.o -Wl,--start-group -lc -lm -Wl,--end-group -o sender.elf

i686-elf-g++ -c cxx_test.cpp -o cxx_test.o $CXXFLAGS
i686-elf-g++ -Ttext=0x400100 -nostdlib cxx/cxx_init.o cxx/cxx_new_delete.o cxx/cxx_runtime.o crt0.o cxx_test.o syscall/syscall.o syscall/lib5ht/lib5ht.o -Wl,--start-group -lc -lm -Wl,--end-group -o cxxtest.elf

# Build STLport stubs (provides range error functions for bare-metal)
echo "Building STLport stubs..."
i686-elf-g++ -c cxx/stlport_stubs.cpp -o cxx/stlport_stubs.o $CXXFLAGS $STLPORT_FLAGS

# STLport library location
STLPORT_LIB="$DIR/../user/cxx/STLport-5.2.1/build-output"

# Build STL test program (uses STLport containers)
echo "Building STL test..."
i686-elf-g++ -c stl_test.cpp -o stl_test.o $CXXFLAGS $STLPORT_FLAGS
i686-elf-g++ -Ttext=0x400100 -nostdlib cxx/cxx_init.o cxx/cxx_new_delete.o cxx/cxx_runtime.o cxx/stlport_stubs.o crt0.o stl_test.o syscall/syscall.o syscall/lib5ht/lib5ht.o -Wl,--start-group -lc -lm -Wl,--end-group -o stltest.elf
echo "STL test build complete: stltest.elf"

# Build iostream test program (uses STLport with full iostream)
# Note: don't link stlport_stubs.o when using libstlport.a (it provides those functions)
echo "Building iostream test..."
i686-elf-g++ -c iostream_test.cpp -o iostream_test.o $CXXFLAGS $STLPORT_FLAGS
i686-elf-g++ -Ttext=0x400100 -nostdlib cxx/cxx_init.o cxx/cxx_new_delete.o cxx/cxx_runtime.o crt0.o iostream_test.o syscall/syscall.o syscall/lib5ht/lib5ht.o -L$STLPORT_LIB -lstlport -Wl,--start-group -lc -lm -Wl,--end-group -o iostr.elf
echo "iostream test build complete: iostreamtest.elf"

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
i686-elf-gcc -m32 -ffreestanding -O2 -Wall -Wextra -c lua.c -o lua.o

# Link Lua interpreter as ELF binary for Serotonin OS
cd ../../..

i686-elf-gcc -c lua_stubs.c -o lua_stubs.o $CFLAGS
i686-elf-gcc -c libgcc_stubs.c -o libgcc_stubs.o $CFLAGS

# Link without -lgcc
i686-elf-gcc -Ttext=0x400100 -nostdlib cxx/cxx_init.o cxx/cxx_new_delete.o cxx/cxx_runtime.o crt0.o lua/lua-5.4.8/src/lua.o lua/lua-5.4.8/src/liblua.a syscall.o lua_stubs.o libgcc_stubs.o -Wl,--start-group -lc -lm -Wl,--end-group -o lua.elf

echo "Lua build complete: lua.elf"

echo ""
echo "=== Build complete ==="
echo "STLport containers are available (header-only mode)"