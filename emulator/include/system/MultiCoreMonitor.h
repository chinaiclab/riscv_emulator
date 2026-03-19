#pragma once

#include <cstdint>
#include <vector>
#include <mutex>
#include <atomic>

/**
 * Enhanced Multi-Core State Monitor
 *
 * Supports up to 8192 cores with comprehensive state management.
 * States: HALT, RUNNING, IDLE, ERROR
 *
 * Core State Lifecycle:
 * Core#0: RUNNING → IDLE (after program execution)
 * Core#1+: HALT → RUNNING → IDLE (after release and execution)
 * Any core: ERROR (if invalid state detected)
 */
class MultiCoreMonitor {
public:
    // Core states - enhanced design
    enum class CoreState {
        HALT     = 0x01,    // Core is halted (initial state for cores > 0)
        RUNNING  = 0x02,    // Core is actively executing
        IDLE     = 0x03,    // Core is idle (finished execution, in infinite loop/waiting)
        ERROR    = 0xFF     // Core encountered an error (should stop execution)
    };

    // Memory locations for multi-core synchronization
    // Core state array: 0x90000-0x97FFF (8192 cores * 4 bytes each)
    static constexpr uint32_t CORE_STATES_ADDR = 0x90000;      // Start of core state array
    static constexpr uint32_t RELEASE_SIGNAL_ADDR = 0x98000;   // Core#0 writes 0xDEADBEEF here to release cores
    static constexpr uint32_t INIT_PHASE_ADDR = 0x98004;       // Initialization phase flag (1=init, 0=normal execution)
    static constexpr uint32_t MAX_CORES = 8192;                // Maximum supported cores
    static constexpr uint32_t CORE_STATE_ARRAY_SIZE = MAX_CORES * sizeof(uint32_t);  // 32KB

    static constexpr uint32_t RELEASE_SIGNAL_MAGIC = 0xDEADBEEF;

public:
    explicit MultiCoreMonitor(uint32_t num_cores);
    ~MultiCoreMonitor() = default;

    // Core state management
    void set_core_state(uint32_t core_id, CoreState state);
    CoreState get_core_state(uint32_t core_id) const;

    // Core self-reporting (cores write their own state)
    void core_self_report_state(uint32_t core_id, CoreState state);

    // Release signal management
    bool check_release_signal(std::vector<uint8_t>& memory);
    void handle_release_signal(std::vector<uint8_t>& memory, uint32_t current_cycle);

    // State queries
    uint32_t get_cores_in_state(CoreState state) const;
    bool all_cores_idle() const;
    bool has_error_cores() const;
    uint32_t get_active_core_count() const;

    // Core lifecycle management
    void initialize_core_states();
    void halt_all_cores();
    bool should_core_execute(uint32_t core_id) const;

    // Initialization phase management
    void set_initialization_phase(bool is_init);
    bool get_initialization_phase() const;
    void write_initialization_phase_to_memory(std::vector<uint8_t>& memory);

    // Memory synchronization
    void write_state_to_memory(std::vector<uint8_t>& memory);

    // Debug and logging (enabled with DDEBUG=1)
    void print_core_states() const;
    void print_core_summary() const;
    void reset();

private:
    uint32_t num_cores_;

    // Core state tracking (protected by mutex)
    mutable std::mutex state_mutex_;
    std::vector<CoreState> core_states_;

    // Release signal tracking
    std::atomic<bool> release_signal_detected_;
    std::atomic<uint32_t> release_signal_cycle_;
    std::atomic<bool> release_processed_;

    // Initialization phase tracking
    std::atomic<bool> initialization_phase_;

    // Helper methods
    void write_core_state_to_memory(std::vector<uint8_t>& memory, uint32_t core_id);
    uint32_t core_state_to_value(CoreState state) const;
    CoreState value_to_core_state(uint32_t value) const;
    const char* core_state_to_string(CoreState state) const;

    // Debug helper
    void debug_log(const char* format, ...) const;

    // Validate core ID
    bool is_valid_core_id(uint32_t core_id) const;
};