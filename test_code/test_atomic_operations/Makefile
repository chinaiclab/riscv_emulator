# Makefile for test case - automatically extracts name from directory

# RISC-V compiler settings
CC = riscv64-unknown-elf-gcc
OBJCOPY = riscv64-unknown-elf-objcopy
CFLAGS = -march=rv32ima_zicsr -mabi=ilp32 -ffreestanding -nostartfiles -g

# Extract test name from directory name
TEST_NAME := $(notdir $(abspath $(dir $(lastword $(MAKEFILE_LIST)))))
TEST_TARGET = $(TEST_NAME)

# Default emulator parameters
CORES ?= 4
CYCLES ?= 100000
OPTIONS ?=

# Default target
all: $(TEST_TARGET)

# Build test - compile RISC-V binary
$(TEST_TARGET): main.c
	$(CC) $(CFLAGS) -Wl,-T,../../software/init_code/ldscript.ld -nostdlib -o $(TEST_TARGET).elf ../../software/init_code/start.S main.c ../../software/init_code/bare-metal.c ../../software/init_code/uart.c ../../software/init_code/vprintf.c
	$(OBJCOPY) -O binary $(TEST_TARGET).elf $(TEST_TARGET)

# Run the test
run: $(TEST_TARGET)
	@echo "Running $(TEST_NAME) test with $(CORES) cores, $(CYCLES) cycles, and options: $(OPTIONS)..."
	@../../riscv_simulator --cores $(CORES) --cycles $(CYCLES) --program $(TEST_TARGET) $(OPTIONS)

# Clean build artifacts
clean:
	rm -f $(TEST_TARGET) $(TEST_TARGET).elf

.PHONY: all run clean