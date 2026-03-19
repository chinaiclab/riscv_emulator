#include "../include/interrupt/PLIC.h"
#include "../include/utils/DebugLogger.h"
#include <cstdarg>
#include <cstring>

PLIC::PLIC(uint32_t num_sources, uint32_t num_targets)
    : num_sources_(num_sources), num_targets_(num_targets) {

    // Validate parameters
    if (num_sources_ == 0 || num_sources_ > MAX_SOURCES) {
        num_sources_ = 32; // Default to 32 sources
    }
    if (num_targets_ == 0 || num_targets_ > MAX_TARGETS) {
        num_targets_ = 4; // Default to 4 targets
    }

    // Initialize priorities (priority 0 = disabled, 1-7 = active levels)
    priorities_.resize(num_sources_, 0);

    // Initialize pending bitmap (32 sources per 32-bit word)
    uint32_t pending_words = (num_sources_ + 31) / 32;
    pending_.resize(pending_words, 0);

    // Initialize enable bitmap per target
    enable_.resize(num_targets_);
    uint32_t enable_words = (num_sources_ + 31) / 32;
    for (uint32_t target = 0; target < num_targets_; target++) {
        enable_[target].resize(enable_words, 0);
    }

    // Initialize thresholds (0 = allow all)
    thresholds_.resize(num_targets_, 0);

    // Initialize claimed interrupts (0 = no claim)
    claimed_.resize(num_targets_, 0);

    debug_log("PLIC initialized: %u sources, %u targets", num_sources_, num_targets_);
    SIM_LOGF("PLIC: Platform Level Interrupt Controller initialized with %u sources, %u targets",
             num_sources_, num_targets_);
}

uint32_t PLIC::read(uint64_t addr, int size) {
    std::lock_guard<std::mutex> lock(plic_mutex_);

    uint64_t offset = addr - PLIC_BASE;
    uint32_t value = 0;

    if (offset >= PRIORITY_BASE && offset < PRIORITY_BASE + num_sources_ * 4) {
        // Source priority register
        uint32_t source_id = (offset - PRIORITY_BASE) / 4;
        if (is_valid_source_id(source_id)) {
            value = priorities_[source_id];
            debug_log("Read priority[%u] = %u", source_id, value);
        }
    } else if (offset >= PENDING_BASE && offset < PENDING_BASE + ((num_sources_ + 31) / 32) * 4) {
        // Pending bitmap
        uint32_t word_index = (offset - PENDING_BASE) / 4;
        value = pending_[word_index];
        debug_log("Read pending[%u] = 0x%08x", word_index, value);
    } else if (offset >= ENABLE_BASE && offset < ENABLE_BASE + num_targets_ * ((num_sources_ + 31) / 32) * 4) {
        // Enable bitmap for target
        uint32_t target_enable_offset = offset - ENABLE_BASE;
        uint32_t target_id = target_enable_offset / (((num_sources_ + 31) / 32) * 4);
        uint32_t word_index = (target_enable_offset % (((num_sources_ + 31) / 32) * 4)) / 4;

        if (is_valid_target_id(target_id) && word_index < enable_[target_id].size()) {
            value = enable_[target_id][word_index];
            debug_log("Read enable[%u][%u] = 0x%08x", target_id, word_index, value);
        }
    } else if (offset >= CLAIM_COMPLETE_BASE && offset < CLAIM_COMPLETE_BASE + num_targets_ * 4) {
        // Claim/complete register
        uint32_t target_id = (offset - CLAIM_COMPLETE_BASE) / 4;
        if (is_valid_target_id(target_id)) {
            value = claim(target_id);
            debug_log("Read claim/complete[%u] = %u", target_id, value);
        }
    } else if (offset >= CLAIM_COMPLETE_BASE + num_targets_ * 4 &&
               offset < CLAIM_COMPLETE_BASE + num_targets_ * 8) {
        // Threshold register
        uint32_t target_id = (offset - CLAIM_COMPLETE_BASE - num_targets_ * 4) / 4;
        if (is_valid_target_id(target_id)) {
            value = thresholds_[target_id];
            debug_log("Read threshold[%u] = %u", target_id, value);
        }
    } else {
        debug_log("Invalid PLIC read: offset=0x%llx, size=%d", (unsigned long long)offset, size);
    }

    return value;
}

void PLIC::write(uint64_t addr, uint32_t value, int size) {
    std::lock_guard<std::mutex> lock(plic_mutex_);

    uint64_t offset = addr - PLIC_BASE;

    if (offset >= PRIORITY_BASE && offset < PRIORITY_BASE + num_sources_ * 4) {
        // Source priority register
        uint32_t source_id = (offset - PRIORITY_BASE) / 4;
        if (is_valid_source_id(source_id)) {
            uint32_t old_value = priorities_[source_id];
            priorities_[source_id] = value & 0x7; // Priority 0-7
            debug_log("Write priority[%u] = %u (was %u)", source_id, priorities_[source_id], old_value);
        }
    } else if (offset >= ENABLE_BASE && offset < ENABLE_BASE + num_targets_ * ((num_sources_ + 31) / 32) * 4) {
        // Enable bitmap for target
        uint32_t target_enable_offset = offset - ENABLE_BASE;
        uint32_t target_id = target_enable_offset / (((num_sources_ + 31) / 32) * 4);
        uint32_t word_index = (target_enable_offset % (((num_sources_ + 31) / 32) * 4)) / 4;

        if (is_valid_target_id(target_id) && word_index < enable_[target_id].size()) {
            uint32_t old_value = enable_[target_id][word_index];
            enable_[target_id][word_index] = value;
            debug_log("Write enable[%u][%u] = 0x%08x (was 0x%08x)", target_id, word_index, value, old_value);
        }
    } else if (offset >= CLAIM_COMPLETE_BASE && offset < CLAIM_COMPLETE_BASE + num_targets_ * 4) {
        // Claim/complete register
        uint32_t target_id = (offset - CLAIM_COMPLETE_BASE) / 4;
        if (is_valid_target_id(target_id)) {
            complete(target_id, value);
            debug_log("Write claim/complete[%u] = %u (complete)", target_id, value);
        }
    } else if (offset >= CLAIM_COMPLETE_BASE + num_targets_ * 4 &&
               offset < CLAIM_COMPLETE_BASE + num_targets_ * 8) {
        // Threshold register
        uint32_t target_id = (offset - CLAIM_COMPLETE_BASE - num_targets_ * 4) / 4;
        if (is_valid_target_id(target_id)) {
            uint32_t old_value = thresholds_[target_id];
            thresholds_[target_id] = value & 0x7; // Threshold 0-7
            debug_log("Write threshold[%u] = %u (was %u)", target_id, thresholds_[target_id], old_value);
        }
    } else {
        debug_log("Invalid PLIC write: offset=0x%llx, value=0x%08x, size=%d",
                 (unsigned long long)offset, value, size);
    }
}

void PLIC::set_pending(uint32_t source_id, bool pending) {
    if (!is_valid_source_id(source_id)) {
        debug_log("Invalid source_id %u for set_pending", source_id);
        return;
    }

    update_pending_bit(source_id, pending);
    debug_log("Set pending[%u] = %s", source_id, pending ? "true" : "false");
}

bool PLIC::is_pending(uint32_t source_id) const {
    if (!is_valid_source_id(source_id)) {
        return false;
    }

    return get_pending_bit(source_id);
}

void PLIC::claim_interrupt(uint32_t target_id, uint32_t source_id) {
    if (!is_valid_target_id(target_id) || !is_valid_source_id(source_id)) {
        debug_log("Invalid target_id %u or source_id %u for claim", target_id, source_id);
        return;
    }

    std::lock_guard<std::mutex> lock(plic_mutex_);
    claimed_[target_id] = source_id;
    update_pending_bit(source_id, false);
    debug_log("Target %u claimed interrupt %u", target_id, source_id);
}

void PLIC::complete_interrupt(uint32_t target_id, uint32_t source_id) {
    complete(target_id, source_id);
}

void PLIC::set_priority(uint32_t source_id, uint32_t priority) {
    if (!is_valid_source_id(source_id)) {
        debug_log("Invalid source_id %u for set_priority", source_id);
        return;
    }

    std::lock_guard<std::mutex> lock(plic_mutex_);
    uint32_t old_priority = priorities_[source_id];
    priorities_[source_id] = priority & 0x7;
    debug_log("Set priority[%u] = %u (was %u)", source_id, priorities_[source_id], old_priority);
}

uint32_t PLIC::get_priority(uint32_t source_id) const {
    if (!is_valid_source_id(source_id)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(plic_mutex_);
    return priorities_[source_id];
}

void PLIC::set_enable(uint32_t target_id, uint32_t source_id, bool enabled) {
    if (!is_valid_target_id(target_id) || !is_valid_source_id(source_id)) {
        debug_log("Invalid target_id %u or source_id %u for set_enable", target_id, source_id);
        return;
    }

    update_enable_bit(target_id, source_id, enabled);
    debug_log("Set enable[%u][%u] = %s", target_id, source_id, enabled ? "true" : "false");
}

bool PLIC::is_enabled(uint32_t target_id, uint32_t source_id) const {
    if (!is_valid_target_id(target_id) || !is_valid_source_id(source_id)) {
        return false;
    }

    return get_enable_bit(target_id, source_id);
}

void PLIC::set_threshold(uint32_t target_id, uint32_t threshold) {
    if (!is_valid_target_id(target_id)) {
        debug_log("Invalid target_id %u for set_threshold", target_id);
        return;
    }

    std::lock_guard<std::mutex> lock(plic_mutex_);
    uint32_t old_threshold = thresholds_[target_id];
    thresholds_[target_id] = threshold & 0x7;
    debug_log("Set threshold[%u] = %u (was %u)", target_id, thresholds_[target_id], old_threshold);
}

uint32_t PLIC::get_threshold(uint32_t target_id) const {
    if (!is_valid_target_id(target_id)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(plic_mutex_);
    return thresholds_[target_id];
}

uint32_t PLIC::claim(uint32_t target_id) {
    if (!is_valid_target_id(target_id)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(plic_mutex_);

    // Return currently claimed interrupt if any
    if (claimed_[target_id] != 0) {
        return claimed_[target_id];
    }

    // Find highest priority pending interrupt
    uint32_t source_id = find_highest_priority_pending(target_id);
    if (source_id != 0) {
        claimed_[target_id] = source_id;
        update_pending_bit(source_id, false);
        debug_log("Target %u claimed interrupt %u", target_id, source_id);
    }

    return source_id;
}

void PLIC::complete(uint32_t target_id, uint32_t source_id) {
    if (!is_valid_target_id(target_id) || !is_valid_source_id(source_id)) {
        debug_log("Invalid target_id %u or source_id %u for complete", target_id, source_id);
        return;
    }

    std::lock_guard<std::mutex> lock(plic_mutex_);

    if (claimed_[target_id] == source_id) {
        claimed_[target_id] = 0;
        debug_log("Target %u completed interrupt %u", target_id, source_id);
    }
}

bool PLIC::has_pending_interrupts(uint32_t target_id) const {
    if (!is_valid_target_id(target_id)) {
        return false;
    }

    return get_highest_priority_pending(target_id) != 0;
}

uint32_t PLIC::get_highest_priority_pending(uint32_t target_id) const {
    if (!is_valid_target_id(target_id)) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(plic_mutex_);
    return find_highest_priority_pending(target_id);
}

void PLIC::reset() {
    std::lock_guard<std::mutex> lock(plic_mutex_);

    std::fill(priorities_.begin(), priorities_.end(), 0);

    for (uint32_t i = 0; i < pending_.size(); i++) {
        pending_[i] = 0;
    }

    for (uint32_t target = 0; target < num_targets_; target++) {
        std::fill(enable_[target].begin(), enable_[target].end(), 0);
    }

    std::fill(thresholds_.begin(), thresholds_.end(), 0);
    std::fill(claimed_.begin(), claimed_.end(), 0);

    debug_log("PLIC reset to initial state");
    SIM_LOGF("PLIC: Reset all interrupt state");
}

bool PLIC::is_valid_source_id(uint32_t source_id) const {
    return source_id > 0 && source_id < num_sources_; // Source 0 is reserved
}

bool PLIC::is_valid_target_id(uint32_t target_id) const {
    return target_id < num_targets_;
}

uint32_t PLIC::find_highest_priority_pending(uint32_t target_id) const {
    uint32_t threshold = thresholds_[target_id];
    uint32_t best_source = 0;
    uint32_t best_priority = 0;

    // Check all sources
    for (uint32_t source = 1; source < num_sources_; source++) {
        if (get_pending_bit(source) && get_enable_bit(target_id, source)) {
            uint32_t priority = priorities_[source];
            if (priority > threshold && priority > best_priority) {
                best_source = source;
                best_priority = priority;
            }
        }
    }

    return best_source;
}

void PLIC::update_enable_bit(uint32_t target_id, uint32_t source_id, bool enabled) {
    uint32_t word_index = source_id / 32;
    uint32_t bit_index = source_id % 32;

    if (word_index < enable_[target_id].size()) {
        if (enabled) {
            enable_[target_id][word_index] |= (1 << bit_index);
        } else {
            enable_[target_id][word_index] &= ~(1 << bit_index);
        }
    }
}

bool PLIC::get_enable_bit(uint32_t target_id, uint32_t source_id) const {
    uint32_t word_index = source_id / 32;
    uint32_t bit_index = source_id % 32;

    if (word_index < enable_[target_id].size()) {
        return (enable_[target_id][word_index] >> bit_index) & 1;
    }

    return false;
}

void PLIC::update_pending_bit(uint32_t source_id, bool pending) {
    uint32_t word_index = source_id / 32;
    uint32_t bit_index = source_id % 32;

    if (word_index < pending_.size()) {
        uint32_t old_value = pending_[word_index];
        uint32_t new_value;

        if (pending) {
            new_value = old_value | (1 << bit_index);
        } else {
            new_value = old_value & ~(1 << bit_index);
        }

        pending_[word_index] = new_value;
    }
}

bool PLIC::get_pending_bit(uint32_t source_id) const {
    uint32_t word_index = source_id / 32;
    uint32_t bit_index = source_id % 32;

    if (word_index < pending_.size()) {
        uint32_t word_value = pending_[word_index];
        return (word_value >> bit_index) & 1;
    }

    return false;
}

void PLIC::debug_log(const char* format, ...) const {
#if DEBUG == 1
    va_list args;
    va_start(args, format);

    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);

    SIM_LOGF("[PLIC] %s", buffer);

    va_end(args);
#else
    // Suppress unused parameter warning
    (void)format;
#endif
}