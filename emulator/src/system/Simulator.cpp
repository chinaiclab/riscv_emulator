// Simulator.cpp – minimal multi‑core RV32I simulator skeleton

#include "../include/system/Simulator.h"
#include "../include/system/Device.h"
#include "../include/device/UART.h" // Include UART header
#include "../include/memory/PMA.h"
#include "../include/interrupt/CLINT.h"
#include "../include/interrupt/PLIC.h"
#include "../include/utils/DebugLogger.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <map>
#include <vector>

// Forward declaration for the DDR access completion callback
void ddr_access_complete_callback(uint32_t addr, bool is_write, uint64_t latency);

// Create a static pointer to maintain reference to simulator instance for callbacks
static Simulator* global_simulator_instance = nullptr;

Simulator::Simulator(uint32_t num_cores, uint32_t mem_size)
    : num_cores(num_cores), memory(mem_size, 0),
      l1_icaches(num_cores, CacheConfig(4096, 4, 64, true, 1, 10)),   // Per-core 4KB, 4-way, 64B block, instruction caches
      l1_dcaches(num_cores, CacheConfig(4096, 4, 64, false, 1, 10)),  // Per-core 4KB, 4-way, 64B block, data caches
      l2_cache(CacheConfig(16384, 8, 64, false, 5, 50)),              // Shared 16KB, 8-way, 64B block, unified L2 cache
      mmus(num_cores, MMUConfig(4096, 32, true)),                     // Per-core MMUs with 32 TLB entries
      coherence_controller(num_cores),                                // Cache coherence controller
      ddr_memory(DDRConfig(0x40000000, 4096, 15, 8, 17000000000ULL)), // 1GB DDR with 17GB/s bandwidth
      memory_interface(memory, &ddr_memory), core_booted_flags(num_cores, false),
      multi_core_monitor(new MultiCoreMonitor(num_cores)),
      clint(new CLINT(num_cores)),
      plic(new PLIC(32, num_cores)) {

    // Initialize cores with shared memory pointer and back‑reference
    for (uint32_t i = 0; i < num_cores; ++i) {
        cores.emplace_back(i, memory.data());
        cores.back().set_simulator(this);
        // Connect L1 caches to cores
        cores.back().set_instruction_cache(&l1_icaches[i]);
        cores.back().set_data_cache(&l1_dcaches[i]);
        // Connect MMU to core
        cores.back().set_mmu(&mmus[i]);
        // Connect interrupt controllers to core
        cores.back().set_clint(clint);
        cores.back().set_plic(plic);
    }
    
    // Initialize per-core statistics for L1 caches
    for (uint32_t i = 0; i < num_cores; ++i) {
        l1_icaches[i].set_num_cores(num_cores);
        l1_dcaches[i].set_num_cores(num_cores);
    }
    
    // Initialize per-core statistics for L2 cache
    l2_cache.set_num_cores(num_cores);
    
    // Initialize per-core statistics for MMUs
    for (uint32_t i = 0; i < num_cores; ++i) {
        mmus[i].set_num_cores(num_cores);
    }
    
    // Connect L1 caches to L2 cache
    for (uint32_t i = 0; i < num_cores; ++i) {
        l1_icaches[i].set_memory_interface(&l2_cache);
        l1_dcaches[i].set_memory_interface(&l2_cache);
        // Connect MMU to L1 caches
        l1_icaches[i].set_mmu(&mmus[i]);
        l1_dcaches[i].set_mmu(&mmus[i]);
    }
    
    // Connect L2 cache to DDR memory controller
    l2_cache.set_memory_interface(&ddr_memory);

    // Create direct memory interface for MMU page table walks
    // This bypasses the cache hierarchy to avoid recursive MMU calls
    direct_memory_interface = new SimulatorMemoryInterface(memory, &ddr_memory, nullptr, &l2_cache);

    // Connect MMUs to direct memory interface for page table walks
    // This bypasses the cache hierarchy to avoid recursive MMU calls
    for (uint32_t i = 0; i < num_cores; ++i) {
        mmus[i].set_memory_interface(direct_memory_interface);
    }

    // Initialize profiler instances
    function_profiler = new FunctionProfiler();
    performance_profiler = new PerformanceProfiler();
    memory_tracker = new MemoryAccessTracker();

    // Connect profilers to cores
    for (uint32_t i = 0; i < num_cores; ++i) {
        cores[i].function_profiler = function_profiler;
        cores[i].performance_profiler = performance_profiler;
    }

    // Register UART device
    uart_device = new UART();
    register_device(0x10000000, uart_device);
#if DEBUG
    SIM_LOG("Registered UART device at address 0x10000000");
#endif

    // Initialize PMA controller with default regions based on linker script
    PMAController::getInstance().initializeDefaultRegions();

    // Initialize MultiCoreMonitor state in memory (including initialization_phase)
    multi_core_monitor->write_state_to_memory(memory);
#if DEBUG
    SIM_LOG("Initialized MultiCoreMonitor state in memory (initialization_phase=true)");
#endif
}

void Simulator::initialize_callbacks() {
    // Register callback for when memory access completes
    // This callback will be used to properly attribute latency to the function that
    // initiated the memory access
    ddr_memory.set_access_complete_callback(ddr_access_complete_callback);

    // Set the global simulator instance for callbacks
    global_simulator_instance = this;
}

void Simulator::reset() {
    for (auto &c : cores) {
        c.reset();
    }
    std::fill(memory.begin(), memory.end(), 0);
    devices.clear();
    
    // Reset caches
    for (auto& cache : l1_icaches) {
        cache.invalidate_all();
        cache.reset_stats();
    }
    for (auto& cache : l1_dcaches) {
        cache.invalidate_all();
        cache.reset_stats();
    }
    l2_cache.invalidate_all();
    l2_cache.reset_stats();
    
    // Reset MMUs
    for (auto& mmu : mmus) {
        mmu.flush_tlb();
    }
}

void Simulator::register_device(uint32_t base_addr, Device* dev) {
    devices[base_addr] = dev;
}

Device* Simulator::get_device(uint32_t addr) const {
    auto it = devices.find(addr);
    if (it != devices.end()) {
#if DEBUG
        SIM_LOGF("Found device at address 0x%08x", addr);
#endif
        return it->second;
    }
#if DEBUG
    SIM_LOGF("No device found at address 0x%08x", addr);
#endif
    return nullptr;
}

// ELF32 header and program header structures for loading ELF files
// This allows proper handling of LMA (Load Memory Address) for segments
namespace {
    struct Elf32_Ehdr {
        uint8_t  e_ident[16];
        uint16_t e_type;
        uint16_t e_machine;
        uint32_t e_version;
        uint32_t e_entry;
        uint32_t e_phoff;
        uint32_t e_shoff;
        uint32_t e_flags;
        uint16_t e_ehsize;
        uint16_t e_phentsize;
        uint16_t e_phnum;
        uint16_t e_shentsize;
        uint16_t e_shnum;
        uint16_t e_shstrndx;
    };

    struct Elf32_Phdr {
        uint32_t p_type;
        uint32_t p_offset;
        uint32_t p_vaddr;
        uint32_t p_paddr;    // Physical address (LMA) - critical for loading
        uint32_t p_filesz;
        uint32_t p_memsz;
        uint32_t p_flags;
        uint32_t p_align;
    };

    struct Elf32_Shdr {
        uint32_t sh_name;
        uint32_t sh_type;
        uint32_t sh_flags;
        uint32_t sh_addr;
        uint32_t sh_offset;
        uint32_t sh_size;
        uint32_t sh_link;
        uint32_t sh_info;
        uint32_t sh_addralign;
        uint32_t sh_entsize;
    };

    struct Elf32_Sym {
        uint32_t st_name;
        uint32_t st_value;
        uint32_t st_size;
        uint8_t  st_info;
        uint8_t  st_other;
        uint16_t st_shndx;
    };

    constexpr uint32_t PT_LOAD = 1;
    constexpr uint32_t SHT_SYMTAB = 2;
    constexpr uint32_t SHT_STRTAB = 3;

    // Helper function to find _start symbol in ELF symbol table
    uint32_t find_symbol_start(const std::vector<char>& buffer, const Elf32_Ehdr& ehdr, const char* symbol_name) {
        if (ehdr.e_shoff == 0 || ehdr.e_shnum == 0) {
            return 0;  // No section headers
        }

        // First, find symbol table and string table sections
        uint32_t symtab_offset = 0, symtab_size = 0, symtab_entsize = 0;
        uint32_t strtab_offset = 0, strtab_size = 0;
        uint32_t shstrtab_offset = 0;

        // Read section headers
        for (uint16_t i = 0; i < ehdr.e_shnum; i++) {
            uint32_t sh_offset = ehdr.e_shoff + i * ehdr.e_shentsize;
            if (sh_offset + sizeof(Elf32_Shdr) > buffer.size()) continue;

            Elf32_Shdr shdr;
            std::memcpy(&shdr, buffer.data() + sh_offset, sizeof(Elf32_Shdr));

            if (shdr.sh_type == SHT_SYMTAB) {
                symtab_offset = shdr.sh_offset;
                symtab_size = shdr.sh_size;
                symtab_entsize = shdr.sh_entsize;
                // sh_link points to the string table for symbol names
                uint32_t strtab_sh_offset = ehdr.e_shoff + shdr.sh_link * ehdr.e_shentsize;
                if (strtab_sh_offset + sizeof(Elf32_Shdr) <= buffer.size()) {
                    Elf32_Shdr strtab_shdr;
                    std::memcpy(&strtab_shdr, buffer.data() + strtab_sh_offset, sizeof(Elf32_Shdr));
                    strtab_offset = strtab_shdr.sh_offset;
                    strtab_size = strtab_shdr.sh_size;
                }
                break;
            }
        }

        if (symtab_offset == 0 || symtab_entsize == 0) {
            return 0;  // No symbol table
        }

        // Search for the symbol
        size_t name_len = std::strlen(symbol_name);
        for (uint32_t offset = 0; offset + sizeof(Elf32_Sym) <= symtab_size; offset += symtab_entsize) {
            Elf32_Sym sym;
            std::memcpy(&sym, buffer.data() + symtab_offset + offset, sizeof(Elf32_Sym));

            if (sym.st_name < strtab_size) {
                const char* name = buffer.data() + strtab_offset + sym.st_name;
                if (std::strncmp(name, symbol_name, name_len + 1) == 0) {
                    return sym.st_value;
                }
            }
        }

        return 0;  // Symbol not found
    }
}

bool Simulator::load_program(const std::string &path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open file: " << path << std::endl;
        return false;
    }
    std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

    // Check if this is an ELF file by magic number
    bool is_elf = (buffer.size() >= 16 &&
                   static_cast<uint8_t>(buffer[0]) == 0x7f &&
                   buffer[1] == 'E' && buffer[2] == 'L' && buffer[3] == 'F');

    uint32_t entry_point = 0;
    uint32_t max_loaded_addr = 0;

    if (is_elf) {
        // Parse ELF32 header
        if (buffer.size() < sizeof(Elf32_Ehdr)) {
            std::cerr << "Error: Invalid ELF file (too small for header)" << std::endl;
            return false;
        }

        Elf32_Ehdr ehdr;
        std::memcpy(&ehdr, buffer.data(), sizeof(Elf32_Ehdr));

        // Verify it's a 32-bit ELF (EI_CLASS = 1)
        if (ehdr.e_ident[4] != 1) {
            std::cerr << "Error: Not a 32-bit ELF file (e_ident[4]=" << (int)ehdr.e_ident[4] << ")" << std::endl;
            return false;
        }

        entry_point = ehdr.e_entry;
        std::cout << "Loading ELF file, ELF entry point: 0x" << std::hex << entry_point << std::dec << std::endl;

        // Parse and load each program segment
        for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
            uint32_t ph_offset = ehdr.e_phoff + i * ehdr.e_phentsize;
            if (ph_offset + sizeof(Elf32_Phdr) > buffer.size()) {
                std::cerr << "Warning: Program header " << i << " exceeds file size, skipping" << std::endl;
                continue;
            }

            Elf32_Phdr phdr;
            std::memcpy(&phdr, buffer.data() + ph_offset, sizeof(Elf32_Phdr));

            // Only load PT_LOAD segments
            if (phdr.p_type != PT_LOAD) {
                continue;
            }

            // Use physical address (LMA) for loading - this is critical for LMA != VMA cases
            uint32_t load_addr = phdr.p_paddr;

            std::cout << "ELF Segment " << i << ": LMA=0x" << std::hex << load_addr
                      << ", VMA=0x" << phdr.p_vaddr
                      << ", filesz=" << std::dec << phdr.p_filesz
                      << ", memsz=" << phdr.p_memsz << std::endl;

            if (load_addr + phdr.p_memsz > memory.size()) {
                std::cerr << "Error: Segment " << i << " exceeds memory size" << std::endl;
                continue;
            }

            // Copy segment content from file to memory at LMA
            if (phdr.p_filesz > 0 && phdr.p_offset + phdr.p_filesz <= buffer.size()) {
                std::memcpy(memory.data() + load_addr,
                           buffer.data() + phdr.p_offset,
                           phdr.p_filesz);
            }

            // Zero-fill the BSS portion (memsz > filesz)
            if (phdr.p_memsz > phdr.p_filesz) {
                std::memset(memory.data() + load_addr + phdr.p_filesz, 0,
                           phdr.p_memsz - phdr.p_filesz);
            }

            // Track maximum loaded address
            if (load_addr + phdr.p_memsz > max_loaded_addr) {
                max_loaded_addr = load_addr + phdr.p_memsz;
            }
        }

        // Try to find _start symbol as the entry point (more reliable than ELF header entry)
        uint32_t symbol_start = find_symbol_start(buffer, ehdr, "_start");
        if (symbol_start != 0) {
            entry_point = symbol_start;
            std::cout << "Found _start symbol at 0x" << std::hex << entry_point << std::dec << ", using as entry point" << std::endl;
        }

        // Write loaded segments through memory interface for cache coherence
        for (uint16_t i = 0; i < ehdr.e_phnum; i++) {
            uint32_t ph_offset = ehdr.e_phoff + i * ehdr.e_phentsize;
            Elf32_Phdr phdr;
            std::memcpy(&phdr, buffer.data() + ph_offset, sizeof(Elf32_Phdr));

            if (phdr.p_type != PT_LOAD || phdr.p_filesz == 0) continue;

            uint32_t load_addr = phdr.p_paddr;
            for (uint32_t j = 0; j < phdr.p_filesz; j += 4) {
                uint32_t word = 0;
                for (int k = 0; k < 4 && (j + k) < phdr.p_filesz; k++) {
                    word |= (static_cast<uint8_t>(memory[load_addr + j + k]) << (k * 8));
                }
                memory_interface.write_word(load_addr + j, word);
            }
        }

    } else {
        // Raw binary loading (original logic)
        if (buffer.size() > memory.size()) {
            std::cerr << "Error: Program size (" << buffer.size() << " bytes) exceeds memory size ("
                      << memory.size() << " bytes)" << std::endl;
            std::cerr << "Use --memory option to increase memory size (e.g., --memory 4M)" << std::endl;
            return false;
        }

        // Copy the program to memory starting at address 0
        std::copy(buffer.begin(), buffer.end(), memory.begin());

        // Write through memory interface for cache coherence
        for (size_t i = 0; i < buffer.size(); i += 4) {
            uint32_t word = 0;
            for (int j = 0; j < 4 && (i + j) < buffer.size(); j++) {
                word |= (static_cast<uint8_t>(buffer[i + j]) << (j * 8));
            }
            memory_interface.write_word(static_cast<uint32_t>(i), word);
        }

        max_loaded_addr = static_cast<uint32_t>(buffer.size());

        // For raw binary, try to find entry point by pattern matching
        if (buffer.size() >= 4) {
            uint32_t entry_pc = 0;
            for (int i = static_cast<int>(buffer.size()) - 8; i >= 0; i -= 4) {
                uint32_t word = 0;
                for (int j = 0; j < 4 && (i + j) < static_cast<int>(buffer.size()); j++) {
                    word |= (static_cast<uint8_t>(buffer[i + j]) << (j * 8));
                }
                // Look for lui sp, followed by jump
                if ((word & 0x7f) == 0x37 && ((word >> 7) & 0x1f) == 2) {
                    if (i + 4 < static_cast<int>(buffer.size())) {
                        uint32_t next_word = 0;
                        for (int j = 0; j < 4 && (i + 4 + j) < static_cast<int>(buffer.size()); j++) {
                            next_word |= (static_cast<uint8_t>(buffer[i + 4 + j]) << (j * 8));
                        }
                        if ((next_word & 0x7f) == 0x6f) {
                            entry_pc = i;
                            break;
                        }
                    }
                }
            }
            if (entry_pc != 0) {
                entry_point = entry_pc;
            }
        }
    }

    // Invalidate all caches
    for (auto& cache : l1_icaches) {
        cache.invalidate_all();
    }
    for (auto& cache : l1_dcaches) {
        cache.invalidate_all();
    }
    l2_cache.invalidate_all();

    // Set entry point for all cores
        // FIX: For raw binary files, always use address 0 as entry point
        // Pattern matching can find wrong entry points (e.g., lui sp + j pattern)
        if (!is_elf) {
            entry_point = 0;
        }
    if (entry_point != 0) {
        for (auto& core : cores) {
            core.set_pc(entry_point);
        }
    }

#if DEBUG
    SIM_LOGF("Loaded %zu bytes from %s (ELF: %s)", buffer.size(), path.c_str(), is_elf ? "yes" : "no");
    SIM_LOGF("Entry point: 0x%08x, Max loaded addr: 0x%08x", entry_point, max_loaded_addr);
#endif

    // Clear BSS section: zero out memory from program end to MMIO region
    uint32_t program_end = max_loaded_addr;
    const uint32_t bss_end = 0x90000;

    if (program_end < bss_end) {
        SIM_LOGF("Clearing BSS section: 0x%08x to 0x%08x (%u bytes)",
                 program_end, bss_end, bss_end - program_end);
        std::fill(memory.begin() + program_end, memory.begin() + bss_end, 0);

        for (auto& cache : l1_icaches) {
            cache.invalidate_all();
        }
        for (auto& cache : l1_dcaches) {
            cache.invalidate_all();
        }
        l2_cache.invalidate_all();
    }

    return true;
}

// Execute a single step for one core (non-blocking)
void Simulator::execute_core_step(uint32_t core_id) {
    // Check if core is finished
    if (cores[core_id].get_finished()) {
        return;
    }

    // Check if core is booted (non-blocking)
    if (!cores[core_id].get_booted()) {
        if (core_id == 0) {
            // Core 0 can start immediately
            cores[core_id].set_booted(true);
        } else {
            // Other cores check if they have been released
            if (!core_booted_flags[core_id]) {
                return;  // Not yet released, skip this step
            } else {
                cores[core_id].set_booted(true);
            }
        }
    }

    // Execute one instruction step
    cores[core_id].step();

    // Old 0x90004-based release mechanism removed - using MultiCoreMonitor only
    // This prevents conflicts between two different release detection systems

    // Check for infinite loop
    uint32_t pc = cores[core_id].get_pc();
    uint32_t instr = cores[core_id].read_instruction(pc);

    // Check if it's a jump instruction to the same address (0x6f is the opcode for JAL)
    if ((instr & 0x7f) == 0x6f) {
        // Extract the jump offset
        uint32_t imm_j = ((instr >> 21) & 0x3ff) << 1;
        imm_j |= ((instr >> 20) & 0x1) << 11;
        imm_j |= ((instr >> 12) & 0xff) << 12;
        imm_j |= ((instr >> 31) & 0x1) << 20;
        // Sign extend
        int32_t signed_imm = (imm_j & 0x100000) ? (imm_j | 0xffe00000) : imm_j;

        // Calculate target address
        uint32_t target = pc + signed_imm;

        // If target equals current PC, it's an infinite loop
        if (target == pc) {
            cores[core_id].set_finished(true);
#if DEBUG
            SIM_LOGF("Core %d reached infinite loop at PC=0x%08x", core_id, pc);
#endif
        }
    }
}

// Sequential execution loop: simulate parallel execution of all cores
void Simulator::run(uint32_t cycles) {
    try {
        SIM_LOGF("=== ENTERING Simulator::run() ===");
        SIM_LOGF("Parameters: cycles=%u, num_cores=%u", cycles, num_cores);

        // Record start time for profiling
        if (profiling_enabled) {
            start_time = std::chrono::high_resolution_clock::now();
        }

        // Reset global cycle counter
        global_cycle_count = 0;
        simulation_running = true;

        // Set up DebugLogger to use our global cycle counter
        DebugLogger::getInstance().setExternalCycleCounter(&global_cycle_count);

        SIM_LOGF("About to start main execution loop");
        SIM_LOGF("Initial state: current_cycle=0, all_cores_finished=false");

#if DEBUG
        SIM_LOGF("Starting sequential simulation of %d cores for %d cycles", num_cores, cycles);
#endif

        // Main execution loop: one cycle = all cores execute one step
        bool all_cores_finished = false;
        uint32_t current_cycle = 0;

        SIM_LOGF("Entering while loop: current_cycle=%u, cycles=%u, all_cores_finished=%s",
                 current_cycle, cycles, all_cores_finished ? "true" : "false");

        while (current_cycle < cycles && !all_cores_finished) {
            SIM_LOGF("Starting cycle %u: current_cycle=%u < cycles=%u, all_cores_finished=%s",
                     current_cycle, current_cycle, cycles, all_cores_finished ? "true" : "false");

            try {
                // Execute one step on each core (simulating parallel execution)
                for (uint32_t i = 0; i < num_cores; ++i) {
                    SIM_LOGF("Executing step for Core#%u", i);
                    execute_core_step(i);
                    SIM_LOGF("Core#%u step completed", i);
                }

                // Update CLINT timer for this cycle
                if (clint) {
                    clint->update_timer(1); // Advance timer by 1 cycle
                }
            } catch (const std::exception& e) {
                SIM_LOGF("CYCLE %u: Exception during core execution: %s", current_cycle, e.what());
                throw;
            } catch (...) {
                SIM_LOGF("CYCLE %u: Unknown exception during core execution", current_cycle);
                throw;
            }

            // Check for release signals AFTER core execution to avoid timing conflicts
            // This ensures Core#0 can complete its release_all_cores function execution
            if (multi_core_monitor->check_release_signal(memory) && !release_all_cores_called) {
                SIM_LOGF("Cycle %u: Release signal detected, processing...", current_cycle);

                // Verify the release signal was written correctly by reading back from memory
                uint32_t release_value = 0;
                if (memory.size() >= MultiCoreMonitor::RELEASE_SIGNAL_ADDR + sizeof(uint32_t)) {
                    std::memcpy(&release_value, memory.data() + MultiCoreMonitor::RELEASE_SIGNAL_ADDR, sizeof(uint32_t));
                    SIM_LOGF("Memory verification: release signal 0x%08x successfully written at address 0x%08x",
                            release_value, MultiCoreMonitor::RELEASE_SIGNAL_ADDR);
                }

                // Call release_all_cores after cores have executed their current step
                SIM_LOGF("Cycle %u: Calling release_all_cores()...", current_cycle);
                try {
                    release_all_cores();
                    SIM_LOGF("Cycle %u: release_all_cores() completed successfully", current_cycle);
                } catch (const std::exception& e) {
                    SIM_LOGF("Cycle %u: release_all_cores() threw exception: %s", current_cycle, e.what());
                    throw; // Re-throw to see full stack
                }

                release_all_cores_called = true;

                // Handle in MultiCoreMonitor for state tracking
                SIM_LOGF("Cycle %u: Calling multi_core_monitor->handle_release_signal()...", current_cycle);
                try {
                    multi_core_monitor->handle_release_signal(memory, current_cycle);
                    SIM_LOGF("Cycle %u: handle_release_signal() completed successfully", current_cycle);
                } catch (const std::exception& e) {
                    SIM_LOGF("Cycle %u: handle_release_signal() threw exception: %s", current_cycle, e.what());
                    throw; // Re-throw to see full stack
                }
            }

            // Check if all cores have finished
            all_cores_finished = true;
            for (uint32_t i = 0; i < num_cores; ++i) {
                if (!cores[i].get_finished()) {
                    all_cores_finished = false;
                    break;
                }
            }

            SIM_LOGF("Cycle %u: all_cores_finished=%s", current_cycle, all_cores_finished ? "true" : "false");

            // Increment cycle counter (after all cores have executed one step)
            current_cycle++;
            global_cycle_count.store(current_cycle);

            // Update CLINT timer for timer interrupt generation
            if (clint) {
                clint->update_timer(1);
            }

#if DEBUG
            if (current_cycle % 1000 == 0) {
                SIM_LOGF("Completed cycle %u", current_cycle);
            }
#endif

            SIM_LOGF("Cycle %u completed, checking conditions: current_cycle=%u, cycles=%u, all_cores_finished=%s",
                     current_cycle-1, current_cycle, cycles, all_cores_finished ? "true" : "false");

            if (all_cores_finished) {
#if DEBUG
                SIM_LOG("All cores have finished, ending simulation early");
#endif
                SIM_LOGF("Breaking loop due to all_cores_finished=true at cycle %u", current_cycle);
                break;
            }
        }

        SIM_LOGF("Loop completed: current_cycle=%u, cycles=%u, all_cores_finished=%s",
                 current_cycle, cycles, all_cores_finished ? "true" : "false");

        simulation_running = false;
        core_threads.clear();

#if DEBUG
        SIM_LOGF("Parallel execution completed. Global cycle count: %u", global_cycle_count.load());
#endif

        SIM_LOGF("=== EXITING Simulator::run() NORMALLY ===");

        // Record end time for profiling
        if (profiling_enabled) {
            end_time = std::chrono::high_resolution_clock::now();
        }

        // Stop performance profiler timing
        if (performance_profiler && performance_profiler->is_profiling_enabled()) {
            performance_profiler->stop_timing();
        }

        if (cycles % 1000000 == 0) {
            std::cout << "Progress: " << cycles << " cycles completed..." << std::endl;
        }

        std::cout << "Simulation completed " << cycles << " cycles for " << num_cores << " cores." << std::endl;
    }
    catch (const std::exception& e) {
        SIM_LOGF("=== EXCEPTION IN Simulator::run(): %s ===", e.what());
        simulation_running = false;
        throw;
    } catch (...) {
        SIM_LOGF("=== UNKNOWN EXCEPTION IN Simulator::run() ===");
        simulation_running = false;
        throw;
    }
}


void Simulator::set_function_profiling(bool enable) {
    if (function_profiler) {
        function_profiler->enable_profiling(enable);
    }

    if (performance_profiler) {
        performance_profiler->enable_profiling(enable);
    }

    // Enable function profiling for all cores
    for (auto& core : cores) {
        core.set_function_profiling(enable);
    }
}

void Simulator::update_function_l2_stats(uint32_t core_id, size_t func_idx, uint64_t l2_hits, uint64_t l2_misses) {
    if (function_profiler) {
        function_profiler->update_cache_stats(core_id, func_idx, 0, 0, 0, 0, l2_hits, l2_misses);
    }
}

void Simulator::update_function_ddr_stats(uint32_t core_id, size_t func_idx, uint64_t ddr_reads, uint64_t ddr_writes, uint64_t latency) {
    if (function_profiler) {
        function_profiler->update_ddr_stats(core_id, func_idx, ddr_reads, ddr_writes, latency);
    }
}


void Simulator::print_cache_stats() {
    if (!profiling_enabled) {
        return;
    }
    
    std::cout << "\n===== CACHE STATISTICS =====" << std::endl;
    
    // Print L1 instruction cache stats for each core
    for (size_t i = 0; i < l1_icaches.size(); ++i) {
        std::cout << "Core " << i << " ";
        l1_icaches[i].print_stats("L1 Instruction Cache");
        std::cout << std::endl;
    }
    
    // Print L1 data cache stats for each core
    for (size_t i = 0; i < l1_dcaches.size(); ++i) {
        std::cout << "Core " << i << " ";
        l1_dcaches[i].print_stats("L1 Data Cache");
        std::cout << std::endl;
    }
    
    // Print L2 cache stats
    l2_cache.print_stats("L2 Cache");
    
    // Print per-core statistics for each cache
    std::cout << "\n===== PER-CORE CACHE STATISTICS =====" << std::endl;
    for (size_t core_id = 0; core_id < num_cores; ++core_id) {
        std::cout << "\n--- Core " << core_id << " Cache Statistics ---" << std::endl;
        
        // Print L1 instruction cache per-core stats
        for (size_t i = 0; i < l1_icaches.size(); ++i) {
            l1_icaches[i].print_core_stats("L1 Instruction Cache", core_id);
            std::cout << std::endl;
        }
        
        // Print L1 data cache per-core stats
        for (size_t i = 0; i < l1_dcaches.size(); ++i) {
            l1_dcaches[i].print_core_stats("L1 Data Cache", core_id);
            std::cout << std::endl;
        }
        
        // Print L2 cache per-core stats
        l2_cache.print_core_stats("L2 Cache", core_id);
        std::cout << std::endl;
    }
    
    // Print DDR memory statistics
    std::cout << "\n===== DDR MEMORY STATISTICS =====" << std::endl;
    ddr_memory.print_stats("DDR Memory");
    
    // Print cache coherence statistics
    std::cout << "\n===== CACHE COHERENCE STATISTICS =====" << std::endl;
    coherence_controller.print_stats();
    
    // Print cache hierarchy access time summary
    std::cout << "\n===== CACHE ACCESS TIME SUMMARY =====" << std::endl;
    uint64_t total_l1_i_access_time = 0;
    uint64_t total_l1_d_access_time = 0;
    uint64_t total_l2_access_time = 0;
    
    for (size_t i = 0; i < l1_icaches.size(); ++i) {
        total_l1_i_access_time += l1_icaches[i].get_stats().total_access_time;
    }
    
    for (size_t i = 0; i < l1_dcaches.size(); ++i) {
        total_l1_d_access_time += l1_dcaches[i].get_stats().total_access_time;
    }
    
    total_l2_access_time = l2_cache.get_stats().total_access_time;
    
    std::cout << "Total L1 Instruction Cache Access Time: " << total_l1_i_access_time << " cycles" << std::endl;
    std::cout << "Total L1 Data Cache Access Time: " << total_l1_d_access_time << " cycles" << std::endl;
    std::cout << "Total L2 Cache Access Time: " << total_l2_access_time << " cycles" << std::endl;
    std::cout << "Total Cache Access Time: " << (total_l1_i_access_time + total_l1_d_access_time + total_l2_access_time) << " cycles" << std::endl;
    std::cout << "Total DDR Access Time: " << ddr_memory.get_total_latency() << " cycles" << std::endl;
    
    std::cout << "=============================" << std::endl;
}

void Simulator::print_mmu_stats() {
    if (!profiling_enabled) {
        return;
    }
    
    std::cout << "\n===== MMU STATISTICS =====" << std::endl;
    
    // Print TLB stats for each core
    for (size_t i = 0; i < mmus.size(); ++i) {
        std::cout << "Core " << i << " ";
        mmus[i].print_tlb_stats();
        std::cout << std::endl;
    }
    
    // Print per-core statistics for each MMU
    std::cout << "\n===== PER-CORE MMU STATISTICS =====" << std::endl;
    for (size_t core_id = 0; core_id < num_cores; ++core_id) {
        std::cout << "\n--- Core " << core_id << " MMU Statistics ---" << std::endl;
        
        // Print TLB per-core stats
        for (size_t i = 0; i < mmus.size(); ++i) {
            mmus[i].print_core_tlb_stats(core_id);
            std::cout << std::endl;
        }
    }
    
    std::cout << "=========================" << std::endl;
}

// MemoryInterface implementation
uint32_t Simulator::read_word(uint32_t addr) {
    // Handle device MMIO access (e.g., UART at 0x10000000)
    // Check if address matches any registered device
    for (const auto& dev_entry : devices) {
        uint32_t dev_base = dev_entry.first;
        // UART device has 32 bytes of register space
        if (addr >= dev_base && addr < dev_base + 32) {
            uint32_t offset = addr - dev_base;
            return dev_entry.second->read(offset);
        }
    }

    // Handle CLINT MMIO access (0x02000000 - 0x0200FFFF or 0x20000000 - 0x2000FFFF)
    // The assembler may generate either address format
    if ((addr >= 0x02000000 && addr < 0x02010000) || (addr >= 0x20000000 && addr < 0x20010000)) {
        if (clint) {
            // Convert 0x20000000 range to 0x02000000 range if needed
            // Example: 0x200bff8 -> 0x0200bff8, difference is 0x1E000000
            uint32_t clint_addr = (addr >= 0x20000000 && addr < 0x20010000) ? addr + 0x1E000000 : addr;
            SIM_LOGF("[CLINT] read addr=0x%08x clint_addr=0x%08x", addr, clint_addr);
            return clint->read(clint_addr, 4);
        }
        return 0;
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

void Simulator::write_word(uint32_t addr, uint32_t data) {
    // Handle device MMIO access (e.g., UART at 0x10000000)
    // Check if address matches any registered device
    for (const auto& dev_entry : devices) {
        uint32_t dev_base = dev_entry.first;
        // UART device has 32 bytes of register space
        if (addr >= dev_base && addr < dev_base + 32) {
            uint32_t offset = addr - dev_base;
            dev_entry.second->write(offset, data);
            return;
        }
    }

    // Handle CLINT MMIO access (0x02000000 - 0x0200FFFF or 0x20000000 - 0x2000FFFF)
    // The assembler may generate either address format
    if ((addr >= 0x02000000 && addr < 0x02010000) || (addr >= 0x20000000 && addr < 0x20010000)) {
        if (clint) {
            // Convert 0x20000000 range to 0x02000000 range if needed
            // Example: 0x200bff8 -> 0x0200bff8, difference is 0x1E000000
            uint32_t clint_addr = (addr >= 0x20000000 && addr < 0x20010000) ? addr + 0x1E000000 : addr;
            clint->write(clint_addr, data, 4);
        }
        return;
    }

    if (addr + sizeof(uint32_t) <= memory.size()) {
        memory[addr] = data & 0xFF;
        memory[addr + 1] = (data >> 8) & 0xFF;
        memory[addr + 2] = (data >> 16) & 0xFF;
        memory[addr + 3] = (data >> 24) & 0xFF;

        // Verify the write was successful by reading back
        uint32_t verify_data = (memory[addr + 3] << 24) | (memory[addr + 2] << 16) | (memory[addr + 1] << 8) | memory[addr];

        // Log confirmation for critical addresses (MultiCoreMonitor, UART MMIO, etc.)
        if ((addr >= 0x90000 && addr < 0x90010) || (addr >= 0x10000000 && addr < 0x10000020)) {
            SIM_LOGF("Memory write confirmed: addr=0x%08x data=0x%08x verify=0x%08x", addr, data, verify_data);
        }
    }
}

void Simulator::write_byte(uint32_t addr, uint8_t data) {
    // Handle device MMIO access (e.g., UART at 0x10000000)
    // Check if address matches any registered device
    for (const auto& dev_entry : devices) {
        uint32_t dev_base = dev_entry.first;
        // UART device has 32 bytes of register space
        if (addr >= dev_base && addr < dev_base + 32) {
            uint32_t offset = addr - dev_base;
            // Device write methods expect 32-bit values, but we're writing a byte
            dev_entry.second->write(offset, static_cast<uint32_t>(data));
            return;
        }
    }

    // Regular memory access
    if (addr < memory.size()) {
        memory[addr] = data;
    }
}

void Simulator::write_halfword(uint32_t addr, uint16_t data) {
    // Handle device MMIO access (e.g., UART at 0x10000000)
    // Check if address matches any registered device
    for (const auto& dev_entry : devices) {
        uint32_t dev_base = dev_entry.first;
        // UART device has 32 bytes of register space
        if (addr >= dev_base && addr < dev_base + 32) {
            uint32_t offset = addr - dev_base;
            // Device write methods expect 32-bit values, but we're writing a halfword
            dev_entry.second->write(offset, static_cast<uint32_t>(data));
            return;
        }
    }

    // Regular memory access - handle unaligned access
    if (addr + sizeof(uint16_t) <= memory.size()) {
        memory[addr] = data & 0xFF;
        memory[addr + 1] = (data >> 8) & 0xFF;
    }
}

uint8_t Simulator::read_byte(uint32_t addr) {
    // Handle device MMIO access (e.g., UART at 0x10000000)
    // Check if address matches any registered device
    for (const auto& dev_entry : devices) {
        uint32_t dev_base = dev_entry.first;
        // UART device has 32 bytes of register space
        if (addr >= dev_base && addr < dev_base + 32) {
            uint32_t offset = addr - dev_base;
            uint32_t value = dev_entry.second->read(offset);
            return static_cast<uint8_t>(value);
        }
    }

    // Regular memory access
    if (addr < memory.size()) {
        return memory[addr];
    }
    return 0;
}

uint16_t Simulator::read_halfword(uint32_t addr) {
    // Handle device MMIO access (e.g., UART at 0x10000000)
    // Check if address matches any registered device
    for (const auto& dev_entry : devices) {
        uint32_t dev_base = dev_entry.first;
        // UART device has 32 bytes of register space
        if (addr >= dev_base && addr < dev_base + 32) {
            uint32_t offset = addr - dev_base;
            uint32_t value = dev_entry.second->read(offset);
            return static_cast<uint16_t>(value);
        }
    }

    // Regular memory access - handle unaligned access
    if (addr + sizeof(uint16_t) <= memory.size()) {
        uint16_t value = memory[addr] | (memory[addr + 1] << 8);
        return value;
    }
    return 0;
}

void Simulator::read_block(uint32_t addr, uint32_t* data, uint32_t words) {
#if DEBUG
    SIM_LOGF("Simulator reading %u words from addr=0x%08x", words, addr);
    // Show first few words of memory at this address
    SIM_LOGF("Memory content at addr 0x%08x:", addr);
    for (uint32_t i = 0; i < words && i < 4; i++) {
        uint32_t word_addr = addr + i * sizeof(uint32_t);
        if (word_addr + sizeof(uint32_t) <= memory.size()) {
            uint32_t mem_word = memory[word_addr] |
                                (memory[word_addr + 1] << 8) |
                                (memory[word_addr + 2] << 16) |
                                (memory[word_addr + 3] << 24);
            SIM_LOGF("memory word %d at 0x%08x: 0x%08x", i, word_addr, mem_word);
        } else {
            SIM_LOGF("memory word %d at 0x%08x: out of bounds", i, word_addr);
        }
    }
#endif
    for (uint32_t i = 0; i < words; i++) {
        uint32_t word_addr = addr + i * sizeof(uint32_t);
#if DEBUG
        SIM_LOGF("Reading word %d from addr=0x%08x", i, word_addr);
#endif
        if (word_addr + sizeof(uint32_t) <= memory.size()) {
            data[i] = memory[word_addr] |
                      (memory[word_addr + 1] << 8) |
                      (memory[word_addr + 2] << 16) |
                      (memory[word_addr + 3] << 24);
#if DEBUG
            SIM_LOGF("Read data=0x%08x", data[i]);
#endif
        } else {
            data[i] = 0;
#if DEBUG
            SIM_LOG("Address out of bounds, setting data=0x0");
#endif
        }
    }
#if DEBUG
    SIM_LOG("Final data array:");
    for (uint32_t i = 0; i < words && i < 4; i++) {
        SIM_LOGF("data[%d]=0x%08x", i, data[i]);
    }
#endif
}

void Simulator::write_block(uint32_t addr, const uint32_t* data, uint32_t words) {
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
uint32_t Simulator::atomic_add(uint32_t addr, uint32_t value) {
    if (addr + sizeof(uint32_t) <= memory.size()) {
        std::lock_guard<std::mutex> lock(memory_interface.memory_mutex);
        uint32_t* ptr = reinterpret_cast<uint32_t*>(memory.data() + addr);
        uint32_t old_value = *ptr;
        *ptr = old_value + value;
        return old_value;
    }
    return 0;
}

uint32_t Simulator::atomic_swap(uint32_t addr, uint32_t value) {
    if (addr + sizeof(uint32_t) <= memory.size()) {
        std::lock_guard<std::mutex> lock(memory_interface.memory_mutex);
        uint32_t* ptr = reinterpret_cast<uint32_t*>(memory.data() + addr);
        uint32_t old_value = *ptr;
        *ptr = value;
        return old_value;
    }
    return 0;
}

uint32_t Simulator::atomic_compare_and_swap(uint32_t addr, uint32_t expected, uint32_t desired) {
    if (addr + sizeof(uint32_t) <= memory.size()) {
        std::lock_guard<std::mutex> lock(memory_interface.memory_mutex);
        uint32_t* ptr = reinterpret_cast<uint32_t*>(memory.data() + addr);
        uint32_t current_value = *ptr;

        if (current_value == expected) {
            *ptr = desired;
        }

        return current_value;
    }
    return 0;
}

uint32_t Simulator::atomic_fetch_and_add(uint32_t addr, uint32_t value) {
    return atomic_add(addr, value);
}

uint32_t Simulator::atomic_fetch_and_sub(uint32_t addr, uint32_t value) {
    if (addr + sizeof(uint32_t) <= memory.size()) {
        std::lock_guard<std::mutex> lock(memory_interface.memory_mutex);
        uint32_t* ptr = reinterpret_cast<uint32_t*>(memory.data() + addr);
        uint32_t old_value = *ptr;
        *ptr = old_value - value;
        return old_value;
    }
    return 0;
}

uint32_t Simulator::atomic_fetch_and_and(uint32_t addr, uint32_t value) {
    if (addr + sizeof(uint32_t) <= memory.size()) {
        std::lock_guard<std::mutex> lock(memory_interface.memory_mutex);
        uint32_t* ptr = reinterpret_cast<uint32_t*>(memory.data() + addr);
        uint32_t old_value = *ptr;
        *ptr = old_value & value;
        return old_value;
    }
    return 0;
}

uint32_t Simulator::atomic_fetch_and_or(uint32_t addr, uint32_t value) {
    if (addr + sizeof(uint32_t) <= memory.size()) {
        std::lock_guard<std::mutex> lock(memory_interface.memory_mutex);
        uint32_t* ptr = reinterpret_cast<uint32_t*>(memory.data() + addr);
        uint32_t old_value = *ptr;
        *ptr = old_value | value;
        return old_value;
    }
    return 0;
}

uint32_t Simulator::atomic_fetch_and_xor(uint32_t addr, uint32_t value) {
    if (addr + sizeof(uint32_t) <= memory.size()) {
        std::lock_guard<std::mutex> lock(memory_interface.memory_mutex);
        uint32_t* ptr = reinterpret_cast<uint32_t*>(memory.data() + addr);
        uint32_t old_value = *ptr;
        *ptr = old_value ^ value;
        return old_value;
    }
    return 0;
}

// Boot synchronization methods
void Simulator::wait_for_boot(uint32_t core_id) {
    if (core_id == 0) {
        // Core 0 doesn't wait, it's the boot master
        return;
    }

    // Other cores wait until core 0 releases them
    std::unique_lock<std::mutex> lock(boot_mutex);
    boot_cv.wait(lock, [this, core_id] {
        return core_booted_flags[core_id];
    });
}

void Simulator::signal_core_booted(uint32_t core_id) {
    {
        std::lock_guard<std::mutex> lock(boot_mutex);
        core_booted_flags[core_id] = true;

        if (core_id == 0) {
            core0_booted = true;
        }
    }

    // Notify waiting cores
    boot_cv.notify_all();
}

void Simulator::release_all_cores() {
    SIM_LOGF("=== ENTERING release_all_cores() ===");

    {
        SIM_LOGF("Acquiring boot_mutex...");
        std::lock_guard<std::mutex> lock(boot_mutex);
        SIM_LOGF("boot_mutex acquired, releasing %zu cores", core_booted_flags.size() - 1);

        for (size_t i = 1; i < core_booted_flags.size(); ++i) {
            SIM_LOGF("Releasing core %zu: setting core_booted_flags[%zu]=true", i, i);
            core_booted_flags[i] = true;
            // Also set the Core object's booted state
            if (i < cores.size()) {
                SIM_LOGF("Setting Core object %zu booted state to true", i);
                cores[i].set_booted(true);
            }
        }
        SIM_LOGF("All core_booted_flags updated successfully");
    }

    SIM_LOGF("About to call boot_cv.notify_all()");
    // Notify all waiting cores
    boot_cv.notify_all();
    SIM_LOGF("=== EXITING release_all_cores() ===");
}

void ddr_access_complete_callback(uint32_t addr, bool is_write, uint64_t latency) {
    if (global_simulator_instance) {
        global_simulator_instance->handle_ddr_access_completion(addr, is_write, latency);
    }
}

void Simulator::handle_ddr_access_completion(uint32_t addr, bool is_write, uint64_t latency) {
    std::lock_guard<std::mutex> lock(memory_access_mutex);

    // Find the initiating function for this address access
    auto it = pending_accesses_by_addr.find(addr);
    if (it != pending_accesses_by_addr.end() && !it->second.empty()) {
        // Get the oldest access for this address (FIFO order)
        auto [core_id, function_idx, timestamp] = it->second.front();
        it->second.pop();

        // Attribute the DDR access to the function that initiated it
        if (function_profiler && core_id < cores.size()) {
            function_profiler->update_ddr_stats(core_id, function_idx, is_write ? 0 : 1, is_write ? 1 : 0, latency);
        }
    }
}

void Simulator::record_memory_access(uint32_t core_id, size_t function_idx, uint32_t addr, bool /* is_write */, uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(memory_access_mutex);

    // Add to the queue for this address
    pending_accesses_by_addr[addr].push({core_id, function_idx, timestamp});
}

void Simulator::process_completed_memory_accesses() {
    // This would be called when DDR accesses complete to properly attribute latency
    std::lock_guard<std::mutex> lock(memory_access_mutex);
    // Implementation for matching completed DDR accesses to initiating functions
}

// Initialize static pointer in constructor

size_t Simulator::get_current_function_index(uint32_t core_id, uint32_t pc) {
    if (function_profiler && core_id < cores.size()) {
        return function_profiler->get_current_function_index(core_id, pc);
    }
    // Return -1 if no function is found for this PC
    return static_cast<size_t>(-1);
}

// Additional profiling methods using new profiler classes
void Simulator::set_profiling(bool enable) {
    if (performance_profiler) {
        performance_profiler->enable_profiling(enable);
    }
}

bool Simulator::get_profiling() const {
    return performance_profiler ? performance_profiler->is_profiling_enabled() : false;
}

bool Simulator::get_function_profiling() const {
    return function_profiler ? function_profiler->is_profiling_enabled() : false;
}

void Simulator::add_function_profile(uint32_t /* core_id */, const std::string& name, uint64_t start_pc, uint64_t end_pc) {
    if (function_profiler) {
        function_profiler->add_function_profile(name, start_pc, end_pc);
    }
}

void Simulator::print_function_profiling_results() {
    if (function_profiler) {
        function_profiler->print_function_profiling_results();
    }
}

void Simulator::print_profiling_results() {
    if (performance_profiler) {
        performance_profiler->print_profiling_results();
    }

    if (function_profiler) {
        function_profiler->print_function_profiling_results();
    }
}

Simulator::~Simulator() {
    delete uart_device;
    delete function_profiler;
    delete performance_profiler;
    delete memory_tracker;
    delete direct_memory_interface;
    delete multi_core_monitor;
    delete debugger;
}

// Debugger support methods
void Simulator::set_debug_mode(bool enable) {
    debug_mode = enable;
    if (enable && !debugger) {
        debugger = new Debugger(this);
    }
}

bool Simulator::check_breakpoint(uint32_t core_id, uint32_t pc) const {
    if (debugger) {
        return debugger->should_break(pc);
    }
    return false;
}

void Simulator::enter_debug_loop() {
    if (debugger) {
        debugger->command_loop();
    }
}

Core* Simulator::get_core(uint32_t core_id) {
    if (core_id < cores.size()) {
        return &cores[core_id];
    }
    return nullptr;
}

void Simulator::debug_run(uint32_t steps) {
    // Run specified number of steps in debug mode
    for (uint32_t step = 0; step < steps; step++) {
        // Check if all cores finished
        bool all_finished = true;
        for (uint32_t i = 0; i < num_cores; i++) {
            if (!cores[i].get_finished()) {
                all_finished = false;
                break;
            }
        }
        if (all_finished) {
            break;
        }

        // Execute one step on each core
        for (uint32_t i = 0; i < num_cores; i++) {
            if (!cores[i].get_finished()) {
                execute_core_step(i);
            }
        }

        // Check for release signals AFTER core execution (same as normal mode)
        if (multi_core_monitor && !release_all_cores_called) {
            if (multi_core_monitor->check_release_signal(memory)) {
                // Release all cores for parallel execution
                release_all_cores();
                release_all_cores_called = true;

                // Handle in MultiCoreMonitor for state tracking
                // Use a dummy cycle count since we're in debug mode
                multi_core_monitor->handle_release_signal(memory, 0);
            }
        }

        // Update CLINT timer
        if (clint) {
            clint->update_timer(1);
        }
    }
}
