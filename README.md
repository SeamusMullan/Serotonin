# Serotonin OS

A hobby x86 operating system with a custom kernel, userspace, and GNU Makefile build system.

## Host dependencies

Install these before running `make all`:

**Arch Linux**
```
sudo pacman -S base-devel grub xorriso mtools parted qemu-system-x86
```

**Ubuntu / Debian**
```
sudo apt install build-essential grub-pc-bin grub-common xorriso mtools parted qemu-system-x86 dosfstools
```

**macOS (Homebrew)**
```
brew install i686-elf-grub xorriso qemu
```

> `grub-mkrescue` and `xorriso` are required for `make iso`.  
> `qemu-system-x86_64` is required for `make run` / `make run-iso` / `make test`.  
> `parted`, `mtools`, and `sudo` access are required for `make img` (creates a FAT32 disk image via losetup).  
> Use `make run-iso` to skip disk image creation and boot directly from the ISO.

## Quick start

```
make all       # toolchain → sysroot → kernel → user → iso
make run       # boot in QEMU
make test      # headless QEMU kernel test
```

The first run downloads and builds a `i686-elf` cross-compiler (~15 min). Subsequent runs are incremental.

## Targets

| Target          | Action                                              |
|-----------------|-----------------------------------------------------|
| `make toolchain`| Download + build i686-elf GCC cross-compiler (once)|
| `make sysroot`  | Build newlib sysroot + runtime libs                 |
| `make kernel`   | Compile kernel → `build/serotonin.bin`              |
| `make user`     | Compile all userspace ELFs → `build/user/`          |
| `make iso`      | Package into bootable ISO                           |
| `make img`      | Create FAT32 disk image with userspace binaries     |
| `make all`      | Full build from scratch                             |
| `make run`      | Launch in QEMU (requires disk image + sudo)         |
| `make run-iso`  | Boot from ISO only — no disk image or sudo needed   |
| `make run-debug`| Launch in QEMU, wait for GDB on `:1234`             |
| `make test`     | Kernel test mode (headless QEMU)                    |
| `make clean`    | Remove build artifacts (keeps toolchain + sysroot)  |
| `make distclean`| Remove everything including toolchain + sysroot     |

## Not yet implemented

- **Networking** (`ifconfig`, `ping`, `httpd`, `lwipd`) — requires `user/lwip/` port layer
- **STLport programs** (`stltest`, `iostr`, `shplus`) — requires `user/cxx/STLport-5.2.1/`

## Directory structure

```
kernel/       kernel source
user/         userspace programs + syscall library
make/         modular Makefile includes
patches/      source patches applied during toolchain build
build/        build output (generated)
build-tools/  cross-compiler (generated)
sysroot/      newlib sysroot (generated)
```
