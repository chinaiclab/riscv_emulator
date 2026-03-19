/*
 * PyTorch模型在RISC-V上的运行时测试
 * 使用TVM生成的代码和AI扩展指令
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>

// AI扩展指令支持
#include "../../../emulator/include/core/AIExtensions.h"

// 模型参数定义
#define INPUT_SIZE (1 * 3 * 32 * 32)
#define HIDDEN_SIZE 128
#define OUTPUT_SIZE 10

// 内存布局
#define MODEL_BASE 0x10000
#define INPUT_ADDR 0x20000
#define OUTPUT_ADDR 0x30000
#define TEMP_ADDR 0x40000

// 简化的PyTorch模型参数结构
typedef struct {
    // 卷积层1参数 (3x3x3 -> 16)
    float conv1_weight[16][3][3][3];
    float conv1_bias[16];

    // 卷积层2参数 (16x3x3 -> 32)
    float conv2_weight[32][16][3][3];
    float conv2_bias[32];

    // 全连接层参数 (32 -> 10)
    float fc_weight[10][32];
    float fc_bias[10];
} PyTorchModel;

// RISC-V AI性能计数器访问
static inline void riscv_write_pmc(uint32_t addr, uint32_t value) {
    #ifdef __riscv
    asm volatile ("csrw %0, %1" : : "I" (addr), "r" (value));
    #endif
}

static inline uint32_t riscv_read_pmc(uint32_t addr) {
    uint32_t value;
    #ifdef __riscv
    asm volatile ("csrr %0, %1" : "=r" (value) : "I" (addr));
    #else
    value = 0; // 模拟器环境下的默认值
    #endif
    return value;
}

// 模拟TVM运行时API
typedef struct {
    void* handle;
    uint32_t model_size;
    uint32_t input_size;
    uint32_t output_size;
} TVMModule;

// 初始化简单的PyTorch模型
void init_pytorch_model(PyTorchModel* model) {
    printf("Initializing PyTorch model...\n");

    // 初始化卷积层1参数（使用简单模式）
    for (int i = 0; i < 16; i++) {
        model->conv1_bias[i] = 0.1f * i;
        for (int j = 0; j < 3; j++) {
            for (int k = 0; k < 3; k++) {
                for (int l = 0; l < 3; l++) {
                    model->conv1_weight[i][j][k][l] = (float)(i + j + k + l) * 0.1f;
                }
            }
        }
    }

    // 初始化卷积层2参数
    for (int i = 0; i < 32; i++) {
        model->conv2_bias[i] = 0.05f * i;
        for (int j = 0; j < 16; j++) {
            for (int k = 0; k < 3; k++) {
                for (int l = 0; l < 3; l++) {
                    model->conv2_weight[i][j][k][l] = (float)(i * j + k + l) * 0.05f;
                }
            }
        }
    }

    // 初始化全连接层参数
    for (int i = 0; i < 10; i++) {
        model->fc_bias[i] = 0.01f * i;
        for (int j = 0; j < 32; j++) {
            model->fc_weight[i][j] = (float)(i + j) * 0.01f;
        }
    }

    printf("Model initialized successfully\n");
}

// 使用RISC-V AI指令执行卷积
void riscv_conv2d_ai(const float* input, const float* weight, const float* bias,
                     float* output, int in_channels, int out_channels,
                     int input_size, int kernel_size) {

    using namespace RISCV_AI;

    printf("Executing convolution with RISC-V AI instructions...\n");

    // 设置张量描述符
    TensorDesc input_desc = {INPUT_ADDR, {1, input_size, input_size, in_channels}, 0, 0};
    TensorDesc output_desc = {OUTPUT_ADDR, {1, input_size, input_size, out_channels}, 0, 0};
    TensorDesc weight_desc = {MODEL_BASE, {out_channels, in_channels, kernel_size, kernel_size}, 0, 0};
    TensorDesc bias_desc = {MODEL_BASE + 0x1000, {out_channels}, 0, 0};

    // 设置卷积参数
    ConvParams conv_params = {
        .input = input_desc,
        .output = output_desc,
        .weight = weight_desc,
        .bias = bias_desc,
        .kernel_h = kernel_size,
        .kernel_w = kernel_size,
        .stride_h = 1,
        .stride_w = 1,
        .pad_h = 1,
        .pad_w = 1,
        .dilation_h = 1,
        .dilation_w = 1,
        .groups = 1
    };

    // 创建AI计算单元并执行
    auto ai_unit = RISCV_AI::createAIComputeUnit((uint8_t*)0x8000000);

    // 模拟内存数据（在实际环境中数据已在指定地址）
    uint8_t* memory_base = (uint8_t*)0x8000000;
    memcpy(memory_base + INPUT_ADDR, input, INPUT_SIZE * sizeof(float));
    memcpy(memory_base + MODEL_BASE, weight, out_channels * in_channels * kernel_size * kernel_size * sizeof(float));
    if (bias) {
        memcpy(memory_base + MODEL_BASE + 0x1000, bias, out_channels * sizeof(float));
    }

    // 执行卷积
    ai_unit->execute_tconv(conv_params);

    // 拷贝结果
    memcpy(output, memory_base + OUTPUT_ADDR, out_channels * input_size * input_size * sizeof(float));

    // 更新性能计数器
    RISCV_AI::AIPerformanceCounters::increment_counter(RISCV_AI::AIPerformanceCounters::AI_INSTRUCTIONS, 1);
    RISCV_AI::AIPerformanceCounters::increment_counter(RISCV_AI::AIPerformanceCounters::AI_MAC_OPS,
                                                      input_size * input_size * out_channels * in_channels * kernel_size * kernel_size);
}

// 使用RISC-V AI指令执行ReLU激活
void riscv_relu_ai(float* data, int size) {
    using namespace RISCV_AI;

    printf("Executing ReLU activation with RISC-V AI instructions...\n");

    TensorDesc tensor_desc = {TEMP_ADDR, {1, 1, 1, size}, 0, 0};

    auto ai_unit = RISCV_AI::createAIComputeUnit((uint8_t*)0x8000000);

    // 模拟内存数据
    uint8_t* memory_base = (uint8_t*)0x8000000;
    memcpy(memory_base + TEMP_ADDR, data, size * sizeof(float));

    // 执行ReLU
    ai_unit->execute_trelu(tensor_desc);

    // 拷贝结果
    memcpy(data, memory_base + TEMP_ADDR, size * sizeof(float));
}

// 使用RISC-V AI指令执行矩阵乘法
void riscv_matmul_ai(const float* A, const float* B, float* C, int m, int n, int k) {
    using namespace RISCV_AI;

    printf("Executing matrix multiplication with RISC-V AI instructions...\n");

    TensorDesc A_desc = {TEMP_ADDR, {m, k, 1, 1}, 0, 0};
    TensorDesc B_desc = {TEMP_ADDR + 0x1000, {k, n, 1, 1}, 0, 0};
    TensorDesc C_desc = {TEMP_ADDR + 0x2000, {m, n, 1, 1}, 0, 0};

    MatMulParams matmul_params = {
        .A = A_desc,
        .B = B_desc,
        .C = C_desc,
        .transA = false,
        .transB = false,
        .accumulator_dtype = 0  // float32
    };

    auto ai_unit = RISCV_AI::createAIComputeUnit((uint8_t*)0x8000000);

    uint8_t* memory_base = (uint8_t*)0x8000000;
    memcpy(memory_base + TEMP_ADDR, A, m * k * sizeof(float));
    memcpy(memory_base + TEMP_ADDR + 0x1000, B, k * n * sizeof(float));

    ai_unit->execute_tmul(matmul_params);

    memcpy(C, memory_base + TEMP_ADDR + 0x2000, m * n * sizeof(float));
}

// 执行完整的PyTorch模型推理
void run_pytorch_inference(const PyTorchModel* model, const float* input, float* output) {
    printf("Starting PyTorch model inference on RISC-V...\n");

    // 开始性能计数
    riscv_write_pmc(0xB00, 0);  // 重置周期计数器
    riscv_write_pmc(0xB01, 0);  // 重置指令计数器

    float conv1_output[16][32][32];
    float conv2_output[32][16][16];
    float pooled_output[32][8][8];
    float fc_input[32];
    float fc_output[10];

    // 开始性能计数
    RISCV_AI::AIPerformanceCounters::write_counter(RISCV_AI::AIPerformanceCounters::AI_CYCLES, 0);
    RISCV_AI::AIPerformanceCounters::write_counter(RISCV_AI::AIPerformanceCounters::AI_OPS_TOTAL, 0);

    printf("Step 1: Convolution Layer 1\n");
    // 卷积层1: input(1,3,32,32) -> output(1,16,32,32)
    riscv_conv2d_ai(input, (float*)model->conv1_weight, model->conv1_bias,
                    (float*)conv1_output, 3, 16, 32, 3);

    printf("Step 2: ReLU Activation 1\n");
    // ReLU激活1
    riscv_relu_ai((float*)conv1_output, 16 * 32 * 32);

    printf("Step 3: Convolution Layer 2\n");
    // 卷积层2: (1,16,32,32) -> (1,32,16,16) with stride=2
    riscv_conv2d_ai((float*)conv1_output, (float*)model->conv2_weight, model->conv2_bias,
                    (float*)conv2_output, 16, 32, 32, 3);

    printf("Step 4: ReLU Activation 2\n");
    // ReLU激活2
    riscv_relu_ai((float*)conv2_output, 32 * 16 * 16);

    printf("Step 5: Average Pooling\n");
    // 平均池化: (1,32,16,16) -> (1,32,8,8)
    for (int c = 0; c < 32; c++) {
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                float sum = 0.0f;
                for (int pi = 0; pi < 2; pi++) {
                    for (int pj = 0; pj < 2; pj++) {
                        sum += conv2_output[c][i*2+pi][j*2+pj];
                    }
                }
                pooled_output[c][i][j] = sum / 4.0f;
            }
        }
    }

    printf("Step 6: Global Average Pooling\n");
    // 全局平均池化: (1,32,8,8) -> (32)
    for (int c = 0; c < 32; c++) {
        float sum = 0.0f;
        for (int i = 0; i < 8; i++) {
            for (int j = 0; j < 8; j++) {
                sum += pooled_output[c][i][j];
            }
        }
        fc_input[c] = sum / 64.0f;
    }

    printf("Step 7: Fully Connected Layer\n");
    // 全连接层: (32) -> (10)
    riscv_matmul_ai(fc_input, (float*)model->fc_weight, fc_output, 1, 10, 32);

    printf("Step 8: Add Bias and Final Activation\n");
    // 添加偏置并输出
    for (int i = 0; i < 10; i++) {
        fc_output[i] += model->fc_bias[i];
        output[i] = fc_output[i];  // 线性输出
    }

    // 结束性能计数
    uint32_t cycles = riscv_read_pmc(0xB00);
    uint32_t instructions = riscv_read_pmc(0xB01);

    printf("Performance metrics:\n");
    printf("  Total cycles: %u\n", cycles);
    printf("  Total instructions: %u\n", instructions);
    printf("  CPI: %.2f\n", (float)cycles / instructions);
    printf("  AI operations: %u\n", RISCV_AI::AIPerformanceCounters::read_counter(RISCV_AI::AIPerformanceCounters::AI_OPS_TOTAL));
    printf("  AI MAC operations: %u\n", RISCV_AI::AIPerformanceCounters::read_counter(RISCV_AI::AIPerformanceCounters::AI_MAC_OPS));
}

int main() {
    printf("=== PyTorch Model on RISC-V Emulator ===\n");
    printf("Running CNN inference with AI extensions\n\n");

    // 初始化随机数种子
    srand(42);

    // 创建和初始化模型
    PyTorchModel model;
    init_pytorch_model(&model);

    // 创建输入数据（模拟图像）
    float input[INPUT_SIZE];
    printf("Generating input data...\n");
    for (int i = 0; i < INPUT_SIZE; i++) {
        input[i] = (float)rand() / RAND_MAX;
    }

    // 输出结果
    float output[OUTPUT_SIZE];

    printf("Input data size: %d floats\n", INPUT_SIZE);
    printf("Output data size: %d floats\n", OUTPUT_SIZE);
    printf("\n");

    // 执行推理
    clock_t start_time = clock();
    run_pytorch_inference(&model, input, output);
    clock_t end_time = clock();

    double execution_time = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
    printf("\nExecution time: %.4f seconds\n", execution_time);

    // 输出前10个结果
    printf("\nOutput results:\n");
    for (int i = 0; i < 10; i++) {
        printf("  class[%d] = %.6f\n", i, output[i]);
    }

    // 找到最大值（预测类别）
    int max_class = 0;
    float max_value = output[0];
    for (int i = 1; i < 10; i++) {
        if (output[i] > max_value) {
            max_value = output[i];
            max_class = i;
        }
    }

    printf("\nPredicted class: %d (confidence: %.6f)\n", max_class, max_value);
    printf("PyTorch model inference completed successfully!\n");

    return 0;
}