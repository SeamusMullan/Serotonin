# ---------------------------------------------------------------------------
# Userspace programs
# ---------------------------------------------------------------------------

USER_BUILD    := $(BUILD_DIR)/user
CRT0          := $(SYSROOT)/usr/lib/crt0.o
LDFLAGS_USER  := -T $(SYSROOT)/usr/lib/user.ld -nostdlib -L$(SYSROOT)/usr/lib
LDLIBS_USER   := -Wl,--start-group -lsyscall -lcxxrt -lc -lm -Wl,--end-group

STLPORT_DIR   := $(USER_DIR)/cxx/STLport-5.2.1/stlport
STLPORT_LIB   := $(USER_DIR)/cxx/STLport-5.2.1/build-output
STLPORT_FLAGS := -I$(STLPORT_DIR) -D__SEROTONIN__

LWIP_SRC_DIR  := $(USER_DIR)/lwip/src
LWIP_PORT_DIR := $(USER_DIR)/lwip/serotonin
LWIP_INCLUDES := -I$(LWIP_PORT_DIR) -I$(LWIP_PORT_DIR)/arch -I$(LWIP_SRC_DIR)/include
LWIP_INC      := -I$(LWIP_PORT_DIR)

USER_ELFS     :=

# ---------------------------------------------------------------------------
# Compile rule: all user .c → .o  (EXTRA_CFLAGS injectable via target-specific vars)
# ---------------------------------------------------------------------------
$(USER_BUILD)/%.o: $(USER_DIR)/%.c | $(SYSROOT_STAMP)
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(USER_CFLAGS) $(EXTRA_CFLAGS) -MMD -MP

# Extra include flags per file
$(USER_BUILD)/ifconfig.o $(USER_BUILD)/ping.o $(USER_BUILD)/httpd.o: EXTRA_CFLAGS := $(LWIP_INC)
$(USER_BUILD)/initctl.o: EXTRA_CFLAGS := -I$(USER_DIR)/init

# ---------------------------------------------------------------------------
# Link rule generator: user_prog(outname, obj [obj2 ...], [extra_ldflags])
# ---------------------------------------------------------------------------
define user_prog
$(USER_BUILD)/$(1).elf: $(2) $(CRT0) | $(SYSROOT_STAMP)
	$(CC) $(LDFLAGS_USER) $(CRT0) $(2) $(3) $(LDLIBS_USER) -o $$@
USER_ELFS += $(USER_BUILD)/$(1).elf
endef

# ---------------------------------------------------------------------------
# Simple programs: output name matches source stem
# ---------------------------------------------------------------------------
SIMPLE_PROGS := test ls cat pipe_test fb_layer_test receiver sender \
                fs_syscall_test devfs_example fetch nettest seriald initctl

$(foreach p,$(SIMPLE_PROGS),\
  $(eval $(call user_prog,$(p),$(USER_BUILD)/$(p).o)))

# ---------------------------------------------------------------------------
# Renamed / subdir programs
# ---------------------------------------------------------------------------
$(eval $(call user_prog,init,    $(USER_BUILD)/init/init.o))
$(eval $(call user_prog,login,   $(USER_BUILD)/login/login.o))
$(eval $(call user_prog,sh,      $(USER_BUILD)/shell.o))
$(eval $(call user_prog,ps2tst,  $(USER_BUILD)/mouse_test.o))
$(eval $(call user_prog,mouse,   $(USER_BUILD)/mouse_cursor.o))
$(eval $(call user_prog,kbtest,  $(USER_BUILD)/kb_test.o))
$(eval $(call user_prog,listproc,$(USER_BUILD)/listproc/listproc.o))
$(eval $(call user_prog,sponk,   $(USER_BUILD)/games/sponk/sponk.o))
$(eval $(call user_prog,opl2edit,$(USER_BUILD)/opl2_editor/opl2_editor.o))
$(eval $(call user_prog,getty,   $(USER_BUILD)/getty/getty.o))
$(eval $(call user_prog,cortex,  $(USER_BUILD)/cortex/cortex.o))

# ---------------------------------------------------------------------------
# lwIP-linked programs (need lwip_client.o)
# ---------------------------------------------------------------------------
LWIP_CLIENT_OBJ := $(USER_BUILD)/lwip/serotonin/lwip_client.o

$(LWIP_CLIENT_OBJ): $(USER_DIR)/lwip/serotonin/lwip_client.c | $(SYSROOT_STAMP)
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(USER_CFLAGS) $(LWIP_INC) -MMD -MP

$(eval $(call user_prog,ifconfig,$(USER_BUILD)/ifconfig.o $(LWIP_CLIENT_OBJ)))
$(eval $(call user_prog,ping,    $(USER_BUILD)/ping.o     $(LWIP_CLIENT_OBJ)))
$(eval $(call user_prog,httpd,   $(USER_BUILD)/httpd.o    $(LWIP_CLIENT_OBJ)))

# ---------------------------------------------------------------------------
# lwIP network daemon
# ---------------------------------------------------------------------------
LWIP_SRCS := \
  $(LWIP_SRC_DIR)/core/init.c \
  $(LWIP_SRC_DIR)/core/def.c \
  $(LWIP_SRC_DIR)/core/dns.c \
  $(LWIP_SRC_DIR)/core/inet_chksum.c \
  $(LWIP_SRC_DIR)/core/ip.c \
  $(LWIP_SRC_DIR)/core/mem.c \
  $(LWIP_SRC_DIR)/core/memp.c \
  $(LWIP_SRC_DIR)/core/netif.c \
  $(LWIP_SRC_DIR)/core/pbuf.c \
  $(LWIP_SRC_DIR)/core/raw.c \
  $(LWIP_SRC_DIR)/core/stats.c \
  $(LWIP_SRC_DIR)/core/sys.c \
  $(LWIP_SRC_DIR)/core/altcp.c \
  $(LWIP_SRC_DIR)/core/altcp_alloc.c \
  $(LWIP_SRC_DIR)/core/altcp_tcp.c \
  $(LWIP_SRC_DIR)/core/tcp.c \
  $(LWIP_SRC_DIR)/core/tcp_in.c \
  $(LWIP_SRC_DIR)/core/tcp_out.c \
  $(LWIP_SRC_DIR)/core/timeouts.c \
  $(LWIP_SRC_DIR)/core/udp.c \
  $(LWIP_SRC_DIR)/core/ipv4/autoip.c \
  $(LWIP_SRC_DIR)/core/ipv4/dhcp.c \
  $(LWIP_SRC_DIR)/core/ipv4/etharp.c \
  $(LWIP_SRC_DIR)/core/ipv4/icmp.c \
  $(LWIP_SRC_DIR)/core/ipv4/igmp.c \
  $(LWIP_SRC_DIR)/core/ipv4/ip4_frag.c \
  $(LWIP_SRC_DIR)/core/ipv4/ip4.c \
  $(LWIP_SRC_DIR)/core/ipv4/ip4_addr.c \
  $(LWIP_SRC_DIR)/netif/ethernet.c \
  $(LWIP_SRC_DIR)/api/err.c \
  $(LWIP_PORT_DIR)/serotonin_netif.c \
  $(LWIP_PORT_DIR)/lwip_daemon.c

LWIP_OBJS := $(patsubst $(USER_DIR)/%.c, $(USER_BUILD)/lwip/%.o, \
               $(filter $(USER_DIR)/lwip/%,$(LWIP_SRCS))) \
             $(patsubst $(LWIP_PORT_DIR)/%.c, $(USER_BUILD)/lwip/serotonin/%.o, \
               $(filter $(LWIP_PORT_DIR)/%,$(LWIP_SRCS)))

$(USER_BUILD)/lwip/%.o: $(USER_DIR)/lwip/%.c | $(SYSROOT_STAMP)
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(USER_CFLAGS) $(LWIP_INCLUDES) -Wno-address -MMD -MP

$(USER_BUILD)/lwipd.elf: $(LWIP_OBJS) $(CRT0) | $(SYSROOT_STAMP)
	$(CC) $(LDFLAGS_USER) $(CRT0) $(LWIP_OBJS) $(LDLIBS_USER) -o $@
USER_ELFS += $(USER_BUILD)/lwipd.elf

# ---------------------------------------------------------------------------
# Lua interpreter
# ---------------------------------------------------------------------------
LUA_SRC_DIR   := $(USER_DIR)/lua/lua-5.4.8/src
LUA_LIB       := $(LUA_SRC_DIR)/liblua.a

$(LUA_LIB): | $(SYSROOT_STAMP)
	$(MAKE) -C $(LUA_SRC_DIR) clean
	$(MAKE) -C $(LUA_SRC_DIR) \
	  CC="$(CC)" AR="$(AR) rcu" RANLIB="$(RANLIB)" \
	  MYCFLAGS="$(USER_CFLAGS)" a -j$(shell nproc)

$(USER_BUILD)/lua/lua.o: $(LUA_SRC_DIR)/lua.c | $(SYSROOT_STAMP)
	@mkdir -p $(dir $@)
	$(CC) -m32 -ffreestanding -O2 -Wall -Wextra --sysroot=$(SYSROOT) -c $< -o $@

$(USER_BUILD)/lua_stubs.o:       $(USER_DIR)/lua_stubs.c       | $(SYSROOT_STAMP)
$(USER_BUILD)/libgcc_stubs.o:    $(USER_DIR)/libgcc_stubs.c    | $(SYSROOT_STAMP)
$(USER_BUILD)/binutils/posix_stubs.o: $(USER_DIR)/binutils/posix_stubs.c | $(SYSROOT_STAMP)

$(USER_BUILD)/lua.elf: $(USER_BUILD)/lua/lua.o $(LUA_LIB) \
                       $(USER_BUILD)/lua_stubs.o $(USER_BUILD)/libgcc_stubs.o \
                       $(CRT0) | $(SYSROOT_STAMP)
	$(CC) $(LDFLAGS_USER) $(CRT0) \
	  $(USER_BUILD)/lua/lua.o $(LUA_LIB) \
	  $(USER_BUILD)/lua_stubs.o $(USER_BUILD)/libgcc_stubs.o \
	  $(LDLIBS_USER) -o $@
USER_ELFS += $(USER_BUILD)/lua.elf

# ---------------------------------------------------------------------------
# C++ programs
# ---------------------------------------------------------------------------
$(USER_BUILD)/%.o: $(USER_DIR)/%.cpp | $(SYSROOT_STAMP)
	@mkdir -p $(dir $@)
	$(CXX) -c $< -o $@ $(USER_CXXFLAGS) $(EXTRA_CFLAGS) -MMD -MP

$(USER_BUILD)/stl_test.o $(USER_BUILD)/cxx/stlport_stubs.o \
$(USER_BUILD)/iostream_test.o $(USER_BUILD)/shell_plus.o: EXTRA_CFLAGS := $(STLPORT_FLAGS)

$(USER_BUILD)/cxxtest.elf: $(USER_BUILD)/cxx_test.o $(CRT0) | $(SYSROOT_STAMP)
	$(CXX) $(LDFLAGS_USER) $(CRT0) $< $(LDLIBS_USER) -o $@
USER_ELFS += $(USER_BUILD)/cxxtest.elf

$(USER_BUILD)/stltest.elf: $(USER_BUILD)/stl_test.o \
                            $(USER_BUILD)/cxx/stlport_stubs.o $(CRT0) | $(SYSROOT_STAMP)
	$(CXX) $(LDFLAGS_USER) $(CRT0) $(USER_BUILD)/stl_test.o \
	  $(USER_BUILD)/cxx/stlport_stubs.o $(LDLIBS_USER) -o $@
USER_ELFS += $(USER_BUILD)/stltest.elf

$(USER_BUILD)/iostr.elf: $(USER_BUILD)/iostream_test.o $(CRT0) | $(SYSROOT_STAMP)
	$(CXX) $(LDFLAGS_USER) $(CRT0) $< -L$(STLPORT_LIB) -lstlport $(LDLIBS_USER) -o $@
USER_ELFS += $(USER_BUILD)/iostr.elf

$(USER_BUILD)/shplus.elf: $(USER_BUILD)/shell_plus.o $(CRT0) | $(SYSROOT_STAMP)
	$(CXX) $(LDFLAGS_USER) $(CRT0) $< -L$(STLPORT_LIB) -lstlport $(LDLIBS_USER) -o $@
USER_ELFS += $(USER_BUILD)/shplus.elf

# ---------------------------------------------------------------------------
.PHONY: user
user: $(USER_ELFS)

-include $(shell find $(USER_BUILD) -name '*.d' 2>/dev/null)
