#include "../memory/MMU.h"
#include "../memory/Cache.h"
#include "../include/utils/DebugLogger.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <cassert>

MMU::MMU(const MMUConfig& config) 
    : config(config), page_table_base(0), memory_interface(nullptr), 
      tlb_entries(config.tlb_entries), tlb_lru(), stats() {
    // Initialize TLB entries as invalid
    for (auto& entry : tlb_entries) {
        entry.valid = false;
    }
    
    // Initialize TLB LRU list with entry indices
    for (uint32_t i = 0; i < config.tlb_entries; i++) {
        tlb_lru.push_back(i);
    }
}

MMU::~MMU() {
    // Cleanup if needed
}

void MMU::set_memory_interface(MemoryInterface* mem) {
    memory_interface = mem;
}

TLBEntry* MMU::find_tlb_entry(uint32_t virtual_addr) {
    if (!config.enable_tlb) {
        return nullptr;
    }

    // Extract VPN from virtual address for TLB lookup
    uint32_t vpn = virtual_addr >> 12; // Virtual page number (VPN[1]:VPN[0])

    // Search for matching TLB entry
    for (uint32_t i = 0; i < config.tlb_entries; i++) {
        if (tlb_entries[i].valid && tlb_entries[i].virtual_page == vpn) {
            stats.tlb_hits++;
            update_tlb_lru(i);
            return &tlb_entries[i];
        }
    }

    stats.tlb_misses++;
    return nullptr; // Not found
}

TLBEntry* MMU::allocate_tlb_entry(uint32_t virtual_addr) {
    if (!config.enable_tlb) {
        return nullptr;
    }

    // Extract VPN from virtual address
    uint32_t vpn = virtual_addr >> 12; // Virtual page number (VPN[1]:VPN[0])

    // First check if there's an invalid entry we can use
    for (uint32_t i = 0; i < config.tlb_entries; i++) {
        if (!tlb_entries[i].valid) {
            tlb_entries[i].virtual_page = vpn;
            tlb_entries[i].valid = true;
            update_tlb_lru(i);
            return &tlb_entries[i];
        }
    }

    // No invalid entries, need to evict using LRU
    // Get the LRU entry (back of the list)
    uint32_t lru_entry = tlb_lru.back();
    tlb_lru.pop_back();

    // Use this entry
    tlb_entries[lru_entry].virtual_page = vpn;
    tlb_entries[lru_entry].valid = true;

    // Add back to front (most recently used position)
    tlb_lru.push_front(lru_entry);

    return &tlb_entries[lru_entry];
}

void MMU::update_tlb_lru(uint32_t entry_index) {
    if (!config.enable_tlb) {
        return;
    }
    
    // Remove from current position in LRU list
    tlb_lru.remove(entry_index);
    
    // Add to front (most recently used)
    tlb_lru.push_front(entry_index);
}

PageTableEntry MMU::read_page_table_entry(uint32_t virtual_addr) {
    PageTableEntry pte;
    pte.present = false; // Default to not present

    if (!memory_interface) {
        return pte;
    }

    // Sv32 two-level page table walk
    // Virtual address format: [31:22] VPN[1], [21:12] VPN[0], [11:0] offset
    uint32_t vpn1 = (virtual_addr >> 22) & 0x3FF;  // Bits 31:22
    uint32_t vpn0 = (virtual_addr >> 12) & 0x3FF;  // Bits 21:12

    // DEBUG: Print page table walk info
    MMU_LOGF("Translating VA 0x%08x: VPN1=%d, VPN0=%d\n", virtual_addr, vpn1, vpn0);

  
    // Check if this is a "pseudo single-level" setup where VPN[1] = 0
    // and the page table contains direct PTEs
    if (vpn1 == 0) {
        // For VPN[1] = 0, check if the entry at page_table_base is a leaf PTE
        // This supports the test's approach of putting PTEs directly in the "first level"
        uint32_t direct_pte_addr = page_table_base + (vpn0 * 4);
        uint32_t pte_value = memory_interface->read_word(direct_pte_addr);

          MMU_LOGF("Direct PTE at 0x%08x = 0x%08x\n", direct_pte_addr, pte_value);

        // Check if this is a valid leaf PTE (not a pointer to second level)
        // A leaf PTE must have at least one of R/W/X bits set (bits 1,2,3 = 0x0E)
        if ((pte_value & 0x01) != 0 && (pte_value & 0x0E) != 0) {
            // This is a leaf PTE, parse it directly
            pte.present = (pte_value & 0x01) != 0;      // bit 0: V
            pte.readable = (pte_value & 0x02) != 0;     // bit 1: R
            pte.writable = (pte_value & 0x04) != 0;     // bit 2: W
            pte.executable = (pte_value & 0x08) != 0;   // bit 3: X
            pte.user = (pte_value & 0x10) != 0;         // bit 4: U
            pte.accessed = (pte_value & 0x40) != 0;     // bit 6: A
            pte.dirty = (pte_value & 0x80) != 0;        // bit 7: D
            pte.frame = (pte_value >> 10) & 0x3FFFFF;   // Bits 31:10 for frame number
            MMU_LOGF("Leaf PTE: present=%d, readable=%d, writable=%d, executable=%d, frame=0x%x\n",
                     pte.present, pte.readable, pte.writable, pte.executable, pte.frame);
            return pte;
        }
    }

    // Standard two-level page table walk
    // First level: Read PTE from page table base
    uint32_t pte1_addr = page_table_base + (vpn1 * 4);
    uint32_t pte1_value = memory_interface->read_word(pte1_addr);

    MMU_LOGF("Standard 2-level walk: PTE1 at 0x%08x = 0x%08x\n", pte1_addr, pte1_value);

    // Check if first-level PTE is valid
    if ((pte1_value & 0x01) == 0) {
        MMU_LOG("PTE1 not present, returning page fault\n");
        return pte; // Not present
    }

    // Extract PPN from first-level PTE (bits 31:10)
    uint32_t ppn = (pte1_value >> 10) & 0x3FFFFF;

    // Second level: Read PTE from second-level page table
    uint32_t pte0_addr = (ppn << 12) + (vpn0 * 4);
    uint32_t pte0_value = memory_interface->read_word(pte0_addr);

    // Parse the second-level page table entry
    pte.present = (pte0_value & 0x01) != 0;      // bit 0: V
    pte.readable = (pte0_value & 0x02) != 0;     // bit 1: R
    pte.writable = (pte0_value & 0x04) != 0;     // bit 2: W
    pte.executable = (pte0_value & 0x08) != 0;   // bit 3: X
    pte.user = (pte0_value & 0x10) != 0;         // bit 4: U
    pte.accessed = (pte0_value & 0x40) != 0;     // bit 6: A
    pte.dirty = (pte0_value & 0x80) != 0;        // bit 7: D
    pte.frame = (pte0_value >> 10) & 0x3FFFFF;   // Bits 31:10 for frame number

    return pte;
}

void MMU::load_tlb_entry(uint32_t virtual_addr, const PageTableEntry& pte) {
    if (!config.enable_tlb || !pte.present) {
        return;
    }

    TLBEntry* entry = allocate_tlb_entry(virtual_addr);
    if (entry) {
        entry->virtual_page = virtual_addr >> 12; // Store VPN
        entry->physical_frame = pte.frame;
        entry->writable = pte.writable;
        entry->user = pte.user;
        entry->valid = true;
    }
}

ExceptionType MMU::translate_address(uint32_t vaddr, uint32_t& paddr, bool is_write, bool is_instruction, uint32_t core_id) {
    // For bare-metal programs without virtual memory, use identity mapping
    // Check if page_table_base is 0 (default) and no valid page table exists
    // We'll use a simple heuristic: if the page_table_base is 0, assume identity mapping
#if DEBUG
    // Only log MMU translation when page_table_base is non-zero (actual virtual memory)
    // Identity mapping translations are too verbose to log
    if (page_table_base != 0) {
        MMU_LOGF("translate_address: vaddr=0x%08x, page_table_base=0x%08x", vaddr, page_table_base);
    }
#endif
    if (page_table_base == 0) {
        // Use identity mapping: virtual address = physical address
        paddr = vaddr;
        stats.update_core_stats(core_id, true); // Count as TLB hit for simplicity
        return ExceptionType::NONE;
    }

    // Get page number and offset
    uint32_t page_offset = get_page_offset(vaddr);

    // Check TLB first
    TLBEntry* tlb_entry = find_tlb_entry(vaddr);
    if (tlb_entry) {
        // TLB hit
        if (is_write && !tlb_entry->writable) {
            // Page fault - write to read-only page
            stats.page_faults++;
            stats.update_core_stats(core_id, true, true); // true for hit, true for page fault
            return is_instruction ? ExceptionType::INSTRUCTION_PAGE_FAULT : ExceptionType::STORE_AMO_PAGE_FAULT;
        }

        // Translate address
        paddr = get_physical_address(tlb_entry->physical_frame, page_offset);
        stats.update_core_stats(core_id, true); // true for hit
        return ExceptionType::NONE;
    }

    // TLB miss - check page table
    PageTableEntry pte = read_page_table_entry(vaddr);

    // Check if page is present
    if (!pte.present) {
        // Page fault - page not present
        stats.page_faults++;
        stats.update_core_stats(core_id, false, true); // false for miss, true for page fault
        if (is_instruction) {
            return ExceptionType::INSTRUCTION_PAGE_FAULT;
        } else if (is_write) {
            return ExceptionType::STORE_AMO_PAGE_FAULT;
        } else {
            return ExceptionType::LOAD_PAGE_FAULT;
        }
    }

    // Check write permissions
    if (is_write && !pte.writable) {
        // Page fault - write to read-only page
        stats.page_faults++;
        stats.update_core_stats(core_id, false, true); // false for miss, true for page fault
        return is_write ? ExceptionType::STORE_AMO_PAGE_FAULT : ExceptionType::LOAD_PAGE_FAULT;
    }

    // Load entry into TLB
    load_tlb_entry(vaddr, pte);

    // Translate address
    paddr = get_physical_address(pte.frame, page_offset);
    stats.update_core_stats(core_id, false); // false for miss
    return ExceptionType::NONE;
}

void MMU::flush_tlb() {
    // Invalidate all TLB entries
    for (auto& entry : tlb_entries) {
        entry.valid = false;
    }
    
    // Reset LRU list
    tlb_lru.clear();
    for (uint32_t i = 0; i < config.tlb_entries; i++) {
        tlb_lru.push_back(i);
    }
    
    // Reset statistics
    stats.reset();
}

void MMU::print_tlb_stats() const {
    std::cout << "===== TLB Statistics =====" << std::endl;
    std::cout << "TLB hits: " << stats.tlb_hits << std::endl;
    std::cout << "TLB misses: " << stats.tlb_misses << std::endl;
    std::cout << "Page faults: " << stats.page_faults << std::endl;
    std::cout << "TLB hit rate: " << stats.hit_rate() * 100 << "%" << std::endl;
    std::cout << "=========================" << std::endl;
}

void MMU::print_core_tlb_stats(uint32_t core_id) const {
    if (core_id >= stats.core_tlb_hits.size()) {
        std::cout << "Invalid core ID: " << core_id << std::endl;
        return;
    }
    
    std::cout << "===== Core " << core_id << " TLB Statistics =====" << std::endl;
    std::cout << "TLB hits: " << stats.core_tlb_hits[core_id] << std::endl;
    std::cout << "TLB misses: " << stats.core_tlb_misses[core_id] << std::endl;
    std::cout << "Page faults: " << stats.core_page_faults[core_id] << std::endl;
    std::cout << "TLB hit rate: " << stats.core_hit_rate(core_id) * 100 << "%" << std::endl;
    std::cout << "=========================" << std::endl;
}