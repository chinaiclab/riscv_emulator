#include "../../include/core/AIExtensions.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace RISCV_AI {

// 简单的AI计算单元实现
class SimpleAIComputeUnit : public AIComputeUnit {
private:
    PerformanceStats stats;
    uint8_t* memory_base;

    float get_element(const TensorDesc& tensor, uint32_t idx) {
        uint8_t* data = memory_base + tensor.addr;
        switch (tensor.dtype) {
            case 0: // float32
                return reinterpret_cast<float*>(data)[idx];
            case 1: // float16
                // 简化的float16到float32转换
                return reinterpret_cast<uint16_t*>(data)[idx] / 1000.0f;
            case 2: // int8
                return static_cast<float>(reinterpret_cast<int8_t*>(data)[idx]);
            case 3: // uint8
                return static_cast<float>(reinterpret_cast<uint8_t*>(data)[idx]);
            default:
                return 0.0f;
        }
    }

    void set_element(const TensorDesc& tensor, uint32_t idx, float value) {
        uint8_t* data = memory_base + tensor.addr;
        switch (tensor.dtype) {
            case 0: // float32
                reinterpret_cast<float*>(data)[idx] = value;
                break;
            case 1: // float16
                reinterpret_cast<uint16_t*>(data)[idx] = static_cast<uint16_t>(value * 1000.0f);
                break;
            case 2: // int8
                reinterpret_cast<int8_t*>(data)[idx] = static_cast<int8_t>(std::round(value));
                break;
            case 3: // uint8
                reinterpret_cast<uint8_t*>(data)[idx] = static_cast<uint8_t>(std::round(value));
                break;
        }
    }

    uint32_t get_tensor_index(const TensorDesc& tensor, uint32_t n, uint32_t h, uint32_t w, uint32_t c) {
        if (tensor.layout == 0) { // NCHW
            return n * tensor.dims[1] * tensor.dims[2] * tensor.dims[3] +
                   c * tensor.dims[1] * tensor.dims[2] +
                   h * tensor.dims[3] + w;
        } else { // NHWC
            return n * tensor.dims[1] * tensor.dims[2] * tensor.dims[3] +
                   h * tensor.dims[2] * tensor.dims[3] +
                   w * tensor.dims[3] + c;
        }
    }

public:
    SimpleAIComputeUnit(uint8_t* mem_base) : memory_base(mem_base) {}

    void execute_tconv(const ConvParams& params) override {
        auto cycles = 1000; // 简化的周期计数
        auto ops = params.input.total_elements() * params.kernel_h * params.kernel_w * params.output.dims[3];

        // 简化的卷积实现（仅用于演示）
        for (uint32_t n = 0; n < params.input.dims[0]; n++) {
            for (uint32_t h = 0; h < params.output.dims[1]; h++) {
                for (uint32_t w = 0; w < params.output.dims[2]; w++) {
                    for (uint32_t c = 0; c < params.output.dims[3]; c++) {
                        float sum = 0.0f;

                        // 简单的卷积计算
                        for (uint32_t kh = 0; kh < params.kernel_h; kh++) {
                            for (uint32_t kw = 0; kw < params.kernel_w; kw++) {
                                for (uint32_t ic = 0; ic < params.input.dims[3]; ic++) {
                                    uint32_t in_idx = get_tensor_index(params.input, n,
                                        h + kh, w + kw, ic);
                                    uint32_t weight_idx = get_tensor_index(params.weight, 0,
                                        kh, kw, ic);

                                    float input_val = get_element(params.input, in_idx);
                                    float weight_val = get_element(params.weight, weight_idx);
                                    sum += input_val * weight_val;
                                }
                            }
                        }

                        // 添加偏置
                        if (params.bias.addr != 0) {
                            float bias_val = get_element(params.bias, c);
                            sum += bias_val;
                        }

                        // 存储结果
                        uint32_t out_idx = get_tensor_index(params.output, n, h, w, c);
                        set_element(params.output, out_idx, sum);
                    }
                }
            }
        }

        stats.update(ops, cycles, ops * 2);
        std::cout << "[AI] Convolution executed: " << ops << " ops, " << cycles << " cycles" << std::endl;
    }

    void execute_tmul(const MatMulParams& params) override {
        auto cycles = 500;
        auto m = params.A.dims[0];
        auto n = params.B.dims[1];
        auto k = params.A.dims[1];
        auto ops = m * n * k;

        // 简化的矩阵乘法实现
        for (uint32_t i = 0; i < m; i++) {
            for (uint32_t j = 0; j < n; j++) {
                float sum = 0.0f;
                for (uint32_t p = 0; p < k; p++) {
                    uint32_t a_idx = i * k + p;
                    uint32_t b_idx = p * n + j;

                    float a_val = get_element(params.A, a_idx);
                    float b_val = get_element(params.B, b_idx);
                    sum += a_val * b_val;
                }
                uint32_t c_idx = i * n + j;
                set_element(params.C, c_idx, sum);
            }
        }

        stats.update(ops, cycles, ops * 3);
        std::cout << "[AI] Matrix multiplication executed: " << ops << " ops, " << cycles << " cycles" << std::endl;
    }

    void execute_tadd(const TensorDesc& dst, const TensorDesc& src1, const TensorDesc& src2) override {
        auto cycles = 100;
        auto elements = dst.total_elements();
        auto ops = elements;

        for (uint32_t i = 0; i < elements; i++) {
            float val1 = get_element(src1, i);
            float val2 = get_element(src2, i);
            set_element(dst, i, val1 + val2);
        }

        stats.update(ops, cycles, ops * 3);
        std::cout << "[AI] Tensor addition executed: " << ops << " ops" << std::endl;
    }

    void execute_trelu(const TensorDesc& tensor) override {
        auto cycles = 50;
        auto elements = tensor.total_elements();

        for (uint32_t i = 0; i < elements; i++) {
            float val = get_element(tensor, i);
            set_element(tensor, i, std::max(0.0f, val));
        }

        stats.update(elements, cycles, elements * 2);
        std::cout << "[AI] ReLU executed on " << elements << " elements" << std::endl;
    }

    void execute_tpool(const PoolParams& params, bool max_pooling) override {
        auto cycles = 200;
        auto ops = params.output.total_elements();

        // 简化的池化实现
        for (uint32_t n = 0; n < params.input.dims[0]; n++) {
            for (uint32_t h = 0; h < params.output.dims[1]; h++) {
                for (uint32_t w = 0; w < params.output.dims[2]; w++) {
                    for (uint32_t c = 0; c < params.output.dims[3]; c++) {
                        float result = max_pooling ? -1e9f : 0.0f;

                        for (uint32_t kh = 0; kh < params.pool_h; kh++) {
                            for (uint32_t kw = 0; kw < params.pool_w; kw++) {
                                uint32_t in_idx = get_tensor_index(params.input, n,
                                    h * params.stride_h + kh, w * params.stride_w + kw, c);
                                float val = get_element(params.input, in_idx);

                                if (max_pooling) {
                                    result = std::max(result, val);
                                } else {
                                    result += val;
                                }
                            }
                        }

                        if (!max_pooling) {
                            result /= (params.pool_h * params.pool_w);
                        }

                        uint32_t out_idx = get_tensor_index(params.output, n, h, w, c);
                        set_element(params.output, out_idx, result);
                    }
                }
            }
        }

        stats.update(ops, cycles, ops * 4);
        std::cout << "[AI] " << (max_pooling ? "Max" : "Avg") << " Pooling executed" << std::endl;
    }

    void execute_tsigmoid(const TensorDesc& tensor) override {
        auto cycles = 100;
        auto elements = tensor.total_elements();

        for (uint32_t i = 0; i < elements; i++) {
            float val = get_element(tensor, i);
            float sigmoid_val = 1.0f / (1.0f + std::exp(-val));
            set_element(tensor, i, sigmoid_val);
        }

        stats.update(elements, cycles, elements * 2);
        std::cout << "[AI] Sigmoid executed on " << elements << " elements" << std::endl;
    }

    void execute_tsoftmax(const TensorDesc& tensor) override {
        auto cycles = 200;
        auto elements = tensor.total_elements();

        // 简化的softmax实现（假设最后一个维度是channel）
        for (uint32_t i = 0; i < tensor.dims[0]; i++) {
            for (uint32_t h = 0; h < tensor.dims[1]; h++) {
                for (uint32_t w = 0; w < tensor.dims[2]; w++) {
                    // 找到最大值
                    float max_val = -1e9f;
                    for (uint32_t c = 0; c < tensor.dims[3]; c++) {
                        uint32_t idx = get_tensor_index(tensor, i, h, w, c);
                        max_val = std::max(max_val, get_element(tensor, idx));
                    }

                    // 计算exp并求和
                    float sum = 0.0f;
                    for (uint32_t c = 0; c < tensor.dims[3]; c++) {
                        uint32_t idx = get_tensor_index(tensor, i, h, w, c);
                        float exp_val = std::exp(get_element(tensor, idx) - max_val);
                        set_element(tensor, idx, exp_val);
                        sum += exp_val;
                    }

                    // 归一化
                    for (uint32_t c = 0; c < tensor.dims[3]; c++) {
                        uint32_t idx = get_tensor_index(tensor, i, h, w, c);
                        float val = get_element(tensor, idx) / sum;
                        set_element(tensor, idx, val);
                    }
                }
            }
        }

        stats.update(elements, cycles, elements * 3);
        std::cout << "[AI] Softmax executed" << std::endl;
    }

    void execute_tnorm(const TensorDesc& tensor, float eps) override {
        auto cycles = 150;
        auto elements = tensor.total_elements();

        // 简化的批归一化实现
        for (uint32_t i = 0; i < elements; i++) {
            float val = get_element(tensor, i);
            // 简单的归一化：x = (x - mean) / sqrt(var + eps)
            float norm_val = (val - 0.5f) / std::sqrt(0.25f + eps);
            set_element(tensor, i, norm_val);
        }

        stats.update(elements, cycles, elements * 2);
        std::cout << "[AI] Normalization executed with eps=" << eps << std::endl;
    }

    const PerformanceStats& get_stats() const override {
        return stats;
    }

    void reset_stats() override {
        stats = PerformanceStats();
    }
};

// 性能计数器实现
static uint32_t ai_counters[8] = {0};

void AIPerformanceCounters::write_counter(Counter counter, uint32_t value) {
    uint32_t idx = (counter - AI_CYCLES) / 1;
    if (idx < 8) {
        ai_counters[idx] = value;
    }
}

uint32_t AIPerformanceCounters::read_counter(Counter counter) {
    uint32_t idx = (counter - AI_CYCLES) / 1;
    if (idx < 8) {
        return ai_counters[idx];
    }
    return 0;
}

void AIPerformanceCounters::increment_counter(Counter counter, uint32_t delta) {
    uint32_t idx = (counter - AI_CYCLES) / 1;
    if (idx < 8) {
        ai_counters[idx] += delta;
    }
}

// 工厂函数
std::unique_ptr<AIComputeUnit> createAIComputeUnit(uint8_t* memory_base) {
    return std::make_unique<SimpleAIComputeUnit>(memory_base);
}

} // namespace RISCV_AI