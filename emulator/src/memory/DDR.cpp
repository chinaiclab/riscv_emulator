#include "../memory/DDR.h"
#include <iostream>
#include <cstring>
#include <chrono>

DDRMemory::DDRMemory(const DDRConfig& config)
    : config(config), memory(config.size, 0) {
}

DDRMemory::~DDRMemory() {
}

uint32_t DDRMemory::read_word(uint32_t addr) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    // Calculate access latency
    uint32_t latency = calculate_latency(addr, sizeof(uint32_t));
    total_latency += latency;
    read_count++;

    // Call callback if available
    if (access_complete_callback) {
        access_complete_callback(addr, false, latency);  // false for read
    }

    if (addr + sizeof(uint32_t) <= memory.size()) {
        uint32_t value = memory[addr] |
                         (memory[addr + 1] << 8) |
                         (memory[addr + 2] << 16) |
                         (memory[addr + 3] << 24);
        return value;
    }
    return 0;
}

void DDRMemory::write_word(uint32_t addr, uint32_t data) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    // Calculate access latency
    uint32_t latency = calculate_latency(addr, sizeof(uint32_t));
    total_latency += latency;
    write_count++;

    // Call callback if available
    if (access_complete_callback) {
        access_complete_callback(addr, true, latency);  // true for write
    }

    if (addr + sizeof(uint32_t) <= memory.size()) {
        memory[addr] = data & 0xFF;
        memory[addr + 1] = (data >> 8) & 0xFF;
        memory[addr + 2] = (data >> 16) & 0xFF;
        memory[addr + 3] = (data >> 24) & 0xFF;
    }
}

void DDRMemory::read_block(uint32_t addr, uint32_t* data, uint32_t words) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    // Calculate access latency for the entire block
    uint32_t block_size = words * sizeof(uint32_t);
    uint32_t latency = calculate_latency(addr, block_size);
    total_latency += latency;
    read_count += words;

    for (uint32_t i = 0; i < words; i++) {
        uint32_t word_addr = addr + i * sizeof(uint32_t);
        if (word_addr + sizeof(uint32_t) <= memory.size()) {
            data[i] = memory[word_addr] |
                      (memory[word_addr + 1] << 8) |
                      (memory[word_addr + 2] << 16) |
                      (memory[word_addr + 3] << 24);
        } else {
            data[i] = 0;
        }
    }
}

void DDRMemory::write_block(uint32_t addr, const uint32_t* data, uint32_t words) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    // Calculate access latency for the entire block
    uint32_t block_size = words * sizeof(uint32_t);
    uint32_t latency = calculate_latency(addr, block_size);
    total_latency += latency;
    write_count += words;

    for (uint32_t i = 0; i < words; i++) {
        uint32_t word_addr = addr + i * sizeof(uint32_t);
        if (word_addr + sizeof(uint32_t) <= memory.size()) {
            memory[word_addr] = data[i] & 0xFF;
            memory[word_addr + 1] = (data[i] >> 8) & 0xFF;
            memory[word_addr + 2] = (data[i] >> 16) & 0xFF;
            memory[word_addr + 3] = (data[i] >> 24) & 0xFF;
        }
    }
}

// Atomic operations implementation
uint32_t DDRMemory::atomic_add(uint32_t addr, uint32_t value) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    if (addr + sizeof(uint32_t) <= memory.size()) {
        // Read the current value manually
        uint32_t old_value = memory[addr] |
                             (memory[addr + 1] << 8) |
                             (memory[addr + 2] << 16) |
                             (memory[addr + 3] << 24);

        // Calculate new value
        uint32_t new_value = old_value + value;

        // Write the new value manually
        memory[addr] = new_value & 0xFF;
        memory[addr + 1] = (new_value >> 8) & 0xFF;
        memory[addr + 2] = (new_value >> 16) & 0xFF;
        memory[addr + 3] = (new_value >> 24) & 0xFF;

        // Update statistics
        uint32_t latency = calculate_latency(addr, sizeof(uint32_t));
        total_latency += latency;
        read_count++;  // Count as a read operation
        write_count++; // Count as a write operation

        // Call callback for the read part (first access)
        if (access_complete_callback) {
            access_complete_callback(addr, false, latency);  // false for read
        }

        return old_value;
    }
    return 0;
}

uint32_t DDRMemory::atomic_swap(uint32_t addr, uint32_t value) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    if (addr + sizeof(uint32_t) <= memory.size()) {
        // Read the current value manually
        uint32_t old_value = memory[addr] |
                             (memory[addr + 1] << 8) |
                             (memory[addr + 2] << 16) |
                             (memory[addr + 3] << 24);

        // Write the new value manually
        memory[addr] = value & 0xFF;
        memory[addr + 1] = (value >> 8) & 0xFF;
        memory[addr + 2] = (value >> 16) & 0xFF;
        memory[addr + 3] = (value >> 24) & 0xFF;

        // Update statistics
        uint32_t latency = calculate_latency(addr, sizeof(uint32_t));
        total_latency += latency;
        read_count++;  // Count as a read operation
        write_count++; // Count as a write operation

        // Call callback for the read part (first access)
        if (access_complete_callback) {
            access_complete_callback(addr, false, latency);  // false for read
        }

        return old_value;
    }
    return 0;
}

uint32_t DDRMemory::atomic_compare_and_swap(uint32_t addr, uint32_t expected, uint32_t desired) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    if (addr + sizeof(uint32_t) <= memory.size()) {
        // Read the current value manually
        uint32_t current_value = memory[addr] |
                                 (memory[addr + 1] << 8) |
                                 (memory[addr + 2] << 16) |
                                 (memory[addr + 3] << 24);

        if (current_value == expected) {
            // Write the desired value manually
            memory[addr] = desired & 0xFF;
            memory[addr + 1] = (desired >> 8) & 0xFF;
            memory[addr + 2] = (desired >> 16) & 0xFF;
            memory[addr + 3] = (desired >> 24) & 0xFF;

            // Update statistics
            uint32_t latency = calculate_latency(addr, sizeof(uint32_t));
            total_latency += latency;
            read_count++;  // Count as a read operation
            write_count++; // Count as a write operation
        } else {
            // Only read operation was performed
            uint32_t latency = calculate_latency(addr, sizeof(uint32_t));
            total_latency += latency;
            read_count++;  // Count as a read operation
        }

        return current_value;
    }
    return 0;
}

uint32_t DDRMemory::atomic_fetch_and_add(uint32_t addr, uint32_t value) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    if (addr + sizeof(uint32_t) <= memory.size()) {
        // Read the current value manually
        uint32_t old_value = memory[addr] |
                             (memory[addr + 1] << 8) |
                             (memory[addr + 2] << 16) |
                             (memory[addr + 3] << 24);

        // Calculate new value
        uint32_t new_value = old_value + value;

        // Write the new value manually
        memory[addr] = new_value & 0xFF;
        memory[addr + 1] = (new_value >> 8) & 0xFF;
        memory[addr + 2] = (new_value >> 16) & 0xFF;
        memory[addr + 3] = (new_value >> 24) & 0xFF;

        // Update statistics
        uint32_t latency = calculate_latency(addr, sizeof(uint32_t));
        total_latency += latency;
        read_count++;  // Count as a read operation
        write_count++; // Count as a write operation

        return old_value;
    }
    return 0;
}

uint32_t DDRMemory::atomic_fetch_and_sub(uint32_t addr, uint32_t value) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    if (addr + sizeof(uint32_t) <= memory.size()) {
        // Read the current value manually
        uint32_t old_value = memory[addr] |
                             (memory[addr + 1] << 8) |
                             (memory[addr + 2] << 16) |
                             (memory[addr + 3] << 24);

        // Calculate new value
        uint32_t new_value = old_value - value;

        // Write the new value manually
        memory[addr] = new_value & 0xFF;
        memory[addr + 1] = (new_value >> 8) & 0xFF;
        memory[addr + 2] = (new_value >> 16) & 0xFF;
        memory[addr + 3] = (new_value >> 24) & 0xFF;

        // Update statistics
        uint32_t latency = calculate_latency(addr, sizeof(uint32_t));
        total_latency += latency;
        read_count++;  // Count as a read operation
        write_count++; // Count as a write operation

        return old_value;
    }
    return 0;
}

uint32_t DDRMemory::atomic_fetch_and_and(uint32_t addr, uint32_t value) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    if (addr + sizeof(uint32_t) <= memory.size()) {
        // Read the current value manually
        uint32_t old_value = memory[addr] |
                             (memory[addr + 1] << 8) |
                             (memory[addr + 2] << 16) |
                             (memory[addr + 3] << 24);

        // Calculate new value
        uint32_t new_value = old_value & value;

        // Write the new value manually
        memory[addr] = new_value & 0xFF;
        memory[addr + 1] = (new_value >> 8) & 0xFF;
        memory[addr + 2] = (new_value >> 16) & 0xFF;
        memory[addr + 3] = (new_value >> 24) & 0xFF;

        // Update statistics
        uint32_t latency = calculate_latency(addr, sizeof(uint32_t));
        total_latency += latency;
        read_count++;  // Count as a read operation
        write_count++; // Count as a write operation

        return old_value;
    }
    return 0;
}

uint32_t DDRMemory::atomic_fetch_and_or(uint32_t addr, uint32_t value) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    if (addr + sizeof(uint32_t) <= memory.size()) {
        // Read the current value manually
        uint32_t old_value = memory[addr] |
                             (memory[addr + 1] << 8) |
                             (memory[addr + 2] << 16) |
                             (memory[addr + 3] << 24);

        // Calculate new value
        uint32_t new_value = old_value | value;

        // Write the new value manually
        memory[addr] = new_value & 0xFF;
        memory[addr + 1] = (new_value >> 8) & 0xFF;
        memory[addr + 2] = (new_value >> 16) & 0xFF;
        memory[addr + 3] = (new_value >> 24) & 0xFF;

        // Update statistics
        uint32_t latency = calculate_latency(addr, sizeof(uint32_t));
        total_latency += latency;
        read_count++;  // Count as a read operation
        write_count++; // Count as a write operation

        return old_value;
    }
    return 0;
}

uint32_t DDRMemory::atomic_fetch_and_xor(uint32_t addr, uint32_t value) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    if (addr + sizeof(uint32_t) <= memory.size()) {
        // Read the current value manually
        uint32_t old_value = memory[addr] |
                             (memory[addr + 1] << 8) |
                             (memory[addr + 2] << 16) |
                             (memory[addr + 3] << 24);

        // Calculate new value
        uint32_t new_value = old_value ^ value;

        // Write the new value manually
        memory[addr] = new_value & 0xFF;
        memory[addr + 1] = (new_value >> 8) & 0xFF;
        memory[addr + 2] = (new_value >> 16) & 0xFF;
        memory[addr + 3] = (new_value >> 24) & 0xFF;

        // Update statistics
        uint32_t latency = calculate_latency(addr, sizeof(uint32_t));
        total_latency += latency;
        read_count++;  // Count as a read operation
        write_count++; // Count as a write operation

        return old_value;
    }
    return 0;
}

uint32_t DDRMemory::calculate_latency(uint32_t addr, uint32_t size) {
    // Simplified DDR latency calculation
    // In a real DDR controller, this would be more complex

    // Base latency is CAS latency
    uint32_t latency = config.cas_latency;

    // Add some latency for burst transfers
    uint32_t burst_cycles = (size + config.burst_length - 1) / config.burst_length;
    latency += burst_cycles;

    // Add some latency for page conflicts (simplified)
    // In a real implementation, this would depend on the memory access pattern
    static uint32_t last_page = 0;
    uint32_t current_page = addr / config.page_size;
    if (current_page != last_page) {
        latency += 5; // Page conflict penalty
        last_page = current_page;
    }

    return latency;
}

void DDRMemory::reset_stats() {
    read_count = 0;
    write_count = 0;
    total_latency = 0;
}

void DDRMemory::print_stats(const std::string& name) const {
    std::cout << "===== " << name << " Statistics =====" << std::endl;
    std::cout << "Total reads: " << read_count << std::endl;
    std::cout << "Total writes: " << write_count << std::endl;
    std::cout << "Total operations: " << (read_count + write_count) << std::endl;
    std::cout << "Total latency: " << total_latency << " cycles" << std::endl;
    std::cout << "Average latency: " << get_average_latency() << " cycles" << std::endl;
    std::cout << "=============================" << std::endl;
}