#pragma once

#include <cstdint>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

class PLIC {
public:
    PLIC(uint32_t num_sources, uint32_t num_targets);
    ~PLIC() = default;

    // Memory-mapped I/O interface
    uint32_t read(uint64_t addr, int size);
    void write(uint64_t addr, uint32_t value, int size);

    // Interrupt source management
    void set_pending(uint32_t source_id, bool pending);
    bool is_pending(uint32_t source_id) const;
    void claim_interrupt(uint32_t target_id, uint32_t source_id);
    void complete_interrupt(uint32_t target_id, uint32_t source_id);

    // Priority and enable management
    void set_priority(uint32_t source_id, uint32_t priority);
    uint32_t get_priority(uint32_t source_id) const;
    void set_enable(uint32_t target_id, uint32_t source_id, bool enabled);
    bool is_enabled(uint32_t target_id, uint32_t source_id) const;

    // Threshold management for each target (HART)
    void set_threshold(uint32_t target_id, uint32_t threshold);
    uint32_t get_threshold(uint32_t target_id) const;

    // Interrupt claim and completion
    uint32_t claim(uint32_t target_id);
    void complete(uint32_t target_id, uint32_t source_id);

    // Check if target has pending interrupts above threshold
    bool has_pending_interrupts(uint32_t target_id) const;

    // Get highest priority pending interrupt for target
    uint32_t get_highest_priority_pending(uint32_t target_id) const;

    // Reset PLIC state
    void reset();

    // Memory map constants (based on SiFive U54 PLIC)
    static constexpr uint64_t PLIC_BASE = 0x0C000000;
    static constexpr uint64_t PRIORITY_BASE = 0x000000;    // Source priorities
    static constexpr uint64_t PENDING_BASE = 0x001000;    // Pending bits
    static constexpr uint64_t ENABLE_BASE = 0x002000;     // Enable bits per target
    static constexpr uint64_t CLAIM_COMPLETE_BASE = 0x200000;  // Claim/complete per target

    // Maximum number of sources and targets
    static constexpr uint32_t MAX_SOURCES = 1024;
    static constexpr uint32_t MAX_TARGETS = 15872;  // Maximum per PLIC spec

private:
    uint32_t num_sources_;
    uint32_t num_targets_;

    // Interrupt source priorities
    std::vector<uint32_t> priorities_;

    // Pending interrupt bits (bitmap)
    std::vector<uint32_t> pending_;

    // Enable bits per target (bitmap)
    std::vector<std::vector<uint32_t>> enable_;

    // Threshold per target
    std::vector<uint32_t> thresholds_;

    // Currently claimed interrupts per target
    std::vector<uint32_t> claimed_;

    // Mutex for thread-safe access
    mutable std::mutex plic_mutex_;

    // Helper methods
    bool is_valid_source_id(uint32_t source_id) const;
    bool is_valid_target_id(uint32_t target_id) const;
    uint32_t find_highest_priority_pending(uint32_t target_id) const;
    void update_enable_bit(uint32_t target_id, uint32_t source_id, bool enabled);
    bool get_enable_bit(uint32_t target_id, uint32_t source_id) const;
    void update_pending_bit(uint32_t source_id, bool pending);
    bool get_pending_bit(uint32_t source_id) const;
    void debug_log(const char* format, ...) const;

    // Address calculation helpers
    uint64_t priority_addr(uint32_t source_id) const;
    uint64_t pending_addr(uint32_t source_id) const;
    uint64_t enable_addr(uint32_t target_id, uint32_t source_id) const;
    uint64_t claim_complete_addr(uint32_t target_id) const;
};