export DIR=$(pwd)
export PREFIX="$DIR/../build-tools/bin"
export TARGET=i686-elf
export PATH="$PREFIX/bin:$PATH"
OS_TYPE="$(uname)"

# compiler flags
CFLAGS="-m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra -msse -msse2 -mfpmath=sse"

cd ../user

i686-elf-as crt0.s -o crt0.o

i686-elf-gcc -c init/init.c -o init/init.o
i686-elf-gcc -c syscall.c -o syscall.o
i686-elf-gcc -c shell.c -o shell.o
i686-elf-gcc -c games/sponk/sponk.c -o games/sponk/sponk.o

i686-elf-gcc -Ttext=0x400100 -nostdlib crt0.o init/init.o syscall.o -Wl,--start-group -lc -lm -Wl,--end-group -o init.elf
i686-elf-gcc -Ttext=0x400100 -nostdlib crt0.o shell.o syscall.o -Wl,--start-group -lc -lm -Wl,--end-group -o sh.elf
i686-elf-gcc -Ttext=0x400100 -nostdlib crt0.o games/sponk/sponk.o syscall.o -Wl,--start-group -lc -lm -Wl,--end-group -o sponk.elf
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
i686-elf-gcc -Ttext=0x400100 -nostdlib crt0.o lua/lua-5.4.8/src/lua.o lua/lua-5.4.8/src/liblua.a syscall.o lua_stubs.o libgcc_stubs.o -Wl,--start-group -lc -lm -Wl,--end-group -o lua.elf

echo "Lua build complete: lua.elf" 