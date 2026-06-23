# ---------------------------------------------------------------------------
# Toolchain: auto-download + build i686-elf cross-compiler if absent
# ---------------------------------------------------------------------------

ROOT_DIR      := $(realpath $(dir $(lastword $(MAKEFILE_LIST)))..)
TOOLS_DIR     := $(ROOT_DIR)/build-tools
TOOLS_PREFIX  := $(TOOLS_DIR)/prefix
TOOLS_SRC     := $(TOOLS_DIR)/src

TARGET        := i686-elf
CC            := $(TOOLS_PREFIX)/bin/$(TARGET)-gcc
CXX           := $(TOOLS_PREFIX)/bin/$(TARGET)-g++
AS            := $(TOOLS_PREFIX)/bin/$(TARGET)-as
AR            := $(TOOLS_PREFIX)/bin/$(TARGET)-ar
RANLIB        := $(TOOLS_PREFIX)/bin/$(TARGET)-ranlib
LD            := $(TOOLS_PREFIX)/bin/$(TARGET)-ld
OBJCOPY       := $(TOOLS_PREFIX)/bin/$(TARGET)-objcopy

# Pinned stable release tarballs (mirrors, no git clone needed)
BINUTILS_VER  := 2.42
GCC_VER       := 13.3.0
NEWLIB_VER    := 4.4.0.20231231

BINUTILS_TAR  := $(TOOLS_SRC)/binutils-$(BINUTILS_VER).tar.gz
GCC_TAR       := $(TOOLS_SRC)/gcc-$(GCC_VER).tar.gz
NEWLIB_TAR    := $(TOOLS_SRC)/newlib-$(NEWLIB_VER).tar.gz

BINUTILS_URL  := https://ftp.gnu.org/gnu/binutils/binutils-$(BINUTILS_VER).tar.gz
GCC_URL       := https://ftp.gnu.org/gnu/gcc/gcc-$(GCC_VER)/gcc-$(GCC_VER).tar.gz
NEWLIB_URL    := https://sourceware.org/pub/newlib/newlib-$(NEWLIB_VER).tar.gz

TOOLCHAIN_STAMP := $(TOOLS_DIR)/.built-$(GCC_VER)

export PATH := $(TOOLS_PREFIX)/bin:$(PATH)

# ---------------------------------------------------------------------------
# Public target: build toolchain only if stamp missing
# ---------------------------------------------------------------------------
.PHONY: toolchain
toolchain: $(TOOLCHAIN_STAMP)

$(TOOLCHAIN_STAMP): $(TOOLS_SRC)/binutils-$(BINUTILS_VER) \
                    $(TOOLS_SRC)/gcc-$(GCC_VER)
	@echo "[toolchain] Building binutils $(BINUTILS_VER)..."
	mkdir -p $(TOOLS_SRC)/build-binutils
	cd $(TOOLS_SRC)/build-binutils && \
	  ../binutils-$(BINUTILS_VER)/configure \
	    --target=$(TARGET) --prefix=$(TOOLS_PREFIX) \
	    --with-sysroot --disable-nls --disable-werror --quiet && \
	  $(MAKE) -j$(shell nproc) --quiet && \
	  $(MAKE) install --quiet
	@echo "[toolchain] Building GCC $(GCC_VER) (C + C++)..."
	mkdir -p $(TOOLS_SRC)/build-gcc
	cd $(TOOLS_SRC)/build-gcc && \
	  ../gcc-$(GCC_VER)/configure \
	    --target=$(TARGET) --prefix=$(TOOLS_PREFIX) \
	    --disable-nls --enable-languages=c,c++ \
	    --without-headers --disable-hosted-libstdcxx --quiet && \
	  $(MAKE) all-gcc -j$(shell nproc) --quiet && \
	  $(MAKE) install-gcc --quiet
	@echo "[toolchain] Building newlib $(NEWLIB_VER)..."
	mkdir -p $(TOOLS_SRC)/build-newlib
	cd $(TOOLS_SRC)/build-newlib && \
	  ../newlib-$(NEWLIB_VER)/configure \
	    --target=$(TARGET) --prefix=$(TOOLS_PREFIX) \
	    --disable-newlib-supplied-syscalls \
	    --disable-libgloss \
	    --enable-newlib-reent-small \
	    --disable-nls --quiet && \
	  $(MAKE) -j$(shell nproc) --quiet && \
	  $(MAKE) install --quiet
	touch $@
	@echo "[toolchain] Done. Cross-compiler at $(TOOLS_PREFIX)/bin/$(TARGET)-gcc"

# ---------------------------------------------------------------------------
# Source extraction
# ---------------------------------------------------------------------------
$(TOOLS_SRC)/binutils-$(BINUTILS_VER): $(BINUTILS_TAR)
	@echo "[toolchain] Extracting binutils..."
	tar -xzf $< -C $(TOOLS_SRC)

$(TOOLS_SRC)/gcc-$(GCC_VER): $(GCC_TAR)
	@echo "[toolchain] Extracting GCC..."
	tar -xzf $< -C $(TOOLS_SRC)
	@echo "[toolchain] Downloading GCC prerequisites..."
	cd $(TOOLS_SRC)/gcc-$(GCC_VER) && ./contrib/download_prerequisites

$(TOOLS_SRC)/newlib-$(NEWLIB_VER): $(NEWLIB_TAR)
	@echo "[toolchain] Extracting newlib..."
	tar -xzf $< -C $(TOOLS_SRC)

# ---------------------------------------------------------------------------
# Tarball downloads
# ---------------------------------------------------------------------------
$(BINUTILS_TAR):
	@mkdir -p $(TOOLS_SRC)
	@echo "[toolchain] Downloading binutils $(BINUTILS_VER)..."
	curl -# -L --retry 3 -o $@ $(BINUTILS_URL)

$(GCC_TAR):
	@mkdir -p $(TOOLS_SRC)
	@echo "[toolchain] Downloading GCC $(GCC_VER)..."
	curl -# -L --retry 3 -o $@ $(GCC_URL)

$(NEWLIB_TAR):
	@mkdir -p $(TOOLS_SRC)
	@echo "[toolchain] Downloading newlib $(NEWLIB_VER)..."
	curl -# -L --retry 3 -o $@ $(NEWLIB_URL)
