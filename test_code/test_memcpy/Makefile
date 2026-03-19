# Makefile for memcpy test case - automatically extracts name from directory

# Compiler settings
RISCV_CC = riscv64-unknown-elf-gcc
RISCV_OBJCOPY = riscv64-unknown-elf-objcopy

# Determine which linker script to use
LDSCRIPT := $(wildcard ldscript.ld)
ifeq ($(LDSCRIPT),)
    LDSCRIPT = ../../software/init_code/ldscript.ld
endif

CFLAGS = -march=rv32ima_zicsr -mabi=ilp32 -ffreestanding -nostartfiles -nostdlib -g -Wl,-T,$(LDSCRIPT)

# Extract test name from directory name
TEST_NAME := $(notdir $(abspath $(dir $(lastword $(MAKEFILE_LIST)))))
TEST_ELF = $(TEST_NAME).elf
TEST_BIN = $(TEST_NAME)

# Default emulator parameters
CORES ?= 4
CYCLES ?= 20000  # Increased cycles for memcpy testing
OPTIONS ?=

# Source files
SOURCES = ../../software/init_code/start.S main.c ../../software/init_code/bare-metal.c ../../software/init_code/uart.c ../../software/init_code/vprintf.c

# Default target
all: $(TEST_BIN)

# Build ELF file
$(TEST_ELF): main.c
	$(RISCV_CC) $(CFLAGS) -o $@ $(SOURCES)

# Build binary
$(TEST_BIN): $(TEST_ELF)
	$(RISCV_OBJCOPY) -O binary $< $@

# Run the test with simulator
run: $(TEST_BIN)
	@echo "Running $(TEST_NAME) test with $(CORES) cores, $(CYCLES) cycles, and options: $(OPTIONS)..."
	@../../riscv_simulator --cores $(CORES) --cycles $(CYCLES) $(OPTIONS) --program $(TEST_BIN)

# Clean build artifacts
clean:
	rm -f $(TEST_ELF) $(TEST_BIN)

.PHONY: all run clean