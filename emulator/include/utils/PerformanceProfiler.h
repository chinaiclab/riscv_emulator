#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

// Enhanced cache statistics structure for performance profiler
struct EnhancedCacheStats {
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t accesses = 0;
    uint64_t writebacks = 0;
    uint64_t total_access_time = 0;
    uint32_t access_latency = 4;  // Cache hit latency in cycles
    uint32_t miss_penalty = 50;   // Cache miss penalty in cycles

    double get_hit_rate() const {
        return accesses > 0 ? static_cast<double>(hits) / accesses : 0.0;
    }

    double get_miss_rate() const {
        return 1.0 - get_hit_rate();
    }

    uint64_t get_total_cycles_spent() const {
        return hits * access_latency + misses * (access_latency + miss_penalty);
    }
};

// Memory statistics structure
struct MemoryStats {
    uint64_t total_accesses = 0;
    uint64_t total_latency_cycles = 0;
    uint64_t l1_reads = 0;
    uint64_t l1_writes = 0;
    uint64_t l2_reads = 0;
    uint64_t l2_writes = 0;
    uint64_t ddr_reads = 0;
    uint64_t ddr_writes = 0;

    // Memory hierarchy latencies (in cycles)
    static constexpr uint32_t L1_HIT_LATENCY = 4;
    static constexpr uint32_t L1_MISS_PENALTY = 12;
    static constexpr uint32_t L2_HIT_LATENCY = 12;
    static constexpr uint32_t L2_MISS_PENALTY = 100;
    static constexpr uint32_t DDR_ACCESS_LATENCY = 200;

    uint64_t calculate_total_memory_cycles() const {
        return total_accesses * DDR_ACCESS_LATENCY;  // Simplified model
    }
};

// TLB statistics structure
struct TLBStats {
    uint64_t hits = 0;
    uint64_t misses = 0;
    uint64_t accesses = 0;
    uint32_t hit_latency = 1;
    uint32_t miss_penalty = 30;

    double get_hit_rate() const {
        return accesses > 0 ? static_cast<double>(hits) / accesses : 0.0;
    }

    uint64_t get_total_cycles_spent() const {
        return hits * hit_latency + misses * (hit_latency + miss_penalty);
    }
};

// Realistic timing constants for RISC-V hardware
struct HardwareTiming {
    static constexpr double TARGET_FREQUENCY_HZ = 1e9;  // 1 GHz target frequency
    static constexpr uint32_t PIPELINE_DEPTH = 5;
    static constexpr double BASE_CPI = 1.0;  // Ideal CPI
    static constexpr double BRANCH_MISPREDICT_PENALTY = 3.0;
    static constexpr uint32_t INSTRUCTION_DECODE_LATENCY = 1;
    static constexpr uint32_t REGISTER_READ_LATENCY = 1;
    static constexpr uint32_t ALU_LATENCY = 1;
    static constexpr uint32_t REGISTER_WRITE_LATENCY = 1;
};

class PerformanceProfiler {
public:
    PerformanceProfiler();
    ~PerformanceProfiler() = default;

    // Profiling control
    void enable_profiling(bool enable) { profiling_enabled = enable; }
    bool is_profiling_enabled() const { return profiling_enabled; }

    // Timing control
    void start_timing();
    void stop_timing();
    void reset_timing();

    // Instruction counting
    void increment_instruction_count(uint32_t core_id);
    void record_instruction_execution(uint32_t core_id, uint32_t cycles_spent);
    uint64_t get_instruction_count(uint32_t core_id) const;
    uint64_t get_total_instruction_count() const;

    // Cache statistics access
    void update_cache_stats(uint32_t core_id, bool l1_hit, bool l2_hit, uint32_t latency);
    EnhancedCacheStats get_l1_cache_stats(uint32_t core_id) const;
    EnhancedCacheStats get_l2_cache_stats(uint32_t core_id) const;

    // TLB statistics access
    void update_tlb_stats(uint32_t core_id, bool hit, uint32_t latency);
    TLBStats get_tlb_stats(uint32_t core_id) const;

    // Memory statistics access
    void update_memory_stats(uint32_t core_id, uint64_t reads, uint64_t writes);
    MemoryStats get_memory_stats(uint32_t core_id) const;

    // Realistic performance metrics
    double get_execution_time_seconds() const;
    double get_execution_time_scaled() const;  // Scaled to hardware frequency
    double get_instructions_per_second() const;
    double get_cycles_per_instruction() const;
    uint64_t get_total_cycles() const { return total_cycles; }
    uint64_t get_realistic_total_cycles() const;  // Including memory stalls
    void set_total_cycles(uint64_t cycles) { total_cycles = cycles; }

    // Results and reporting
    void print_profiling_results() const;

private:
    bool profiling_enabled = false;
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;
    bool timing_started = false;

    // Instruction counting and cycles per core
    std::unordered_map<uint32_t, uint64_t> instruction_counts;
    std::unordered_map<uint32_t, uint64_t> execution_cycles;
    std::unordered_map<uint32_t, uint64_t> memory_stall_cycles;
    uint64_t total_cycles = 0;

    // Cache statistics per core
    std::unordered_map<uint32_t, EnhancedCacheStats> l1_cache_stats;
    std::unordered_map<uint32_t, EnhancedCacheStats> l2_cache_stats;

    // TLB statistics per core
    std::unordered_map<uint32_t, TLBStats> tlb_stats;

    // Memory statistics per core
    std::unordered_map<uint32_t, MemoryStats> memory_stats;

    // Helper methods
    double calculate_execution_time() const;
    uint64_t calculate_memory_stall_cycles(uint32_t core_id) const;
    void simulate_cache_behavior();
};