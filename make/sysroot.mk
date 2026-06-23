# ---------------------------------------------------------------------------
# Sysroot: newlib headers/libs + Serotonin syscall lib + CRT
# ---------------------------------------------------------------------------

USER_DIR      := $(ROOT_DIR)/user
SYSROOT       := $(ROOT_DIR)/sysroot
SYSROOT_STAMP := $(SYSROOT)/.built

USER_CFLAGS   := -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
                 -msse -msse2 -mfpmath=sse \
                 --sysroot=$(SYSROOT)
USER_CXXFLAGS := -m32 -std=c++11 -ffreestanding -O2 -Wall -Wextra \
                 -msse -msse2 -mfpmath=sse \
                 -fno-exceptions -fno-rtti -fno-threadsafe-statics \
                 --sysroot=$(SYSROOT)

.PHONY: sysroot
sysroot: $(SYSROOT_STAMP)

$(SYSROOT_STAMP): $(TOOLCHAIN_STAMP)
	@echo "[sysroot] Building..."
	rm -rf $(SYSROOT)
	mkdir -p $(SYSROOT)/usr/include/serotonin $(SYSROOT)/usr/lib

	# newlib headers + libs
	cp -r $(TOOLS_PREFIX)/$(TARGET)/include/. $(SYSROOT)/usr/include/
	cp $(TOOLS_PREFIX)/$(TARGET)/lib/libc.a  $(SYSROOT)/usr/lib/
	cp $(TOOLS_PREFIX)/$(TARGET)/lib/libm.a  $(SYSROOT)/usr/lib/
	-cp $(TOOLS_PREFIX)/$(TARGET)/lib/libg.a $(SYSROOT)/usr/lib/ 2>/dev/null || true
	-cp -r $(TOOLS_PREFIX)/$(TARGET)/lib/ldscripts $(SYSROOT)/usr/lib/ 2>/dev/null || true

	# Serotonin headers
	cp $(USER_DIR)/syscall/syscall_table.h $(SYSROOT)/usr/include/serotonin/
	cp $(USER_DIR)/syscall/lib5ht/lib5ht.h $(SYSROOT)/usr/include/serotonin/

	# crt0.o
	$(AS) $(USER_DIR)/crt0.s -o $(SYSROOT)/usr/lib/crt0.o

	# libsyscall.a
	$(CC) -c $(USER_DIR)/syscall/syscall.c  -o /tmp/syscall_a.o \
	  -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
	  -I$(USER_DIR)/syscall -I$(SYSROOT)/usr/include
	$(CC) -c $(USER_DIR)/syscall/lib5ht/lib5ht.c -o /tmp/lib5ht_a.o \
	  -m32 -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
	  -I$(USER_DIR)/syscall -I$(SYSROOT)/usr/include
	$(AR) rcs $(SYSROOT)/usr/lib/libsyscall.a /tmp/syscall_a.o /tmp/lib5ht_a.o

	# libcxxrt.a
	$(CC)  -c $(USER_DIR)/cxx/cxx_init.c       -o /tmp/cxx_init.o \
	  -m32 -std=gnu99 -ffreestanding -O2
	$(CXX) -c $(USER_DIR)/cxx/cxx_runtime.cpp  -o /tmp/cxx_rt.o \
	  -m32 -std=c++11 -ffreestanding -O2 -fno-exceptions -fno-rtti
	$(CXX) -c $(USER_DIR)/cxx/cxx_new_delete.cpp -o /tmp/cxx_nd.o \
	  -m32 -std=c++11 -ffreestanding -O2 -fno-exceptions -fno-rtti
	$(AR) rcs $(SYSROOT)/usr/lib/libcxxrt.a /tmp/cxx_init.o /tmp/cxx_rt.o /tmp/cxx_nd.o

	# linker script
	cp $(USER_DIR)/user.ld $(SYSROOT)/usr/lib/user.ld

	touch $@
	@echo "[sysroot] Done at $(SYSROOT)"
