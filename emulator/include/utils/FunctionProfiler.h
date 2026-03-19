#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

// Structure to track function profiling data
struct FunctionProfile {
    std::string name;
    uint64_t instruction_count = 0;
    uint64_t start_pc = 0;
    uint64_t end_pc = 0;
    bool is_active = false;

    // Memory and cache statistics
    uint64_t l1_icache_hits = 0;
    uint64_t l1_icache_misses = 0;
    uint64_t l1_dcache_hits = 0;
    uint64_t l1_dcache_misses = 0;
    uint64_t l2_cache_hits = 0;
    uint64_t l2_cache_misses = 0;
    uint64_t ddr_reads = 0;
    uint64_t ddr_writes = 0;
    uint64_t total_memory_latency = 0;

    // Execution cycles considering cache and memory access
    uint64_t execution_cycles = 0;

    FunctionProfile(const std::string& n, uint64_t s, uint64_t e)
        : name(n), start_pc(s), end_pc(e) {}
};

// Structure to track memory access associated with a function
struct MemoryAccessRecord {
    uint32_t core_id;      // Core that initiated the access
    size_t function_idx;   // Function that initiated the access
    uint32_t address;      // Address accessed
    bool is_write;         // True if write, false if read
    uint64_t timestamp;    // Cycle when access was initiated
    uint64_t latency;      // Expected latency of the access

    MemoryAccessRecord(uint32_t core, size_t func_idx, uint32_t addr, bool write, uint64_t ts, uint64_t lat = 0)
        : core_id(core), function_idx(func_idx), address(addr), is_write(write), timestamp(ts), latency(lat) {}
};

class FunctionProfiler {
public:
    FunctionProfiler();
    ~FunctionProfiler() = default;

    // Core profiling methods
    void enable_profiling(bool enable) { profiling_enabled = enable; }
    bool is_profiling_enabled() const { return profiling_enabled; }

    // Function management
    void add_function_profile(const std::string& name, uint64_t start_pc, uint64_t end_pc);
    void clear_function_profiles();
    
    // Function execution tracking
    void track_instruction_execution(uint32_t pc, uint32_t core_id);
    size_t get_current_function_index(uint32_t core_id, uint32_t pc) const;
    
    // Cache statistics tracking
    void update_cache_stats(uint32_t core_id, size_t func_idx, 
                           uint64_t l1_i_hits, uint64_t l1_i_misses,
                           uint64_t l1_d_hits, uint64_t l1_d_misses,
                           uint64_t l2_hits, uint64_t l2_misses);
    
    // Memory access tracking
    void record_memory_access(uint32_t core_id, size_t function_idx, 
                             uint32_t addr, bool is_write, uint64_t timestamp);
    void update_ddr_stats(uint32_t core_id, size_t func_idx, 
                         uint64_t ddr_reads, uint64_t ddr_writes, uint64_t latency);
    
    // Results and reporting
    void print_function_profiling_results() const;
    const std::vector<FunctionProfile>& get_function_profiles() const { return function_profiles; }
    const std::unordered_map<uint64_t, size_t>& get_pc_to_function_map() const { return pc_to_function_map; }

private:
    bool profiling_enabled = false;
    std::vector<FunctionProfile> function_profiles;
    std::unordered_map<uint64_t, size_t> pc_to_function_map; // Maps PC to function index
    
    // Previous cache statistics for calculating differences
    struct CacheStats {
        uint64_t l1_icache_hits = 0;
        uint64_t l1_icache_misses = 0;
        uint64_t l1_dcache_hits = 0;
        uint64_t l1_dcache_misses = 0;
        uint64_t l2_cache_hits = 0;
        uint64_t l2_cache_misses = 0;
        uint64_t ddr_reads = 0;
        uint64_t ddr_writes = 0;
        uint64_t memory_latency = 0;
    };
    
    std::unordered_map<uint32_t, CacheStats> prev_cache_stats; // Per-core previous statistics
    
    // Helper methods
    void calculate_execution_cycles(size_t func_idx, uint32_t core_id);
    void update_function_cycles(size_t func_idx, uint64_t base_cycles, 
                               uint64_t cache_cycles, uint64_t memory_cycles);
};