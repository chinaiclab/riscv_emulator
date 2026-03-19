#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>

// Cache coherence states
enum class CoherenceState {
    INVALID = 0,
    SHARED = 1,
    MODIFIED = 2
};

// Cache line structure for coherence tracking
struct CoherenceLine {
    uint32_t address;           // Memory address
    CoherenceState state;       // Coherence state
    uint32_t owner_core;        // Core that owns the line (for Modified state)
    
    CoherenceLine() : address(0), state(CoherenceState::INVALID), owner_core(0) {}
    CoherenceLine(uint32_t addr) : address(addr), state(CoherenceState::INVALID), owner_core(0) {}
};

// Cache coherence protocol statistics
struct CoherenceStats {
    uint64_t read_hits;
    uint64_t read_misses;
    uint64_t write_hits;
    uint64_t write_misses;
    uint64_t invalidations;
    uint64_t broadcasts;
    
    CoherenceStats() : read_hits(0), read_misses(0), write_hits(0), write_misses(0), 
                       invalidations(0), broadcasts(0) {}
    
    void reset() {
        read_hits = 0;
        read_misses = 0;
        write_hits = 0;
        write_misses = 0;
        invalidations = 0;
        broadcasts = 0;
    }
};

// Simple MSI cache coherence protocol
class CacheCoherence {
public:
    CacheCoherence(uint32_t num_cores);
    
    // Handle read request from a core
    CoherenceState handle_read(uint32_t core_id, uint32_t address);
    
    // Handle write request from a core
    CoherenceState handle_write(uint32_t core_id, uint32_t address);
    
    // Invalidate a cache line in all other cores
    void invalidate_other_cores(uint32_t core_id, uint32_t address);
    
    // Broadcast a cache line update to all cores
    void broadcast_update(uint32_t core_id, uint32_t address);
    
    // Statistics
    const CoherenceStats& get_stats() const { return stats; }
    void reset_stats() { stats.reset(); }
    void print_stats() const;

private:
    uint32_t num_cores;
    
    // Coherence directory - tracks which cores have copies of each cache line
    std::unordered_map<uint32_t, std::vector<CoherenceState>> directory;
    
    // Statistics
    CoherenceStats stats;
};