#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <list>

// Forward declaration for MemoryInterface
class MemoryInterface;

// Page table entry structure
struct PageTableEntry {
    bool present;        // Present bit (V)
    bool readable;       // Readable bit (R)
    bool writable;       // Writable bit (W)
    bool executable;     // Executable bit (X)
    bool user;           // User mode accessible (U)
    uint32_t frame;      // Physical frame number (PPN)
    bool accessed;       // Accessed bit (A)
    bool dirty;          // Dirty bit (D)

    PageTableEntry() : present(false), readable(false), writable(false),
                       executable(false), user(false), frame(0),
                       accessed(false), dirty(false) {}
};

// TLB entry structure
struct TLBEntry {
    uint32_t virtual_page;  // Virtual page number
    uint32_t physical_frame; // Physical frame number
    bool writable;          // Writable bit
    bool user;              // User mode accessible
    bool valid;             // Valid bit
    
    TLBEntry() : virtual_page(0), physical_frame(0), writable(false), 
                 user(false), valid(false) {}
};

// MMU configuration structure
struct MMUConfig {
    uint32_t page_size;          // Page size in bytes (typically 4KB)
    uint32_t tlb_entries;        // Number of TLB entries
    bool enable_tlb;             // Enable TLB caching
    
    MMUConfig(uint32_t ps = 4096, uint32_t te = 32, bool et = true)
        : page_size(ps), tlb_entries(te), enable_tlb(et) {}
};

// Exception codes
enum class ExceptionType {
    NONE = 0,

    // Exceptions (cause < 16)
    INSTRUCTION_ADDRESS_MISALIGNED = 1,
    INSTRUCTION_ACCESS_FAULT = 2,
    ILLEGAL_INSTRUCTION = 3,
    BREAKPOINT = 4,
    LOAD_ADDRESS_MISALIGNED = 5,
    LOAD_ACCESS_FAULT = 6,
    STORE_AMO_ADDRESS_MISALIGNED = 7,
    STORE_AMO_ACCESS_FAULT = 8,
    ECALL_USER_MODE = 9,
    ECALL_SUPERVISOR_MODE = 10,
    ECALL_MACHINE_MODE = 11,
    INSTRUCTION_PAGE_FAULT = 12,
    LOAD_PAGE_FAULT = 13,
    STORE_AMO_PAGE_FAULT = 15,

    // Interrupts (cause >= 16, high bit set in mcause)
    MACHINE_SOFTWARE_INTERRUPT = 3,      // Bit 16 + 3 = 19
    MACHINE_TIMER_INTERRUPT = 7,         // Bit 16 + 7 = 23
    MACHINE_EXTERNAL_INTERRUPT = 11      // Bit 16 + 11 = 27
};

// MMU statistics structure
struct MMUStats {
    uint64_t tlb_hits;
    uint64_t tlb_misses;
    uint64_t page_faults;
    
    // Per-core statistics
    std::vector<uint64_t> core_tlb_hits;
    std::vector<uint64_t> core_tlb_misses;
    std::vector<uint64_t> core_page_faults;
    
    MMUStats() : tlb_hits(0), tlb_misses(0), page_faults(0) {}
    
    // Initialize per-core statistics for a given number of cores
    void init_core_stats(uint32_t num_cores) {
        core_tlb_hits.assign(num_cores, 0);
        core_tlb_misses.assign(num_cores, 0);
        core_page_faults.assign(num_cores, 0);
    }
    
    // Update statistics for a specific core
    void update_core_stats(uint32_t core_id, bool is_hit, bool is_page_fault = false) {
        if (core_id < core_tlb_hits.size()) {
            if (is_hit) {
                core_tlb_hits[core_id]++;
            } else {
                core_tlb_misses[core_id]++;
            }
            if (is_page_fault) {
                core_page_faults[core_id]++;
            }
        }
    }
    
    double hit_rate() const {
        uint64_t total = tlb_hits + tlb_misses;
        return total > 0 ? static_cast<double>(tlb_hits) / total : 0.0;
    }
    
    double core_hit_rate(uint32_t core_id) const {
        if (core_id < core_tlb_hits.size()) {
            uint64_t total = core_tlb_hits[core_id] + core_tlb_misses[core_id];
            return total > 0 ? static_cast<double>(core_tlb_hits[core_id]) / total : 0.0;
        }
        return 0.0;
    }
    
    void reset() {
        tlb_hits = 0;
        tlb_misses = 0;
        page_faults = 0;
        
        // Reset per-core statistics
        std::fill(core_tlb_hits.begin(), core_tlb_hits.end(), 0);
        std::fill(core_tlb_misses.begin(), core_tlb_misses.end(), 0);
        std::fill(core_page_faults.begin(), core_page_faults.end(), 0);
    }
};

// MMU class for RISC-V emulator
class MMU {
public:
    // Constructor with configurable parameters
    MMU(const MMUConfig& config);
    
    // Destructor
    ~MMU();
    
    // Set memory interface
    void set_memory_interface(MemoryInterface* mem);
    
    // Address translation
    ExceptionType translate_address(uint32_t vaddr, uint32_t& paddr, bool is_write, bool is_instruction, uint32_t core_id = 0);
    
    // Page table management
    void set_page_table_base(uint32_t base) { page_table_base = base; }
    uint32_t get_page_table_base() const { return page_table_base; }
    
    // TLB management
    void flush_tlb();
    void print_tlb_stats() const;
    void print_core_tlb_stats(uint32_t core_id) const;
    
    // Statistics
    const MMUStats& get_stats() const { return stats; }
    void reset_stats() { stats.reset(); }
    void set_num_cores(uint32_t num_cores) { stats.init_core_stats(num_cores); }
    
    uint64_t get_tlb_hits() const { return stats.tlb_hits; }
    uint64_t get_tlb_misses() const { return stats.tlb_misses; }
    double get_tlb_hit_rate() const { return stats.hit_rate(); }

private:
    // MMU configuration
    MMUConfig config;
    
    // Page table base address
    uint32_t page_table_base;
    
    // Memory interface
    MemoryInterface* memory_interface;
    
    // TLB entries
    std::vector<TLBEntry> tlb_entries;
    
    // LRU tracking for TLB
    std::list<uint32_t> tlb_lru;
    
    // Statistics
    MMUStats stats;
    
    // Internal methods
    TLBEntry* find_tlb_entry(uint32_t virtual_page);
    TLBEntry* allocate_tlb_entry(uint32_t virtual_page);
    void update_tlb_lru(uint32_t entry_index);
    PageTableEntry read_page_table_entry(uint32_t virtual_page);
    void load_tlb_entry(uint32_t virtual_page, const PageTableEntry& pte);
    
    // Helper methods for address decomposition
    uint32_t get_page_number(uint32_t addr) const { return addr / config.page_size; }
    uint32_t get_page_offset(uint32_t addr) const { return addr % config.page_size; }
    uint32_t get_physical_address(uint32_t frame, uint32_t offset) const { 
        return (frame * config.page_size) + offset; 
    }
};