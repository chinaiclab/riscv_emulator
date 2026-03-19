#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include "Cache.h"

// DDR memory configuration structure
struct DDRConfig {
    uint64_t size;              // Total DDR size in bytes
    uint32_t page_size;         // Page size in bytes
    uint32_t cas_latency;       // CAS latency in cycles
    uint32_t burst_length;      // Burst length
    uint64_t bandwidth;         // Memory bandwidth in bytes/second

    DDRConfig(uint64_t s = 0x40000000, uint32_t ps = 4096, uint32_t cl = 15,
              uint32_t bl = 8, uint64_t bw = 17000000000ULL)  // 17 GB/s default
        : size(s), page_size(ps), cas_latency(cl), burst_length(bl), bandwidth(bw) {}
};

// DDR memory controller class
class DDRMemory : public MemoryInterface {
public:
    // Constructor with configurable parameters
    DDRMemory(const DDRConfig& config);

    // Destructor
    ~DDRMemory();

    // MemoryInterface implementation
    uint32_t read_word(uint32_t addr) override;
    void write_word(uint32_t addr, uint32_t data) override;
    void read_block(uint32_t addr, uint32_t* data, uint32_t words) override;
    void write_block(uint32_t addr, const uint32_t* data, uint32_t words) override;

    // Atomic operations
    uint32_t atomic_add(uint32_t addr, uint32_t value);
    uint32_t atomic_swap(uint32_t addr, uint32_t value);
    uint32_t atomic_compare_and_swap(uint32_t addr, uint32_t expected, uint32_t desired);
    uint32_t atomic_fetch_and_add(uint32_t addr, uint32_t value);
    uint32_t atomic_fetch_and_sub(uint32_t addr, uint32_t value);
    uint32_t atomic_fetch_and_and(uint32_t addr, uint32_t value);
    uint32_t atomic_fetch_and_or(uint32_t addr, uint32_t value);
    uint32_t atomic_fetch_and_xor(uint32_t addr, uint32_t value);

    // Statistics
    uint64_t get_read_count() const { return read_count; }
    uint64_t get_write_count() const { return write_count; }
    uint64_t get_total_latency() const { return total_latency; }
    double get_average_latency() const {
        uint64_t total_ops = read_count + write_count;
        return total_ops > 0 ? static_cast<double>(total_latency) / total_ops : 0.0;
    }
    void reset_stats();
    void print_stats(const std::string& name) const;

private:
    // DDR configuration
    DDRConfig config;

    // Memory storage
    std::vector<uint8_t> memory;

    // Mutex for thread-safe access to memory
    mutable std::mutex memory_mutex;

    // Statistics
    std::atomic<uint64_t> read_count{0};
    std::atomic<uint64_t> write_count{0};
    std::atomic<uint64_t> total_latency{0};

    // Internal methods
    uint32_t calculate_latency(uint32_t addr, uint32_t size);

public:
    // Callback function type for when memory access completes
    using AccessCompleteCallback = void(*)(uint32_t addr, bool is_write, uint64_t latency);

    // Set callback function to be called when memory access completes
    void set_access_complete_callback(AccessCompleteCallback callback) {
        access_complete_callback = callback;
    }

private:
    // Callback function to be called when access completes
    AccessCompleteCallback access_complete_callback = nullptr;
};