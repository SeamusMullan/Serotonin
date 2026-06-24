# ---------------------------------------------------------------------------
# Kernel build: auto-discovers all .c and .s sources, incremental via -MMD
# ---------------------------------------------------------------------------

KERNEL_DIR    := $(ROOT_DIR)/kernel
BUILD_DIR     := $(ROOT_DIR)/build
KERNEL_BIN    := $(BUILD_DIR)/serotonin.bin

# Per-file flag overrides via target-specific variables
# Files that need -O0 instead of -O2

KERNEL_CFLAGS := -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
                 -msse -msse2 -mfpmath=sse \
                 -fstack-protector-strong \
                 -I$(KERNEL_DIR) -I$(ROOT_DIR)
KERNEL_CFLAGS_BASE := -std=gnu99 -ffreestanding -O2 -Wall -Wextra \
                      -I$(KERNEL_DIR) -I$(ROOT_DIR)

ifdef DEBUG
  KERNEL_CFLAGS += -g
endif
ifdef TEST_MODE
  KERNEL_CFLAGS += -DKERNEL_TEST_MODE
endif

# ---------------------------------------------------------------------------
# Source discovery
# ---------------------------------------------------------------------------

KERNEL_C_SRCS := $(shell find $(KERNEL_DIR) -name '*.c' ! -path '*/test/*')
KERNEL_S_SRCS := $(shell find $(KERNEL_DIR) -name '*.s' ! -path '*/test/*')

ifdef TEST_MODE
  KERNEL_C_SRCS += $(shell find $(KERNEL_DIR)/test -name '*.c')
endif

# Object files under build/kernel/
KERNEL_C_OBJS := $(patsubst $(KERNEL_DIR)/%.c, $(BUILD_DIR)/kernel/%.o, $(KERNEL_C_SRCS))
KERNEL_S_OBJS := $(patsubst $(KERNEL_DIR)/%.s, $(BUILD_DIR)/kernel/%.o, $(KERNEL_S_SRCS))
KERNEL_OBJS   := $(KERNEL_C_OBJS) $(KERNEL_S_OBJS)

# Auto-generated dependency files
KERNEL_DEPS   := $(KERNEL_C_OBJS:.o=.d)
-include $(KERNEL_DEPS)

# ---------------------------------------------------------------------------
# Rules
# ---------------------------------------------------------------------------

.PHONY: kernel
kernel: $(KERNEL_BIN)

$(KERNEL_BIN): $(KERNEL_OBJS) $(KERNEL_DIR)/linker.ld
	@mkdir -p $(BUILD_DIR)
	@echo "[kernel] Linking $@"
	$(CC) -T $(KERNEL_DIR)/linker.ld -o $@ \
	  -ffreestanding -O2 -nostdlib \
	  $(KERNEL_OBJS) -lgcc

# Target-specific flag overrides
$(BUILD_DIR)/kernel/vmm/vmm.o:          KERNEL_CFLAGS := $(KERNEL_CFLAGS_BASE)
$(BUILD_DIR)/kernel/video/vbe/vbe.o:    KERNEL_CFLAGS += -mstackrealign
$(BUILD_DIR)/kernel/syscall/syscall.o:  KERNEL_CFLAGS += -mno-sse -mno-sse2
$(BUILD_DIR)/kernel/filesystem/ide.o:   KERNEL_CFLAGS := $(KERNEL_CFLAGS_BASE) -msse -mfpmath=sse
$(BUILD_DIR)/kernel/io/irq.o \
$(BUILD_DIR)/kernel/io/rtc.o \
$(BUILD_DIR)/kernel/gdt.o \
$(BUILD_DIR)/kernel/idt.o \
$(BUILD_DIR)/kernel/fault.o \
$(BUILD_DIR)/kernel/vmm/paging_init.o:  KERNEL_CFLAGS := $(KERNEL_CFLAGS_BASE)

# C sources
$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) -c $< -o $@ $(KERNEL_CFLAGS) -MMD -MP

# Assembly sources
$(BUILD_DIR)/kernel/%.o: $(KERNEL_DIR)/%.s
	@mkdir -p $(dir $@)
	$(AS) $< -o $@
