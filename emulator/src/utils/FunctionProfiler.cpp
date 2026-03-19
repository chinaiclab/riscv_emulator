#include "../include/utils/FunctionProfiler.h"
#include "../include/memory/Cache.h"
#include "../include/memory/DDR.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

FunctionProfiler::FunctionProfiler() {
    // Initialize with profiling disabled
    profiling_enabled = false;
}

void FunctionProfiler::add_function_profile(const std::string& name, uint64_t start_pc, uint64_t end_pc) {
    function_profiles.emplace_back(name, start_pc, end_pc);
    size_t index = function_profiles.size() - 1;
    
    // Map all PCs in this function range to the function index
    for (uint64_t pc = start_pc; pc <= end_pc; pc += 4) {
        pc_to_function_map[pc] = index;
    }
}

void FunctionProfiler::clear_function_profiles() {
    function_profiles.clear();
    pc_to_function_map.clear();
    prev_cache_stats.clear();
}

void FunctionProfiler::track_instruction_execution(uint32_t pc, uint32_t /* core_id */) {
    if (!profiling_enabled) {
        return;
    }

    // Check if the current PC is in a known function
    auto it = pc_to_function_map.find(pc);
    if (it != pc_to_function_map.end()) {
        size_t func_idx = it->second;
        if (func_idx < function_profiles.size()) {
            // Calculate execution cycles considering cache and memory access
            // Base execution time: 1 cycle per instruction
            function_profiles[func_idx].execution_cycles += 1;
            
            // Update the instruction count for this function
            function_profiles[func_idx].instruction_count++;
        }
    }
}

size_t FunctionProfiler::get_current_function_index(uint32_t /* core_id */, uint32_t pc) const {
    auto it = pc_to_function_map.find(pc);
    return (it != pc_to_function_map.end()) ? it->second : static_cast<size_t>(-1);
}

void FunctionProfiler::update_cache_stats(uint32_t core_id, size_t func_idx,
                                         uint64_t l1_i_hits, uint64_t l1_i_misses,
                                         uint64_t l1_d_hits, uint64_t l1_d_misses,
                                         uint64_t l2_hits, uint64_t l2_misses) {
    if (!profiling_enabled || func_idx >= function_profiles.size()) {
        return;
    }

    // Get previous statistics for this core
    CacheStats& prev = prev_cache_stats[core_id];
    
    // Calculate differences since last update
    uint64_t l1_i_hits_diff = l1_i_hits - prev.l1_icache_hits;
    uint64_t l1_i_misses_diff = l1_i_misses - prev.l1_icache_misses;
    uint64_t l1_d_hits_diff = l1_d_hits - prev.l1_dcache_hits;
    uint64_t l1_d_misses_diff = l1_d_misses - prev.l1_dcache_misses;
    uint64_t l2_hits_diff = l2_hits - prev.l2_cache_hits;
    uint64_t l2_misses_diff = l2_misses - prev.l2_cache_misses;
    
    // Update function's cache statistics
    function_profiles[func_idx].l1_icache_hits += l1_i_hits_diff;
    function_profiles[func_idx].l1_icache_misses += l1_i_misses_diff;
    function_profiles[func_idx].l1_dcache_hits += l1_d_hits_diff;
    function_profiles[func_idx].l1_dcache_misses += l1_d_misses_diff;
    function_profiles[func_idx].l2_cache_hits += l2_hits_diff;
    function_profiles[func_idx].l2_cache_misses += l2_misses_diff;
    
    // Calculate cache access cycles
    uint64_t cache_cycles = 0;
    cache_cycles += l1_i_hits_diff * 1;      // 1 cycle per L1 I-cache hit
    cache_cycles += l1_i_misses_diff * 10;   // 10 cycle penalty per L1 I-cache miss
    cache_cycles += l1_d_hits_diff * 1;      // 1 cycle per L1 D-cache hit
    cache_cycles += l1_d_misses_diff * 10;   // 10 cycle penalty per L1 D-cache miss
    cache_cycles += l2_hits_diff * 5;        // 5 cycles per L2 hit
    cache_cycles += l2_misses_diff * 50;     // 50 cycle penalty per L2 miss
    
    // Update execution cycles with cache access time
    function_profiles[func_idx].execution_cycles += cache_cycles;
    
    // Update previous statistics
    prev.l1_icache_hits = l1_i_hits;
    prev.l1_icache_misses = l1_i_misses;
    prev.l1_dcache_hits = l1_d_hits;
    prev.l1_dcache_misses = l1_d_misses;
    prev.l2_cache_hits = l2_hits;
    prev.l2_cache_misses = l2_misses;
}

void FunctionProfiler::record_memory_access(uint32_t /* core_id */, size_t function_idx,
                                           uint32_t /* addr */, bool is_write, uint64_t /* timestamp */) {
    if (!profiling_enabled || function_idx >= function_profiles.size()) {
        return;
    }
    
    // This method records the memory access for later processing
    // The actual latency will be calculated when the access completes
    // For now, we can just increment the appropriate counter
    if (is_write) {
        function_profiles[function_idx].ddr_writes++;
    } else {
        function_profiles[function_idx].ddr_reads++;
    }
}

void FunctionProfiler::update_ddr_stats(uint32_t core_id, size_t func_idx,
                                       uint64_t ddr_reads, uint64_t ddr_writes, uint64_t latency) {
    if (!profiling_enabled || func_idx >= function_profiles.size()) {
        return;
    }

    // Get previous statistics for this core
    CacheStats& prev = prev_cache_stats[core_id];
    
    // Calculate differences since last update
    uint64_t ddr_reads_diff = ddr_reads - prev.ddr_reads;
    uint64_t ddr_writes_diff = ddr_writes - prev.ddr_writes;
    uint64_t latency_diff = latency - prev.memory_latency;
    
    // Update function's DDR statistics
    function_profiles[func_idx].ddr_reads += ddr_reads_diff;
    function_profiles[func_idx].ddr_writes += ddr_writes_diff;
    function_profiles[func_idx].total_memory_latency += latency_diff;
    
    // Update execution cycles with DDR access time
    function_profiles[func_idx].execution_cycles += latency_diff;
    
    // Update previous statistics
    prev.ddr_reads = ddr_reads;
    prev.ddr_writes = ddr_writes;
    prev.memory_latency = latency;
}

void FunctionProfiler::print_function_profiling_results() const {
    if (!profiling_enabled || function_profiles.empty()) {
        std::cout << "Function profiling is disabled or no functions profiled.\n";
        return;
    }

    std::cout << "\n===== OVERALL FUNCTION PROFILING RESULTS =====\n\n";

    for (size_t i = 0; i < function_profiles.size(); ++i) {
        const auto& func = function_profiles[i];
        
        if (func.instruction_count == 0) {
            continue; // Skip functions that weren't executed
        }

        std::cout << "Function: " << func.name << "\n";
        std::cout << "  Instructions executed: " << func.instruction_count << "\n";
        std::cout << "  Execution cycles: " << func.execution_cycles << "\n";
        
        // Calculate cache hit rates
        uint64_t total_i_accesses = func.l1_icache_hits + func.l1_icache_misses;
        uint64_t total_d_accesses = func.l1_dcache_hits + func.l1_dcache_misses;
        uint64_t total_l2_accesses = func.l2_cache_hits + func.l2_cache_misses;
        
        double i_hit_rate = total_i_accesses > 0 ? 
            (static_cast<double>(func.l1_icache_hits) / total_i_accesses) * 100.0 : 0.0;
        double d_hit_rate = total_d_accesses > 0 ? 
            (static_cast<double>(func.l1_dcache_hits) / total_d_accesses) * 100.0 : 0.0;
        double l2_hit_rate = total_l2_accesses > 0 ? 
            (static_cast<double>(func.l2_cache_hits) / total_l2_accesses) * 100.0 : 0.0;
        
        std::cout << "  L1 I-cache: " << func.l1_icache_hits << " hits, " 
                  << func.l1_icache_misses << " misses (" << std::fixed 
                  << std::setprecision(1) << i_hit_rate << "% hit rate)\n";
        
        std::cout << "  L1 D-cache: " << func.l1_dcache_hits << " hits, " 
                  << func.l1_dcache_misses << " misses (" << std::fixed 
                  << std::setprecision(1) << d_hit_rate << "% hit rate)\n";
        
        std::cout << "  L2 cache: " << func.l2_cache_hits << " hits, " 
                  << func.l2_cache_misses << " misses (" << std::fixed 
                  << std::setprecision(1) << l2_hit_rate << "% hit rate)\n";
        
        std::cout << "  DDR: " << func.ddr_reads << " reads, " 
                  << func.ddr_writes << " writes\n";
        
        std::cout << "  Memory latency: " << func.total_memory_latency << " cycles\n";
        
        if (func.instruction_count > 0) {
            double cycles_per_instruction = static_cast<double>(func.execution_cycles) / func.instruction_count;
            std::cout << "  Cycles per instruction: " << std::fixed 
                      << std::setprecision(2) << cycles_per_instruction << "\n";
        }
        
        std::cout << "\n";
    }
    
    std::cout << "===============================================\n\n";
}

void FunctionProfiler::calculate_execution_cycles(size_t func_idx, uint32_t /* core_id */) {
    if (func_idx >= function_profiles.size()) {
        return;
    }
    
    // This method can be used for more complex cycle calculations
    // For now, the basic calculation is done in track_instruction_execution
}

void FunctionProfiler::update_function_cycles(size_t func_idx, uint64_t base_cycles,
                                             uint64_t cache_cycles, uint64_t memory_cycles) {
    if (func_idx >= function_profiles.size()) {
        return;
    }
    
    function_profiles[func_idx].execution_cycles += base_cycles + cache_cycles + memory_cycles;
}