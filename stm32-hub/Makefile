# Makefile for the STM32 time-sync hub (STM32L476RG / Nucleo-64)
#
# Bare-metal, register-level build: CMSIS core + device headers only, no
# HAL/CubeMX. Plain arm-none-eabi-gcc/objcopy/openocd, no IDE required.

TOOLS   = arm-none-eabi
CC      = $(TOOLS)-gcc
OBJCOPY = $(TOOLS)-objcopy
SIZE    = $(TOOLS)-size
GDB     = $(TOOLS)-gdb

BUILD   = build
SRC_DIR = src
LINKER  = linker/stm32l476rg.ld

DEVICE_DEFINE = -DSTM32L476xx

CMSIS_CORE      = vendor/CMSIS/Core/Include
CMSIS_DEVICE    = vendor/CMSIS/Device/ST/STM32L4xx/Include
CMSIS_SYSTEM_SRC = vendor/CMSIS/Device/ST/STM32L4xx/Source/Templates

INCLUDES = -I$(CMSIS_CORE) -I$(CMSIS_DEVICE) -I$(SRC_DIR)

# Application + vendor sources. Add a new .c file here (or drop it in
# $(SRC_DIR)) when adding functionality -- no other Makefile changes needed.
C_SRC_NAMES  = main.c system_stm32l4xx.c
ASM_SRC_NAME = startup_stm32l476rg.S

OBJS = $(addprefix $(BUILD)/,$(C_SRC_NAMES:.c=.o)) $(BUILD)/$(ASM_SRC_NAME:.S=.o)

vpath %.c $(SRC_DIR):$(CMSIS_SYSTEM_SRC)
vpath %.S $(SRC_DIR)

FLOAT        = soft
DEBUG        = 1
OPTIMIZATION = -O0

ifeq ($(FLOAT), soft)
  FLOAT_ARCH = -mfloat-abi=soft
else
  FLOAT_ARCH = -mfloat-abi=hard -mfpu=fpv4-sp-d16
endif

ARCH = -mcpu=cortex-m4 -mthumb -mlittle-endian $(FLOAT_ARCH)

ifeq ($(DEBUG), 1)
  DEBUG_FLAGS = -g -DDEBUG
endif

CFLAGS  = $(ARCH) $(DEVICE_DEFINE) $(INCLUDES) $(OPTIMIZATION) $(DEBUG_FLAGS) \
          -std=gnu11 -Wall -Wextra -ffreestanding -fno-builtin
LDFLAGS = $(ARCH) -T$(LINKER) -nostdlib -Wl,--gc-sections -Wl,-Map=$(BUILD)/stm32-hub.map

BINARY = $(BUILD)/stm32-hub

.PHONY: all clean flash size help
all: $(BINARY).bin size

help:
	@echo "Targets:"
	@echo "  all    - build $(BINARY).elf/.bin (default)"
	@echo "  flash  - build, then flash+reset over ST-LINK via OpenOCD"
	@echo "  size   - print .text/.data/.bss size of the built ELF"
	@echo "  clean  - remove build/"

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.S | $(BUILD)
	$(CC) $(ARCH) $(DEVICE_DEFINE) -c $< -o $@

$(BINARY).elf: $(OBJS)
	$(CC) $(LDFLAGS) $^ -o $@

$(BINARY).bin: $(BINARY).elf
	$(OBJCOPY) -O binary $< $@

size: $(BINARY).elf
	$(SIZE) $<

# Requires OpenOCD on PATH (the same OpenOCD used for 18-349 works fine).
flash: $(BINARY).elf
	openocd -f interface/stlink.cfg -f target/stm32l4x.cfg \
	  -c "program $(BINARY).elf verify reset exit"

clean:
	rm -rf $(BUILD)
