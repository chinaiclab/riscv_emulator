# Makefile for test_interrupt
TEST_NAME = test_interrupt

# RISC-V toolchain
CC = riscv64-unknown-elf-gcc
OBJCOPY = riscv64-unknown-elf-objcopy

# Compiler flags
CFLAGS = -march=rv32ima_zicsr -mabi=ilp32 -ffreestanding -nostdlib -nostartfiles -g
LDFLAGS = -Wl,-T,../../software/init_code/ldscript.ld

# Source files
SOURCES = ../../software/init_code/start.S main.c ../../software/init_code/bare-metal.c ../../software/init_code/uart.c ../../software/init_code/vprintf.c

# Default target
all: $(TEST_NAME)

# Build ELF file
$(TEST_NAME).elf: $(SOURCES)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^

# Build binary file
$(TEST_NAME): $(TEST_NAME).elf
	$(OBJCOPY) -O binary --set-section-flags .core0_boot=alloc,load,code $< $@

# Clean
clean:
	rm -f $(TEST_NAME).elf $(TEST_NAME)

.PHONY: all clean