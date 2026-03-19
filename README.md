# RISC-V Emulator

A high-performance, multi-core RISC-V (RV32I/RV32IM) simulator written in C++. This emulator provides a complete simulation environment for RISC-V programs with support for virtual memory, caching, interrupts, and advanced profiling features.

## Features

### Core Functionality
- **RISC-V ISA Support**: RV32I base integer instruction set with M extension (multiply/divide)
- **Multi-Core Execution**: Configurable number of cores (default: 4, supports 1-8 cores)
- **Virtual Memory**: Full MMU support with Sv32 page table format
- **Memory Hierarchy**:
  - Per-core L1 instruction and data caches
  - Shared L2 cache
  - DDR memory controller with timing simulation
  - Cache coherence protocol (MESI-based)
- **Interrupt System**:
  - CLINT (Core-Local Interrupt Controller)
  - PLIC (Platform-Level Interrupt Controller)
  - Software, timer, and external interrupts

### Debugging & Profiling
- **Interactive Debugger**: Breakpoint support, step execution, register inspection
- **Disassembler**: Instruction trace output with human-readable assembly
- **Performance Profiling**: Instruction counts, memory access statistics, branch prediction
- **Function Profiling**: Per-function execution time, cache hit/miss rates, memory bandwidth
- **Memory Access Tracking**: Detailed tracking of all memory operations

### I/O & Devices
- **UART Device**: Serial input/output for program communication
- **MMIO Support**: Memory-mapped I/O device interface
- **Boot Sequence**: Multi-core boot synchronization with proper initialization

## Prerequisites

### Build Requirements
- **C++ Compiler**: g++ with C++17 support
- **RISC-V Toolchain**: riscv64-unknown-elf-gcc (for building test programs)
  - Available at: https://github.com/riscv-collab/riscv-gnu-toolchain

### Installation (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install build-essential gcc g++
```

### RISC-V Toolchain Installation
```bash
# Build from source
git clone https://github.com/riscv-collab/riscv-gnu-toolchain
cd riscv-gnu-toolchain
./configure --prefix=/opt/riscv
make
export PATH=$PATH:/opt/riscv/bin
```

## Building the Emulator

### Build Simulator Only
```bash
make all
```

This creates the `riscv_simulator` executable.

### Build Debug Version
```bash
make clean
make DEBUG=1 all
```

### Build All Test Programs
```bash
make build-tests
```

## Running Programs

### Quick Start (Default Test)
```bash
make
```
This runs the default `hello_rv32` test program.

### Run Specific Test
```bash
make run TEST_NAME=test_memcpy
```

### Run with Custom Options
```bash
make run TEST_NAME=test_vector_add CORES=2 CYCLES=5000
```

### Available Make Options
- **CORES=n**: Number of cores (default: 4)
- **CYCLES=n**: Maximum simulation cycles (default: 4000)
- **TRACE=true**: Enable instruction trace output
- **PROFILING=true**: Enable performance profiling
- **FUNC_PROFILING=true**: Enable function-level profiling
- **DEBUG=1**: Build debug version
- **DEBUG_LEVEL**: Set to CONCISE, VERBOSE, NORMAL, or OFF

### Run with Custom Program
```bash
./riscv_simulator --cores 2 --cycles 10000 --program path/to/program
```

### Run with Trace Output
```bash
make run TEST_NAME=hello_rv32 TRACE=true
```

### Disassemble Program
```bash
make disasm TEST_NAME=hello_rv32
```

## Command-Line Options

```
Usage: ./riscv_simulator [OPTIONS]

Options:
  --cores N              Number of cores (default: 2)
  --cycles N             Maximum cycles to execute (default: 1000)
  --memory SIZE          Memory size with K/M/G suffix (default: 2M)
  --program PATH         Program binary to load (default: hello_rv32)
  --trace                Enable instruction tracing
  --profiling            Enable performance profiling
  --func-profiling       Enable function profiling
  --func-profile-file    Load function profiles from CSV file
  --debug                Enable interactive debugger mode

Memory Size Examples:
  --memory 1M            1 Megabyte
  --memory 256K          256 Kilobytes
  --memory 0x200000      Hexadecimal size
```

## Test Programs

The emulator includes various test programs in `test_code/`:

| Test Name | Description |
|-----------|-------------|
| `hello_rv32` | Basic hello world program |
| `test_memcpy` | Memory copy operations |
| `test_rv32im` | Integer and multiply instructions |
| `test_atomic_operations` | Atomic memory operations (LR/SC) |
| `test_interrupt` | Interrupt handling |
| `test_boot_sequence` | Multi-core boot synchronization |
| `test_identity_mmu` | Identity-mapped virtual memory |
| `test_mmu_walk` | Page table walk testing |
| `test_mmu_page_fault` | Page fault handling |
| `test_satp` | SATP register and address translation |
| `test_wfi` | Wait-For-Interrupt instruction |
| `test_vector_add` | Simple vector addition |
| `test_function_profiling` | Function profiling demonstration |

### List All Available Tests
```bash
make list-testcases
```

### Run All Tests
```bash
make run-tests
```

## Writing Your Own Programs

### Program Structure

Test programs should follow this structure:
```
test_code/
  my_test/
    Makefile
    my_test.c
```

### Example Makefile
```makefile
RISC_PREFIX ?= riscv64-unknown-elf-
CC = $(RISC_PREFIX)gcc
OBJCOPY = $(RISC_PREFIX)objcopy
OBJDUMP = $(RISC_PREFIX)objdump

CFLAGS = -Wall -O2 -g -march=rv32imac -mabi=ilp32 -ffreestanding
LDFLAGS = -nostartfiles -Wl,-Ttext=0x0

all: my_test

my_test: my_test.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@.elf $<
	$(OBJCOPY) -O binary $@.elf $@
	$(OBJDUMP) -d $@.elf > $@.dis

run: my_test
	../../riscv_simulator --cores 2 --cycles 5000 --program my_test

clean:
	rm -f *.elf *.bin *.dis

.PHONY: all run clean
```

### Example C Program
```c
// my_test.c
volatile unsigned int* uart = (unsigned int*)0x10000000;

void print_char(char c) {
    *uart = (unsigned int)c;
}

void print_string(const char* s) {
    while (*s) {
        print_char(*s++);
    }
}

int main() {
    print_string("Hello from RISC-V!\n");
    return 0;
}
```

### Bare-Metal Library

The emulator provides a bare-metal C library in `software/init_code/`:
- **bare-metal.c**: `_sbrk()`, `memset()`, `memcpy()`, `strlen()`, etc.
- **uart.c**: UART driver for serial I/O
- **vprintf.c**: `printf()` implementation

Include these in your test Makefile:
```makefile
C_FILES = my_test.c ../../software/init_code/bare-metal.c \
          ../../software/init_code/uart.c \
          ../../software/init_code/vprintf.c

INCLUDES = -I../../software/init_code
```

## Memory Map

| Address Range | Description |
|---------------|-------------|
| 0x00000000 - 0x0FFFFFFF | Main memory (program code and data) |
| 0x10000000 - 0x1000000F | UART device |
| 0x02000000 - 0x02002000 | CLINT (Core-Local Interrupt Controller) |
| 0x0C000000 - 0x0C000FFF | PLIC (Platform-Level Interrupt Controller) |

## Debugging

### Interactive Debugger
```bash
./riscv_simulator --program my_test --debug
```

Debugger commands:
- `s` or `step`: Execute one instruction on each core
- `c` or `continue`: Continue execution
- `r` or `regs`: Print all registers
- `b <address>`: Set breakpoint
- `q` or `quit`: Exit debugger

### Trace Output
```bash
make run TEST_NAME=my_test TRACE=true > trace.log 2>&1
```

### Debug Build
```bash
make clean
make DEBUG=1 DEBUG_LEVEL=VERBOSE all
```

## Profiling

### Performance Profiling
```bash
./riscv_simulator --program my_test --profiling
```
Outputs:
- Per-core instruction counts
- Memory access statistics
- Cache hit/miss rates (L1 I-cache, L1 D-cache, L2)
- Branch prediction statistics
- DDR memory bandwidth and latency

### Function Profiling
```bash
./riscv_simulator --program my_test --func-profiling
```

Or with a function profile file:
```bash
./riscv_simulator --program my_test --func-profiling --func-profile-file functions.csv
```

Function profile CSV format:
```csv
# core_id,function_name,start_address,end_address
0,main,0x0,0x100
0,foo,0x100,0x200
1,bar,0x200,0x300
```

## Project Structure
```
.
├── emulator/
│   ├── include/
│   │   ├── core/          Core execution engine
│   │   ├── memory/        MMU, Cache, DDR, Coherence
│   │   ├── interrupt/     CLINT, PLIC
│   │   ├── device/        UART and other devices
│   │   ├── system/        Simulator, MultiCoreMonitor
│   │   ├── utils/         Profiling and debugging tools
│   │   └── debug/         Debugger and Disassembler
│   └── src/               Implementation files
├── software/
│   └── init_code/         Bare-metal C library
├── test_code/             Test programs
└── Makefile               Top-level build system
```

## Limitations

- Only RV32I and RV32IM instruction sets supported (no RV64, no A/F/D extensions)
- Sv32 page table format only (39-bit virtual addresses)
- Maximum 8 cores
- No floating-point support
- No compressed instructions (RV16C)

## Troubleshooting

### Program Loads but Produces No Output
- Increase cycle count: `make run CYCLES=10000`
- Enable trace to see execution: `make run TRACE=true`
- Check UART output is properly configured

### Build Errors
- Ensure g++ supports C++17: `g++ --version`
- Check include paths in Makefile
- Verify RISC-V toolchain is installed

### Simulation Timeout
- The emulator has a 300-second timeout to prevent infinite loops
- If your program legitimately needs more time, modify `main.cpp:226`

## Contributing

Contributions are welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Submit a pull request

## License

GNU GENERAL PUBLIC LICENSE
Version 3, 29 June 2007

Copyright (C) 2025 RISC-V Emulator Contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.

The full text of the GNU General Public License v3 can be found at:
https://www.gnu.org/licenses/gpl-3.0.txt
