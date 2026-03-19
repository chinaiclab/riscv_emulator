# Makefile for standard C library memcpy test

# Compiler settings
RISCV_CC = riscv64-unknown-elf-gcc
RISCV_OBJCOPY = riscv64-unknown-elf-objcopy

# Standard C library compilation with our printf modules
CFLAGS = -march=rv32ima_zicsr -mabi=ilp32 -ffreestanding -nostartfiles -g -O2

# Extract test name from directory name
TEST_NAME := $(notdir $(abspath $(dir $(lastword $(MAKEFILE_LIST)))))
TEST_ELF = $(TEST_NAME).elf
TEST_BIN = $(TEST_NAME)

# Default emulator parameters
CORES ?= 1
CYCLES ?= 10000
OPTIONS ?=

# Source files (our test program with startup code and printf modules - no bare-metal.c)
SOURCES = ../../software/init_code/start.S main.c ../../software/init_code/uart.c ../../software/init_code/vprintf.c

# Default target
all: $(TEST_BIN)

# Determine which linker script to use
LDSCRIPT := $(wildcard ldscript.ld)
ifeq ($(LDSCRIPT),)
    LDSCRIPT = ../../software/init_code/ldscript.ld
endif

# Build ELF file with bare-metal startup and standard library functions
$(TEST_ELF): main.c
	$(RISCV_CC) $(CFLAGS) -Wl,-T,$(LDSCRIPT) -o $@ $(SOURCES)

# Build binary
$(TEST_BIN): $(TEST_ELF)
	$(RISCV_OBJCOPY) -O binary $< $@

# Run the test with simulator
run: $(TEST_BIN)
	@echo "Running $(TEST_NAME) test with $(CORES) cores, $(CYCLES) cycles, and options: $(OPTIONS)..."
	@echo "Note: This test uses standard C library (libc)"
	@../../riscv_simulator --cores $(CORES) --cycles $(CYCLES) $(OPTIONS) --program $(TEST_BIN)

# Clean build artifacts
clean:
	rm -f $(TEST_ELF) $(TEST_BIN)

# Disassemble the binary (useful for debugging)
disasm: $(TEST_ELF)
	$(RISCV_OBJCOPY) -O binary --set-section-flags .core0_boot=alloc,load,code $< $@
	@echo "Disassembly saved to $(TEST_NAME).disasm"

.PHONY: all run clean disasm