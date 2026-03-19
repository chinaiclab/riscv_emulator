#pragma once

#include <cstdint>
#include <list>
#include <string>
#include <functional>
#include <mutex>

// Forward declaration for MMU
class MMU;

// Cache configuration structure
struct CacheConfig {
    uint32_t size;          // Total cache size in bytes
    uint32_t associativity; // Number of ways (1 = direct mapped, size/block_size = fully associative)
    uint32_t block_size;    // Block size in bytes
    bool is_instruction_cache; // True for instruction cache, false for data cache
    uint32_t access_latency;   // Access latency in cycles
    uint32_t miss_penalty;     // Miss penalty in cycles
    
    CacheConfig(uint32_t s = 4096, uint32_t a = 4, uint32_t bs = 64, bool ic = false,
                uint32_t al = 1, uint32_t mp = 10)
        : size(s), associativity(a), block_size(bs), is_instruction_cache(ic),
          access_latency(al), miss_penalty(mp) {}
};

// Cache statistics structure
struct CacheStats {
    uint64_t hits;
    uint64_t misses;
    uint64_t accesses;
    uint64_t writebacks;  // Number of writebacks to memory
    uint64_t total_access_time;  // Total time spent in cache accesses (in cycles)
    uint64_t total_miss_penalty; // Total cycles spent on cache misses
    
    // Detailed timing statistics
    uint64_t total_hit_time;     // Total time spent on cache hits (in cycles)
    uint64_t total_miss_time;    // Total time spent on cache misses (in cycles)
    
    // Per-core statistics for multi-core systems
    std::vector<uint64_t> core_hits;
    std::vector<uint64_t> core_misses;
    std::vector<uint64_t> core_accesses;
    std::vector<uint64_t> core_writebacks;
    std::vector<uint64_t> core_access_time;
    std::vector<uint64_t> core_miss_penalty;
    std::vector<uint64_t> core_hit_time;     // Per-core time spent on cache hits
    std::vector<uint64_t> core_miss_time;    // Per-core time spent on cache misses
    
    CacheStats() : hits(0), misses(0), accesses(0), writebacks(0), 
                   total_access_time(0), total_miss_penalty(0),
                   total_hit_time(0), total_miss_time(0) {}
    
    // Initialize per-core statistics for a given number of cores
    void init_core_stats(uint32_t num_cores) {
        core_hits.assign(num_cores, 0);
        core_misses.assign(num_cores, 0);
        core_accesses.assign(num_cores, 0);
        core_writebacks.assign(num_cores, 0);
        core_access_time.assign(num_cores, 0);
        core_miss_penalty.assign(num_cores, 0);
        core_hit_time.assign(num_cores, 0);
        core_miss_time.assign(num_cores, 0);
    }
    
    // Update statistics for a specific core
    void update_core_stats(uint32_t core_id, bool is_hit, uint32_t access_time, uint32_t miss_penalty = 0) {
        if (core_id < core_accesses.size()) {
            core_accesses[core_id]++;
            core_access_time[core_id] += access_time;
            if (is_hit) {
                core_hits[core_id]++;
                core_hit_time[core_id] += access_time;
                total_hit_time += access_time;
            } else {
                core_misses[core_id]++;
                core_miss_penalty[core_id] += miss_penalty;
                core_miss_time[core_id] += access_time;
                total_miss_time += access_time;
            }
        }
    }
    
    // Update writeback statistics for a specific core
    void update_core_writeback(uint32_t core_id) {
        if (core_id < core_writebacks.size()) {
            core_writebacks[core_id]++;
        }
    }
    
    double hit_rate() const {
        return accesses > 0 ? static_cast<double>(hits) / accesses : 0.0;
    }
    
    double miss_rate() const {
        return accesses > 0 ? static_cast<double>(misses) / accesses : 0.0;
    }
    
    double average_access_time() const {
        return accesses > 0 ? static_cast<double>(total_access_time) / accesses : 0.0;
    }
    
    // Get per-core hit rate
    double core_hit_rate(uint32_t core_id) const {
        if (core_id < core_accesses.size() && core_accesses[core_id] > 0) {
            return static_cast<double>(core_hits[core_id]) / core_accesses[core_id];
        }
        return 0.0;
    }
    
    // Get per-core miss rate
    double core_miss_rate(uint32_t core_id) const {
        if (core_id < core_accesses.size() && core_accesses[core_id] > 0) {
            return static_cast<double>(core_misses[core_id]) / core_accesses[core_id];
        }
        return 0.0;
    }
    
    // Get per-core average access time
    double core_average_access_time(uint32_t core_id) const {
        if (core_id < core_accesses.size() && core_accesses[core_id] > 0) {
            return static_cast<double>(core_access_time[core_id]) / core_accesses[core_id];
        }
        return 0.0;
    }
    
    // Get per-core hit time
    uint64_t core_hit_time_total(uint32_t core_id) const {
        if (core_id < core_hit_time.size()) {
            return core_hit_time[core_id];
        }
        return 0;
    }
    
    // Get per-core miss time
    uint64_t core_miss_time_total(uint32_t core_id) const {
        if (core_id < core_miss_time.size()) {
            return core_miss_time[core_id];
        }
        return 0;
    }
    
    // Get average hit time
    double average_hit_time() const {
        return hits > 0 ? static_cast<double>(total_hit_time) / hits : 0.0;
    }
    
    // Get average miss time
    double average_miss_time() const {
        return misses > 0 ? static_cast<double>(total_miss_time) / misses : 0.0;
    }
    
    // Get per-core average hit time
    double core_average_hit_time(uint32_t core_id) const {
        if (core_id < core_hits.size() && core_hits[core_id] > 0) {
            return static_cast<double>(core_hit_time[core_id]) / core_hits[core_id];
        }
        return 0.0;
    }
    
    // Get per-core average miss time
    double core_average_miss_time(uint32_t core_id) const {
        if (core_id < core_misses.size() && core_misses[core_id] > 0) {
            return static_cast<double>(core_miss_time[core_id]) / core_misses[core_id];
        }
        return 0.0;
    }
    
    void reset() {
        hits = 0;
        misses = 0;
        accesses = 0;
        writebacks = 0;
        total_access_time = 0;
        total_miss_penalty = 0;
        total_hit_time = 0;
        total_miss_time = 0;
        
        // Reset per-core statistics
        std::fill(core_hits.begin(), core_hits.end(), 0);
        std::fill(core_misses.begin(), core_misses.end(), 0);
        std::fill(core_accesses.begin(), core_accesses.end(), 0);
        std::fill(core_writebacks.begin(), core_writebacks.end(), 0);
        std::fill(core_access_time.begin(), core_access_time.end(), 0);
        std::fill(core_miss_penalty.begin(), core_miss_penalty.end(), 0);
        std::fill(core_hit_time.begin(), core_hit_time.end(), 0);
        std::fill(core_miss_time.begin(), core_miss_time.end(), 0);
    }
};

// Memory interface for cache to interact with the memory system
class MemoryInterface {
public:
    virtual ~MemoryInterface() = default;
    virtual uint32_t read_word(uint32_t addr) = 0;
    virtual void write_word(uint32_t addr, uint32_t data) = 0;
    virtual void read_block(uint32_t addr, uint32_t* data, uint32_t words) = 0;
    virtual void write_block(uint32_t addr, const uint32_t* data, uint32_t words) = 0;
};

// Cache class for RISC-V emulator
class Cache : public MemoryInterface {
public:
    // Constructor with configurable parameters
    Cache(const CacheConfig& config);
    
    // Copy constructor
    Cache(const Cache& other);
    
    // Assignment operator
    Cache& operator=(const Cache& other);
    
    // Destructor
    ~Cache();
    
    // Set memory interface
    void set_memory_interface(MemoryInterface* mem);

    // Set MMU for virtual address translation
    void set_mmu(MMU* mmu) { this->mmu = mmu; }

    // Set number of cores for per-core statistics
    void set_num_cores(uint32_t num_cores) { stats.init_core_stats(num_cores); }

    // Set L2 miss handler for tracking DDR accesses
    void set_l2_miss_handler(std::function<void(uint32_t, bool, uint32_t, uint32_t)> handler) {
        l2_miss_handler = handler;
    }
    
    // Access cache for read/write operations
    bool read(uint32_t addr, uint32_t& data, bool is_instruction = false, uint32_t core_id = 0);
    bool write(uint32_t addr, uint32_t data, uint32_t core_id = 0);
    
    // Cache management
    void invalidate(uint32_t addr);
    void invalidate_all();
    void flush(uint32_t addr);  // Write back dirty cache line for address
    void flush_range(uint32_t start_addr, uint32_t size);  // Write back dirty cache lines in range
    
    // Statistics
    const CacheStats& get_stats() const { return stats; }
    void reset_stats() { stats.reset(); }
    void print_stats(const std::string& cache_name) const;
    void print_core_stats(const std::string& cache_name, uint32_t core_id) const;

    // MemoryInterface implementation
    uint32_t read_word(uint32_t addr) override;
    void write_word(uint32_t addr, uint32_t data) override;
    void read_block(uint32_t addr, uint32_t* data, uint32_t words) override;
    void write_block(uint32_t addr, const uint32_t* data, uint32_t words) override;

    // Atomic operations
    uint32_t atomic_add(uint32_t addr, uint32_t value, uint32_t core_id = 0);
    uint32_t atomic_swap(uint32_t addr, uint32_t value, uint32_t core_id = 0);
    uint32_t atomic_compare_and_swap(uint32_t addr, uint32_t expected, uint32_t desired, uint32_t core_id = 0);
    uint32_t atomic_fetch_and_add(uint32_t addr, uint32_t value, uint32_t core_id = 0);
    uint32_t atomic_fetch_and_sub(uint32_t addr, uint32_t value, uint32_t core_id = 0);
    uint32_t atomic_fetch_and_and(uint32_t addr, uint32_t value, uint32_t core_id = 0);
    uint32_t atomic_fetch_and_or(uint32_t addr, uint32_t value, uint32_t core_id = 0);
    uint32_t atomic_fetch_and_xor(uint32_t addr, uint32_t value, uint32_t core_id = 0);

private:
    // Cache configuration
    CacheConfig config;

    // Calculated parameters
    uint32_t num_sets;
    uint32_t block_offset_bits;
    uint32_t set_index_bits;
    uint32_t tag_shift;

    // Cache statistics
    CacheStats stats;

    // Memory interface
    MemoryInterface* memory_interface;

    // MMU for virtual address translation
    MMU* mmu;

    // L2 miss handler for tracking DDR access attribution
    std::function<void(uint32_t, bool, uint32_t, uint32_t)> l2_miss_handler;

    // Mutex for thread-safe access to cache
    mutable std::mutex cache_mutex;

    // Cache entry structure
    struct CacheEntry {
        uint32_t tag;
        uint32_t* data;  // Block data
        bool valid;
        bool dirty;
        std::list<uint32_t>::iterator lru_it; // Iterator to LRU position
    };

    // Cache storage - array of sets, each set is an array of ways
    CacheEntry** cache_entries;

    // LRU tracking - one list per set
    std::list<uint32_t>** lru_lists;

    // Internal methods
    void calculate_parameters();
    CacheEntry* find_entry(uint32_t addr, uint32_t& set_index, uint32_t& tag);
    CacheEntry* allocate_entry(uint32_t set_index, uint32_t tag);
    void evict_lru_entry(uint32_t set_index);
    void update_lru(uint32_t set_index, CacheEntry* entry);
    void load_block(uint32_t addr, CacheEntry* entry);
    void write_back_block(uint32_t set_index, uint32_t entry_index);
    uint32_t translate_address(uint32_t addr, bool is_write, bool is_instruction);

    // Helper methods for address decomposition
    uint32_t get_tag(uint32_t addr) const { return addr >> tag_shift; }
    uint32_t get_set_index(uint32_t addr) const { return (addr >> block_offset_bits) & ((1 << set_index_bits) - 1); }
    uint32_t get_block_offset(uint32_t addr) const { return addr & (config.block_size - 1); }
    uint32_t get_block_address(uint32_t addr) const { return addr & ~(config.block_size - 1); }
};