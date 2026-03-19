#ifndef AI_EXTENSIONS_H
#define AI_EXTENSIONS_H

#include <cstdint>
#include <vector>
#include <memory>

// RISC-V AI扩展指令定义
namespace RISCV_AI {

// AI扩展指令编码格式
// opcode[6:0] = 0b1011011 (custom-0 opcode)
// funct3[2:0], funct7[6:0] 用于AI指令子类型
enum AI_OPCODE {
    AI_TCONV     = 0b000,  // 张量卷积
    AI_TMUL      = 0b001,  // 张量矩阵乘法
    AI_TADD      = 0b010,  // 张量加法
    AI_TRELU     = 0b011,  // ReLU激活
    AI_TPOOL     = 0b100,  // 池化操作
    AI_TSIGMOID  = 0b101,  // Sigmoid激活
    AI_TSOFTMAX  = 0b110,  // Softmax
    AI_TNORM     = 0b111   // 归一化
};

// 张量描述符
struct TensorDesc {
    uint32_t addr;        // 数据地址
    uint16_t dims[4];     // 维度 [batch, height, width, channels]
    uint8_t dtype;        // 数据类型 (0=fp32, 1=fp16, 2=int8, 3=uint8)
    uint8_t layout;       // 数据布局 (0=NCHW, 1=NHWC)

    uint32_t total_elements() const {
        return dims[0] * dims[1] * dims[2] * dims[3];
    }

    uint32_t size_bytes() const {
        uint32_t element_size = (dtype == 0) ? 4 : ((dtype == 1) ? 2 : 1);
        return total_elements() * element_size;
    }
};

// 卷积操作参数
struct ConvParams {
    TensorDesc input, output, weight, bias;
    uint16_t kernel_h, kernel_w;
    uint16_t stride_h, stride_w;
    uint16_t pad_h, pad_w;
    uint16_t dilation_h, dilation_w;
    uint8_t groups;
};

// 矩阵乘法参数
struct MatMulParams {
    TensorDesc A, B, C;
    bool transA, transB;
    uint8_t accumulator_dtype;  // 累加器数据类型
};

// 池化操作参数
struct PoolParams {
    TensorDesc input, output;
    uint16_t pool_h, pool_w;
    uint16_t stride_h, stride_w;
    uint16_t pad_h, pad_w;
    bool count_include_pad;
};

// AI计算单元接口
class AIComputeUnit {
public:
    virtual ~AIComputeUnit() = default;

    // 指令执行接口
    virtual void execute_tconv(const ConvParams& params) = 0;
    virtual void execute_tmul(const MatMulParams& params) = 0;
    virtual void execute_tadd(const TensorDesc& dst, const TensorDesc& src1, const TensorDesc& src2) = 0;
    virtual void execute_trelu(const TensorDesc& tensor) = 0;
    virtual void execute_tpool(const PoolParams& params, bool max_pooling) = 0;
    virtual void execute_tsigmoid(const TensorDesc& tensor) = 0;
    virtual void execute_tsoftmax(const TensorDesc& tensor) = 0;
    virtual void execute_tnorm(const TensorDesc& tensor, float eps = 1e-5f) = 0;

    // 性能统计
    struct PerformanceStats {
        uint64_t total_ops = 0;
        uint64_t total_time_cycles = 0;
        uint64_t memory_accesses = 0;
        double ops_per_cycle = 0.0;

        void update(uint64_t ops, uint64_t cycles, uint64_t mem_accesses) {
            total_ops += ops;
            total_time_cycles += cycles;
            memory_accesses += mem_accesses;
            ops_per_cycle = static_cast<double>(total_ops) / total_time_cycles;
        }
    };

    virtual const PerformanceStats& get_stats() const = 0;
    virtual void reset_stats() = 0;
};

// AI指令解码器
class AIInstructionDecoder {
public:
    static uint32_t get_opcode(uint32_t instruction) {
        return instruction & 0x7F;
    }

    static uint32_t get_funct3(uint32_t instruction) {
        return (instruction >> 12) & 0x7;
    }

    static uint32_t get_funct7(uint32_t instruction) {
        return (instruction >> 25) & 0x7F;
    }

    static uint32_t get_rs1(uint32_t instruction) {
        return (instruction >> 15) & 0x1F;
    }

    static uint32_t get_rs2(uint32_t instruction) {
        return (instruction >> 20) & 0x1F;
    }

    static uint32_t get_rd(uint32_t instruction) {
        return (instruction >> 7) & 0x1F;
    }

    // 检查是否为AI指令
    static bool is_ai_instruction(uint32_t instruction) {
        return get_opcode(instruction) == 0b1011011;  // custom-0
    }
};

// 性能计数器扩展
class AIPerformanceCounters {
public:
    enum Counter {
        AI_CYCLES = 0xB10,        // AI计算周期
        AI_OPS_TOTAL = 0xB11,     // 总操作数
        AI_MAC_OPS = 0xB12,       // MAC操作数
        AI_MEMORY_ACCESSES = 0xB13,// 内存访问次数
        AI_CACHE_HITS = 0xB14,    // 缓存命中
        AI_CACHE_MISSES = 0xB15,  // 缓存未命中
        AI_INSTRUCTIONS = 0xB16   // AI指令数
    };

    static void write_counter(Counter counter, uint32_t value);
    static uint32_t read_counter(Counter counter);
    static void increment_counter(Counter counter, uint32_t delta = 1);
};

} // namespace RISCV_AI

#endif // AI_EXTENSIONS_H