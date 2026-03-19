#include "memory/PMA.h"
#include "utils/DebugLogger.h"
#include <algorithm>
#include <sstream>

PMAController& PMAController::getInstance() {
    static PMAController instance;
    return instance;
}

void PMAController::addRegion(uint32_t start_addr, uint32_t end_addr, MemoryType type, CachePolicy cache_policy) {
    PMAEntry entry;
    entry.start_addr = start_addr;
    entry.end_addr = end_addr;

    // Set memory type specific attributes
    switch (type) {
        case MemoryType::NORMAL_MEMORY:
            entry.readable = true;
            entry.writable = true;
            entry.executable = true;
            entry.atomic = true;
            entry.device_memory = false;
            entry.cacheable = true;
            entry.instruction_cache = true;
            entry.data_cache = true;
            break;

        case MemoryType::DEVICE_MEMORY:
            entry.readable = true;
            entry.writable = true;
            entry.executable = false;
            entry.atomic = true;  // Many devices support atomic operations
            entry.device_memory = true;
            entry.cacheable = false;  // Device memory should be uncached
            entry.instruction_cache = false;
            entry.data_cache = false;
            break;

        case MemoryType::IO_MEMORY:
            entry.readable = true;
            entry.writable = true;
            entry.executable = false;
            entry.atomic = false;
            entry.device_memory = true;
            entry.cacheable = false;  // I/O memory must be uncached
            entry.instruction_cache = false;
            entry.data_cache = false;
            break;

        case MemoryType::RESERVED:
            entry.readable = false;
            entry.writable = false;
            entry.executable = false;
            entry.atomic = false;
            entry.device_memory = false;
            entry.cacheable = false;
            entry.instruction_cache = false;
            entry.data_cache = false;
            break;
    }

    // Override cache policy if specified
    switch (cache_policy) {
        case CachePolicy::WRITE_BACK:
            entry.cacheable = true;
            entry.instruction_cache = true;
            entry.data_cache = true;
            break;

        case CachePolicy::WRITE_THROUGH:
            entry.cacheable = true;
            entry.instruction_cache = true;
            entry.data_cache = true;
            // Note: Write-through implementation would be handled in cache
            break;

        case CachePolicy::WRITE_COMBINING:
            entry.cacheable = false;
            entry.data_cache = false;
            entry.instruction_cache = false;
            break;

        case CachePolicy::UNCACHED:
        case CachePolicy::UNCACHEABLE:
            entry.cacheable = false;
            entry.instruction_cache = false;
            entry.data_cache = false;
            break;
    }

    // Insert region in order
    auto it = std::lower_bound(pma_regions.begin(), pma_regions.end(), entry,
        [](const PMAEntry& a, const PMAEntry& b) {
            return a.start_addr < b.start_addr;
        });
    pma_regions.insert(it, entry);

    SIM_LOGF("PMA: Added region 0x%08x-0x%08x type=%d cache_policy=%d",
            start_addr, end_addr, static_cast<int>(type), static_cast<int>(cache_policy));
}

void PMAController::removeRegion(uint32_t start_addr) {
    auto it = std::remove_if(pma_regions.begin(), pma_regions.end(),
        [start_addr](const PMAEntry& entry) {
            return entry.start_addr == start_addr;
        });
    pma_regions.erase(it, pma_regions.end());
}

void PMAController::clearRegions() {
    pma_regions.clear();
}

const PMAEntry* PMAController::findEntry(uint32_t addr) const {
    for (const auto& entry : pma_regions) {
        if (addr >= entry.start_addr && addr <= entry.end_addr) {
            return &entry;
        }
    }
    return nullptr;
}

PMAEntry* PMAController::findEntry(uint32_t addr) {
    for (auto& entry : pma_regions) {
        if (addr >= entry.start_addr && addr <= entry.end_addr) {
            return &entry;
        }
    }
    return nullptr;
}

bool PMAController::isReadable(uint32_t addr) const {
    const PMAEntry* entry = findEntry(addr);
    return entry ? entry->readable : false;
}

bool PMAController::isWritable(uint32_t addr) const {
    const PMAEntry* entry = findEntry(addr);
    return entry ? entry->writable : false;
}

bool PMAController::isExecutable(uint32_t addr) const {
    const PMAEntry* entry = findEntry(addr);
    return entry ? entry->executable : false;
}

bool PMAController::supportsAtomic(uint32_t addr) const {
    const PMAEntry* entry = findEntry(addr);
    return entry ? entry->atomic : false;
}

bool PMAController::isCacheable(uint32_t addr) const {
    const PMAEntry* entry = findEntry(addr);
    return entry ? entry->cacheable : false;
}

bool PMAController::isDeviceMemory(uint32_t addr) const {
    const PMAEntry* entry = findEntry(addr);
    return entry ? entry->device_memory : false;
}

bool PMAController::hasInstructionCache(uint32_t addr) const {
    const PMAEntry* entry = findEntry(addr);
    return entry ? entry->instruction_cache : false;
}

bool PMAController::hasDataCache(uint32_t addr) const {
    const PMAEntry* entry = findEntry(addr);
    return entry ? entry->data_cache : false;
}

MemoryType PMAController::getMemoryType(uint32_t addr) const {
    const PMAEntry* entry = findEntry(addr);
    if (!entry) return MemoryType::RESERVED;

    if (entry->device_memory) {
        return entry->writable ? MemoryType::DEVICE_MEMORY : MemoryType::RESERVED;
    }
    return MemoryType::NORMAL_MEMORY;
}

CachePolicy PMAController::getCachePolicy(uint32_t addr) const {
    const PMAEntry* entry = findEntry(addr);
    if (!entry) return CachePolicy::UNCACHEABLE;

    if (!entry->cacheable) return CachePolicy::UNCACHEABLE;
    if (entry->device_memory) return CachePolicy::UNCACHED;
    return CachePolicy::WRITE_BACK;
}

void PMAController::printPMARegions() const {
    SIM_LOG("PMA Controller Regions:");
    for (size_t i = 0; i < pma_regions.size(); i++) {
        const auto& entry = pma_regions[i];
        SIM_LOGF("  Region %zu: 0x%08x-0x%08x R=%d W=%d X=%d A=%d C=%d D=%d IC=%d DC=%d",
                i, entry.start_addr, entry.end_addr,
                entry.readable, entry.writable, entry.executable,
                entry.atomic, entry.cacheable, entry.device_memory,
                entry.instruction_cache, entry.data_cache);
    }
}

void PMAController::initializeDefaultRegions() {
    // Clear existing regions
    clearRegions();

    // Based on the linker script memory layout:
    // core0_boot (rx) : ORIGIN = 0x00000000, LENGTH = 0x00001000
    addRegion(0x00000000, 0x00000FFF, MemoryType::NORMAL_MEMORY, CachePolicy::WRITE_BACK);

    // Chapter 10 kernel code (rx) : 0x00002000-0x0000FFFF (for SBI + kernel testing)
    addRegion(0x00002000, 0x0000FFFF, MemoryType::NORMAL_MEMORY, CachePolicy::WRITE_BACK);

    // main_code (rx) : ORIGIN = 0x00010000, LENGTH = 0x0003F000
    addRegion(0x00010000, 0x0004EFFF, MemoryType::NORMAL_MEMORY, CachePolicy::WRITE_BACK);

    // ram (rwx) : ORIGIN = 0x00050000, LENGTH = 0x00030000
    addRegion(0x00050000, 0x0007FFFF, MemoryType::NORMAL_MEMORY, CachePolicy::WRITE_BACK);

    // core0_data part 1 (rwx) : 0x00080000-0x00082007
    addRegion(0x00080000, 0x00082007, MemoryType::NORMAL_MEMORY, CachePolicy::WRITE_BACK);

    // CRITICAL: uart_mutex and other shared synchronization variables
    // These MUST use WRITE_THROUGH to ensure all cores see updates immediately
    // uart_mutex is at 0x82010, and we protect a range around it
    addRegion(0x00082008, 0x0008201F, MemoryType::NORMAL_MEMORY, CachePolicy::WRITE_THROUGH);

    // core0_data part 2 (rwx) : 0x00082020-0x00087FFF
    addRegion(0x00082020, 0x00087FFF, MemoryType::NORMAL_MEMORY, CachePolicy::WRITE_BACK);

    // mmio (rwx) : ORIGIN = 0x00090000, LENGTH = 0x00010000
    // This is our critical region - MUST be device memory with no caching
    addRegion(0x00090000, 0x0009FFFF, MemoryType::DEVICE_MEMORY, CachePolicy::UNCACHED);

    // UART device at 0x10000000-0x1000001F (32 bytes)
    // This MUST be device memory with no caching for proper UART operation
    addRegion(0x10000000, 0x1000001F, MemoryType::DEVICE_MEMORY, CachePolicy::UNCACHED);

    // Core-local data regions
    addRegion(0x000A0000, 0x000A3FFF, MemoryType::NORMAL_MEMORY, CachePolicy::WRITE_BACK); // core_local_0
    addRegion(0x000A4000, 0x000A7FFF, MemoryType::NORMAL_MEMORY, CachePolicy::WRITE_BACK); // core_local_1
    addRegion(0x000A8000, 0x000ABFFF, MemoryType::NORMAL_MEMORY, CachePolicy::WRITE_BACK); // core_local_2
    addRegion(0x000AC000, 0x000AFFFF, MemoryType::NORMAL_MEMORY, CachePolicy::WRITE_BACK); // core_local_3

    SIM_LOG("PMA: Initialized default regions based on linker script");
    printPMARegions();
}

// CSR Interface Implementation
uint32_t PMAController::readPMACfg(uint32_t addr) const {
    if (!pma_csr.enabled) return 0;

    uint32_t index = (addr >> 2) & 0xF;  // pmpcfg0-pmpcfg15
    if (index >= 16) return 0;

    return pma_csr.pmacfg[index];
}

void PMAController::writePMACfg(uint32_t addr, uint32_t value) {
    if (!pma_csr.enabled) return;

    uint32_t index = (addr >> 2) & 0xF;  // pmpcfg0-pmpcfg15
    if (index >= 16) return;

    pma_csr.pmacfg[index] = value;
    SIM_LOGF("PMA: Wrote pmpcfg%d = 0x%08x", index, value);

    // In a real implementation, this would update the PMA regions
    // For now, we just store the value
}

uint32_t PMAController::readPMAAddr(uint32_t index) const {
    if (!pma_csr.enabled || index >= 64) return 0;
    return pma_csr.pmaaddr[index];
}

void PMAController::writePMAAddr(uint32_t index, uint32_t value) {
    if (!pma_csr.enabled || index >= 64) return;

    pma_csr.pmaaddr[index] = value;
    SIM_LOGF("PMA: Wrote pmaaddr%d = 0x%08x", index, value);

    // In a real implementation, this would update the PMA regions
    // For now, we just store the value
}

// Helper functions
MemoryType stringToMemoryType(const std::string& type) {
    if (type == "normal") return MemoryType::NORMAL_MEMORY;
    if (type == "device") return MemoryType::DEVICE_MEMORY;
    if (type == "io") return MemoryType::IO_MEMORY;
    if (type == "reserved") return MemoryType::RESERVED;
    return MemoryType::RESERVED;
}

CachePolicy stringToCachePolicy(const std::string& policy) {
    if (policy == "write_back") return CachePolicy::WRITE_BACK;
    if (policy == "write_through") return CachePolicy::WRITE_THROUGH;
    if (policy == "write_combining") return CachePolicy::WRITE_COMBINING;
    if (policy == "uncached") return CachePolicy::UNCACHED;
    if (policy == "uncacheable") return CachePolicy::UNCACHEABLE;
    return CachePolicy::UNCACHEABLE;
}

std::string memoryTypeToString(MemoryType type) {
    switch (type) {
        case MemoryType::NORMAL_MEMORY: return "normal";
        case MemoryType::DEVICE_MEMORY: return "device";
        case MemoryType::IO_MEMORY: return "io";
        case MemoryType::RESERVED: return "reserved";
        default: return "unknown";
    }
}

std::string cachePolicyToString(CachePolicy policy) {
    switch (policy) {
        case CachePolicy::WRITE_BACK: return "write_back";
        case CachePolicy::WRITE_THROUGH: return "write_through";
        case CachePolicy::WRITE_COMBINING: return "write_combining";
        case CachePolicy::UNCACHED: return "uncached";
        case CachePolicy::UNCACHEABLE: return "uncacheable";
        default: return "unknown";
    }
}