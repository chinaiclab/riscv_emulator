#include "../include/system/MultiCoreMonitor.h"
#include "../include/utils/DebugLogger.h"
#include <cstring>
#include <cstdarg>

MultiCoreMonitor::MultiCoreMonitor(uint32_t num_cores)
    : num_cores_(num_cores),
      core_states_(num_cores_, CoreState::HALT),
      release_signal_detected_(false),
      release_signal_cycle_(0),
      release_processed_(false),
      initialization_phase_(true) {

    // Core#0 starts in RUNNING state, others in HALT
    if (num_cores_ > 0) {
        core_states_[0] = CoreState::RUNNING;
    }

    SIM_LOGF("MultiCoreMonitor initialized for %u cores", num_cores_);
    debug_log("MultiCoreMonitor: Core#0=RUNNING, Core#1+=HALT, initialization_phase=true");

    initialize_core_states();
}

void MultiCoreMonitor::initialize_core_states() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    // Initialize all core states - Core#0 runs, others halt
    for (uint32_t core_id = 0; core_id < num_cores_; ++core_id) {
        if (core_id == 0) {
            core_states_[core_id] = CoreState::RUNNING;
        } else {
            core_states_[core_id] = CoreState::HALT;
        }
    }

    debug_log("Initialized %u core states: Core#0=RUNNING, Core#1+=%s",
              num_cores_, core_state_to_string(CoreState::HALT));
}

void MultiCoreMonitor::set_core_state(uint32_t core_id, CoreState state) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!is_valid_core_id(core_id)) {
        debug_log("Invalid core_id %u for state setting", core_id);
        return;
    }

    CoreState old_state = core_states_[core_id];
    core_states_[core_id] = state;

    debug_log("Core#%u state: %s → %s", core_id,
              core_state_to_string(old_state), core_state_to_string(state));
}

MultiCoreMonitor::CoreState MultiCoreMonitor::get_core_state(uint32_t core_id) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!is_valid_core_id(core_id)) {
        return CoreState::ERROR;
    }
    return core_states_[core_id];
}

void MultiCoreMonitor::core_self_report_state(uint32_t core_id, CoreState state) {
    if (!is_valid_core_id(core_id)) {
        debug_log("Core#%u attempted invalid self-report state", core_id);
        return;
    }

    // Core#0 can report any state, other cores have restrictions
    if (core_id != 0) {
        CoreState current_state = get_core_state(core_id);
        // Error cores can't change their state
        if (current_state == CoreState::ERROR) {
            debug_log("Core#%u in ERROR state cannot change state", core_id);
            return;
        }
    }

    set_core_state(core_id, state);
}

bool MultiCoreMonitor::check_release_signal(std::vector<uint8_t>& memory) {
    if (memory.size() < RELEASE_SIGNAL_ADDR + sizeof(uint32_t)) {
        return false;
    }

    uint32_t signal;
    std::memcpy(&signal, memory.data() + RELEASE_SIGNAL_ADDR, sizeof(uint32_t));

    if (signal == RELEASE_SIGNAL_MAGIC) {
        if (!release_signal_detected_.exchange(true)) {
            debug_log("Release signal detected at 0x%08x = 0x%08x", RELEASE_SIGNAL_ADDR, signal);
        }
        return true;
    }

    return false;
}

void MultiCoreMonitor::handle_release_signal(std::vector<uint8_t>& memory, uint32_t current_cycle) {
    SIM_LOGF("MultiCoreMonitor: handle_release_signal called at cycle %u", current_cycle);

    if (!release_signal_detected_ || release_processed_.load()) {
        SIM_LOGF("MultiCoreMonitor: Returning early - detected=%s, processed=%s",
                 release_signal_detected_ ? "true" : "false",
                 release_processed_.load() ? "true" : "false");
        return;
    }

    release_signal_cycle_ = current_cycle;
    debug_log("Processing release signal at cycle %u", current_cycle);
    SIM_LOGF("MultiCoreMonitor: Processing release signal");

    SIM_LOGF("MultiCoreMonitor: Acquiring state_mutex...");
    std::lock_guard<std::mutex> lock(state_mutex_);
    SIM_LOGF("MultiCoreMonitor: state_mutex acquired");

    uint32_t released_count = 0;

    // Release all HALTED cores (except Core#0 which should already be RUNNING)
    for (uint32_t core_id = 1; core_id < num_cores_; ++core_id) {
        if (core_states_[core_id] == CoreState::HALT) {
            SIM_LOGF("MultiCoreMonitor: Releasing Core#%u: HALT → RUNNING", core_id);
            core_states_[core_id] = CoreState::RUNNING;
            released_count++;

            debug_log("Released Core#%u: HALT → RUNNING", core_id);
        }
    }

    SIM_LOGF("MultiCoreMonitor: Setting release_processed_=true");
    release_processed_ = true;

    // Calculate running cores directly to avoid deadlock (we already hold state_mutex_)
    uint32_t running_cores = 0;
    for (uint32_t i = 0; i < num_cores_; ++i) {
        if (core_states_[i] == CoreState::RUNNING) {
            running_cores++;
        }
    }

    debug_log("Release complete: %u cores released, total %u running",
              released_count, running_cores);
    SIM_LOGF("MultiCoreMonitor: Release complete: %u cores released, total %u running",
             released_count, running_cores);

    SIM_LOGF("MultiCoreMonitor: Writing states to memory...");
    // Write updated states to memory directly since we already hold state_mutex_
    if (memory.size() >= CORE_STATES_ADDR + num_cores_ * sizeof(uint32_t)) {
        for (uint32_t core_id = 0; core_id < num_cores_; ++core_id) {
            write_core_state_to_memory(memory, core_id);
        }
        debug_log("Wrote %u core states to memory starting at 0x%08x",
                  num_cores_, CORE_STATES_ADDR);
    } else {
        debug_log("Insufficient memory for core state array");
    }

    // NOTE: Don't clear release signal immediately to allow other cores to detect it
    // SIM_LOGF("MultiCoreMonitor: Keeping release signal at address 0x%08x for other cores to detect", RELEASE_SIGNAL_ADDR);
    // Clear the release signal after processing
    // uint32_t clear_signal = 0;
    // std::memcpy(memory.data() + RELEASE_SIGNAL_ADDR, &clear_signal, sizeof(uint32_t));
    // release_signal_detected_ = false;

    debug_log("Release signal kept in memory for other cores to detect");
    SIM_LOGF("MultiCoreMonitor: handle_release_signal completed successfully");
}

uint32_t MultiCoreMonitor::get_cores_in_state(CoreState state) const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    uint32_t count = 0;

    for (uint32_t core_id = 0; core_id < num_cores_; ++core_id) {
        if (core_states_[core_id] == state) {
            count++;
        }
    }

    return count;
}

bool MultiCoreMonitor::all_cores_idle() const {
    std::lock_guard<std::mutex> lock(state_mutex_);

    for (uint32_t core_id = 0; core_id < num_cores_; ++core_id) {
        if (core_states_[core_id] != CoreState::IDLE && core_states_[core_id] != CoreState::ERROR) {
            return false;
        }
    }

    return true;
}

bool MultiCoreMonitor::has_error_cores() const {
    return get_cores_in_state(CoreState::ERROR) > 0;
}

uint32_t MultiCoreMonitor::get_active_core_count() const {
    return get_cores_in_state(CoreState::RUNNING);
}

void MultiCoreMonitor::halt_all_cores() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    for (uint32_t core_id = 0; core_id < num_cores_; ++core_id) {
        core_states_[core_id] = CoreState::HALT;
    }

    debug_log("All %u cores halted", num_cores_);
}

bool MultiCoreMonitor::should_core_execute(uint32_t core_id) const {
    CoreState state = get_core_state(core_id);

    switch (state) {
        case CoreState::RUNNING:
            return true;
        case CoreState::HALT:
        case CoreState::IDLE:
            return false;
        case CoreState::ERROR:
            // Core#0 in ERROR state can still execute to handle recovery
            return core_id == 0;
        default:
            return false;
    }
}

void MultiCoreMonitor::print_core_states() const {
    std::lock_guard<std::mutex> lock(state_mutex_);

    SIM_LOG("=== MultiCoreMonitor State Report ===");
    for (uint32_t core_id = 0; core_id < num_cores_; ++core_id) {
        SIM_LOGF("  Core#%u: %s", core_id, core_state_to_string(core_states_[core_id]));
    }

    SIM_LOGF("Summary: %u RUNNING, %u HALT, %u IDLE, %u ERROR",
              get_cores_in_state(CoreState::RUNNING),
              get_cores_in_state(CoreState::HALT),
              get_cores_in_state(CoreState::IDLE),
              get_cores_in_state(CoreState::ERROR));
    SIM_LOG("===================================");
}

void MultiCoreMonitor::print_core_summary() const {
    uint32_t running = get_cores_in_state(CoreState::RUNNING);
    uint32_t halted = get_cores_in_state(CoreState::HALT);
    uint32_t idle = get_cores_in_state(CoreState::IDLE);
    uint32_t error = get_cores_in_state(CoreState::ERROR);

    SIM_LOGF("Core Summary: %uR/%uH/%uI/%uE (Total: %u)",
              running, halted, idle, error, num_cores_);
}

void MultiCoreMonitor::reset() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    core_states_.assign(num_cores_, CoreState::HALT);
    if (num_cores_ > 0) {
        core_states_[0] = CoreState::RUNNING;
    }

    release_signal_detected_ = false;
    release_signal_cycle_ = 0;
    release_processed_ = false;
    initialization_phase_ = true;

    debug_log("MultiCoreMonitor reset to initial state (initialization_phase=true)");
}

void MultiCoreMonitor::write_state_to_memory(std::vector<uint8_t>& memory) {
    if (memory.size() < CORE_STATES_ADDR + num_cores_ * sizeof(uint32_t)) {
        debug_log("Insufficient memory for core state array");
        return;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);

    for (uint32_t core_id = 0; core_id < num_cores_; ++core_id) {
        write_core_state_to_memory(memory, core_id);
    }

    debug_log("Wrote %u core states to memory starting at 0x%08x",
              num_cores_, CORE_STATES_ADDR);

    // Also write initialization phase flag to memory
    write_initialization_phase_to_memory(memory);
}

void MultiCoreMonitor::write_core_state_to_memory(std::vector<uint8_t>& memory, uint32_t core_id) {
    if (!is_valid_core_id(core_id)) {
        return;
    }

    uint32_t addr = CORE_STATES_ADDR + core_id * sizeof(uint32_t);
    if (memory.size() < addr + sizeof(uint32_t)) {
        return;
    }

    uint32_t state_value = core_state_to_value(core_states_[core_id]);
    std::memcpy(memory.data() + addr, &state_value, sizeof(uint32_t));
}

uint32_t MultiCoreMonitor::core_state_to_value(CoreState state) const {
    switch (state) {
        case CoreState::HALT:    return 0x01;
        case CoreState::RUNNING: return 0x02;
        case CoreState::IDLE:    return 0x03;
        case CoreState::ERROR:   return 0xFF;
        default:                 return 0x00;
    }
}

MultiCoreMonitor::CoreState MultiCoreMonitor::value_to_core_state(uint32_t value) const {
    switch (value) {
        case 0x01: return CoreState::HALT;
        case 0x02: return CoreState::RUNNING;
        case 0x03: return CoreState::IDLE;
        case 0xFF: return CoreState::ERROR;
        default:   return CoreState::ERROR;
    }
}

const char* MultiCoreMonitor::core_state_to_string(CoreState state) const {
    switch (state) {
        case CoreState::HALT:    return "HALT";
        case CoreState::RUNNING: return "RUNNING";
        case CoreState::IDLE:    return "IDLE";
        case CoreState::ERROR:   return "ERROR";
        default:                 return "UNKNOWN";
    }
}

void MultiCoreMonitor::debug_log(const char* format, ...) const {
#if DEBUG == 1
    va_list args;
    va_start(args, format);

    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);

    SIM_LOGF("[MultiCoreMonitor] %s", buffer);

    va_end(args);
#else
    // Suppress unused parameter warning
    (void)format;
#endif
}

bool MultiCoreMonitor::is_valid_core_id(uint32_t core_id) const {
    if (core_id >= num_cores_) {
        debug_log("Invalid core ID: %u (max: %u)", core_id, num_cores_ - 1);
        return false;
    }
    return true;
}

// Initialization phase management methods
void MultiCoreMonitor::set_initialization_phase(bool is_init) {
    bool old_phase = initialization_phase_.exchange(is_init);
    initialization_phase_ = is_init;
    debug_log("Initialization phase: %s → %s",
              old_phase ? "true" : "false", is_init ? "true" : "false");
    SIM_LOGF("MultiCoreMonitor: initialization_phase set to %s", is_init ? "true" : "false");
}

bool MultiCoreMonitor::get_initialization_phase() const {
    return initialization_phase_.load();
}

void MultiCoreMonitor::write_initialization_phase_to_memory(std::vector<uint8_t>& memory) {
    if (memory.size() < INIT_PHASE_ADDR + sizeof(uint32_t)) {
        debug_log("Insufficient memory for initialization phase flag");
        return;
    }

    uint32_t phase_value = initialization_phase_.load() ? 1 : 0;
    std::memcpy(memory.data() + INIT_PHASE_ADDR, &phase_value, sizeof(uint32_t));
    debug_log("Wrote initialization_phase=%u to memory at 0x%08x", phase_value, INIT_PHASE_ADDR);
}