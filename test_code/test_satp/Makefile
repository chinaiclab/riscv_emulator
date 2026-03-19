# Makefile for test_satp

# Default RISC-V toolchain (adjust if needed)
RISCV_PREFIX ?= riscv64-unknown-elf-
RISCV_CC = $(RISCV_PREFIX)gcc
RISCV_OBJCOPY = $(RISCV_PREFIX)objcopy

# Target name
TARGET = test_satp

# Source files
SRCS = ../../software/init_code/start.S main.c ../../software/init_code/bare-metal.c ../../software/init_code/uart.c ../../software/init_code/vprintf.c

# Compiler flags
CFLAGS = -march=rv32ima_zicsr -mabi=ilp32 -ffreestanding -nostdlib -nostartfiles -g
LDFLAGS = -Wl,-T,../../software/init_code/ldscript.ld

# Default target
all: $(TARGET)

# Link to create ELF
$(TARGET).elf: $(SRCS)
	$(RISCV_CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

# Create raw binary
$(TARGET): $(TARGET).elf
	$(RISCV_OBJCOPY) -O binary $< $@

# Clean
clean:
	rm -f *.o *.elf $(TARGET)

.PHONY: all clean
