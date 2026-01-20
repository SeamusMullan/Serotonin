# Binutils userland port

This port builds binutils utilities for Serotonin userland using the existing
source tree at `build-tools/src/binutils-gdb`.

Build integration lives in `build/build-user.sh`. The build steps configure
binutils as a cross-compiled userland package and stage the installed utilities,
then copy each tool into `user/` as `<tool>.elf` so the image scripts can place
them under `/bin/`.

Notes:
- Output build artifacts live under `build/binutils-user-build` and
  `build/binutils-user-stage`.
- The installed tool names mirror binutils defaults (e.g., `objdump`, `readelf`,
  `nm`, `ar`, `ranlib`, `strip`, `size`, `addr2line`, `strings`, `c++filt`).
Build details:
- Links `user/crt0.o`, `user/cxx/cxx_init.o`, `user/cxx/cxx_runtime.o`, and syscall objects directly in `LDFLAGS` so configure can link test programs.
- Uses `-nostartfiles` and `-Wl,-Ttext=0x400100` to match the userland load address.
- Disables libsframe, libbacktrace, libctf, readline, and gprof to avoid unsupported dependencies for the target.
- Adds `-Wno-error=incompatible-pointer-types` to keep bfd builds warning-only on GCC 16.
- Removes libsframe linkage from `bfd` to avoid requiring `libsframe.la` in userland builds.
- Uses a compiler wrapper so startup objects and userland link flags only apply to final utilities, not libraries.
- Ensures `libiberty.a` exists by building it from `required-list` if make does not emit the archive.
- Adds `posix_stubs.c` and `sframe_stubs.c` to satisfy missing syscalls and libsframe symbols at link time.
- Removes libsframe linkage from binutils utilities (`objdump`, `readelf`) in the generated Makefile.
