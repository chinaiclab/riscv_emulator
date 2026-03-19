#include "../include/interrupt/CLINT.h"
#include "../include/utils/DebugLogger.h"
#include <cstdarg>
#include <cstring>
#include <iostream>

CLINT::CLINT(uint32_t num_cores)
    : num_cores_(num_cores), mtime_(0) {

    // Initialize timer compare registers
    mtimecmp_.resize(num_cores_, 0xFFFFFFFFFFFFFFFFULL);

    // Initialize software interrupt pending bits
    msip_.resize(num_cores_, false);

    debug_log("CLINT initialized for %u cores", num_cores_);
    SIM_LOGF("CLINT: Core Local Interrupt Timer initialized with %u cores", num_cores_);
}

uint64_t CLINT::read(uint64_t addr, int size) {
    std::lock_guard<std::mutex> lock(clint_mutex_);

    uint64_t offset = addr - CLINT_BASE;
    uint64_t value = 0;

    if (offset >= MSIP_BASE && offset < MSIP_BASE + num_cores_ * 4) {
        // Machine software interrupt pending register
        uint32_t core_id = (offset - MSIP_BASE) / 4;
        if (is_valid_core_id(core_id)) {
            value = msip_[core_id] ? 1 : 0;
            debug_log("Read MSIP[%u] = %u", core_id, (uint32_t)value);
        }
    } else if (offset >= MTIMECMP_BASE && offset < MTIMECMP_BASE + num_cores_ * 8) {
        // Machine timer compare register
        uint32_t core_id = (offset - MTIMECMP_BASE) / 8;
        if (is_valid_core_id(core_id)) {
            value = mtimecmp_[core_id];
            debug_log("Read MTIMECMP[%u] = 0x%llx", core_id, (unsigned long long)value);
        }
    } else if (offset == MTIME && size >= 8) {
        // Machine timer register
        value = mtime_;
        debug_log("Read MTIME = 0x%llx", (unsigned long long)value);
    } else {
        debug_log("Invalid CLINT read: offset=0x%llx, size=%d", (unsigned long long)offset, size);
    }

    return value;
}

void CLINT::write(uint64_t addr, uint64_t value, int size) {
    std::lock_guard<std::mutex> lock(clint_mutex_);

    uint64_t offset = addr - CLINT_BASE;

    if (offset >= MSIP_BASE && offset < MSIP_BASE + num_cores_ * 4) {
        // Machine software interrupt pending register
        uint32_t core_id = (offset - MSIP_BASE) / 4;
        if (is_valid_core_id(core_id)) {
            bool old_pending = msip_[core_id];
            bool new_pending = (value != 0);
            msip_[core_id] = new_pending;
            std::cout << "[CLINT] Write MSIP[" << core_id << "] = " << new_pending << " (was " << old_pending << ")" << std::endl;
        }
    } else if (offset >= MTIMECMP_BASE && offset < MTIMECMP_BASE + num_cores_ * 8) {
        // Machine timer compare register
        uint32_t core_id = (offset - MTIMECMP_BASE) / 8;
        if (is_valid_core_id(core_id)) {
            uint64_t old_value = mtimecmp_[core_id];
            mtimecmp_[core_id] = value;
            std::cout << "[CLINT] Write MTIMECMP[" << core_id << "] = 0x" << std::hex << value << std::dec
                      << " (was 0x" << std::hex << old_value << std::dec << ")" << std::endl;
        }
    } else if (offset == MTIME && size >= 8) {
        // Machine timer register (read-only, but allow writes for debugging)
        uint64_t old_value = mtime_;
        mtime_ = value;
        std::cout << "[CLINT] Write MTIME = 0x" << std::hex << value << std::dec
                  << " (was 0x" << std::hex << old_value << std::dec << ") - WARNING" << std::endl;
    } else {
        std::cout << "[CLINT] Invalid write: addr=0x" << std::hex << addr << std::dec
                  << " offset=0x" << std::hex << offset << std::dec
                  << " value=0x" << std::hex << value << std::dec << std::endl;
    }
}

void CLINT::set_mtimecmp(uint32_t core_id, uint64_t value) {
    if (!is_valid_core_id(core_id)) {
        debug_log("Invalid core_id %u for set_mtimecmp", core_id);
        return;
    }

    std::lock_guard<std::mutex> lock(clint_mutex_);
    uint64_t old_value = mtimecmp_[core_id];
    mtimecmp_[core_id] = value;
    debug_log("Set MTIMECMP[%u] = 0x%llx (was 0x%llx)", core_id,
             (unsigned long long)value, (unsigned long long)old_value);
}

uint64_t CLINT::get_mtimecmp(uint32_t core_id) const {
    if (!is_valid_core_id(core_id)) {
        debug_log("Invalid core_id %u for get_mtimecmp", core_id);
        return 0;
    }

    std::lock_guard<std::mutex> lock(clint_mutex_);
    return mtimecmp_[core_id];
}

void CLINT::generate_software_interrupt(uint32_t core_id) {
    if (!is_valid_core_id(core_id)) {
        debug_log("Invalid core_id %u for software interrupt", core_id);
        return;
    }

    bool old_pending = msip_[core_id];
    msip_[core_id] = true;
    if (!old_pending) {
        debug_log("Generated software interrupt for core#%u", core_id);
        SIM_LOGF("CLINT: Software interrupt generated for core#%u", core_id);
    }
}

void CLINT::clear_software_interrupt(uint32_t core_id) {
    if (!is_valid_core_id(core_id)) {
        debug_log("Invalid core_id %u for clearing software interrupt", core_id);
        return;
    }

    bool old_pending = msip_[core_id];
    msip_[core_id] = false;
    if (old_pending) {
        debug_log("Cleared software interrupt for core#%u", core_id);
    }
}

void CLINT::set_timer(uint64_t time) {
    std::lock_guard<std::mutex> lock(clint_mutex_);
    uint64_t old_time = mtime_;
    mtime_ = time;
    debug_log("Set timer to 0x%llx (was 0x%llx)", (unsigned long long)time, (unsigned long long)old_time);
}

uint64_t CLINT::get_timer() const {
    std::lock_guard<std::mutex> lock(clint_mutex_);
    return mtime_;
}

void CLINT::update_timer(uint64_t delta_cycles) {
    std::lock_guard<std::mutex> lock(clint_mutex_);
    uint64_t old_time = mtime_;
    mtime_ += delta_cycles;

    // Check for timer interrupts on any core
    for (uint32_t core_id = 0; core_id < num_cores_; core_id++) {
        if (mtime_ >= mtimecmp_[core_id] && old_time < mtimecmp_[core_id]) {
            debug_log("Timer interrupt triggered for core#%u (mtime=0x%llx >= mtimecmp=0x%llx)",
                     core_id, (unsigned long long)mtime_, (unsigned long long)mtimecmp_[core_id]);
            SIM_LOGF("CLINT: Timer interrupt triggered for core#%u", core_id);
        }
    }
}

bool CLINT::has_timer_interrupt(uint32_t core_id) const {
    if (!is_valid_core_id(core_id)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(clint_mutex_);
    return mtime_ >= mtimecmp_[core_id];
}

bool CLINT::has_software_interrupt(uint32_t core_id) const {
    if (!is_valid_core_id(core_id)) {
        return false;
    }

    return msip_[core_id];
}

bool CLINT::has_any_interrupt(uint32_t core_id) const {
    return has_timer_interrupt(core_id) || has_software_interrupt(core_id);
}

void CLINT::clear_timer_interrupt(uint32_t core_id) {
    if (!is_valid_core_id(core_id)) {
        debug_log("Invalid core_id %u for clearing timer interrupt", core_id);
        return;
    }

    std::lock_guard<std::mutex> lock(clint_mutex_);
    // Clear timer interrupt by setting mtimecmp to maximum value
    mtimecmp_[core_id] = 0xFFFFFFFFFFFFFFFFULL;
    debug_log("Cleared timer interrupt for core#%u (set mtimecmp to max)", core_id);
}

void CLINT::clear_software_interrupt_pending(uint32_t core_id) {
    clear_software_interrupt(core_id);
}

void CLINT::reset() {
    std::lock_guard<std::mutex> lock(clint_mutex_);

    mtime_ = 0;
    std::fill(mtimecmp_.begin(), mtimecmp_.end(), 0xFFFFFFFFFFFFFFFFULL);

    for (uint32_t i = 0; i < num_cores_; i++) {
        msip_[i] = false;
    }

    debug_log("CLINT reset to initial state");
    SIM_LOGF("CLINT: Reset all timer and interrupt state");
}

uint32_t CLINT::core_id_from_addr(uint64_t addr) const {
    uint64_t offset = addr - CLINT_BASE;

    if (offset >= MSIP_BASE && offset < MSIP_BASE + num_cores_ * 4) {
        return (offset - MSIP_BASE) / 4;
    } else if (offset >= MTIMECMP_BASE && offset < MTIMECMP_BASE + num_cores_ * 8) {
        return (offset - MTIMECMP_BASE) / 8;
    }

    return num_cores_; // Invalid core ID
}

bool CLINT::is_valid_core_id(uint32_t core_id) const {
    return core_id < num_cores_;
}

void CLINT::debug_log(const char* format, ...) const {
#if DEBUG == 1
    va_list args;
    va_start(args, format);

    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);

    SIM_LOGF("[CLINT] %s", buffer);

    va_end(args);
#else
    // Suppress unused parameter warning
    (void)format;
#endif
}