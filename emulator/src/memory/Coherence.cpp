#include "../memory/Coherence.h"
#include <iostream>
#include <algorithm>

CacheCoherence::CacheCoherence(uint32_t num_cores) 
    : num_cores(num_cores) {
    // Initialize directory with empty vectors for each core
    // The directory will be populated dynamically as cache lines are accessed
}

CoherenceState CacheCoherence::handle_read(uint32_t core_id, uint32_t address) {
    // Check if this address is already in the directory
    auto it = directory.find(address);
    
    if (it == directory.end()) {
        // First access to this address
        directory[address] = std::vector<CoherenceState>(num_cores, CoherenceState::INVALID);
        directory[address][core_id] = CoherenceState::SHARED;
        stats.read_misses++;
        return CoherenceState::SHARED;
    }
    
    std::vector<CoherenceState>& states = it->second;
    
    // Check current state for this core
    CoherenceState current_state = states[core_id];
    
    if (current_state == CoherenceState::INVALID) {
        // Cache miss - need to fetch from memory or another core
        stats.read_misses++;
        
        // Check if any other core has the line in MODIFIED state
        bool modified_found = false;
        for (uint32_t i = 0; i < num_cores; i++) {
            if (i != core_id && states[i] == CoherenceState::MODIFIED) {
                // Need to get the updated data from the owner core
                modified_found = true;
                break;
            }
        }
        
        // Set this core's state to SHARED
        states[core_id] = CoherenceState::SHARED;
        
        // If another core had it MODIFIED, that core should now have it SHARED
        if (modified_found) {
            for (uint32_t i = 0; i < num_cores; i++) {
                if (i != core_id && states[i] == CoherenceState::MODIFIED) {
                    states[i] = CoherenceState::SHARED;
                }
            }
        }
        
        return CoherenceState::SHARED;
    } else {
        // Cache hit
        stats.read_hits++;
        return current_state;
    }
}

CoherenceState CacheCoherence::handle_write(uint32_t core_id, uint32_t address) {
    // Check if this address is already in the directory
    auto it = directory.find(address);
    
    if (it == directory.end()) {
        // First access to this address
        directory[address] = std::vector<CoherenceState>(num_cores, CoherenceState::INVALID);
        directory[address][core_id] = CoherenceState::MODIFIED;
        stats.write_misses++;
        return CoherenceState::MODIFIED;
    }
    
    std::vector<CoherenceState>& states = it->second;
    
    // Check current state for this core
    CoherenceState current_state = states[core_id];
    
    if (current_state == CoherenceState::INVALID) {
        // Cache miss - need to fetch from memory or another core
        stats.write_misses++;
        
        // Invalidate other cores that have this line
        for (uint32_t i = 0; i < num_cores; i++) {
            if (i != core_id && states[i] != CoherenceState::INVALID) {
                states[i] = CoherenceState::INVALID;
                stats.invalidations++;
            }
        }
        
        // Set this core's state to MODIFIED
        states[core_id] = CoherenceState::MODIFIED;
        
        return CoherenceState::MODIFIED;
    } else if (current_state == CoherenceState::SHARED) {
        // Write hit in SHARED state - need to invalidate other cores
        stats.write_hits++;
        
        // Invalidate other cores that have this line
        for (uint32_t i = 0; i < num_cores; i++) {
            if (i != core_id && states[i] != CoherenceState::INVALID) {
                states[i] = CoherenceState::INVALID;
                stats.invalidations++;
            }
        }
        
        // Set this core's state to MODIFIED
        states[core_id] = CoherenceState::MODIFIED;
        
        return CoherenceState::MODIFIED;
    } else {
        // Write hit in MODIFIED state
        stats.write_hits++;
        return CoherenceState::MODIFIED;
    }
}

void CacheCoherence::invalidate_other_cores(uint32_t core_id, uint32_t address) {
    auto it = directory.find(address);
    if (it != directory.end()) {
        std::vector<CoherenceState>& states = it->second;
        for (uint32_t i = 0; i < num_cores; i++) {
            if (i != core_id && states[i] != CoherenceState::INVALID) {
                states[i] = CoherenceState::INVALID;
                stats.invalidations++;
            }
        }
    }
}

void CacheCoherence::broadcast_update(uint32_t core_id, uint32_t address) {
    // In a simple implementation, we just count the broadcast
    stats.broadcasts++;
    
    // In a more complex implementation, we might update all cores' states
    // For now, we'll just invalidate other cores
    invalidate_other_cores(core_id, address);
}

void CacheCoherence::print_stats() const {
    std::cout << "===== Cache Coherence Statistics =====" << std::endl;
    std::cout << "Read hits: " << stats.read_hits << std::endl;
    std::cout << "Read misses: " << stats.read_misses << std::endl;
    std::cout << "Write hits: " << stats.write_hits << std::endl;
    std::cout << "Write misses: " << stats.write_misses << std::endl;
    std::cout << "Invalidations: " << stats.invalidations << std::endl;
    std::cout << "Broadcasts: " << stats.broadcasts << std::endl;
    std::cout << "=====================================" << std::endl;
}