# Makefile for TVM Virtual Machine test - automatically extracts name from directory

# Compiler settings
RISCV_CC = riscv64-unknown-elf-gcc
RISCV_OBJCOPY = riscv64-unknown-elf-objcopy
# Determine which linker script to use
LDSCRIPT := $(wildcard ldscript.ld)
ifeq ($(LDSCRIPT),)
    LDSCRIPT = ../../software/init_code/ldscript.ld
endif
CFLAGS = -march=rv32ima_zicsr -mabi=ilp32 -ffreestanding -nostartfiles -g -Wl,-T,../../software/init_code/ldscript.ld -O2 -fno-builtin -nostdlib
LDFLAGS = -Wl,--gc-sections -Wl,--print-gc-sections

# Extract test name from directory name
TEST_NAME := $(notdir $(abspath $(dir $(lastword $(MAKEFILE_LIST)))))
TEST_ELF = $(TEST_NAME).elf
TEST_BIN = $(TEST_NAME)

# Default emulator parameters
CORES ?= 1
CYCLES ?= 1000000
OPTIONS ?=

# Source files (minimal self-contained version)
SOURCES = ../../software/init_code/start.S minimal_main.c

# Include paths
INCLUDES = -I../tvm_runtime

# Default target
all: $(TEST_BIN)

# Build ELF file
$(TEST_ELF): $(SOURCES)
	$(RISCV_CC) $(CFLAGS) $(INCLUDES) -o $@ $(SOURCES) $(LDFLAGS)

# Build binary
$(TEST_BIN): $(TEST_ELF)
	$(RISCV_OBJCOPY) -O binary $< $@

# Run the test with simulator
run: $(TEST_BIN)
	@echo "Running $(TEST_NAME) test with $(CORES) cores, $(CYCLES) cycles..."
	@echo "Testing TVM Virtual Machine on RISC-V Emulator"
	@../../riscv_simulator --cores $(CORES) --cycles $(CYCLES) $(OPTIONS) --program $(TEST_BIN)

# Clean build artifacts
clean:
	rm -f $(TEST_ELF) $(TEST_BIN)

# Debug build (with debug symbols)
debug: CFLAGS += -DDEBUG -O0
debug: $(TEST_BIN)

# Quick test (fewer cycles)
test: CYCLES = 500000
test: run

# Performance test (more cycles)
perf: CYCLES = 2000000
perf: run

.PHONY: all run clean debug test perf