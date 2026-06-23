OS := $(shell uname -s)

ifeq ($(OS),Darwin)
  PLATFORM          := darwin
  GRUB_MKRESCUE     := /opt/homebrew/Cellar/i686-elf-grub/2.12/bin/i686-elf-grub-mkrescue
  QEMU              := qemu-system-x86_64
else
  PLATFORM          := linux
  GRUB_MKRESCUE     := grub-mkrescue
  QEMU              := qemu-system-x86_64
endif
