#include "../include/utils/MemoryAccessTracker.h"
#include "../include/memory/Cache.h"
#include "../include/memory/DDR.h"
#include <iostream>
#include <algorithm>

MemoryAccessTracker::MemoryAccessTracker() {
    tracking_enabled = false;
}

void MemoryAccessTracker::record_memory_access(uint32_t core_id, size_t function_idx,
                                              uint32_t addr, bool is_write, uint64_t timestamp) {
    if (!tracking_enabled) {
        return;
    }

    std::lock_guard<std::mutex> lock(access_mutex);
    
    // Create a memory access record
    MemoryAccessRecord record(core_id, function_idx, addr, is_write, timestamp);
    
    // Add to completed accesses queue
    completed_memory_accesses.push(record);
    
    // Limit queue size to prevent memory issues
    const size_t MAX_QUEUE_SIZE = 10000;
    if (completed_memory_accesses.size() > MAX_QUEUE_SIZE) {
        completed_memory_accesses.pop();
    }
}

void MemoryAccessTracker::process_completed_accesses() {
    if (!tracking_enabled) {
        return;
    }

    std::lock_guard<std::mutex> lock(access_mutex);
    
    // Process completed memory accesses
    // This method can be used to perform additional analysis on completed accesses
    while (!completed_memory_accesses.empty()) {
        completed_memory_accesses.front();
        
        // Here you could perform additional analysis, such as:
        // - Calculate average latency per function
        // - Track memory access patterns
        // - Identify hotspots
        
        completed_memory_accesses.pop();
    }
}

void MemoryAccessTracker::update_cache_statistics(uint32_t core_id, Cache* l1_icache, 
                                                  Cache* l1_dcache, Cache* l2_cache) {
    if (!tracking_enabled || !l1_icache || !l1_dcache || !l2_cache) {
        return;
    }

    // Get current statistics
    CacheStatistics current_stats;
    
    auto& l1_i_stats = l1_icache->get_stats();
    auto& l1_d_stats = l1_dcache->get_stats();
    auto& l2_stats = l2_cache->get_stats();
    
    current_stats.l1_icache_hits = l1_i_stats.hits;
    current_stats.l1_icache_misses = l1_i_stats.misses;
    current_stats.l1_dcache_hits = l1_d_stats.hits;
    current_stats.l1_dcache_misses = l1_d_stats.misses;
    current_stats.l2_cache_hits = l2_stats.hits;
    current_stats.l2_cache_misses = l2_stats.misses;
    
    // Update function statistics based on differences
    update_function_statistics(core_id, current_stats);
    
    // Store current statistics as previous for next iteration
    prev_statistics[core_id] = current_stats;
}

void MemoryAccessTracker::update_memory_statistics(uint32_t core_id, DDRMemory* ddr_memory) {
    if (!tracking_enabled || !ddr_memory) {
        return;
    }

    // Get current statistics
    CacheStatistics current_stats = prev_statistics[core_id]; // Start with cache stats
    
    current_stats.ddr_reads = ddr_memory->get_read_count();
    current_stats.ddr_writes = ddr_memory->get_write_count();
    current_stats.memory_latency = ddr_memory->get_total_latency();
    
    // Update function statistics based on differences
    update_function_statistics(core_id, current_stats);
    
    // Store current statistics as previous for next iteration
    prev_statistics[core_id] = current_stats;
}

void MemoryAccessTracker::add_pending_access(uint32_t addr, uint32_t core_id, 
                                            size_t function_idx, uint64_t timestamp) {
    if (!tracking_enabled) {
        return;
    }

    std::lock_guard<std::mutex> lock(access_mutex);
    
    // Add to pending accesses
    pending_accesses_by_addr[addr].emplace(core_id, function_idx, timestamp);
}

void MemoryAccessTracker::remove_pending_access(uint32_t addr) {
    if (!tracking_enabled) {
        return;
    }

    std::lock_guard<std::mutex> lock(access_mutex);
    
    auto it = pending_accesses_by_addr.find(addr);
    if (it != pending_accesses_by_addr.end()) {
        if (!it->second.empty()) {
            it->second.pop();
        }
        
        // Clean up empty queues
        if (it->second.empty()) {
            pending_accesses_by_addr.erase(it);
        }
    }
}

uint64_t MemoryAccessTracker::get_pending_access_count() const {
    std::lock_guard<std::mutex> lock(access_mutex);
    
    uint64_t total = 0;
    for (const auto& pair : pending_accesses_by_addr) {
        total += pair.second.size();
    }
    return total;
}

std::vector<MemoryAccessRecord> MemoryAccessTracker::get_completed_accesses() const {
    std::lock_guard<std::mutex> lock(access_mutex);
    
    std::vector<MemoryAccessRecord> result;
    
    // Copy completed accesses to a vector
    std::queue<MemoryAccessRecord> temp = completed_memory_accesses;
    while (!temp.empty()) {
        result.push_back(temp.front());
        temp.pop();
    }
    
    return result;
}

void MemoryAccessTracker::update_function_statistics(uint32_t core_id, const CacheStatistics& current_stats) {
    // Get previous statistics for this core
    auto prev_it = prev_statistics.find(core_id);
    if (prev_it == prev_statistics.end()) {
        return; // No previous statistics to compare with
    }
    
    const CacheStatistics& prev = prev_it->second;
    
    // Calculate differences
    uint64_t l1_i_hits_diff = current_stats.l1_icache_hits - prev.l1_icache_hits;
    uint64_t l1_i_misses_diff = current_stats.l1_icache_misses - prev.l1_icache_misses;
    uint64_t l1_d_hits_diff = current_stats.l1_dcache_hits - prev.l1_dcache_hits;
    uint64_t l1_d_misses_diff = current_stats.l1_dcache_misses - prev.l1_dcache_misses;
    uint64_t l2_hits_diff = current_stats.l2_cache_hits - prev.l2_cache_hits;
    uint64_t l2_misses_diff = current_stats.l2_cache_misses - prev.l2_cache_misses;
    uint64_t ddr_reads_diff = current_stats.ddr_reads - prev.ddr_reads;
    uint64_t ddr_writes_diff = current_stats.ddr_writes - prev.ddr_writes;
    uint64_t latency_diff = current_stats.memory_latency - prev.memory_latency;
    
    // This method updates the raw statistics differences
    // The actual function profile updates should be handled by the FunctionProfiler
    // which has access to the function profiles and can properly attribute these differences
    
    // For now, we'll just print the differences for debugging
    if (tracking_enabled && (l1_i_hits_diff > 0 || l1_i_misses_diff > 0 || 
                            l1_d_hits_diff > 0 || l1_d_misses_diff > 0 ||
                            l2_hits_diff > 0 || l2_misses_diff > 0 ||
                            ddr_reads_diff > 0 || ddr_writes_diff > 0)) {
        
        std::cout << "[DEBUG] Core " << core_id << " cache/memory statistics update:\n";
        std::cout << "  L1 I-cache: +" << l1_i_hits_diff << " hits, +" << l1_i_misses_diff << " misses\n";
        std::cout << "  L1 D-cache: +" << l1_d_hits_diff << " hits, +" << l1_d_misses_diff << " misses\n";
        std::cout << "  L2 cache: +" << l2_hits_diff << " hits, +" << l2_misses_diff << " misses\n";
        std::cout << "  DDR: +" << ddr_reads_diff << " reads, +" << ddr_writes_diff << " writes\n";
        std::cout << "  Latency: +" << latency_diff << " cycles\n";
    }
}