# RISC-V Simulator Makefile

# ===== DEFAULT OPTIONS =====
# Compiler settings
CXX = g++
DEBUG ?= 0
DEBUG_LEVEL ?= CONCISE
ifeq ($(DEBUG), 1)
    CXXFLAGS = -std=c++17 -Wall -Wextra -O0 -g -Iemulator/include -Iemulator/include/memory -Iemulator/include/utils -Iemulator/include/interrupt -Iemulator/include/device -Iemulator/include/system -Iemulator/include/debug -DDEBUG=1 -pthread
    # Set debug level based on DEBUG_LEVEL variable
    ifeq ($(DEBUG_LEVEL), CONCISE)
        CXXFLAGS += -DDEBUG_LEVEL_CONCISE
    else ifeq ($(DEBUG_LEVEL), VERBOSE)
        CXXFLAGS += -DDEBUG_LEVEL_VERBOSE
    else ifeq ($(DEBUG_LEVEL), OFF)
        CXXFLAGS += -DDEBUG_LEVEL_OFF
    else
        CXXFLAGS += -DDEBUG_LEVEL_NORMAL
    endif
else
    CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Iemulator/include -Iemulator/include/memory -Iemulator/include/utils -Iemulator/include/interrupt -Iemulator/include/device -Iemulator/include/system -Iemulator/include/debug -DDEBUG=0 -pthread
endif

# Simulator/Emulator settings
CORES ?= 4
CYCLES ?= 4000
OPTIONS ?=
TRACE ?= false
PROFILING ?= false
FUNC_PROFILING ?= false

# Source files
SIM_SRC = $(wildcard emulator/src/*.cpp) $(wildcard emulator/src/*/*.cpp)
# Include debug source files
SIM_SRC += emulator/src/debug/Debugger.cpp emulator/src/debug/Disassembler.cpp
SIM_TARGET = riscv_simulator

# Test directories and names
TEST_DIRS = $(wildcard test_code/*)
TEST_NAMES = $(notdir $(TEST_DIRS))

# ===== HELP INFORMATION =====
# Show help information
help:
	@echo "RISC-V Emulator Makefile"
	@echo ""
	@echo "Main targets:"
	@echo "  make                    - Run hello_rv32 test (default)"
	@echo "  make all                - Build simulator only"
	@echo "  make run TEST_NAME=name - Run specific test"
	@echo "  make build-tests        - Build all tests"
	@echo "  make run-tests          - Run all tests"
	@echo "  make run-program PROGRAM=path - Run with program"
	@echo "  make clean              - Clean build"
	@echo "  make disasm PROGRAM=path - Disassemble program"
	@echo ""
	@echo "Options:"
	@echo "  CORES=n        - Number of cores (default: 4)"
	@echo "  CYCLES=n       - Number of cycles (default: 4000)"
	@echo "  TRACE=true     - Enable tracing"
	@echo "  PROFILING=true  - Enable profiling"
	@echo "  FUNC_PROFILING=true - Enable function profiling"

# List all available test cases
list-testcases:
	@echo "Available test cases:"
	@for name in $(TEST_NAMES); do \
		echo "  $$name"; \
	done

# ===== BUILD TARGETS =====
# Default target
all: $(SIM_TARGET)

# Run test (default target)
.DEFAULT:
	@$(MAKE) run

# Build simulator
$(SIM_TARGET): $(SIM_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^

# Build all tests
build-tests:
	@for dir in $(TEST_DIRS); do \
		echo "Building test in $$dir"; \
		$(MAKE) -C $$dir all || exit 1; \
	done

# ===== RUN TARGETS =====

# Build simulator options from boolean flags
BUILD_OPTIONS = $(OPTIONS)
ifeq ($(TRACE),true)
    BUILD_OPTIONS += --trace
endif
ifeq ($(PROFILING),true)
    BUILD_OPTIONS += --profiling
endif
ifeq ($(FUNC_PROFILING),true)
    BUILD_OPTIONS += --func-profiling
endif

# Run a test with customizable options (defaults to hello_rv32 if no TEST_NAME provided)
# Usage: make run [TEST_NAME=hello_rv32] [OUTPUT=benos.elf] [CORES=4] [CYCLES=4000] [TRACE=true] [DISASM=true]
# OUTPUT: Optional, specify output file name if different from TEST_NAME (e.g., benos.elf for chapter6 tests)
run: $(SIM_TARGET)
	@if [ -z "$(TEST_NAME)" ]; then \
		echo "No TEST_NAME provided, defaulting to hello_rv32..."; \
		TEST_NAME=hello_rv32; \
		$(MAKE) -C test_code/$$TEST_NAME all; \
		if [ "$(TRACE)" = "true" ]; then \
			echo "Running test $$TEST_NAME with trace output..."; \
			./$(SIM_TARGET) --cores $(CORES) --cycles $(CYCLES) $(BUILD_OPTIONS) --program test_code/$$TEST_NAME/$$TEST_NAME 2>&1 | tee trace_output.log; \
		else \
			./$(SIM_TARGET) --cores $(CORES) --cycles $(CYCLES) $(BUILD_OPTIONS) --program test_code/$$TEST_NAME/$$TEST_NAME; \
		fi; \
	else \
		OUTPUT_FILE=$(if $(OUTPUT),$(OUTPUT),$(TEST_NAME)); \
		echo "Running test $(TEST_NAME) with $(CORES) cores, $(CYCLES) cycles, and options: $(BUILD_OPTIONS)..."; \
		echo "Output file: $$OUTPUT_FILE"; \
		$(MAKE) -C test_code/$(TEST_NAME) all; \
		if [ "$(TRACE)" = "true" ]; then \
			echo "Running with trace output to screen and trace_output.log..."; \
			./$(SIM_TARGET) --cores $(CORES) --cycles $(CYCLES) $(BUILD_OPTIONS) --program test_code/$(TEST_NAME)/$$OUTPUT_FILE 2>&1 | tee trace_output.log; \
		else \
			./$(SIM_TARGET) --cores $(CORES) --cycles $(CYCLES) $(BUILD_OPTIONS) --program test_code/$(TEST_NAME)/$$OUTPUT_FILE; \
		fi; \
	fi

# Run simulator with custom program (usage: make run-program PROGRAM=path/to/program)
run-program: $(SIM_TARGET)
	@echo "Running simulator with program $(PROGRAM)..."
	@./$(SIM_TARGET) --cores $(CORES) --cycles $(CYCLES) --program $(PROGRAM)

# Run all tests
run-tests: build-tests
	@for dir in $(TEST_DIRS); do \
		echo "Running test in $$dir"; \
		$(MAKE) -C $$dir run || exit 1; \
	done

# Build and run individual tests
$(TEST_NAMES):
	@echo "Building and running test: $@"
	@$(MAKE) -C test_code/$@ all
	@$(MAKE) -C test_code/$@ run

# ===== DEBUG TARGETS =====
disasm: $(SIM_TARGET)
	@if [ -z "$(TEST_NAME)" ] && [ -z "$(PROGRAM)" ]; then \
		echo "Usage: make disasm TEST_NAME=test_name OR make disasm PROGRAM=path/to/program"; \
	elif [ -n "$(TEST_NAME)" ]; then \
		echo "Disassembling $(TEST_NAME)..."; \
		$(MAKE) -C test_code/$(TEST_NAME) all; \
		riscv64-unknown-elf-objdump -d -S test_code/$(TEST_NAME)/$(TEST_NAME).elf > $(TEST_NAME)_disasm.log && echo "Disassembly saved to $(TEST_NAME)_disasm.log"; \
	elif [ -n "$(PROGRAM)" ]; then \
		echo "Disassembling $(PROGRAM)..."; \
		riscv64-unknown-elf-objdump -d -S $(PROGRAM) > program_disasm.log && echo "Disassembly saved to program_disasm.log"; \
	fi


# ===== CLEANUP TARGETS =====
clean:
	rm -f $(SIM_TARGET)
	@for dir in $(TEST_DIRS); do \
		$(MAKE) -C $$dir clean; \
	done

clean-all: clean
	rm -f *.txt *.log

.PHONY: all build-tests run-tests $(TEST_NAMES) disasm clean clean-all run run-program list-testcases help
