#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <string>

// PMA Entry structure
struct PMAEntry {
    uint32_t start_addr;
    uint32_t end_addr;
    bool readable;
    bool writable;
    bool executable;
    bool atomic;
    bool cacheable;
    bool device_memory;
    bool instruction_cache;
    bool data_cache;

    PMAEntry() : start_addr(0), end_addr(0), readable(false), writable(false),
                 executable(false), atomic(false), cacheable(true),
                 device_memory(false), instruction_cache(true), data_cache(true) {}
};

// Memory types
enum class MemoryType {
    NORMAL_MEMORY = 0,
    DEVICE_MEMORY = 1,
    IO_MEMORY = 2,
    RESERVED = 3
};

// Cache policies
enum class CachePolicy {
    WRITE_BACK = 0,
    WRITE_THROUGH = 1,
    WRITE_COMBINING = 2,
    UNCACHED = 3,
    UNCACHEABLE = 4
};

class PMAController {
public:
    static PMAController& getInstance();

    // PMA configuration methods
    void addRegion(uint32_t start_addr, uint32_t end_addr, MemoryType type, CachePolicy cache_policy);
    void removeRegion(uint32_t start_addr);
    void clearRegions();

    // Memory access checking
    bool isReadable(uint32_t addr) const;
    bool isWritable(uint32_t addr) const;
    bool isExecutable(uint32_t addr) const;
    bool supportsAtomic(uint32_t addr) const;
    bool isCacheable(uint32_t addr) const;
    bool isDeviceMemory(uint32_t addr) const;
    bool hasInstructionCache(uint32_t addr) const;
    bool hasDataCache(uint32_t addr) const;

    // Get memory type for an address
    MemoryType getMemoryType(uint32_t addr) const;
    CachePolicy getCachePolicy(uint32_t addr) const;

    // Debug methods
    void printPMARegions() const;
    size_t getRegionCount() const { return pma_regions.size(); }

    // Initialize default regions based on linker script
    void initializeDefaultRegions();

    // CSR interface methods
    uint32_t readPMACfg(uint32_t addr) const;
    void writePMACfg(uint32_t addr, uint32_t value);
    uint32_t readPMAAddr(uint32_t index) const;
    void writePMAAddr(uint32_t index, uint32_t value);

private:
    PMAController() = default;
    ~PMAController() = default;

    std::vector<PMAEntry> pma_regions;

    // Helper methods
    const PMAEntry* findEntry(uint32_t addr) const;
    PMAEntry* findEntry(uint32_t addr);

    // CSR configuration
    struct PMACSR {
        uint32_t pmacfg[16];  // pmpcfg0-pmpcfg15 equivalent for PMA
        uint32_t pmaaddr[64]; // pmpaddr0-pmpaddr63 equivalent for PMA
        uint32_t mseccfg;     // Machine security configuration
        bool enabled;

        PMACSR() : mseccfg(0), enabled(true) {
            for (int i = 0; i < 16; i++) pmacfg[i] = 0;
            for (int i = 0; i < 64; i++) pmaaddr[i] = 0;
        }
    } pma_csr;
};

// Helper functions
MemoryType stringToMemoryType(const std::string& type);
CachePolicy stringToCachePolicy(const std::string& policy);
std::string memoryTypeToString(MemoryType type);
std::string cachePolicyToString(CachePolicy policy);