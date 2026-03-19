# PyTorch模型在RISC-V上运行的测试用例

# 获取当前目录名作为测试名
TEST_NAME := $(shell basename $(shell pwd))

# 编译器设置
CXX = riscv64-unknown-elf-g++
CC = riscv64-unknown-elf-gcc

# 编译选项
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra
CFLAGS = -std=c99 -O2 -Wall -Wextra
LDFLAGS = -nostdlib -nodefaultlibs -static

# 包含路径
INCLUDES = -I../../../emulator/include -I../../../software/init_code

# 链接脚本
LD_SCRIPT = ../../../software/init_code/ldscript.ld

# 源文件
C_SOURCES = main.c
CPP_SOURCES = ../../../emulator/src/core/AIExtensions.cpp
STARTUP_S = ../../../software/init_code/start.S

# 目标文件
OBJECTS = $(C_SOURCES:.c=.o) $(CPP_SOURCES:.cpp=.o) $(STARTUP_S:.S=.o)
TARGET = $(TEST_NAME)

# 默认目标
all: $(TARGET).elf

# 编译C文件
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 编译C++文件
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# 编译汇编文件
%.o: %.S
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# 链接生成ELF文件
$(TARGET).elf: $(OBJECTS)
	$(CC) $(LDFLAGS) -T$(LD_SCRIPT) $(OBJECTS) -o $@

# 生成二进制文件
$(TARGET): $(TARGET).elf
	riscv64-unknown-elf-objcopy -O binary $(TARGET).elf $@

# 运行测试
run: $(TARGET).elf
	@echo "Running PyTorch model inference on RISC-V emulator..."
	@echo "Command: ../../../riscv_simulator --cores 1 --cycles 1000000 --program $(TARGET).elf"
	../../../riscv_simulator --cores 1 --cycles 1000000 --program $(TARGET).elf

# 调试运行（输出到日志）
debug: $(TARGET).elf
	@echo "Running PyTorch model with debug output..."
	../../../riscv_simulator --cores 1 --cycles 1000000 --trace --program $(TARGET).elf > pytorch_debug.log 2>&1
	@echo "Debug output saved to pytorch_debug.log"

# 分析日志
analyze: pytorch_debug.log
	@echo "Analyzing PyTorch execution..."
	@grep -E "(AI|Performance|cycles|Convolution|ReLU|Matrix)" pytorch_debug.log | head -20
	@echo ""
	@echo "Finding AI instructions..."
	@grep -E "(0x.*1011011|TCONV|TMUL)" pytorch_debug.log | head -10 || echo "No AI instructions found"

# 性能测试
benchmark: $(TARGET).elf
	@echo "Running PyTorch model benchmark..."
	@for i in 1 2 3; do \
		echo "Run $$i:"; \
		../../../riscv_simulator --cores 1 --cycles 1000000 --program $(TARGET).elf | grep -E "(cycles|Performance|Execution time)"; \
		echo "---"; \
	done

# 检查模型结构
check-model:
	@echo "Checking PyTorch model structure..."
	@if [ -f "model.py" ]; then \
		python3 model.py --check; \
	else \
		echo "No model.py found"; \
	fi

# 生成模型可视化
visualize:
	@echo "Generating model visualization..."
	@if [ -f "model.py" ]; then \
		python3 model.py --visualize; \
	else \
		echo "No model.py found"; \
	fi

# 反汇编
disasm: $(TARGET).elf
	riscv64-unknown-elf-objdump -d -S $(TARGET).elf > $(TEST_NAME)_disasm.log
	@echo "Disassembly saved to $(TEST_NAME)_disasm.log"
	@echo "AI instruction search:"
	@grep -E "(1011011|tconv|tmul)" $(TEST_NAME)_disasm.log || echo "No AI instructions in disassembly"

# 获取符号表
symbols: $(TARGET).elf
	riscv64-unknown-elf-objdump -t $(TARGET).elf > $(TEST_NAME)_symbols.log
	@echo "Symbol table saved to $(TEST_NAME)_symbols.log"

# 内存映射分析
memory-map: $(TARGET).elf
	riscv64-unknown-elf-objdump -h $(TARGET).elf
	@echo ""
	@echo "AI extension memory usage:"
	@echo "  Model parameters: ~2KB"
	@echo "  Input data: ~12KB (1x3x32x32)"
	@echo "  Output data: ~160B (10 classes)"
	@echo "  Temporary buffers: ~64KB"

# 比较标准实现
compare-standard:
	@echo "Compiling with standard C library for comparison..."
	$(CC) $(CFLAGS) -DUSE_STANDARD_LIB $(INCLUDES) main.c -o $(TEST_NAME)_standard
	@echo "Running standard version for comparison..."
	./$(TEST_NAME)_standard

# 生成测试报告
report: run disasm symbols
	@echo "Generating test report..."
	@echo "=== PyTorch on RISC-V Test Report ===" > test_report.txt
	@echo "Date: $$(date)" >> test_report.txt
	@echo "Target: RISC-V with AI extensions" >> test_report.txt
	@echo "" >> test_report.txt
	@echo "Performance Results:" >> test_report.txt
	@grep -E "(cycles|CPI|Execution time)" pytorch_debug.log >> test_report.txt
	@echo "" >> test_report.txt
	@echo "AI Operations:" >> test_report.txt
	@grep -E "(AI operations|MAC operations)" pytorch_debug.log >> test_report.txt
	@echo "" >> test_report.txt
	@echo "File sizes:" >> test_report.txt
	@ls -lh $(TARGET).elf $(TARGET) >> test_report.txt
	@echo "Report saved to test_report.txt"

# 清理
clean:
	rm -f *.o *.elf $(TARGET) *.log *.txt

# 清理所有
clean-all: clean
	rm -f *.pyc *.png *.svg model_data/

# 帮助
help:
	@echo "Available targets:"
	@echo "  all          - Build the test program"
	@echo "  run          - Run the test on RISC-V emulator"
	@echo "  debug        - Run with trace output to log file"
	@echo "  analyze      - Analyze debug log for AI operations"
	@echo "  benchmark    - Run performance benchmarks"
	@echo "  disasm       - Generate disassembly listing"
	@echo "  symbols      - Generate symbol table"
	@echo "  memory-map   - Show memory mapping"
	@echo "  report       - Generate comprehensive test report"
	@echo "  clean        - Remove build artifacts"
	@echo "  clean-all    - Remove all generated files"

.PHONY: all run debug analyze benchmark check-model visualize disasm symbols memory-map compare-standard report clean clean-all help