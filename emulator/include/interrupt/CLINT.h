#pragma once

#include <cstdint>
#include <vector>
#include <mutex>
#include <atomic>
#include <memory>

class CLINT {
public:
    CLINT(uint32_t num_cores);
    ~CLINT() = default;

    // Memory-mapped I/O interface
    uint64_t read(uint64_t addr, int size);
    void write(uint64_t addr, uint64_t value, int size);

    // Timer management for each core
    void set_mtimecmp(uint32_t core_id, uint64_t value);
    uint64_t get_mtimecmp(uint32_t core_id) const;

    // Software interrupt generation
    void generate_software_interrupt(uint32_t core_id);
    void clear_software_interrupt(uint32_t core_id);

    // Timer interrupt management
    void set_timer(uint64_t time);
    uint64_t get_timer() const;
    void update_timer(uint64_t delta_cycles);

    // Interrupt pending status
    bool has_timer_interrupt(uint32_t core_id) const;
    bool has_software_interrupt(uint32_t core_id) const;
    bool has_any_interrupt(uint32_t core_id) const;

    // Clear pending interrupts
    void clear_timer_interrupt(uint32_t core_id);
    void clear_software_interrupt_pending(uint32_t core_id);

    // Reset CLINT state
    void reset();

    // Memory map constants
    static constexpr uint64_t CLINT_BASE = 0x02000000;
    static constexpr uint64_t MSIP_BASE = 0x0000;      // Machine software interrupt pending
    static constexpr uint64_t MTIMECMP_BASE = 0x4000;  // Machine timer compare
    static constexpr uint64_t MTIME = 0xBFF8;          // Machine timer

private:
    uint32_t num_cores_;

    // Timer and compare registers
    uint64_t mtime_;                                   // Machine timer
    std::vector<uint64_t> mtimecmp_;                   // Per-core timer compare

    // Software interrupt pending bits
    std::vector<bool> msip_;                           // Per-core software interrupt pending

    // Mutex for thread-safe access
    mutable std::mutex clint_mutex_;

    // Helper methods
    uint32_t core_id_from_addr(uint64_t addr) const;
    bool is_valid_core_id(uint32_t core_id) const;
    void debug_log(const char* format, ...) const;
};