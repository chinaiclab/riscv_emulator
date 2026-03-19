#include "../memory/Cache.h"
#include "../memory/MMU.h"
#include "../include/utils/DebugLogger.h"
#include <iostream>
#include <cstring>
#include <cmath>
#include <cassert>
#include <chrono>

Cache::Cache(const CacheConfig& config) 
    : config(config), memory_interface(nullptr), mmu(nullptr), 
      cache_entries(nullptr), lru_lists(nullptr) {
#if DEBUG
    CACHE_LOGF("Initializing cache with size=%d, associativity=%d, block_size=%d",
                config.size, config.associativity, config.block_size);
#endif
    calculate_parameters();
    
    // Allocate cache entries
    cache_entries = new CacheEntry*[num_sets];
    for (uint32_t i = 0; i < num_sets; i++) {
        cache_entries[i] = new CacheEntry[config.associativity];
        // Initialize entries
        for (uint32_t j = 0; j < config.associativity; j++) {
            cache_entries[i][j].tag = 0;
            cache_entries[i][j].data = new uint32_t[config.block_size / sizeof(uint32_t)];
            cache_entries[i][j].valid = false;
            cache_entries[i][j].dirty = false;
#if DEBUG
            CACHE_VERBOSE_LOGF("Initialized cache entry [%d][%d]", i, j);
#endif
        }
    }
    
    // Allocate LRU lists
    lru_lists = new std::list<uint32_t>*[num_sets];
    for (uint32_t i = 0; i < num_sets; i++) {
        lru_lists[i] = new std::list<uint32_t>();
        // Initialize LRU list with way indices
        for (uint32_t j = 0; j < config.associativity; j++) {
            lru_lists[i]->push_back(j);
        }
#if DEBUG
        CACHE_VERBOSE_LOGF("Initialized LRU list for set %d with %zu entries", i, lru_lists[i]->size());
#endif
    }
#if DEBUG
    CACHE_LOG("Cache initialization complete");
    // Validate initialization
    if (cache_entries == nullptr) {
        CACHE_LOG("ERROR: cache_entries is null after initialization!");
    } else {
        CACHE_LOGF("cache_entries=%p", cache_entries);
        for (uint32_t i = 0; i < std::min(num_sets, 5u); i++) {
            if (cache_entries[i] == nullptr) {
                CACHE_LOGF("ERROR: cache_entries[%d] is null!", i);
            } else {
                CACHE_LOGF("cache_entries[%d]=%p", i, cache_entries[i]);
                for (uint32_t j = 0; j < std::min(config.associativity, 5u); j++) {
                    CACHE_LOGF("cache_entries[%d][%d].valid=%d", i, j, cache_entries[i][j].valid);
                }
            }
        }
    }
#endif
}

Cache::~Cache() {
    // Clean up cache entries
    if (cache_entries) {
        for (uint32_t i = 0; i < num_sets; i++) {
            if (cache_entries[i]) {
                for (uint32_t j = 0; j < config.associativity; j++) {
                    delete[] cache_entries[i][j].data;
                }
                delete[] cache_entries[i];
            }
        }
        delete[] cache_entries;
        cache_entries = nullptr;
    }
    
    if (lru_lists) {
        for (uint32_t i = 0; i < num_sets; i++) {
            if (lru_lists[i]) {
                delete lru_lists[i];
            }
        }
        delete[] lru_lists;
        lru_lists = nullptr;
    }
}

// Copy constructor
Cache::Cache(const Cache& other)
    : config(other.config), memory_interface(other.memory_interface), mmu(other.mmu),
      cache_entries(nullptr), lru_lists(nullptr) {
#if DEBUG
    CACHE_LOG("Copying cache");
#endif
    calculate_parameters();
    
    // Allocate cache entries
    cache_entries = new CacheEntry*[num_sets];
    for (uint32_t i = 0; i < num_sets; i++) {
        cache_entries[i] = new CacheEntry[config.associativity];
        // Copy entries
        for (uint32_t j = 0; j < config.associativity; j++) {
            cache_entries[i][j].tag = other.cache_entries[i][j].tag;
            cache_entries[i][j].valid = other.cache_entries[i][j].valid;
            cache_entries[i][j].dirty = other.cache_entries[i][j].dirty;
            
            // Allocate and copy data
            cache_entries[i][j].data = new uint32_t[config.block_size / sizeof(uint32_t)];
            for (uint32_t k = 0; k < config.block_size / sizeof(uint32_t); k++) {
                cache_entries[i][j].data[k] = other.cache_entries[i][j].data[k];
            }
            
            // Note: We don't copy the LRU iterator as it will be updated when the entry is used
        }
    }
    
    // Allocate LRU lists
    lru_lists = new std::list<uint32_t>*[num_sets];
    for (uint32_t i = 0; i < num_sets; i++) {
        lru_lists[i] = new std::list<uint32_t>();
        // Copy LRU list
        *lru_lists[i] = *other.lru_lists[i];
    }
    
#if DEBUG
    CACHE_LOG("Cache copy complete");
#endif
}

// Assignment operator
Cache& Cache::operator=(const Cache& other) {
    if (this != &other) {
#if DEBUG
        CACHE_LOG("Assigning cache");
#endif
        // Clean up existing resources
        if (cache_entries) {
            for (uint32_t i = 0; i < num_sets; i++) {
                if (cache_entries[i]) {
                    for (uint32_t j = 0; j < config.associativity; j++) {
                        delete[] cache_entries[i][j].data;
                    }
                    delete[] cache_entries[i];
                }
            }
            delete[] cache_entries;
        }
        
        if (lru_lists) {
            for (uint32_t i = 0; i < num_sets; i++) {
                if (lru_lists[i]) {
                    delete lru_lists[i];
                }
            }
            delete[] lru_lists;
        }
        
        // Copy configuration
        config = other.config;
        memory_interface = other.memory_interface;
        mmu = other.mmu;
        cache_entries = nullptr;
        lru_lists = nullptr;
        
        calculate_parameters();
        
        // Allocate cache entries
        cache_entries = new CacheEntry*[num_sets];
        for (uint32_t i = 0; i < num_sets; i++) {
            cache_entries[i] = new CacheEntry[config.associativity];
            // Copy entries
            for (uint32_t j = 0; j < config.associativity; j++) {
                cache_entries[i][j].tag = other.cache_entries[i][j].tag;
                cache_entries[i][j].valid = other.cache_entries[i][j].valid;
                cache_entries[i][j].dirty = other.cache_entries[i][j].dirty;
                
                // Allocate and copy data
                cache_entries[i][j].data = new uint32_t[config.block_size / sizeof(uint32_t)];
                for (uint32_t k = 0; k < config.block_size / sizeof(uint32_t); k++) {
                    cache_entries[i][j].data[k] = other.cache_entries[i][j].data[k];
                }
                
                // Note: We don't copy the LRU iterator as it will be updated when the entry is used
            }
        }
        
        // Allocate LRU lists
        lru_lists = new std::list<uint32_t>*[num_sets];
        for (uint32_t i = 0; i < num_sets; i++) {
            lru_lists[i] = new std::list<uint32_t>();
            // Copy LRU list
            *lru_lists[i] = *other.lru_lists[i];
        }
        
#if DEBUG
        CACHE_LOG("Cache assignment complete");
#endif
    }
    return *this;
}

void Cache::set_memory_interface(MemoryInterface* mem) {
    memory_interface = mem;
}

void Cache::calculate_parameters() {
    // Calculate cache parameters based on configuration
    assert(config.size > 0 && "Cache size must be greater than 0");
    assert(config.block_size > 0 && "Block size must be greater than 0");
    assert((config.block_size & (config.block_size - 1)) == 0 && "Block size must be a power of 2");
    assert(config.associativity > 0 && "Associativity must be greater than 0");
    assert((config.size % (config.block_size * config.associativity)) == 0 && "Cache size must be divisible by (block_size * associativity)");
    
    num_sets = config.size / (config.block_size * config.associativity);
    block_offset_bits = static_cast<uint32_t>(log2(config.block_size));
    set_index_bits = static_cast<uint32_t>(log2(num_sets));
    tag_shift = block_offset_bits + set_index_bits;
    
#if DEBUG
    CACHE_LOGF("Calculated cache parameters: num_sets=%d, block_offset_bits=%d, set_index_bits=%d, tag_shift=%d",
                num_sets, block_offset_bits, set_index_bits, tag_shift);
#endif
    
    // Verify that we have valid parameters
    assert(num_sets > 0 && "Number of sets must be greater than 0");
    assert(block_offset_bits > 0 && "Block offset bits must be greater than 0");
    assert(set_index_bits >= 0 && "Set index bits must be non-negative");
}

Cache::CacheEntry* Cache::find_entry(uint32_t addr, uint32_t& set_index, uint32_t& tag) {
    set_index = get_set_index(addr);
    tag = get_tag(addr);
    
#if DEBUG
    CACHE_LOGF("find_entry addr=0x%08x, set_index=%d, tag=0x%x", addr, set_index, tag);
    CACHE_LOGF("num_sets=%d, associativity=%d", num_sets, config.associativity);
    
    // Validate cache_entries array
    if (cache_entries == nullptr) {
        CACHE_LOG("ERROR: cache_entries is null!");
        return nullptr;
    }
    
      CACHE_LOGF("cache_entries=%p", cache_entries);
#endif
    
    // Bounds checking for set_index
    if (set_index >= num_sets) {
#if DEBUG
        CACHE_LOGF("ERROR: set_index out of bounds! set_index=%d, num_sets=%d", set_index, num_sets);
#endif
        return nullptr;
    }
    
#if DEBUG
    // Validate set array
    if (cache_entries[set_index] == nullptr) {
        CACHE_LOGF("ERROR: cache_entries[%d] is null!", set_index);
        return nullptr;
    }
    
    CACHE_LOGF("cache_entries[%d]=%p", set_index, cache_entries[set_index]);
#endif

    // Search for matching entry in the set
    for (uint32_t i = 0; i < config.associativity; i++) {
#if DEBUG
        CACHE_LOGF("Checking entry %d", i);
        // Check if the entry pointer is valid
        CacheEntry* entry = &cache_entries[set_index][i];
        CACHE_LOGF("Entry pointer: %p", entry);
        
        // Validate entry pointer
        if (entry == nullptr) {
            CACHE_LOGF("ERROR: cache_entries[%d][%d] is null!", set_index, i);
            continue;
        }
        
        // Try to access valid field with exception handling
        try {
            CACHE_LOGF("Entry %d valid=%d", i, entry->valid);
            if (entry->valid) {
                CACHE_LOGF(" tag=0x%08x", entry->tag);
            }
        } catch (...) {
            CACHE_LOGF("ERROR: Exception when accessing entry %d", i);
            continue;
        }
#endif
        if (cache_entries[set_index][i].valid && cache_entries[set_index][i].tag == tag) {
#if DEBUG
            CACHE_LOGF("Found matching entry at index %d", i);
#endif
            return &cache_entries[set_index][i];
        }
    }
    
    return nullptr; // Not found
}

Cache::CacheEntry* Cache::allocate_entry(uint32_t set_index, uint32_t tag) {
#if DEBUG
    CACHE_LOGF("allocate_entry set_index=%d, tag=0x%08x", set_index, tag);

    // Validate parameters
    if (set_index >= num_sets) {
        CACHE_LOG("ERROR: set_index out of bounds in allocate_entry!");
        return nullptr;
    }

    if (cache_entries == nullptr || cache_entries[set_index] == nullptr) {
        CACHE_LOG("ERROR: cache_entries is null in allocate_entry!");
        return nullptr;
    }
#endif

    // First check if there's an invalid entry we can use
    for (uint32_t i = 0; i < config.associativity; i++) {
#if DEBUG
        CACHE_LOGF("Checking entry %d for allocation, valid=%d", i, cache_entries[set_index][i].valid);
#endif
        if (!cache_entries[set_index][i].valid) {
            CacheEntry* entry = &cache_entries[set_index][i];
            entry->tag = tag;
            entry->valid = true;
            entry->dirty = false;
#if DEBUG
            CACHE_LOGF("Allocated entry %d", i);
#endif
            return entry;
        }
    }
    
#if DEBUG
    CACHE_LOG("No invalid entries, evicting LRU entry");
#endif
    
    // No invalid entries, need to evict using LRU
    evict_lru_entry(set_index);
    
    // Now there should be an invalid entry
    for (uint32_t i = 0; i < config.associativity; i++) {
        if (!cache_entries[set_index][i].valid) {
            CacheEntry* entry = &cache_entries[set_index][i];
            entry->tag = tag;
            entry->valid = true;
            entry->dirty = false;
#if DEBUG
            CACHE_LOGF("Allocated entry %d after eviction", i);
#endif
            return entry;
        }
    }
    
    // Should never reach here
    assert(false && "Failed to allocate cache entry");
    return nullptr;
}

void Cache::evict_lru_entry(uint32_t set_index) {
#if DEBUG
    // Validate parameters
    if (set_index >= num_sets) {
        CACHE_LOG("ERROR: set_index out of bounds in evict_lru_entry!");
        return;
    }
    
    if (cache_entries == nullptr || cache_entries[set_index] == nullptr) {
        CACHE_LOG("ERROR: cache_entries is null in evict_lru_entry!");
        return;
    }
    
    if (lru_lists == nullptr || lru_lists[set_index] == nullptr) {
        CACHE_LOG("ERROR: lru_lists is null in evict_lru_entry!");
        return;
    }
    
    if (lru_lists[set_index]->empty()) {
        CACHE_LOG("ERROR: LRU list is empty in evict_lru_entry!");
        return;
    }
#endif
    
    // Get the LRU way (back of the list)
    uint32_t lru_way = lru_lists[set_index]->back();
    
#if DEBUG
    if (lru_way >= config.associativity) {
        CACHE_LOG("ERROR: lru_way out of bounds in evict_lru_entry!");
        return;
    }
#endif
    
    CacheEntry* entry = &cache_entries[set_index][lru_way];
    
    // If the entry is dirty, write it back
    if (entry->dirty) {
        write_back_block(set_index, lru_way);
        stats.writebacks++;
    }
    
    // Invalidate the entry
    entry->valid = false;
    entry->dirty = false;
    
    // Remove from LRU list
    lru_lists[set_index]->pop_back();
    
    // Add back to front (most recently used position) - This is wrong!
    // We should not add it back since we just invalidated it
    // The entry will be allocated by the caller
}

void Cache::update_lru(uint32_t set_index, CacheEntry* entry) {
#if DEBUG
    // Validate parameters
    if (set_index >= num_sets) {
        CACHE_LOG("ERROR: set_index out of bounds in update_lru!");
        return;
    }
    
    if (cache_entries == nullptr || cache_entries[set_index] == nullptr) {
        CACHE_LOG("ERROR: cache_entries is null in update_lru!");
        return;
    }
    
    if (lru_lists == nullptr || lru_lists[set_index] == nullptr) {
        CACHE_LOG("ERROR: lru_lists is null in update_lru!");
        return;
    }
    
    if (entry == nullptr) {
        CACHE_LOG("ERROR: entry is null in update_lru!");
        return;
    }
#endif
    
    // Find which way this entry is
    uint32_t way = entry - cache_entries[set_index];
    
#if DEBUG
    if (way >= config.associativity) {
        CACHE_LOG("ERROR: way out of bounds in update_lru!");
        return;
    }
#endif
    
    // Remove from current position in LRU list
    lru_lists[set_index]->remove(way);
    
    // Add to front (most recently used)
    lru_lists[set_index]->push_front(way);
}

void Cache::load_block(uint32_t addr, CacheEntry* entry) {
#if DEBUG
    CACHE_LOGF("load_block addr=0x%08x", addr);
#endif

    if (!memory_interface) {
#if DEBUG
        CACHE_LOG("No memory interface, returning");
#endif
        return;
    }

    uint32_t block_addr = get_block_address(addr);
    uint32_t words_per_block = config.block_size / sizeof(uint32_t);

#if DEBUG
    CACHE_LOGF("block_addr=0x%08x", block_addr);
    CACHE_LOGF("words_per_block=%d", words_per_block);
#endif
    
    // Read the entire block from memory
    memory_interface->read_block(block_addr, entry->data, words_per_block);
    
#if DEBUG
    CACHE_LOG("Block data after loading:");
    for (uint32_t i = 0; i < words_per_block && i < 16; i++) {
        CACHE_LOGF("word %d: 0x%08x", i, entry->data[i]);
    }
#endif
}

void Cache::write_back_block(uint32_t set_index, uint32_t entry_index) {
    if (!memory_interface) {
        return;
    }
    
    CacheEntry* entry = &cache_entries[set_index][entry_index];
    uint32_t block_addr = (entry->tag << tag_shift) | (set_index << block_offset_bits);
    uint32_t words_per_block = config.block_size / sizeof(uint32_t);
    
    // Write the entire block back to memory
    memory_interface->write_block(block_addr, entry->data, words_per_block);
}

uint32_t Cache::translate_address(uint32_t addr, bool is_write, bool is_instruction) {
    // If no MMU is set, use address as-is (physical address)
    if (!mmu) {
        return addr;
    }
    
    // Translate virtual address to physical address
    // Note: We're using core_id 0 here because the Cache class doesn't have access to the core_id
    // In a more sophisticated implementation, we might want to pass the core_id through the call chain
    uint32_t paddr;
    ExceptionType exception = mmu->translate_address(addr, paddr, is_write, is_instruction, 0);
    
#if DEBUG
    if (exception != ExceptionType::NONE) {
        CACHE_LOGF("Address translation failed for vaddr=0x%08x, exception=%d", addr, static_cast<int>(exception));
    }
#endif
    
    // For now, we'll just return the translated address
    // In a full implementation, we would handle exceptions appropriately
    // For page faults, we might want to propagate the exception to the core
    return exception == ExceptionType::NONE ? paddr : addr;
}

bool Cache::read(uint32_t addr, uint32_t& data, bool is_instruction, uint32_t core_id) {
    // Translate address if MMU is available
    uint32_t phys_addr = translate_address(addr, false, is_instruction);
    
#if DEBUG
    CACHE_LOGF("Cache read at addr=0x%08x (phys=0x%08x)", addr, phys_addr);
#endif
    
    stats.accesses++;
    
    uint32_t set_index, tag;
    CacheEntry* entry = find_entry(phys_addr, set_index, tag);
    
#if DEBUG
    CACHE_LOGF("find_entry returned %s", entry ? "entry" : "nullptr");
    CACHE_LOGF("set_index=%d, tag=0x%08x", set_index, tag);
#endif

    if (entry) {
        // Cache hit - fast access
        stats.hits++;
        stats.update_core_stats(core_id, true, config.access_latency);
        update_lru(set_index, entry);

        // Extract the word from the block
        uint32_t block_offset = get_block_offset(phys_addr);
        uint32_t word_index = block_offset / sizeof(uint32_t);
#if DEBUG
        CACHE_LOGF("Cache hit, extracting word at index %d", word_index);
        CACHE_LOGF("Block data at index %d = 0x%08x", word_index, entry->data[word_index]);
#endif
        data = entry->data[word_index];
        
        // Update total access time
        stats.total_access_time += config.access_latency;
        return true;
    } else {
        // Cache miss - slow access due to memory access
        stats.misses++;
        stats.total_miss_penalty += config.miss_penalty;
        stats.update_core_stats(core_id, false, config.access_latency + config.miss_penalty, config.miss_penalty);
        
#if DEBUG
        CACHE_LOG("Cache miss, allocating entry");
#endif
        
        // Allocate a new entry
        entry = allocate_entry(set_index, tag);
        
#if DEBUG
        CACHE_LOG("Entry allocated, loading block");
#endif
        
        // Load the block from memory
        load_block(phys_addr, entry);
        
#if DEBUG
        CACHE_LOG("Block loaded, extracting word");
        CACHE_LOGF("Block data at index 0 = 0x%08x", entry->data[0]);
#endif

        // Extract the word from the block
        uint32_t block_offset = get_block_offset(phys_addr);
        uint32_t word_index = block_offset / sizeof(uint32_t);
        data = entry->data[word_index];

#if DEBUG
        CACHE_LOGF("Extracted word at index %d = 0x%08x", word_index, data);
#endif
        
        // Update LRU
        update_lru(set_index, entry);
        
        // Update total access time
        stats.total_access_time += config.access_latency + config.miss_penalty;
        return false;
    }
}

bool Cache::write(uint32_t addr, uint32_t data, uint32_t core_id) {
    // Translate address if MMU is available
    uint32_t phys_addr = translate_address(addr, true, false);

    // CRITICAL: For shared synchronization variables (like uart_mutex at 0x82010),
    // use WRITE_THROUGH policy to ensure all cores see updates immediately
    bool is_write_through = (phys_addr >= 0x82008 && phys_addr <= 0x8201F);

    // Special handling for page table region and shared variables
    // Check both virtual and physical addresses since MMU might not be enabled yet
    bool is_page_table = ((addr >= 0x20000 && addr < 0x21000) || (phys_addr >= 0x20000 && phys_addr < 0x21000));

    if (is_write_through || is_page_table) {
        // Determine the correct physical address
        uint32_t write_addr = is_page_table ?
            ((addr >= 0x20000 && addr < 0x21000) ? addr : phys_addr) : phys_addr;

        // Write directly to main memory to ensure all cores see the update immediately
        if (memory_interface) {
            memory_interface->write_word(write_addr, data);
        }

        // Also update cache if present and invalidate to maintain coherence
        uint32_t set_index, tag;
        CacheEntry* entry = find_entry(write_addr, set_index, tag);
        if (entry) {
            uint32_t block_offset = get_block_offset(write_addr);
            uint32_t word_index = block_offset / sizeof(uint32_t);
            entry->data[word_index] = data;
            entry->dirty = false; // Not dirty since we wrote to main memory
            invalidate(write_addr); // Invalidate to ensure coherence
        }

        stats.accesses++;
        stats.hits++; // Treat as hit since we wrote directly
        stats.update_core_stats(core_id, true, config.access_latency);
        stats.total_access_time += config.access_latency;
        return true;
    }

    stats.accesses++;

    uint32_t set_index, tag;
    CacheEntry* entry = find_entry(phys_addr, set_index, tag);

    if (entry) {
        // Cache hit - fast access
        stats.hits++;
        stats.update_core_stats(core_id, true, config.access_latency);
        update_lru(set_index, entry);

        // Update the word in the block
        uint32_t block_offset = get_block_offset(phys_addr);
        uint32_t word_index = block_offset / sizeof(uint32_t);
        entry->data[word_index] = data;
        entry->dirty = true;

        // Update total access time
        stats.total_access_time += config.access_latency;
        return true;
    } else {
        // Cache miss - slow access due to memory access
        stats.misses++;
        stats.total_miss_penalty += config.miss_penalty;
        stats.update_core_stats(core_id, false, config.access_latency + config.miss_penalty, config.miss_penalty);

        // For write-allocate policy, we load the block into cache
        entry = allocate_entry(set_index, tag);

        // Load the block from memory
        load_block(phys_addr, entry);

        // Update the word in the block
        uint32_t block_offset = get_block_offset(phys_addr);
        uint32_t word_index = block_offset / sizeof(uint32_t);
        entry->data[word_index] = data;
        entry->dirty = true;

        // Update LRU
        update_lru(set_index, entry);

        // Update total access time
        stats.total_access_time += config.access_latency + config.miss_penalty;
        return false;
    }
}

void Cache::invalidate(uint32_t addr) {
    uint32_t set_index = get_set_index(addr);
    uint32_t tag = get_tag(addr);
    
    // Search for matching entry in the set
    for (uint32_t i = 0; i < config.associativity; i++) {
        if (cache_entries[set_index][i].valid && cache_entries[set_index][i].tag == tag) {
            // Invalidate the entry
            cache_entries[set_index][i].valid = false;
            cache_entries[set_index][i].dirty = false;
            break;
        }
    }
}

void Cache::invalidate_all() {
    for (uint32_t i = 0; i < num_sets; i++) {
        for (uint32_t j = 0; j < config.associativity; j++) {
            cache_entries[i][j].valid = false;
            cache_entries[i][j].dirty = false;
        }
    }

    // Reset LRU lists
    for (uint32_t i = 0; i < num_sets; i++) {
        lru_lists[i]->clear();
        for (uint32_t j = 0; j < config.associativity; j++) {
            lru_lists[i]->push_back(j);
        }
    }

    // Reset statistics
    stats.reset();
}

void Cache::flush(uint32_t addr) {
    uint32_t set_index = get_set_index(addr);
    uint32_t tag = get_tag(addr);

    // Search for matching entry in the set
    for (uint32_t i = 0; i < config.associativity; i++) {
        if (cache_entries[set_index][i].valid && cache_entries[set_index][i].tag == tag) {
            // If entry is dirty, write it back to memory
            if (cache_entries[set_index][i].dirty) {
                write_back_block(set_index, i);
                cache_entries[set_index][i].dirty = false;
            }
            break;
        }
    }
}

void Cache::flush_range(uint32_t start_addr, uint32_t size) {
    uint32_t end_addr = start_addr + size;

    // Iterate through all cache sets and check if any entries fall within the range
    for (uint32_t set = 0; set < num_sets; set++) {
        for (uint32_t way = 0; way < config.associativity; way++) {
            if (cache_entries[set][way].valid) {
                // Reconstruct the address from tag and set
                uint32_t block_addr = (cache_entries[set][way].tag << tag_shift)
                                    | (set << block_offset_bits);

                // Check if this cache block falls within the flush range
                if (block_addr >= start_addr && block_addr < end_addr) {
                    // If entry is dirty, write it back to memory
                    if (cache_entries[set][way].dirty) {
                        write_back_block(set, way);
                        cache_entries[set][way].dirty = false;
                    }
                }
            }
        }
    }
}

void Cache::print_stats(const std::string& cache_name) const {
    std::cout << "===== " << cache_name << " Statistics =====" << std::endl;
    std::cout << "Total accesses: " << stats.accesses << std::endl;
    std::cout << "Hits: " << stats.hits << std::endl;
    std::cout << "Misses: " << stats.misses << std::endl;
    std::cout << "Writebacks: " << stats.writebacks << std::endl;
    std::cout << "Hit rate: " << stats.hit_rate() * 100 << "%" << std::endl;
    std::cout << "Miss rate: " << stats.miss_rate() * 100 << "%" << std::endl;
    std::cout << "Average access time: " << stats.average_access_time() << " cycles" << std::endl;
    std::cout << "Total access time: " << stats.total_access_time << " cycles" << std::endl;
    std::cout << "Total hit time: " << stats.total_hit_time << " cycles" << std::endl;
    std::cout << "Total miss time: " << stats.total_miss_time << " cycles" << std::endl;
    std::cout << "Average hit time: " << stats.average_hit_time() << " cycles" << std::endl;
    std::cout << "Average miss time: " << stats.average_miss_time() << " cycles" << std::endl;
    std::cout << "Total miss penalty: " << stats.total_miss_penalty << " cycles" << std::endl;
    std::cout << "=============================" << std::endl;
}

void Cache::print_core_stats(const std::string& cache_name, uint32_t core_id) const {
    if (core_id >= stats.core_accesses.size()) {
        std::cout << "Invalid core ID: " << core_id << std::endl;
        return;
    }
    
    std::cout << "===== " << cache_name << " Core " << core_id << " Statistics =====" << std::endl;
    std::cout << "Accesses: " << stats.core_accesses[core_id] << std::endl;
    std::cout << "Hits: " << stats.core_hits[core_id] << std::endl;
    std::cout << "Misses: " << stats.core_misses[core_id] << std::endl;
    std::cout << "Writebacks: " << stats.core_writebacks[core_id] << std::endl;
    std::cout << "Hit rate: " << stats.core_hit_rate(core_id) * 100 << "%" << std::endl;
    std::cout << "Miss rate: " << stats.core_miss_rate(core_id) * 100 << "%" << std::endl;
    std::cout << "Average access time: " << stats.core_average_access_time(core_id) << " cycles" << std::endl;
    std::cout << "Total access time: " << stats.core_access_time[core_id] << " cycles" << std::endl;
    std::cout << "Total hit time: " << stats.core_hit_time_total(core_id) << " cycles" << std::endl;
    std::cout << "Total miss time: " << stats.core_miss_time_total(core_id) << " cycles" << std::endl;
    std::cout << "Average hit time: " << stats.core_average_hit_time(core_id) << " cycles" << std::endl;
    std::cout << "Average miss time: " << stats.core_average_miss_time(core_id) << " cycles" << std::endl;
    std::cout << "Total miss penalty: " << stats.core_miss_penalty[core_id] << " cycles" << std::endl;
    std::cout << "=============================" << std::endl;
}

// MemoryInterface implementation
uint32_t Cache::read_word(uint32_t addr) {
    uint32_t data;
    // Direct access without timing to avoid recursion
    stats.accesses++;
    
    // Translate address if MMU is available
    uint32_t phys_addr = translate_address(addr, false, false);
    
    uint32_t set_index, tag;
    CacheEntry* entry = find_entry(phys_addr, set_index, tag);
    
    if (entry) {
        // Cache hit
        stats.hits++;
        // Use core_id 0 for direct memory interface access
        stats.update_core_stats(0, true, config.access_latency);
        update_lru(set_index, entry);
        
        // Extract the word from the block
        uint32_t block_offset = get_block_offset(phys_addr);
        uint32_t word_index = block_offset / sizeof(uint32_t);
        data = entry->data[word_index];
        
        // Add access latency
        stats.total_access_time += config.access_latency;
    } else {
        // Cache miss
        stats.misses++;
        stats.total_miss_penalty += config.miss_penalty;
        // Use core_id 0 for direct memory interface access
        stats.update_core_stats(0, false, config.access_latency + config.miss_penalty, config.miss_penalty);
        
        // Allocate a new entry
        entry = allocate_entry(set_index, tag);
        
        // Load the block from memory
        load_block(phys_addr, entry);
        
        // Extract the word from the block
        uint32_t block_offset = get_block_offset(phys_addr);
        uint32_t word_index = block_offset / sizeof(uint32_t);
        data = entry->data[word_index];
        
        // Update LRU
        update_lru(set_index, entry);
        
        // Add access latency and miss penalty
        stats.total_access_time += config.access_latency + config.miss_penalty;
    }
    
    return data;
}

void Cache::write_word(uint32_t addr, uint32_t data) {
    // Direct access without timing to avoid recursion
    stats.accesses++;

    // Translate address if MMU is available
    uint32_t phys_addr = translate_address(addr, true, false);

    // CRITICAL: For shared synchronization variables (like uart_mutex at 0x82010),
    // use WRITE_THROUGH policy to ensure all cores see updates immediately
    // Range: 0x82008-0x8201F covers .sbss section with uart_mutex
    bool is_write_through = (phys_addr >= 0x82008 && phys_addr <= 0x8201F);

    uint32_t set_index, tag;
    CacheEntry* entry = find_entry(phys_addr, set_index, tag);

    if (entry) {
        // Cache hit
        stats.hits++;
        // Use core_id 0 for direct memory interface access
        stats.update_core_stats(0, true, config.access_latency);
        update_lru(set_index, entry);

        // Update the word in the block
        uint32_t block_offset = get_block_offset(phys_addr);
        uint32_t word_index = block_offset / sizeof(uint32_t);
        entry->data[word_index] = data;

        if (is_write_through) {
            // WRITE_THROUGH: Write immediately to memory and don't mark as dirty
            if (memory_interface) {
                memory_interface->write_word(phys_addr, data);
            }
            // Don't mark as dirty since we already wrote to memory
        } else {
            // WRITE_BACK: Mark as dirty for later write-back
            entry->dirty = true;
        }

        // Add access latency
        stats.total_access_time += config.access_latency;
    } else {
        // Cache miss
        stats.misses++;
        stats.total_miss_penalty += config.miss_penalty;
        // Use core_id 0 for direct memory interface access
        stats.update_core_stats(0, false, config.access_latency + config.miss_penalty, config.miss_penalty);

        // For write-allocate policy, we load the block into cache
        entry = allocate_entry(set_index, tag);

        // Load the block from memory
        load_block(phys_addr, entry);

        // Update the word in the block
        uint32_t block_offset = get_block_offset(phys_addr);
        uint32_t word_index = block_offset / sizeof(uint32_t);
        entry->data[word_index] = data;

        if (is_write_through) {
            // WRITE_THROUGH: Write immediately to memory and don't mark as dirty
            if (memory_interface) {
                memory_interface->write_word(phys_addr, data);
            }
            // Don't mark as dirty since we already wrote to memory
        } else {
            // WRITE_BACK: Mark as dirty for later write-back
            entry->dirty = true;
        }

        // Update LRU
        update_lru(set_index, entry);

        // Add access latency and miss penalty
        stats.total_access_time += config.access_latency + config.miss_penalty;
    }
}

void Cache::read_block(uint32_t addr, uint32_t* data, uint32_t words) {
    for (uint32_t i = 0; i < words; i++) {
        uint32_t word_addr = addr + i * sizeof(uint32_t);
        data[i] = read_word(word_addr);
    }
}

void Cache::write_block(uint32_t addr, const uint32_t* data, uint32_t words) {
    for (uint32_t i = 0; i < words; i++) {
        uint32_t word_addr = addr + i * sizeof(uint32_t);
        write_word(word_addr, data[i]);
    }
}

// Atomic operations implementation
uint32_t Cache::atomic_add(uint32_t addr, uint32_t value, uint32_t core_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);

    // Translate address if MMU is available
    uint32_t phys_addr = translate_address(addr, true, false);

    // For atomic operations, we must bypass cache and write directly to memory
    // to ensure all cores see the update immediately (cache coherence)
    if (memory_interface) {
        uint32_t result = memory_interface->read_word(phys_addr);
        memory_interface->write_word(phys_addr, result + value);

        // Invalidate this cache line if present to maintain coherence
        invalidate(phys_addr);

        stats.accesses++;
        stats.misses++;  // Atomic operations bypass cache, count as miss for timing
        stats.total_miss_penalty += config.miss_penalty;
        stats.update_core_stats(core_id, false, config.access_latency + config.miss_penalty, config.miss_penalty);
        stats.total_access_time += config.access_latency + config.miss_penalty;

        return result;
    } else {
        // Fallback to regular read-modify-write
        uint32_t result = read_word(phys_addr);
        write_word(phys_addr, result + value);
        return result;
    }
}

uint32_t Cache::atomic_swap(uint32_t addr, uint32_t value, uint32_t core_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);

    // Translate address if MMU is available
    uint32_t phys_addr = translate_address(addr, true, false);

    // For atomic operations, we bypass cache and access memory directly
    // This is safe for shared variables like uart_mutex because:
    // 1. uart_unlock uses WRITE_THROUGH policy (writes go directly to memory)
    // 2. atomic_swap reads directly from memory
    // 3. This ensures all cores see the latest value immediately
    if (memory_interface) {
        uint32_t result = memory_interface->read_word(phys_addr);
        memory_interface->write_word(phys_addr, value);

        // Invalidate this cache line in all cores' caches to maintain coherence
        invalidate(phys_addr);

        stats.accesses++;
        stats.misses++;  // Atomic operations bypass cache, count as miss for timing
        stats.total_miss_penalty += config.miss_penalty;
        stats.update_core_stats(core_id, false, config.access_latency + config.miss_penalty, config.miss_penalty);
        stats.total_access_time += config.access_latency + config.miss_penalty;

        return result;
    } else {
        // Fallback to regular read-modify-write
        uint32_t result = read_word(phys_addr);
        write_word(phys_addr, value);
        return result;
    }
}

uint32_t Cache::atomic_compare_and_swap(uint32_t addr, uint32_t expected, uint32_t desired, uint32_t core_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);

    // Translate address if MMU is available
    uint32_t phys_addr = translate_address(addr, true, false);

    // For atomic operations, we must bypass cache and write directly to memory
    // to ensure all cores see the update immediately (cache coherence)
    if (memory_interface) {
        uint32_t current_value = memory_interface->read_word(phys_addr);
        if (current_value == expected) {
            memory_interface->write_word(phys_addr, desired);
        }

        // Invalidate this cache line if present to maintain coherence
        invalidate(phys_addr);

        stats.accesses++;
        stats.misses++;  // Atomic operations bypass cache, count as miss for timing
        stats.total_miss_penalty += config.miss_penalty;
        stats.update_core_stats(core_id, false, config.access_latency + config.miss_penalty, config.miss_penalty);
        stats.total_access_time += config.access_latency + config.miss_penalty;

        return current_value;
    } else {
        // Fallback to regular read-modify-write
        uint32_t current_value = read_word(phys_addr);
        if (current_value == expected) {
            write_word(phys_addr, desired);
        }
        return current_value;
    }
}

uint32_t Cache::atomic_fetch_and_add(uint32_t addr, uint32_t value, uint32_t core_id) {
    return atomic_add(addr, value, core_id);
}

uint32_t Cache::atomic_fetch_and_sub(uint32_t addr, uint32_t value, uint32_t core_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);

    // Translate address if MMU is available
    uint32_t phys_addr = translate_address(addr, true, false);

    // For atomic operations, we must bypass cache and write directly to memory
    // to ensure all cores see the update immediately (cache coherence)
    if (memory_interface) {
        uint32_t result = memory_interface->read_word(phys_addr);
        memory_interface->write_word(phys_addr, result - value);

        // Invalidate this cache line if present to maintain coherence
        invalidate(phys_addr);

        stats.accesses++;
        stats.misses++;  // Atomic operations bypass cache, count as miss for timing
        stats.total_miss_penalty += config.miss_penalty;
        stats.update_core_stats(core_id, false, config.access_latency + config.miss_penalty, config.miss_penalty);
        stats.total_access_time += config.access_latency + config.miss_penalty;

        return result;
    } else {
        // Fallback to regular read-modify-write
        uint32_t result = read_word(phys_addr);
        write_word(phys_addr, result - value);
        return result;
    }
}

uint32_t Cache::atomic_fetch_and_and(uint32_t addr, uint32_t value, uint32_t core_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);

    // Translate address if MMU is available
    uint32_t phys_addr = translate_address(addr, true, false);

    // For atomic operations, we must bypass cache and write directly to memory
    // to ensure all cores see the update immediately (cache coherence)
    if (memory_interface) {
        uint32_t result = memory_interface->read_word(phys_addr);
        memory_interface->write_word(phys_addr, result & value);

        // Invalidate this cache line if present to maintain coherence
        invalidate(phys_addr);

        stats.accesses++;
        stats.misses++;  // Atomic operations bypass cache, count as miss for timing
        stats.total_miss_penalty += config.miss_penalty;
        stats.update_core_stats(core_id, false, config.access_latency + config.miss_penalty, config.miss_penalty);
        stats.total_access_time += config.access_latency + config.miss_penalty;

        return result;
    } else {
        // Fallback to regular read-modify-write
        uint32_t result = read_word(phys_addr);
        write_word(phys_addr, result & value);
        return result;
    }
}

uint32_t Cache::atomic_fetch_and_or(uint32_t addr, uint32_t value, uint32_t core_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);

    // Translate address if MMU is available
    uint32_t phys_addr = translate_address(addr, true, false);

    // For atomic operations, we must bypass cache and write directly to memory
    // to ensure all cores see the update immediately (cache coherence)
    if (memory_interface) {
        uint32_t result = memory_interface->read_word(phys_addr);
        memory_interface->write_word(phys_addr, result | value);

        // Invalidate this cache line if present to maintain coherence
        invalidate(phys_addr);

        stats.accesses++;
        stats.misses++;  // Atomic operations bypass cache, count as miss for timing
        stats.total_miss_penalty += config.miss_penalty;
        stats.update_core_stats(core_id, false, config.access_latency + config.miss_penalty, config.miss_penalty);
        stats.total_access_time += config.access_latency + config.miss_penalty;

        return result;
    } else {
        // Fallback to regular read-modify-write
        uint32_t result = read_word(phys_addr);
        write_word(phys_addr, result | value);
        return result;
    }
}

uint32_t Cache::atomic_fetch_and_xor(uint32_t addr, uint32_t value, uint32_t core_id) {
    std::lock_guard<std::mutex> lock(cache_mutex);

    // Translate address if MMU is available
    uint32_t phys_addr = translate_address(addr, true, false);

    // For atomic operations, we must bypass cache and write directly to memory
    // to ensure all cores see the update immediately (cache coherence)
    if (memory_interface) {
        uint32_t result = memory_interface->read_word(phys_addr);
        memory_interface->write_word(phys_addr, result ^ value);

        // Invalidate this cache line if present to maintain coherence
        invalidate(phys_addr);

        stats.accesses++;
        stats.misses++;  // Atomic operations bypass cache, count as miss for timing
        stats.total_miss_penalty += config.miss_penalty;
        stats.update_core_stats(core_id, false, config.access_latency + config.miss_penalty, config.miss_penalty);
        stats.total_access_time += config.access_latency + config.miss_penalty;

        return result;
    } else {
        // Fallback to regular read-modify-write
        uint32_t result = read_word(phys_addr);
        write_word(phys_addr, result ^ value);
        return result;
    }
}