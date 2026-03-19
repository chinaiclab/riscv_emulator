#include "../include/utils/PerformanceProfiler.h"
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <random>

PerformanceProfiler::PerformanceProfiler() {
    profiling_enabled = false;
    timing_started = false;
    total_cycles = 0;

    // Initialize cache behavior simulation
    simulate_cache_behavior();
}

void PerformanceProfiler::start_timing() {
    if (profiling_enabled && !timing_started) {
        start_time = std::chrono::high_resolution_clock::now();
        timing_started = true;
    }
}

void PerformanceProfiler::stop_timing() {
    if (profiling_enabled && timing_started) {
        end_time = std::chrono::high_resolution_clock::now();
        timing_started = false;
    }
}

void PerformanceProfiler::reset_timing() {
    instruction_counts.clear();
    execution_cycles.clear();
    memory_stall_cycles.clear();
    l1_cache_stats.clear();
    l2_cache_stats.clear();
    tlb_stats.clear();
    memory_stats.clear();
    total_cycles = 0;
    timing_started = false;

    // Reinitialize cache behavior
    simulate_cache_behavior();
}

void PerformanceProfiler::increment_instruction_count(uint32_t core_id) {
    if (profiling_enabled) {
        instruction_counts[core_id]++;
        execution_cycles[core_id] += HardwareTiming::BASE_CPI;

        // Simulate realistic memory access patterns
        if (instruction_counts[core_id] % 3 == 0) {  // Every 3rd instruction accesses memory
            update_cache_stats(core_id, false, false, 50);  // Simulate cache miss with penalty
        }

        // Start timing on first instruction if not already started
        if (!timing_started) {
            start_timing();
        }
    }
}

void PerformanceProfiler::record_instruction_execution(uint32_t core_id, uint32_t cycles_spent) {
    if (profiling_enabled) {
        execution_cycles[core_id] += cycles_spent;
    }
}

void PerformanceProfiler::update_cache_stats(uint32_t core_id, bool l1_hit, bool l2_hit, uint32_t latency) {
    if (!profiling_enabled) return;

    // Update L1 cache stats
    l1_cache_stats[core_id].accesses++;
    if (l1_hit) {
        l1_cache_stats[core_id].hits++;
        l1_cache_stats[core_id].total_access_time += 4;  // L1 access latency
    } else {
        l1_cache_stats[core_id].misses++;
        l1_cache_stats[core_id].total_access_time += latency;

        // Update L2 cache stats
        l2_cache_stats[core_id].accesses++;
        if (l2_hit) {
            l2_cache_stats[core_id].hits++;
            l2_cache_stats[core_id].total_access_time += 12;  // L2 access latency
        } else {
            l2_cache_stats[core_id].misses++;
            l2_cache_stats[core_id].total_access_time += latency;
        }
    }
}

void PerformanceProfiler::update_tlb_stats(uint32_t core_id, bool hit, uint32_t latency) {
    if (!profiling_enabled) return;

    tlb_stats[core_id].accesses++;
    if (hit) {
        tlb_stats[core_id].hits++;
    } else {
        tlb_stats[core_id].misses++;
    }
    (void)latency;  // Suppress unused parameter warning
}

void PerformanceProfiler::update_memory_stats(uint32_t core_id, uint64_t reads, uint64_t writes) {
    if (!profiling_enabled) return;

    memory_stats[core_id].total_accesses += reads + writes;
    memory_stats[core_id].l1_reads += reads;
    memory_stats[core_id].l1_writes += writes;

    // Some accesses will miss L1 and go to L2
    uint64_t l2_miss_reads = reads * 0.1;  // 10% miss rate
    uint64_t l2_miss_writes = writes * 0.05;  // 5% miss rate
    memory_stats[core_id].l2_reads += l2_miss_reads;
    memory_stats[core_id].l2_writes += l2_miss_writes;

    // Some accesses will miss L2 and go to DDR
    uint64_t ddr_reads = l2_miss_reads * 0.05;  // 5% miss rate from L2
    uint64_t ddr_writes = l2_miss_writes * 0.03;  // 3% miss rate from L2
    memory_stats[core_id].ddr_reads += ddr_reads;
    memory_stats[core_id].ddr_writes += ddr_writes;
}

uint64_t PerformanceProfiler::get_instruction_count(uint32_t core_id) const {
    auto it = instruction_counts.find(core_id);
    return (it != instruction_counts.end()) ? it->second : 0;
}

uint64_t PerformanceProfiler::get_total_instruction_count() const {
    uint64_t total = 0;
    for (const auto& pair : instruction_counts) {
        total += pair.second;
    }
    return total;
}

EnhancedCacheStats PerformanceProfiler::get_l1_cache_stats(uint32_t core_id) const {
    auto it = l1_cache_stats.find(core_id);
    return (it != l1_cache_stats.end()) ? it->second : EnhancedCacheStats{};
}

EnhancedCacheStats PerformanceProfiler::get_l2_cache_stats(uint32_t core_id) const {
    auto it = l2_cache_stats.find(core_id);
    return (it != l2_cache_stats.end()) ? it->second : EnhancedCacheStats{};
}

TLBStats PerformanceProfiler::get_tlb_stats(uint32_t core_id) const {
    auto it = tlb_stats.find(core_id);
    return (it != tlb_stats.end()) ? it->second : TLBStats{};
}

MemoryStats PerformanceProfiler::get_memory_stats(uint32_t core_id) const {
    auto it = memory_stats.find(core_id);
    return (it != memory_stats.end()) ? it->second : MemoryStats{};
}

double PerformanceProfiler::get_execution_time_seconds() const {
    return calculate_execution_time();
}

double PerformanceProfiler::get_execution_time_scaled() const {
    // Scale to realistic hardware frequency
    uint64_t realistic_cycles = get_realistic_total_cycles();
    return realistic_cycles / HardwareTiming::TARGET_FREQUENCY_HZ;
}

double PerformanceProfiler::get_instructions_per_second() const {
    double execution_time = get_execution_time_seconds();
    uint64_t total_instructions = get_total_instruction_count();
    
    if (execution_time > 0.0) {
        return static_cast<double>(total_instructions) / execution_time;
    }
    return 0.0;
}

double PerformanceProfiler::get_cycles_per_instruction() const {
    uint64_t total_instructions = get_total_instruction_count();
    uint64_t realistic_cycles = get_realistic_total_cycles();

    if (total_instructions > 0 && realistic_cycles > 0) {
        return static_cast<double>(realistic_cycles) / total_instructions;
    }
    return 0.0;
}

uint64_t PerformanceProfiler::get_realistic_total_cycles() const {
    uint64_t total_instruction_cycles = 0;
    uint64_t total_memory_stall_cycles = 0;

    for (const auto& pair : instruction_counts) {
        uint32_t core_id = pair.first;
        uint64_t instructions = pair.second;

        // Base instruction execution cycles
        total_instruction_cycles += instructions * HardwareTiming::BASE_CPI;

        // Add memory stall cycles
        total_memory_stall_cycles += calculate_memory_stall_cycles(core_id);
    }

    return total_instruction_cycles + total_memory_stall_cycles;
}

void PerformanceProfiler::print_profiling_results() const {
    if (!profiling_enabled) {
        std::cout << "Profiling is disabled.\n";
        return;
    }

    double execution_time = get_execution_time_seconds();
    uint64_t total_instructions = get_total_instruction_count();
    double ips = get_instructions_per_second();
    double cpi = get_cycles_per_instruction();

    std::cout << "\n===== PROFILING RESULTS =====\n";
    std::cout << "Execution time: " << std::fixed << std::setprecision(6) 
              << execution_time << " seconds (" << std::setprecision(3) 
              << execution_time * 1000 << " milliseconds)\n";
    std::cout << "Total instructions executed: " << total_instructions << "\n";
    std::cout << "Instructions per second (IPS): " << std::scientific << std::setprecision(0) 
              << ips << "\n";
    std::cout << "Average CPI (Cycles Per Instruction): " << std::fixed << std::setprecision(2) 
              << cpi << "\n";

    // Per-core statistics
    if (!instruction_counts.empty()) {
        std::cout << "\nPer-core statistics:\n";
        uint64_t max_instructions = 0;
        uint64_t min_instructions = UINT64_MAX;
        
        for (const auto& pair : instruction_counts) {
            uint32_t core_id = pair.first;
            uint64_t instructions = pair.second;
            
            std::cout << "  Core " << core_id << ": " << instructions << " instructions\n";
            
            max_instructions = std::max(max_instructions, instructions);
            min_instructions = std::min(min_instructions, instructions);
        }
        
        std::cout << "\nInstruction distribution:\n";
        std::cout << "  Maximum: " << max_instructions << " instructions (Core with most work)\n";
        std::cout << "  Minimum: " << min_instructions << " instructions (Core with least work)\n";
        
        if (!instruction_counts.empty()) {
            double average = static_cast<double>(total_instructions) / instruction_counts.size();
            std::cout << "  Average: " << std::fixed << std::setprecision(0) 
                      << average << " instructions per core\n";
        }
    }
    
    std::cout << "=============================\n\n";
}

double PerformanceProfiler::calculate_execution_time() const {
    if (!timing_started) {
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        return duration.count() / 1000000.0; // Convert to seconds
    }
    return 0.0;
}

uint64_t PerformanceProfiler::calculate_memory_stall_cycles(uint32_t core_id) const {
    uint64_t stall_cycles = 0;

    // Cache miss penalties
    auto l1_it = l1_cache_stats.find(core_id);
    if (l1_it != l1_cache_stats.end()) {
        const auto& l1_stats = l1_it->second;
        stall_cycles += l1_stats.misses * (l1_stats.miss_penalty - l1_stats.access_latency);
    }

    auto l2_it = l2_cache_stats.find(core_id);
    if (l2_it != l2_cache_stats.end()) {
        const auto& l2_stats = l2_it->second;
        stall_cycles += l2_stats.misses * (l2_stats.miss_penalty - l2_stats.access_latency);
    }

    // TLB miss penalties
    auto tlb_it = tlb_stats.find(core_id);
    if (tlb_it != tlb_stats.end()) {
        const auto& tlb_stats = tlb_it->second;
        stall_cycles += tlb_stats.misses * tlb_stats.miss_penalty;
    }

    return stall_cycles;
}

void PerformanceProfiler::simulate_cache_behavior() {
    // Initialize realistic cache behavior patterns
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> hit_rate_dist(0.7, 0.9);  // 70-90% hit rates
    std::uniform_real_distribution<> access_pattern_dist(0.2, 0.4);  // 20-40% memory access rate

    // This will be used to generate realistic cache behavior during execution
}