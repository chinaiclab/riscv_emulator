#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include <atomic>
#include <cstring>
#include "device/UART.h" // Include UART header
#include "Cache.h" // Include Cache header
#include "MMU.h" // Include MMU header
#include "DDR.h" // Include DDR header
#include "Coherence.h" // Include Coherence header
#include "utils/FunctionProfiler.h" // Include FunctionProfiler
#include "utils/PerformanceProfiler.h" // Include PerformanceProfiler
#include "utils/MemoryAccessTracker.h" // Include MemoryAccessTracker
#include "core/Core.h" // Include Core header
#include "system/MultiCoreMonitor.h" // Include MultiCoreMonitor
#include "interrupt/CLINT.h" // Include CLINT header
#include "interrupt/PLIC.h" // Include PLIC header
#include "debug/Debugger.h" // Include Debugger header

// Forward declaration of MMIO device interface
class Device;

class Simulator; // forward declaration for Core back‑reference

// Memory interface implementation for the simulator
class SimulatorMemoryInterface : public MemoryInterface {
public:
    SimulatorMemoryInterface(std::vector<uint8_t>& mem, DDRMemory* ddr = nullptr, Cache* icache = nullptr, Cache* dcache = nullptr)
        : memory(mem), ddr_memory(ddr), instruction_cache(icache), data_cache(dcache) {}

    uint32_t read_word(uint32_t addr) override {
        std::lock_guard<std::mutex> lock(memory_mutex);
        uint32_t value = 0;
        if (addr + sizeof(uint32_t) <= memory.size()) {
            std::memcpy(&value, memory.data() + addr, sizeof(uint32_t));
        }
        // Track read in DDR if available
        if (ddr_memory) {
            ddr_memory->read_word(addr); // Just for tracking, don't use the return value
        }
        return value;
    }

    void write_word(uint32_t addr, uint32_t data) override {
        std::lock_guard<std::mutex> lock(memory_mutex);
        if (addr + sizeof(uint32_t) <= memory.size()) {
            std::memcpy(memory.data() + addr, &data, sizeof(uint32_t));
        }
        // Track write in DDR
        if (ddr_memory) {
            ddr_memory->write_word(addr, data);
        }
        // For page table region, ensure cache coherence by forcing write-back and invalidation
        // This forces the MMU's direct memory interface to see the updated values
        if (data_cache && (addr >= 0x20000 && addr < 0x21000)) {
            // Write the data directly to main memory to bypass cache
            std::memcpy(memory.data() + addr, &data, sizeof(uint32_t));
            // Invalidate the cache line to ensure coherence
            data_cache->invalidate(addr);
        }
        // Also invalidate instruction cache if it overlaps with page table region
        if (instruction_cache && (addr >= 0x20000 && addr < 0x21000)) {
            instruction_cache->invalidate(addr);
        }
    }

    void read_block(uint32_t addr, uint32_t* data, uint32_t words) override {
#if DEBUG
        std::cerr << "[DEBUG] Memory interface reading " << words << " words from addr=0x" << std::hex << addr << std::dec << std::endl;
        std::cerr << "[DEBUG] Memory size: " << std::dec << memory.size() << ", requested addr: 0x" << std::hex << addr << std::dec << std::endl;
#endif
        std::lock_guard<std::mutex> lock(memory_mutex);
        for (uint32_t i = 0; i < words; i++) {
            uint32_t word_addr = addr + i * sizeof(uint32_t);
            if (word_addr + sizeof(uint32_t) <= memory.size()) {
                std::memcpy(&data[i], memory.data() + word_addr, sizeof(uint32_t));
#if DEBUG
                if (i < 4) { // Only print first few for debugging
                    std::cerr << "[DEBUG] Read word " << i << " from addr 0x" << std::hex << word_addr << ": 0x" << data[i] << std::dec << std::endl;
                }
#endif
            } else {
                data[i] = 0;
#if DEBUG
                std::cerr << "[DEBUG] Out of bounds read at addr 0x" << std::hex << word_addr << ", size=" << std::dec << memory.size() << std::endl;
#endif
            }
        }
        // Track read in DDR if available
        if (ddr_memory) {
            ddr_memory->read_block(addr, data, words); // Just for tracking
        }
    }

    void write_block(uint32_t addr, const uint32_t* data, uint32_t words) override {
        std::lock_guard<std::mutex> lock(memory_mutex);
        for (uint32_t i = 0; i < words; i++) {
            uint32_t word_addr = addr + i * sizeof(uint32_t);
            if (word_addr + sizeof(uint32_t) <= memory.size()) {
                std::memcpy(memory.data() + word_addr, &data[i], sizeof(uint32_t));
            }
        }
        // Track write in DDR
        if (ddr_memory) {
            ddr_memory->write_block(addr, data, words);
        }
    }

    // Mutex for thread-safe access
    mutable std::mutex memory_mutex;

private:
    std::vector<uint8_t>& memory;
    DDRMemory* ddr_memory;
    Cache* instruction_cache;
    Cache* data_cache;
};

// Note: FunctionProfile and MemoryAccessRecord structures are now in utils/FunctionProfiler.h

// Forward declaration of Core class (defined in core/Core.h)
class Core;

class Simulator : public MemoryInterface {
public:
    Simulator(uint32_t num_cores = 1, uint32_t mem_size = 0x100000);
    // Run the simulation for a specified number of cycles (default 1000)
    void run(uint32_t cycles = 1000);
    void reset();
    // Load a binary program into memory at address 0
    bool load_program(const std::string &path);
    // Register a MMIO device at a base address
    void register_device(uint32_t base_addr, Device* dev);
    // Access device (used in cores in a full implementation)
    Device* get_device(uint32_t addr) const;
    // Enable or disable instruction trace logging
    void set_trace(bool enable) { trace_enabled = enable; }
    bool get_trace() const { return trace_enabled; }
    // Profiling methods - now using profiler classes
    void set_profiling(bool enable);
    bool get_profiling() const;
    void set_function_profiling(bool enable);
    bool get_function_profiling() const;
    void add_function_profile(uint32_t core_id, const std::string& name, uint64_t start_pc, uint64_t end_pc);
    void print_profiling_results();
    void print_function_profiling_results();

    // Shared resource tracking for function profiling
    void update_function_l2_stats(uint32_t core_id, size_t func_idx, uint64_t l2_hits, uint64_t l2_misses);
    void update_function_ddr_stats(uint32_t core_id, size_t func_idx, uint64_t ddr_reads, uint64_t ddr_writes, uint64_t latency);

    // Access to profiler instances
    FunctionProfiler* get_function_profiler() { return function_profiler; }
    PerformanceProfiler* get_performance_profiler() { return performance_profiler; }
    MemoryAccessTracker* get_memory_tracker() { return memory_tracker; }
    // Cache profiling methods
    void print_cache_stats();

    // Method to initialize callbacks after object construction
    void initialize_callbacks();

    // DDR memory access methods
    DDRMemory* get_ddr_memory() { return &ddr_memory; }
    void print_mmu_stats();
    
    // Cache access
    Cache* get_instruction_cache(uint32_t core_id) { 
        return core_id < l1_icaches.size() ? &l1_icaches[core_id] : nullptr; 
    }
    Cache* get_data_cache(uint32_t core_id) { 
        return core_id < l1_dcaches.size() ? &l1_dcaches[core_id] : nullptr; 
    }
    Cache* get_l2_cache() { return &l2_cache; }
    MMU* get_mmu(uint32_t core_id) {
        return core_id < mmus.size() ? &mmus[core_id] : nullptr;
    }

    // Debugger support
    void set_debug_mode(bool enable);
    bool is_debug_mode() const { return debug_mode; }
    bool check_breakpoint(uint32_t core_id, uint32_t pc) const;
    void enter_debug_loop();
    void debug_run(uint32_t steps = 1);  // Run specified number of steps in debug mode
    Debugger* get_debugger() { return debugger; }
    Core* get_core(uint32_t core_id);
    const std::vector<Core>& get_cores() const { return cores; }

private:
    uint32_t num_cores;
    std::vector<Core> cores;
    std::vector<uint8_t> memory;
    std::unordered_map<uint32_t, Device*> devices; // base address -> device
    bool trace_enabled = false; // default off
        UART* uart_device = nullptr; // UART device instance
    
    // Caches
    // L1 caches (per-core)
    std::vector<Cache> l1_icaches;  // Per-core instruction caches
    std::vector<Cache> l1_dcaches;  // Per-core data caches
    // L2 cache (shared)
    Cache l2_cache;  // Shared L2 cache
    
    // MMUs (per-core)
    std::vector<MMU> mmus;  // Per-core MMUs

    // Direct memory interface for MMU page table walks
    SimulatorMemoryInterface* direct_memory_interface;

    // Cache coherence controller
    CacheCoherence coherence_controller;
    
    // DDR Memory Controller
    DDRMemory ddr_memory;

    // Profiler instances - centralized profiling management
    FunctionProfiler* function_profiler;
    PerformanceProfiler* performance_profiler;
    MemoryAccessTracker* memory_tracker;

    // Memory interface for backward compatibility
    SimulatorMemoryInterface memory_interface;

    // Legacy profiling variables (for backward compatibility)
    bool profiling_enabled = false;
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point end_time;

    // Memory access tracking (legacy)
    std::queue<MemoryAccessRecord> pending_memory_accesses;
    std::unordered_map<uint32_t, std::queue<std::tuple<uint32_t, size_t, uint64_t>>> pending_accesses_by_addr;
    mutable std::mutex memory_access_mutex;

    
    // Boot synchronization
    std::mutex boot_mutex;
    std::condition_variable boot_cv;
    bool core0_booted = false;
    std::vector<bool> core_booted_flags;

    // Multi-core coordination
    MultiCoreMonitor* multi_core_monitor;

    // Interrupt controllers
    CLINT* clint;
    PLIC* plic;

    // Parallel execution support
    std::vector<std::thread> core_threads;
    std::atomic<bool> simulation_running{false};
    std::atomic<uint64_t> global_cycle_count{0};
    bool release_all_cores_called = false;

    // Debugger support
    Debugger* debugger = nullptr;
    bool debug_mode = false;

    // Core execution function (non-blocking)
    void execute_core_step(uint32_t core_id);
public:
    ~Simulator(); // Destructor to clean up devices
    // Accessors for cores to query memory
    size_t get_memory_size() const { return memory.size(); }
    uint8_t* get_memory_ptr() const { return const_cast<uint8_t*>(memory.data()); }

    // Accessor for MultiCoreMonitor
    MultiCoreMonitor* get_multi_core_monitor() const { return multi_core_monitor; }

    // Accessors for interrupt controllers
    CLINT* get_clint() const { return clint; }
    PLIC* get_plic() const { return plic; }
    
    // MemoryInterface implementation for cache access to physical memory
    uint32_t read_word(uint32_t addr) override;
    void write_word(uint32_t addr, uint32_t data) override;
    uint8_t read_byte(uint32_t addr);
    uint16_t read_halfword(uint32_t addr);
    void write_byte(uint32_t addr, uint8_t data);
    void write_halfword(uint32_t addr, uint16_t data);
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

    // Boot synchronization methods
    void wait_for_boot(uint32_t core_id);
    void signal_core_booted(uint32_t core_id);
    void release_all_cores();

    // Memory access tracking methods for function profiling
    void record_memory_access(uint32_t core_id, size_t function_idx, uint32_t addr, bool is_write, uint64_t timestamp);
    void process_completed_memory_accesses();

    // Helper method to get current function index for a core
    size_t get_current_function_index(uint32_t core_id, uint32_t pc);

    // Handle DDR access completion for function profiling
    void handle_ddr_access_completion(uint32_t addr, bool is_write, uint64_t latency);
};
