#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <queue>
#include <mutex>
#include "FunctionProfiler.h"

// Forward declarations
class Cache;
class DDRMemory;

class MemoryAccessTracker {
public:
    MemoryAccessTracker();
    ~MemoryAccessTracker() = default;

    // Tracking control
    void enable_tracking(bool enable) { tracking_enabled = enable; }
    bool is_tracking_enabled() const { return tracking_enabled; }

    // Memory access recording
    void record_memory_access(uint32_t core_id, size_t function_idx, 
                             uint32_t addr, bool is_write, uint64_t timestamp);
    void process_completed_accesses();
    
    // Cache and memory statistics tracking
    void update_cache_statistics(uint32_t core_id, Cache* l1_icache, Cache* l1_dcache, Cache* l2_cache);
    void update_memory_statistics(uint32_t core_id, DDRMemory* ddr_memory);
    
    // Pending access management
    void add_pending_access(uint32_t addr, uint32_t core_id, size_t function_idx, uint64_t timestamp);
    void remove_pending_access(uint32_t addr);
    
    // Statistics retrieval
    uint64_t get_pending_access_count() const;
    std::vector<MemoryAccessRecord> get_completed_accesses() const;

private:
    bool tracking_enabled = false;
    
    // Memory access tracking
    std::queue<MemoryAccessRecord> completed_memory_accesses;
    std::unordered_map<uint32_t, std::queue<std::tuple<uint32_t, size_t, uint64_t>>> pending_accesses_by_addr; // addr -> (core_id, function_idx, timestamp)
    
    // Previous statistics for calculating differences
    struct CacheStatistics {
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
    
    std::unordered_map<uint32_t, CacheStatistics> prev_statistics; // Per-core previous statistics
    
    // Thread safety
    mutable std::mutex access_mutex;
    
    // Helper methods
    void update_function_statistics(uint32_t core_id, const CacheStatistics& current_stats);
};