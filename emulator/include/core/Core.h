#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include "../memory/MMU.h"
#include "../memory/Cache.h"
#include "../utils/FunctionProfiler.h"
#include "../utils/PerformanceProfiler.h"

// Forward declarations
class Simulator;
class Device;
class CLINT;
class PLIC;

class Core {
public:
    Core(uint32_t id, uint8_t* shared_mem, uint32_t initial_pc = 0);
    void set_simulator(Simulator* sim) { simulator = sim; }
    void step(); // Execute one instruction
    void reset();
    uint32_t get_id() const { return core_id; }
    uint32_t get_pc() const { return pc; }
    void set_pc(uint32_t new_pc) { pc = new_pc; }
    uint32_t get_initial_pc() const { return initial_pc; }
    void set_initial_pc(uint32_t pc) { initial_pc = pc; }
    bool get_finished() const { return has_finished; }
    void set_finished(bool finished) { has_finished = finished; }
    bool get_booted() const { return has_booted; }
    void set_booted(bool booted) { has_booted = booted; }

    // WFI (Wait for Interrupt) state management
    bool is_waiting_for_interrupt_state() const { return is_waiting_for_interrupt; }
    void exit_wait_state() { is_waiting_for_interrupt = false; }
    // Profiling methods - now delegated to profiler classes
    uint64_t get_instruction_count() const;
    void set_function_profiling(bool enable);
    bool get_function_profiling() const;
    void add_function_profile(const std::string& name, uint64_t start_pc, uint64_t end_pc);
    void print_function_profiling_results();

    // Methods to access profiler instances
    FunctionProfiler* get_function_profiler() { return function_profiler; }
    PerformanceProfiler* get_performance_profiler() { return performance_profiler; }

    // Interrupt controller access
    void set_clint(CLINT* clint) { this->clint = clint; }
    void set_plic(PLIC* plic) { this->plic = plic; }

    // Interrupt handling
    void handle_interrupt();
    bool check_pending_interrupts();
    void take_interrupt(ExceptionType interrupt_type);
    void take_supervisor_interrupt(uint32_t cause);
    void take_exception(uint32_t cause);  // Handle exceptions (like ecall)
    void update_mip_csr();

    // Cache access methods
    void set_instruction_cache(Cache* icache) { instruction_cache = icache; }
    void set_data_cache(Cache* dcache) { data_cache = dcache; }
    void set_mmu(MMU* mmu) { this->mmu = mmu; }

    // Method to read instruction at a specific PC
    uint32_t read_instruction(uint32_t pc_addr);

    // CSR register access methods
    uint32_t read_csr(uint32_t csr_addr);
    void write_csr(uint32_t csr_addr, uint32_t value);

    // Debugger support methods
    uint32_t get_reg(uint32_t reg_num) const;
    void set_reg(uint32_t reg_num, uint32_t value);
    const uint32_t* get_regs() const { return regs; }
    bool check_debug_breakpoint();

private:
    uint32_t core_id;
    uint8_t* memory; // pointer to shared physical memory
    uint32_t initial_pc = 0;  // Initial PC for this core
    uint32_t pc = 0;
    uint32_t regs[32] = {0}; // x0 is always zero
    Simulator* simulator = nullptr; // back‑reference for MMIO

    // Boot sequence state
    bool has_booted = false;
    bool has_finished = false;
    bool is_waiting_for_interrupt = false; // WFI state

    // Caches
    Cache* instruction_cache = nullptr;
    Cache* data_cache = nullptr;

    // MMU
    MMU* mmu = nullptr;

    // Interrupt Controllers
    CLINT* clint = nullptr;
    PLIC* plic = nullptr;

    // CSR Registers (Control and Status Registers)
    // We'll implement a sparse CSR map for efficiency
    std::vector<uint32_t> csr_registers;

    // Privilege level support
    enum PrivilegeLevel {
        PRV_U = 0,  // User mode
        PRV_S = 1,  // Supervisor mode
        PRV_M = 3   // Machine mode
    };
    PrivilegeLevel privilege_level = PRV_M;  // Start in Machine mode

    // Performance Monitor Counter - Per-core implementation
    uint32_t performance_counter = 0;
    uint32_t performance_control = 0;  // Bit 0: Enable, Bit 1: Reset
    uint32_t performance_event_select = 0; // Event type to count

    // Per-core performance monitoring with synchronization
    static std::mutex performance_counter_mutex;
    static uint32_t global_instruction_counter;
    static uint32_t global_memory_access_counter;
    static uint32_t global_branch_counter;
    static uint32_t global_cache_hit_counter;

    // Initialize CSR registers
    void init_csrs();

    // Performance Monitor Counter methods
    void update_performance_counter();
    void increment_performance_counter();

    // Multi-core performance counter methods
    static void increment_global_counter(uint32_t event_type);
    static uint32_t read_global_counter(uint32_t event_type);

    // SATP CSR specific handling
    void update_mmu_from_satp(uint32_t satp_value);
    uint32_t get_satp_value() const;

    // Exception handling
    uint32_t handle_exception(ExceptionType ex, uint32_t fault_pc);

    // Profiler instances - now using dedicated profiling classes
public:
    FunctionProfiler* function_profiler;
    PerformanceProfiler* performance_profiler;

    // TODO: add instruction decode/execute helpers
};