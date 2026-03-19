// Core.cpp – minimal RV32I core skeleton with MMIO support

#include "../include/system/Simulator.h"
#include "../include/system/Device.h"
#include "../memory/MMU.h"
#include "../memory/PMA.h"
#include "../include/core/Core.h"
#include "../include/interrupt/CLINT.h"
#include "../include/interrupt/PLIC.h"
#include "../include/utils/DebugLogger.h"
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <mutex>

// Initialize static member variables for multi-core performance counters
std::mutex Core::performance_counter_mutex;
uint32_t Core::global_instruction_counter = 0;
uint32_t Core::global_memory_access_counter = 0;
uint32_t Core::global_branch_counter = 0;
uint32_t Core::global_cache_hit_counter = 0;

Core::Core(uint32_t id, uint8_t* shared_mem, uint32_t initial_pc)
    : core_id(id), memory(shared_mem), initial_pc(initial_pc), instruction_cache(nullptr), data_cache(nullptr), mmu(nullptr),
      function_profiler(nullptr), performance_profiler(nullptr) {
    // Initialize CSR registers
    init_csrs();
    reset();
}

void Core::reset() {
    pc = initial_pc;  // Use the initial PC for this core
    for (int i = 0; i < 32; ++i) regs[i] = 0;
    regs[0] = 0; // x0 is always zero
    has_booted = false; // Reset boot state
    is_waiting_for_interrupt = false; // Reset WFI state

    // Set mhartid CSR to core ID (read-only, but set internally)
    if (csr_registers.size() > 0xF14) {
        csr_registers[0xF14] = core_id;
    }

    // Initialize stack pointer (x2) to top of memory (grows downwards)
    regs[2] = static_cast<uint32_t>(simulator ? simulator->get_memory_size() : 0x100000);

    // Reset profilers if available
    if (performance_profiler) {
        performance_profiler->reset_timing();
    }

#if DEBUG == 1
    printf("[Core#%u] Reset: PC=0x%08x (initial=0x%08x), SP=0x%08x\n",
           core_id, pc, initial_pc, regs[2]);
#endif
}

// Helper to sign‑extend a value of given bit width
static int32_t sign_extend(uint32_t value, int bits) {
    int32_t mask = 1 << (bits - 1);
    return (value ^ mask) - mask;
}

// Load/store helpers (little‑endian)
static uint32_t load32(const uint8_t *mem, uint32_t addr) {
    uint32_t v = mem[addr] |
                 (mem[addr + 1] << 8) |
                 (mem[addr + 2] << 16) |
                 (mem[addr + 3] << 24);
    return v;
}
__attribute__((unused)) static void store32(uint8_t *mem, uint32_t addr, uint32_t val) {
    mem[addr] = val & 0xFF;
    mem[addr + 1] = (val >> 8) & 0xFF;
    mem[addr + 2] = (val >> 16) & 0xFF;
    mem[addr + 3] = (val >> 24) & 0xFF;
}
// Unused load/store helpers for 8‑ and 16‑bit accesses (retained for future use)
// static uint8_t load8(const uint8_t *mem, uint32_t addr) {
//     return mem[addr];
// }
// static void store8(uint8_t *mem, uint32_t addr, uint8_t val) {
//     mem[addr] = val;
// }
// static uint16_t load16(const uint8_t *mem, uint32_t addr) {
//     uint16_t v = mem[addr] | (mem[addr + 1] << 8);
//     return v;
// }
// static void store16(uint8_t *mem, uint32_t addr, uint16_t val) {
//     mem[addr] = val & 0xFF;
//     mem[addr + 1] = (val >> 8) & 0xFF;
// }

static std::string disasm(uint32_t instr) {
    uint32_t opcode = instr & 0x7f;
    uint32_t rd = (instr >> 7) & 0x1f;
    uint32_t funct3 = (instr >> 12) & 0x7;
    uint32_t rs1 = (instr >> 15) & 0x1f;
    uint32_t rs2 = (instr >> 20) & 0x1f;
    uint32_t funct7 = (instr >> 25) & 0x7f;
    uint32_t imm_i = sign_extend(instr >> 20, 12);
    uint32_t imm_u = instr & 0xFFFFF000;
    uint32_t imm_s = ((instr >> 7) & 0x1f) | ((instr >> 25) << 5);
    imm_s = sign_extend(imm_s, 12);
    uint32_t imm_b = ((instr >> 8) & 0xf) << 1;
    imm_b |= ((instr >> 25) & 0x3f) << 5;
    imm_b |= ((instr >> 7) & 0x1) << 11;
    imm_b |= ((instr >> 31) & 0x1) << 12;
    imm_b = sign_extend(imm_b, 13);
    uint32_t imm_j = ((instr >> 21) & 0x3ff) << 1;
    imm_j |= ((instr >> 20) & 0x1) << 11;
    imm_j |= ((instr >> 12) & 0xff) << 12;
    imm_j |= ((instr >> 31) & 0x1) << 20;
    imm_j = sign_extend(imm_j, 21);
    std::ostringstream oss;
    switch (opcode) {
        case 0x13: // OP-IMM
            switch (funct3) {
                case 0x0: oss << "addi x" << rd << ", x" << rs1 << ", " << (int32_t)imm_i; break;
                case 0x2: oss << "slti x" << rd << ", x" << rs1 << ", " << (int32_t)imm_i; break;
                case 0x3: oss << "sltiu x" << rd << ", x" << rs1 << ", " << imm_i; break;
                case 0x4: oss << "xori x" << rd << ", x" << rs1 << ", " << imm_i; break;
                case 0x6: oss << "ori x" << rd << ", x" << rs1 << ", " << imm_i; break;
                case 0x7: oss << "andi x" << rd << ", x" << rs1 << ", " << imm_i; break;
                case 0x1: oss << "slli x" << rd << ", x" << rs1 << ", " << (imm_i & 0x1f); break;
                case 0x5: {
                    uint32_t shamt = imm_i & 0x1f;
                    uint32_t funct7_imm = (instr >> 25) & 0x7f;
                    if (funct7_imm == 0x00) oss << "srli x" << rd << ", x" << rs1 << ", " << shamt;
                    else if (funct7_imm == 0x20) oss << "srai x" << rd << ", x" << rs1 << ", " << shamt;
                    break; }
                default: oss << "unknown_imm"; break;
            }
            break;
        case 0x2F: // A Extension - Atomic Operations
            switch (funct3) {
                case 0x2: // 32-bit atomic operations
                    switch (funct7 >> 2) { // bits 31-27
                        case 0x02: oss << "lr.w x" << rd << ", (x" << rs1 << ")"; break; // LR.W
                        case 0x03: oss << "sc.w x" << rd << ", x" << rs2 << ", (x" << rs1 << ")"; break; // SC.W
                        case 0x00: oss << "amoadd.w x" << rd << ", x" << rs2 << ", (x" << rs1 << ")"; break; // AMOADD.W
                        case 0x01: oss << "amoswap.w x" << rd << ", x" << rs2 << ", (x" << rs1 << ")"; break; // AMOSWAP.W
                        case 0x04: oss << "amoand.w x" << rd << ", x" << rs2 << ", (x" << rs1 << ")"; break; // AMOAND.W
                        case 0x08: oss << "amoor.w x" << rd << ", x" << rs2 << ", (x" << rs1 << ")"; break; // AMOOR.W
                        case 0x0C: oss << "amoxor.w x" << rd << ", x" << rs2 << ", (x" << rs1 << ")"; break; // AMOXOR.W
                        case 0x10: oss << "amomin.w x" << rd << ", x" << rs2 << ", (x" << rs1 << ")"; break; // AMOMIN.W
                        case 0x14: oss << "amomax.w x" << rd << ", x" << rs2 << ", (x" << rs1 << ")"; break; // AMOMAX.W
                        case 0x18: oss << "amominu.w x" << rd << ", x" << rs2 << ", (x" << rs1 << ")"; break; // AMOMINU.W
                        case 0x1C: oss << "amomaxu.w x" << rd << ", x" << rs2 << ", (x" << rs1 << ")"; break; // AMOMAXU.W
                        default: oss << "unknown_amo"; break;
                    }
                    break;
                default: oss << "unknown_amo"; break;
            }
            break;
        case 0x33: // OP
            switch (funct3) {
                case 0x0:
                    if ((instr >> 25) == 0x00) oss << "add x" << rd << ", x" << rs1 << ", x" << rs2;
                    else if ((instr >> 25) == 0x20) oss << "sub x" << rd << ", x" << rs1 << ", x" << rs2;
                    else if ((instr >> 25) == 0x01) oss << "mul x" << rd << ", x" << rs1 << ", x" << rs2; // MUL (M Extension)
                    else oss << "unknown_op";
                    break;
                case 0x1: {
                    uint32_t funct7 = (instr >> 25) & 0x7f;
                    if (funct7 == 0x01 && rs2 == 0x01) oss << "mulh x" << rd << ", x" << rs1 << ", x" << rs2;
                    else if (funct7 == 0x01 && rs2 == 0x02) oss << "mulhsu x" << rd << ", x" << rs1 << ", x" << rs2;
                    else if (funct7 == 0x01 && rs2 == 0x03) oss << "mulhu x" << rd << ", x" << rs1 << ", x" << rs2;
                    else oss << "sll x" << rd << ", x" << rs1 << ", x" << rs2;
                    break;
                }
                case 0x2: oss << "slt x" << rd << ", x" << rs1 << ", x" << rs2; break;
                case 0x3: oss << "sltu x" << rd << ", x" << rs1 << ", x" << rs2; break;
                case 0x4: oss << "xor x" << rd << ", x" << rs1 << ", x" << rs2; break;
                case 0x5: {
                    uint32_t funct7 = (instr >> 25) & 0x7f;
                    if (funct7 == 0x00) oss << "srl x" << rd << ", x" << rs1 << ", x" << rs2;
                    else if (funct7 == 0x20) oss << "sra x" << rd << ", x" << rs1 << ", x" << rs2;
                    else if (funct7 == 0x01) oss << "div x" << rd << ", x" << rs1 << ", x" << rs2; // DIV (M Extension)
                    else if (funct7 == 0x05) oss << "divu x" << rd << ", x" << rs1 << ", x" << rs2; // DIVU (M Extension)
                    else if (funct7 == 0x21) oss << "rem x" << rd << ", x" << rs1 << ", x" << rs2; // REM (M Extension)
                    else if (funct7 == 0x25) oss << "remu x" << rd << ", x" << rs1 << ", x" << rs2; // REMU (M Extension)
                    else oss << "unknown_op";
                    break;
                }
                case 0x6: oss << "or x" << rd << ", x" << rs1 << ", x" << rs2; break;
                case 0x7: oss << "and x" << rd << ", x" << rs1 << ", x" << rs2; break;
                default: oss << "unknown_op"; break;
            }
            break;
        case 0x37: oss << "lui x" << rd << ", 0x" << std::hex << (imm_u >> 12) << std::dec; break;
        case 0x17: oss << "auipc x" << rd << ", 0x" << std::hex << (imm_u >> 12) << std::dec; break;
        case 0x6F: oss << "jal x" << rd << ", " << (int32_t)imm_j; break;
        case 0x67: // JALR
            if (funct3 == 0x0) oss << "jalr x" << rd << ", " << (int32_t)imm_i << "(x" << rs1 << ")";
            else oss << "unknown_jalr";
            break;
        case 0x63: // branch
            switch (funct3) {
                case 0x0: oss << "beq x" << rs1 << ", x" << rs2 << ", " << (int32_t)imm_b; break;
                case 0x1: oss << "bne x" << rs1 << ", x" << rs2 << ", " << (int32_t)imm_b; break;
                case 0x4: oss << "blt x" << rs1 << ", x" << rs2 << ", " << (int32_t)imm_b; break;
                case 0x5: oss << "bge x" << rs1 << ", x" << rs2 << ", " << (int32_t)imm_b; break;
                case 0x6: oss << "bltu x" << rs1 << ", x" << rs2 << ", " << (int32_t)imm_b; break;
                case 0x7: oss << "bgeu x" << rs1 << ", x" << rs2 << ", " << (int32_t)imm_b; break;
                default: oss << "unknown_branch"; break;
            }
            break;
        case 0x03: // load
            switch (funct3) {
                case 0x2: oss << "lw x" << rd << ", " << (int32_t)imm_i << "(x" << rs1 << ")"; break;
                case 0x0: oss << "lb x" << rd << ", " << (int32_t)imm_i << "(x" << rs1 << ")"; break;
                case 0x1: oss << "lh x" << rd << ", " << (int32_t)imm_i << "(x" << rs1 << ")"; break;
                case 0x4: oss << "lbu x" << rd << ", " << (int32_t)imm_i << "(x" << rs1 << ")"; break;
                case 0x5: oss << "lhu x" << rd << ", " << (int32_t)imm_i << "(x" << rs1 << ")"; break;
                default: oss << "unknown_load"; break;
            }
            break;
        case 0x23: // store
            switch (funct3) {
                case 0x0: oss << "sb x" << rs2 << ", " << (int32_t)imm_s << "(x" << rs1 << ")"; break;
                case 0x1: oss << "sh x" << rs2 << ", " << (int32_t)imm_s << "(x" << rs1 << ")"; break;
                case 0x2: oss << "sw x" << rs2 << ", " << (int32_t)imm_s << "(x" << rs1 << ")"; break;
                default: oss << "unknown_store"; break;
            }
            break;
        case 0x73: // CSR instructions
            {
                rd = (instr >> 7) & 0x1f;
                funct3 = (instr >> 12) & 0x7;
                uint32_t csr_addr = (instr >> 20) & 0xFFF;
                rs1 = (instr >> 15) & 0x1f;
                uint32_t imm = (instr >> 20) & 0x1F;

                switch (funct3) {
                    case 0x0: // System instructions including WFI
                        {
                            uint32_t funct7 = (instr >> 25) & 0x7F;
                            if (funct7 == 0x105 && csr_addr == 0x105) {
                                oss << "wfi x" << rd;
                            } else {
                                oss << "unknown_system";
                            }
                        }
                        break;
                    case 0x1: oss << "csrrw x" << rd << ", 0x" << std::hex << csr_addr << ", x" << rs1 << std::dec; break;
                    case 0x2: oss << "csrrs x" << rd << ", 0x" << std::hex << csr_addr << ", x" << rs1 << std::dec; break;
                    case 0x3: oss << "csrrc x" << rd << ", 0x" << std::hex << csr_addr << ", x" << rs1 << std::dec; break;
                    case 0x5: oss << "csrrwi x" << rd << ", 0x" << std::hex << csr_addr << ", " << imm << std::dec; break;
                    case 0x6: oss << "csrrsi x" << rd << ", 0x" << std::hex << csr_addr << ", " << imm << std::dec; break;
                    case 0x7: oss << "csrrci x" << rd << ", 0x" << std::hex << csr_addr << ", " << imm << std::dec; break;
                    default: oss << "unknown_csr"; break;
                }
            }
            break;
        default:
            oss << "unknown";
    }
    return oss.str();
}

void Core::step() {
    // Check for debug breakpoint before instruction execution
    if (check_debug_breakpoint()) {
        if (simulator && simulator->get_debugger()) {
            simulator->get_debugger()->set_focus_core(core_id);
            simulator->enter_debug_loop();
            // Clear step mode after returning from debug loop
            if (simulator->get_debugger()) {
                simulator->get_debugger()->clear_step_mode();
            }
        }
    }

    // Check for pending interrupts before instruction execution
    handle_interrupt();

    // Check MultiCoreMonitor state - only execute if allowed
    if (simulator) {
        auto multi_core_monitor = simulator->get_multi_core_monitor();
        if (multi_core_monitor && !multi_core_monitor->should_core_execute(core_id)) {
            // Core should not execute (HALT, IDLE, or ERROR state)
            CORE_ID_LOGF(core_id, "Core step: should_core_execute=false, returning early");
            return;
        }
        CORE_ID_LOGF(core_id, "Core step: should_core_execute=true, proceeding with execution");
    }

    // Check if this core has been released to execute (legacy mechanism)
    if (!has_booted) {
        // Only core 0 can execute immediately
        if (core_id != 0) {
            // Other cores wait for boot permission
            if (simulator) {
                simulator->wait_for_boot(core_id);
            }
        }

        // Mark this core as booted
        has_booted = true;

        // Signal that this core has booted
        if (simulator) {
            simulator->signal_core_booted(core_id);
        }
    }

    // Check WFI (Wait for Interrupt) state
    if (is_waiting_for_interrupt) {
        // Check if there are pending interrupts
        if (check_pending_interrupts()) {
            // Exit WFI state when interrupt is pending
            is_waiting_for_interrupt = false;
            CORE_ID_LOGF(core_id, "Exiting WFI state due to pending interrupt");

            // Handle the interrupt that woke us up
            handle_interrupt();
        } else {
            // Still waiting, return without executing instruction
            CORE_ID_LOGF(core_id, "Still in WFI state, no pending interrupt");
            return;
        }
    }

#if DEBUG == 1
    static uint32_t last_debug_pc = 0xFFFFFFFF;
    if (pc != last_debug_pc) {
        printf("[Core#%u] Step: PC=0x%08x, State=%s\n",
               core_id, pc, "EXECUTING");
        last_debug_pc = pc;
    }
#endif

    // Fetch instruction through instruction cache if available
    uint32_t instr;
#if DEBUG
    // Instruction fetching is very verbose, only show in VERBOSE mode
    if (DebugLogger::getInstance().getDebugLevel() >= DebugLogger::VERBOSE) {
        CORE_ID_LOGF(core_id, "Fetching instruction at PC=0x%08x", pc);
    }
#endif
    if (instruction_cache) {
#if DEBUG
        if (DebugLogger::getInstance().getDebugLevel() >= DebugLogger::VERBOSE) {
            CORE_LOG("Using instruction cache");
        }
#endif
        instruction_cache->read(pc, instr, true, core_id);  // true indicates instruction fetch, pass core_id
    } else {
#if DEBUG
        CORE_LOG("Using direct memory access");
#endif
        // Use instruction cache if available (let cache handle MMU translation)
        if (instruction_cache) {
            instruction_cache->read(pc, instr, true, core_id);  // true indicates instruction fetch, pass core_id
        } else {
            // Direct memory access - cache should handle MMU translation
            instr = load32(memory, pc);
        }
    }

    // Handle function profiling using the new FunctionProfiler
    if (function_profiler) {
        function_profiler->track_instruction_execution(pc, core_id);
    }

    // Increment instruction count for profiling using PerformanceProfiler
    if (performance_profiler) {
        performance_profiler->increment_instruction_count(core_id);
    }
#if DEBUG
    // Instruction decode logging is very verbose, only show in VERBOSE mode
    if (DebugLogger::getInstance().getDebugLevel() >= DebugLogger::VERBOSE) {
        CORE_ID_LOGF(core_id, "PC=0x%08x instr=0x%08x %s", pc, instr, disasm(instr).c_str());
    }
#endif
    // Trace logging: PC, raw instruction, disassembly, core ID, registers in hex
    if (simulator && simulator->get_trace()) {
        std::cout << "[TRACE] Core#" << core_id << " PC=0x" << std::hex << pc << " instr=0x" << instr << std::dec << " " << disasm(instr) << "\n";
        std::cout << "[TRACE] regs:";
        for (int i = 0; i < 32; ++i) {
            std::cout << " x" << i << "=0x" << std::hex << regs[i] << std::dec;
        }
        std::cout << "\n";
    }
    uint32_t opcode = instr & 0x7f;
    uint32_t rd, rs1, rs2, funct3, funct7, imm;
    bool pc_updated = false;

    switch (opcode) {
        case 0x13: // OP‑IMM (immediate arithmetic/logical)
            rd = (instr >> 7) & 0x1f;
            funct3 = (instr >> 12) & 0x7;
            rs1 = (instr >> 15) & 0x1f;
            imm = sign_extend(instr >> 20, 12);
            switch (funct3) {
                case 0x0: // ADDI
                    regs[rd] = regs[rs1] + imm;
                    break;
                case 0x2: // SLTI
                    regs[rd] = ((int32_t)regs[rs1] < (int32_t)imm) ? 1 : 0;
                    break;
                case 0x3: // SLTIU
                    regs[rd] = (regs[rs1] < (uint32_t)imm) ? 1 : 0;
                    break;
                case 0x4: // XORI
                    regs[rd] = regs[rs1] ^ imm;
                    break;
                case 0x6: // ORI
                    regs[rd] = regs[rs1] | imm;
                    break;
                case 0x7: // ANDI
                    regs[rd] = regs[rs1] & imm;
                    break;
                case 0x1: // SLLI
                    regs[rd] = regs[rs1] << (imm & 0x1f);
                    break;
                case 0x5: {
                    uint32_t shamt = imm & 0x1f;
                    uint32_t funct7_imm = (instr >> 25) & 0x7f;
                    if (funct7_imm == 0x00) { // SRLI
                        regs[rd] = regs[rs1] >> shamt;
                    } else if (funct7_imm == 0x20) { // SRAI
                        regs[rd] = ((int32_t)regs[rs1]) >> shamt;
                    }
                    break;
                }
                default:
                    // unsupported immediate op
                    break;
            }
            pc += 4;
            pc_updated = true;
            break;
        case 0x33: // OP (R-type arithmetic/logic)
            rd = (instr >> 7) & 0x1f;
            funct3 = (instr >> 12) & 0x7;
            rs1 = (instr >> 15) & 0x1f;
            rs2 = (instr >> 20) & 0x1f;
            funct7 = (instr >> 25) & 0x7f;

    
            switch (funct3) {
                case 0x0:
                    if (funct7 == 0x00) {
                        regs[rd] = regs[rs1] + regs[rs2]; // ADD
#if DEBUG
                        CORE_ID_LOGF(core_id, "ADD PC=0x%08x x%d=x%d+x%d=0x%08x", pc, rd, rs1, rs2, regs[rd]);
#endif
                    }
                    else if (funct7 == 0x20) {
                        regs[rd] = regs[rs1] - regs[rs2]; // SUB
#if DEBUG
                        CORE_ID_LOGF(core_id, "SUB PC=0x%08x x%d=x%d-x%d=0x%08x", pc, rd, rs1, rs2, regs[rd]);
#endif
                    }
                    else if (funct7 == 0x01) {
                        regs[rd] = regs[rs1] * regs[rs2]; // MUL (M Extension)
#if DEBUG
                        CORE_ID_LOGF(core_id, "MUL PC=0x%08x x%d=x%d*x%d=0x%08x", pc, rd, rs1, rs2, regs[rd]);
#endif
                    }
                    break;
                case 0x1:
                    if (funct7 == 0x01) { // MULH, MULHSU, MULHU (M Extension)
                        if (rs2 == 0x01) { // MULH
                            int64_t res = (int64_t)(int32_t)regs[rs1] * (int64_t)(int32_t)regs[rs2];
                            regs[rd] = (uint32_t)(res >> 32);
                        } else if (rs2 == 0x02) { // MULHSU
                            int64_t res = (int64_t)(int32_t)regs[rs1] * (int64_t)(uint32_t)regs[rs2];
                            regs[rd] = (uint32_t)(res >> 32);
                        } else if (rs2 == 0x03) { // MULHU
                            uint64_t res = (uint64_t)(uint32_t)regs[rs1] * (uint64_t)(uint32_t)regs[rs2];
                            regs[rd] = (uint32_t)(res >> 32);
                        } else {
                            regs[rd] = regs[rs1] << (regs[rs2] & 0x1f); // SLL (default)
                        }
                    } else {
                        regs[rd] = regs[rs1] << (regs[rs2] & 0x1f); // SLL (default)
                    }
                    break;
                case 0x2:
                    regs[rd] = ((int32_t)regs[rs1] < (int32_t)regs[rs2]) ? 1 : 0; // SLT
                    break;
                case 0x3:
                    regs[rd] = (regs[rs1] < regs[rs2]) ? 1 : 0; // SLTU
                    break;
                case 0x4:
                    if (funct7 == 0x01) { // DIV (M Extension) - toolchain uses non-standard encoding
                        uint32_t old_rs1 = regs[rs1];
                        uint32_t old_rs2 = regs[rs2];

                        // Check boundary conditions
                        if (old_rs2 == 0) {
                            // Division by zero: return -1
                            regs[rd] = 0xFFFFFFFF;
                        } else if (old_rs1 == 0x80000000u && old_rs2 == 0xFFFFFFFFu) {
                            // Overflow case: 0x80000000 / -1, return dividend
                            regs[rd] = old_rs1;
                        } else {
                            // Normal division
                            regs[rd] = (int32_t)old_rs1 / (int32_t)old_rs2;
                        }
#if DEBUG
                        CORE_ID_LOGF(core_id, "DIV PC=0x%08x x%d=x%d/x%d=0x%08x", pc, rd, rs1, rs2, regs[rd]);
#endif
                    } else if (funct7 == 0x00) {
                        regs[rd] = regs[rs1] ^ regs[rs2]; // XOR
#if DEBUG
                        CORE_ID_LOGF(core_id, "XOR PC=0x%08x x%d=x%d^x%d=0x%08x", pc, rd, rs1, rs2, regs[rd]);
#endif
                    }
                    break;
                case 0x5:
                    if (funct7 == 0x01) { // DIVU (M Extension) - toolchain uses non-standard encoding
                        if (regs[rs2] == 0) {
                            // Division by zero: return all 1s
                            regs[rd] = 0xFFFFFFFF;
                        } else {
                            regs[rd] = (uint32_t)regs[rs1] / (uint32_t)regs[rs2];
                        }
#if DEBUG
                        CORE_ID_LOGF(core_id, "DIVU PC=0x%08x x%d=x%d/x%d=0x%08x", pc, rd, rs1, rs2, regs[rd]);
#endif
                    } else if (funct7 == 0x21) { // REM (M Extension)
                        if (regs[rs2] == 0) {
                            // Division by zero: return dividend
                            regs[rd] = (uint32_t)regs[rs1];
                        } else {
                            regs[rd] = (int32_t)regs[rs1] % (int32_t)regs[rs2];
                        }
#if DEBUG
                        CORE_ID_LOGF(core_id, "REM PC=0x%08x x%d=x%d%%%d=0x%08x", pc, rd, rs1, rs2, regs[rd]);
#endif
                    } else if (funct7 == 0x25) { // REMU (M Extension)
                        if (regs[rs2] == 0) {
                            // Division by zero: return dividend
                            regs[rd] = (uint32_t)regs[rs1];
                        } else {
                            regs[rd] = (uint32_t)regs[rs1] % (uint32_t)regs[rs2];
                        }
#if DEBUG
                        CORE_ID_LOGF(core_id, "REMU PC=0x%08x x%d=x%d%%%d=0x%08x", pc, rd, rs1, rs2, regs[rd]);
#endif
                    } else if (funct7 == 0x00) {
                        regs[rd] = regs[rs1] >> (regs[rs2] & 0x1f); // SRL
#if DEBUG
                        CORE_ID_LOGF(core_id, "SRL PC=0x%08x x%d=x%d>>%d=0x%08x", pc, rd, rs1, regs[rs2] & 0x1f, regs[rd]);
#endif
                    } else if (funct7 == 0x20) {
                        regs[rd] = ((int32_t)regs[rs1]) >> (regs[rs2] & 0x1f); // SRA
#if DEBUG
                        CORE_ID_LOGF(core_id, "SRA PC=0x%08x x%d=x%d>>%d=0x%08x", pc, rd, rs1, regs[rs2] & 0x1f, regs[rd]);
#endif
                    }
                    break;
                case 0x6:
                    if (funct7 == 0x01) { // REM (M Extension) - toolchain uses non-standard encoding
                        if (regs[rs2] == 0) {
                            // Division by zero: return dividend
                            regs[rd] = (uint32_t)regs[rs1];
                        } else {
                            regs[rd] = (int32_t)regs[rs1] % (int32_t)regs[rs2];
                        }
#if DEBUG
                        CORE_ID_LOGF(core_id, "REM PC=0x%08x x%d=x%d%%%d=0x%08x", pc, rd, rs1, rs2, regs[rd]);
#endif
                    } else if (funct7 == 0x00) {
                        regs[rd] = regs[rs1] | regs[rs2]; // OR
#if DEBUG
                        CORE_ID_LOGF(core_id, "OR PC=0x%08x x%d=x%d|x%d=0x%08x", pc, rd, rs1, rs2, regs[rd]);
#endif
                    }
                    break;
                case 0x7:
                    if (funct7 == 0x01) { // REMU (M Extension) - toolchain uses non-standard encoding
                        if (regs[rs2] == 0) {
                            // Division by zero: return dividend
                            regs[rd] = (uint32_t)regs[rs1];
                        } else {
                            regs[rd] = (uint32_t)regs[rs1] % (uint32_t)regs[rs2];
                        }
#if DEBUG
                        CORE_ID_LOGF(core_id, "REMU PC=0x%08x x%d=x%d%%%d=0x%08x", pc, rd, rs1, rs2, regs[rd]);
#endif
                    } else if (funct7 == 0x00) {
                        regs[rd] = regs[rs1] & regs[rs2]; // AND
#if DEBUG
                        CORE_ID_LOGF(core_id, "AND PC=0x%08x x%d=x%d&x%d=0x%08x", pc, rd, rs1, rs2, regs[rd]);
#endif
                    }
                    break;
                default:
                    break;
            }
            pc += 4;
            pc_updated = true;
            break;
        case 0x37: // LUI
            rd = (instr >> 7) & 0x1f;
            imm = instr & 0xFFFFF000; // upper 20 bits shifted
            regs[rd] = imm;
            pc += 4;
            pc_updated = true;
            break;
        case 0x17: // AUIPC
            rd = (instr >> 7) & 0x1f;
            imm = instr & 0xFFFFF000;
            regs[rd] = pc + imm;
            pc += 4;
            pc_updated = true;
            break;
        case 0x03: // LOAD (e.g., LW, LB, LBU, LH, LHU)
            rd = (instr >> 7) & 0x1f;
            funct3 = (instr >> 12) & 0x7;
            rs1 = (instr >> 15) & 0x1f;
            imm = sign_extend(instr >> 20, 12);
            {
                uint32_t addr = regs[rs1] + imm;
#if DEBUG
                CORE_ID_LOGF(core_id, "LOAD PC=0x%08x addr=0x%08x", pc, addr);
#endif
                // Let cache/MMU handle address translation
                if (static_cast<size_t>(addr) < simulator->get_memory_size()) {
                    switch (funct3) {
                        case 0x2: // LW
                            // Use PMA controller to determine cache behavior
                            if (data_cache && PMAController::getInstance().hasDataCache(addr)) {
                                data_cache->read(addr, regs[rd], false, core_id);  // Pass virtual address, let cache handle MMU
                            } else {
                                // For non-cacheable regions, we need to translate virtual address to physical address first
                                uint32_t phys_addr = addr;
                                if (mmu) {
                                    ExceptionType ex = mmu->translate_address(addr, phys_addr, false, false, core_id);
                                    if (ex != ExceptionType::NONE) {
                                        // Handle page fault
                                        if (simulator && simulator->get_trace()) {
                                            std::cout << "[TRACE] Core#" << core_id << " EXCEPTION: LOAD_ACCESS_FAULT at PC=0x" << std::hex << pc << " addr=0x" << addr << " instr=0x" << instr << std::dec << "\n";
                                        }
                                        pc = handle_exception(ex, pc);
                                        return;
                                    }
                                }
                                regs[rd] = simulator->read_word(phys_addr);

#if DEBUG
                                // Debug device memory reads
                                if (PMAController::getInstance().isDeviceMemory(addr)) {
                                    CORE_ID_LOGF(core_id, "PMA: Direct device memory read from 0x%08x (phys=0x%08x) = 0x%08x (through MemoryInterface)",
                                                   addr, phys_addr, regs[rd]);
                                }
#endif
                            }

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, false, instruction_count);
                                }
                            }
                            break;
                        case 0x0: { // LB (signed)
                            // Use PMA controller to determine cache behavior
                            if (data_cache && PMAController::getInstance().hasDataCache(addr)) {
                                uint32_t temp;
                                data_cache->read(addr & ~0x3, temp, false, core_id);  // Pass virtual address and core_id
                                int8_t val = static_cast<int8_t>((temp >> ((addr & 0x3) * 8)) & 0xFF);
                                regs[rd] = static_cast<int32_t>(val);
                            } else {
                                // For non-cacheable regions, we need to translate virtual address to physical address first
                                uint32_t phys_addr = addr;
                                if (mmu) {
                                    ExceptionType ex = mmu->translate_address(addr, phys_addr, false, false, core_id);
                                    if (ex != ExceptionType::NONE) {
                                        // Handle page fault
                                        if (simulator && simulator->get_trace()) {
                                            std::cout << "[TRACE] Core#" << core_id << " EXCEPTION: LOAD_ACCESS_FAULT at PC=0x" << std::hex << pc << " addr=0x" << addr << " instr=0x" << instr << std::dec << "\n";
                                        }
                                        pc = handle_exception(ex, pc);
                                        return;
                                    }
                                }
                                uint8_t val = simulator->read_byte(phys_addr);
                                regs[rd] = static_cast<int32_t>(static_cast<int8_t>(val));
                            }

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, false, instruction_count);
                                }
                            }
                            break; }
                        case 0x4: { // LBU (unsigned)
                            // Use PMA controller to determine cache behavior
                            if (data_cache && PMAController::getInstance().hasDataCache(addr)) {
                                uint32_t temp;
                                data_cache->read(addr & ~0x3, temp, false, core_id);  // Pass virtual address and core_id
                                uint8_t val = static_cast<uint8_t>((temp >> ((addr & 0x3) * 8)) & 0xFF);
                                regs[rd] = static_cast<uint32_t>(val);
                            } else {
                                // For non-cacheable regions, we need to translate virtual address to physical address first
                                uint32_t phys_addr = addr;
                                if (mmu) {
                                    ExceptionType ex = mmu->translate_address(addr, phys_addr, false, false, core_id);
                                    if (ex != ExceptionType::NONE) {
                                        // Handle page fault
                                        if (simulator && simulator->get_trace()) {
                                            std::cout << "[TRACE] Core#" << core_id << " EXCEPTION: LOAD_ACCESS_FAULT at PC=0x" << std::hex << pc << " addr=0x" << addr << " instr=0x" << instr << std::dec << "\n";
                                        }
                                        pc = handle_exception(ex, pc);
                                        return;
                                    }
                                }
                                uint8_t val = simulator->read_byte(phys_addr);
#if DEBUG
                                CORE_ID_LOGF(core_id, "LBU addr=0x%08x (phys=0x%08x) val=0x%02x", addr, phys_addr, static_cast<int>(val));
#endif
                                regs[rd] = static_cast<uint32_t>(val);
                            }

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, false, instruction_count);
                                }
                            }
                            break; }
                        case 0x1: { // LH (signed)
                            // Use PMA controller to determine cache behavior
                            if (data_cache && PMAController::getInstance().hasDataCache(addr)) {
                                // For halfword access, we need to handle alignment
                                uint32_t aligned_addr = addr & ~0x3;
                                uint32_t temp;
                                data_cache->read(aligned_addr, temp, false, core_id);  // Pass virtual address and core_id
                                if ((addr & 0x3) == 0) {
                                    // Lower halfword
                                    int16_t val = static_cast<int16_t>(temp & 0xFFFF);
                                    regs[rd] = static_cast<int32_t>(val);
                                } else {
                                    // Upper halfword
                                    int16_t val = static_cast<int16_t>((temp >> 16) & 0xFFFF);
                                    regs[rd] = static_cast<int32_t>(val);
                                }
                            } else {
                                // For non-cacheable regions, we need to translate virtual address to physical address first
                                uint32_t phys_addr = addr;
                                if (mmu) {
                                    ExceptionType ex = mmu->translate_address(addr, phys_addr, false, false, core_id);
                                    if (ex != ExceptionType::NONE) {
                                        // Handle page fault
                                        if (simulator && simulator->get_trace()) {
                                            std::cout << "[TRACE] Core#" << core_id << " EXCEPTION: LOAD_ACCESS_FAULT at PC=0x" << std::hex << pc << " addr=0x" << addr << " instr=0x" << instr << std::dec << "\n";
                                        }
                                        pc = handle_exception(ex, pc);
                                        return;
                                    }
                                }
                                uint16_t val = simulator->read_halfword(phys_addr);
                                regs[rd] = static_cast<int32_t>(static_cast<int16_t>(val));
                            }

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, false, instruction_count);
                                }
                            }
                            break; }
                        case 0x5: { // LHU (unsigned)
                            // Use PMA controller to determine cache behavior
                            if (data_cache && PMAController::getInstance().hasDataCache(addr)) {
                                // For halfword access, we need to handle alignment
                                uint32_t aligned_addr = addr & ~0x3;
                                uint32_t temp;
                                data_cache->read(aligned_addr, temp, false, core_id);  // Pass virtual address and core_id
                                if ((addr & 0x3) == 0) {
                                    // Lower halfword
                                    uint16_t val = static_cast<uint16_t>(temp & 0xFFFF);
                                    regs[rd] = static_cast<uint32_t>(val);
                                } else {
                                    // Upper halfword
                                    uint16_t val = static_cast<uint16_t>((temp >> 16) & 0xFFFF);
                                    regs[rd] = static_cast<uint32_t>(val);
                                }
                            } else {
                                // For non-cacheable regions, we need to translate virtual address to physical address first
                                uint32_t phys_addr = addr;
                                if (mmu) {
                                    ExceptionType ex = mmu->translate_address(addr, phys_addr, false, false, core_id);
                                    if (ex != ExceptionType::NONE) {
                                        // Handle page fault
                                        if (simulator && simulator->get_trace()) {
                                            std::cout << "[TRACE] Core#" << core_id << " EXCEPTION: LOAD_ACCESS_FAULT at PC=0x" << std::hex << pc << " addr=0x" << addr << " instr=0x" << instr << std::dec << "\n";
                                        }
                                        pc = handle_exception(ex, pc);
                                        return;
                                    }
                                }
                                uint16_t val = simulator->read_halfword(phys_addr);
                                regs[rd] = static_cast<uint32_t>(val);
                            }

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, false, instruction_count);
                                }
                            }
                            break; }
                        default:
                            regs[rd] = 0;
                    }
                } else {
                    // Check for special core ID register
                    if (addr == 0x10000010) {
                        // Return core ID for LW instruction
                        if (funct3 == 0x2) {
                            regs[rd] = core_id;
                        } else {
                            regs[rd] = 0;
                        }
                    } else if (addr >= 0x10000000 && addr < 0x10000020) {
                        // Allow access to UART MMIO region (0x10000000-0x1000001F)
                        Device *dev = nullptr;
                        if (simulator) dev = simulator->get_device(addr & 0xFFFFFFF0);
                        regs[rd] = dev ? dev->read(addr & 0xF) : 0;
                    } else if ((addr >= 0x02000000 && addr < 0x02010000) || (addr >= 0x20000000 && addr < 0x20010000)) {
                        // Allow access to CLINT MMIO region (0x02000000-0x0200FFFF or 0x20000000-0x2000FFFF)
                        // The assembler may generate either address format
                        if (funct3 == 0x2) {  // LW
                            if (simulator) {
                                regs[rd] = simulator->read_word(addr);
#if DEBUG
                                CORE_ID_LOGF(core_id, "CLINT MMIO read from 0x%08x = 0x%08x", addr, regs[rd]);
#endif
                            } else {
                                regs[rd] = 0;
                            }
                        } else {
                            regs[rd] = 0;
                        }
                    } else {
                        // Invalid memory access - trigger load access fault exception
                        // Log the exception for trace output before handling
                        if (simulator && simulator->get_trace()) {
                            std::cout << "[TRACE] Core#" << core_id << " EXCEPTION: LOAD_ACCESS_FAULT at PC=0x" << std::hex << pc << " addr=0x" << addr << " instr=0x" << instr << std::dec << "\n";
                        }
                        pc = handle_exception(ExceptionType::LOAD_ACCESS_FAULT, pc);
                        return;
                    }
                }
            }
            pc += 4;
            pc_updated = true;
            break;
        case 0x23: // STORE (e.g., SW, SB, SH)
            imm = ((instr >> 7) & 0x1f) | ((instr >> 25) << 5);
            imm = sign_extend(imm, 12);
            funct3 = (instr >> 12) & 0x7;
            rs1 = (instr >> 15) & 0x1f;
            rs2 = (instr >> 20) & 0x1f;
            {
                uint32_t addr = regs[rs1] + imm;
#if DEBUG
                CORE_ID_LOGF(core_id, "STORE PC=0x%08x addr=0x%08x val=0x%08x", pc, addr, regs[rs2]);
                // Special debug for string pointer
                if (addr == 0xffffc && funct3 == 0x2) {
                    CORE_ID_LOGF(core_id, "Storing string pointer: 0x%08x", regs[rs2]);
                }
                // Special debug for UART access
                if (addr == 0x10000000 && funct3 == 0x2) {
                    CORE_ID_LOGF(core_id, "UART STORE: 0x%08x", regs[rs2]);
                }
#endif
                // Let cache/MMU handle address translation - remove Core-level translation

                if (static_cast<size_t>(addr) < simulator->get_memory_size()) {
#if DEBUG
#endif
                    switch (funct3) {
                        case 0x2: // SW
                            // Use PMA controller to determine cache behavior
                            if (data_cache && PMAController::getInstance().hasDataCache(addr)) {
                                data_cache->write(addr, regs[rs2], core_id);  // Pass virtual address and core_id, cache will translate
                            } else {
                                // For non-cacheable regions, we need to translate virtual address to physical address first
                                uint32_t phys_addr = addr;
                                if (mmu) {
                                    ExceptionType ex = mmu->translate_address(addr, phys_addr, true, false, core_id);
                                    if (ex != ExceptionType::NONE) {
                                        // Handle page fault
                                        if (simulator && simulator->get_trace()) {
                                            std::cout << "[TRACE] Core#" << core_id << " EXCEPTION: STORE_AMO_PAGE_FAULT at PC=0x" << std::hex << pc << " addr=0x" << addr << " instr=0x" << instr << std::dec << "\n";
                                        }
                                        pc = handle_exception(ex, pc);
                                        return;
                                    }
                                }
                                // Write to physical address
                                simulator->write_word(phys_addr, regs[rs2]);

#if DEBUG
                                // Debug device memory writes
                                if (PMAController::getInstance().isDeviceMemory(addr)) {
                                    CORE_ID_LOGF(core_id, "PMA: Direct device memory write to 0x%08x (phys=0x%08x) = 0x%08x (through MemoryInterface)",
                                                   addr, phys_addr, regs[rs2]);
                                }
#endif
                            }

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, true, instruction_count);
                                }
                            }
                            break;
                        case 0x0: { // SB (store byte)
                            // Use PMA controller to determine cache behavior
                            if (data_cache && PMAController::getInstance().hasDataCache(addr)) {
                                // Read the word, modify the byte, then write back
                                uint32_t aligned_addr = addr & ~0x3;
                                uint32_t temp;
                                data_cache->read(aligned_addr, temp, false, core_id);  // Pass virtual address and core_id
                                uint32_t byte_offset = addr & 0x3;
                                uint32_t byte_mask = 0xFF << (byte_offset * 8);
                                temp = (temp & ~byte_mask) | ((regs[rs2] & 0xFF) << (byte_offset * 8));
                                data_cache->write(aligned_addr, temp, core_id);  // Pass virtual address and core_id
                            } else {
                                // For non-cacheable regions, we need to translate virtual address to physical address first
                                uint32_t phys_addr = addr;
                                if (mmu) {
                                    ExceptionType ex = mmu->translate_address(addr, phys_addr, true, false, core_id);
                                    if (ex != ExceptionType::NONE) {
                                        // Handle page fault
                                        if (simulator && simulator->get_trace()) {
                                            std::cout << "[TRACE] Core#" << core_id << " EXCEPTION: STORE_AMO_PAGE_FAULT at PC=0x" << std::hex << pc << " addr=0x" << addr << " instr=0x" << instr << std::dec << "\n";
                                        }
                                        pc = handle_exception(ex, pc);
                                        return;
                                    }
                                }
                                uint8_t val = static_cast<uint8_t>(regs[rs2] & 0xFF);
                                simulator->write_byte(phys_addr, val);

#if DEBUG
                                // Debug device memory writes
                                if (PMAController::getInstance().isDeviceMemory(addr)) {
                                    CORE_ID_LOGF(core_id, "PMA: Direct device memory byte write to 0x%08x (phys=0x%08x) = 0x%02x (through MemoryInterface)",
                                                   addr, phys_addr, val);
                                }
#endif
                            }

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, true, instruction_count);
                                }
                            }
                            break; }
                        case 0x1: { // SH (store halfword)
                            // Use PMA controller to determine cache behavior
                            if (data_cache && PMAController::getInstance().hasDataCache(addr)) {
                                // Read the word, modify the halfword, then write back
                                uint32_t aligned_addr = addr & ~0x3;
                                uint32_t temp;
                                data_cache->read(aligned_addr, temp, false, core_id);  // Pass virtual address and core_id
                                if ((addr & 0x3) == 0) {
                                    // Lower halfword
                                    temp = (temp & 0xFFFF0000) | (regs[rs2] & 0xFFFF);
                                } else {
                                    // Upper halfword
                                    temp = (temp & 0x0000FFFF) | ((regs[rs2] & 0xFFFF) << 16);
                                }
                                data_cache->write(aligned_addr, temp, core_id);  // Pass virtual address and core_id
                            } else {
                                // For non-cacheable regions, we need to translate virtual address to physical address first
                                uint32_t phys_addr = addr;
                                if (mmu) {
                                    ExceptionType ex = mmu->translate_address(addr, phys_addr, true, false, core_id);
                                    if (ex != ExceptionType::NONE) {
                                        // Handle page fault
                                        if (simulator && simulator->get_trace()) {
                                            std::cout << "[TRACE] Core#" << core_id << " EXCEPTION: STORE_AMO_PAGE_FAULT at PC=0x" << std::hex << pc << " addr=0x" << addr << " instr=0x" << instr << std::dec << "\n";
                                        }
                                        pc = handle_exception(ex, pc);
                                        return;
                                    }
                                }
                                uint16_t val = static_cast<uint16_t>(regs[rs2] & 0xFFFF);
                                simulator->write_halfword(phys_addr, val);
                            }

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, true, instruction_count);
                                }
                            }
                            break; }
                        default:
                            break;
                    }
                } else {
                    if (addr >= 0x10000000 && addr < 0x10000020) {
                        // Allow access to UART MMIO region (0x10000000-0x1000001F)
#if DEBUG
                        CORE_ID_LOGF(core_id, "MMIO STORE access to address 0x%08x", addr);
#endif
                        Device *dev = nullptr;
                        if (simulator) dev = simulator->get_device(addr & 0xFFFFFFF0);
                        if (dev) {
#if DEBUG
                            CORE_ID_LOGF(core_id, "Found device for address 0x%08x", addr & 0xFFFFFFF0);
#endif
                            // For SW instruction, we still want to write to offset 0 of the device
                            // but we should extract the byte value from the word
                            if (funct3 == 0x2) { // SW
                                // Extract the least significant byte for UART
                                dev->write(addr & 0xF, regs[rs2] & 0xFF);
                            } else {
                                dev->write(addr & 0xF, regs[rs2]);
                            }
                        }
                    } else if ((addr >= 0x90000 && addr < 0x98000) || (addr >= 0x98000 && addr < 0x98004)) {
                        // Allow access to MultiCoreMonitor memory-mapped registers:
                        // - 0x90000-0x97FFF: Core state array (8192 cores * 4 bytes)
                        // - 0x98000-0x98003: Release signal register
#if DEBUG
                        if (addr >= 0x90000 && addr < 0x98000) {
                            uint32_t core_id_reporting = (addr - 0x90000) / 4;
                            CORE_ID_LOGF(core_id, "MultiCoreMonitor Core#%u state write: 0x%08x = 0x%08x",
                                       core_id_reporting, addr, regs[rs2]);
                        } else {
                            CORE_ID_LOGF(core_id, "MultiCoreMonitor release signal write: 0x%08x = 0x%08x", addr, regs[rs2]);
                        }
#endif
                        // Use simulator's write_word to ensure proper memory update and MultiCoreMonitor detection
                        if (simulator) {
                            simulator->write_word(addr, regs[rs2]);
                        }
                    } else if ((addr >= 0x02000000 && addr < 0x02010000) || (addr >= 0x20000000 && addr < 0x20010000)) {
                        // Allow access to CLINT MMIO region (0x02000000-0x0200FFFF or 0x20000000-0x2000FFFF)
                        // The assembler may generate either address format
                        if (funct3 == 0x2) {  // SW
                            if (simulator) {
                                simulator->write_word(addr, regs[rs2]);
                            }
                        } else {
                            // For byte/halfword stores, write through simulator
                            if (simulator) {
                                simulator->write_word(addr, regs[rs2]);
                            }
                        }
                    } else {
                        // Invalid memory access - trigger store access fault exception
                        // Log the exception for trace output before handling
                        if (simulator && simulator->get_trace()) {
                            std::cout << "[TRACE] Core#" << core_id << " EXCEPTION: STORE_AMO_ACCESS_FAULT at PC=0x" << std::hex << pc << " addr=0x" << addr << " instr=0x" << instr << std::dec << "\n";
                        }
                        pc = handle_exception(ExceptionType::STORE_AMO_ACCESS_FAULT, pc);
                        return;
                    }
                }
            }
            pc += 4;
            pc_updated = true;
            break;
        case 0x2F: // A Extension - Atomic Operations (AMO)
            rd = (instr >> 7) & 0x1f;
            rs1 = (instr >> 15) & 0x1f;
            rs2 = (instr >> 20) & 0x1f;
            funct3 = (instr >> 12) & 0x7;
            funct7 = (instr >> 25) & 0x7f;

            if (funct3 == 0x2) { // 32-bit operations
                uint32_t addr = regs[rs1];
                uint32_t phys_addr = addr;

                // Translate address if MMU is available
                if (mmu) {
                    ExceptionType ex = mmu->translate_address(addr, phys_addr, false, false, core_id);  // addr, paddr, false for read, false for not instruction, core_id
                    if (ex != ExceptionType::NONE) {
                        // Handle AMO page fault
                        pc = handle_exception(ex, pc);
                        return;  // Skip this instruction and retry with exception handling
                    }
                }

                if (static_cast<size_t>(phys_addr) < simulator->get_memory_size()) {
                    uint32_t current_val;

                    // Perform the atomic operation based on funct7 using cache atomic operations
                    switch (funct7 >> 2) { // bits 31-27
                        case 0x00: // AMOADD.W
                            if (data_cache) {
                                current_val = data_cache->atomic_fetch_and_add(addr, regs[rs2], core_id);
                            } else {
                                current_val = simulator->atomic_fetch_and_add(phys_addr, regs[rs2]);
                            }
                            regs[rd] = current_val;

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, true, instruction_count);
                                }
                            }
                            break;
                        case 0x01: // AMOSWAP.W
                            if (data_cache) {
                                current_val = data_cache->atomic_swap(addr, regs[rs2], core_id);
                            } else {
                                current_val = simulator->atomic_swap(phys_addr, regs[rs2]);
                            }
                            regs[rd] = current_val;

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, true, instruction_count);
                                }
                            }
                            break;
                        case 0x04: // AMOAND.W
                            if (data_cache) {
                                current_val = data_cache->atomic_fetch_and_and(addr, regs[rs2], core_id);
                            } else {
                                current_val = simulator->atomic_fetch_and_and(phys_addr, regs[rs2]);
                            }
                            regs[rd] = current_val;

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, true, instruction_count);
                                }
                            }
                            break;
                        case 0x08: // AMOOR.W
                            if (data_cache) {
                                current_val = data_cache->atomic_fetch_and_or(addr, regs[rs2], core_id);
                            } else {
                                current_val = simulator->atomic_fetch_and_or(phys_addr, regs[rs2]);
                            }
                            regs[rd] = current_val;

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, true, instruction_count);
                                }
                            }
                            break;
                        case 0x0C: // AMOXOR.W
                            if (data_cache) {
                                current_val = data_cache->atomic_fetch_and_xor(addr, regs[rs2], core_id);
                            } else {
                                current_val = simulator->atomic_fetch_and_xor(phys_addr, regs[rs2]);
                            }
                            regs[rd] = current_val;

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, true, instruction_count);
                                }
                            }
                            break;
                        case 0x10: // AMOMIN.W
                            // For MIN/MAX operations, we need to implement them manually
                            if (data_cache) {
                                uint32_t old_val = data_cache->read_word(addr);
                                uint32_t new_val = ((int32_t)old_val < (int32_t)regs[rs2]) ? old_val : regs[rs2];
                                data_cache->write_word(addr, new_val);
                                current_val = old_val;
                            } else {
                                uint32_t old_val = simulator->read_word(phys_addr);
                                uint32_t new_val = ((int32_t)old_val < (int32_t)regs[rs2]) ? old_val : regs[rs2];
                                simulator->write_word(phys_addr, new_val);
                                current_val = old_val;
                            }
                            regs[rd] = current_val;

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, true, instruction_count);
                                }
                            }
                            break;
                        case 0x14: // AMOMAX.W
                            // For MIN/MAX operations, we need to implement them manually
                            if (data_cache) {
                                uint32_t old_val = data_cache->read_word(addr);
                                uint32_t new_val = ((int32_t)old_val > (int32_t)regs[rs2]) ? old_val : regs[rs2];
                                data_cache->write_word(addr, new_val);
                                current_val = old_val;
                            } else {
                                uint32_t old_val = simulator->read_word(phys_addr);
                                uint32_t new_val = ((int32_t)old_val > (int32_t)regs[rs2]) ? old_val : regs[rs2];
                                simulator->write_word(phys_addr, new_val);
                                current_val = old_val;
                            }
                            regs[rd] = current_val;

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, true, instruction_count);
                                }
                            }
                            break;
                        case 0x18: // AMOMINU.W
                            // For MIN/MAX operations, we need to implement them manually
                            if (data_cache) {
                                uint32_t old_val = data_cache->read_word(addr);
                                uint32_t new_val = (old_val < regs[rs2]) ? old_val : regs[rs2];
                                data_cache->write_word(addr, new_val);
                                current_val = old_val;
                            } else {
                                uint32_t old_val = simulator->read_word(phys_addr);
                                uint32_t new_val = (old_val < regs[rs2]) ? old_val : regs[rs2];
                                simulator->write_word(phys_addr, new_val);
                                current_val = old_val;
                            }
                            regs[rd] = current_val;

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, true, instruction_count);
                                }
                            }
                            break;
                        case 0x1C: // AMOMAXU.W
                            // For MIN/MAX operations, we need to implement them manually
                            if (data_cache) {
                                uint32_t old_val = data_cache->read_word(addr);
                                uint32_t new_val = (old_val > regs[rs2]) ? old_val : regs[rs2];
                                data_cache->write_word(addr, new_val);
                                current_val = old_val;
                            } else {
                                uint32_t old_val = simulator->read_word(phys_addr);
                                uint32_t new_val = (old_val > regs[rs2]) ? old_val : regs[rs2];
                                simulator->write_word(phys_addr, new_val);
                                current_val = old_val;
                            }
                            regs[rd] = current_val;

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, true, instruction_count);
                                }
                            }
                            break;
                        case 0x02: { // LR.W (Load Reserved)
                            // For LR, just read the value
                            if (data_cache) {
                                current_val = data_cache->read_word(addr);
                            } else {
                                current_val = simulator->read_word(phys_addr);
                            }
                            regs[rd] = current_val;
                            // Set reservation (simplified - just track the address)
                            // In a real implementation, we'd track reservations per core
                            // For now, we'll just return the value and skip the write back

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, false, instruction_count);
                                }
                            }
                            break; // LR doesn't write back to memory
                        }
                        case 0x03: { // SC.W (Store Conditional)
                            // In a real implementation, we'd check if reservation is valid
                            // For now, we'll always succeed (return 0 for success)
                            if (data_cache) {
                                data_cache->write_word(addr, regs[rs2]);  // Pass virtual address
                            } else {
                                simulator->write_word(phys_addr, regs[rs2]);
                            }
                            regs[rd] = 0; // Success (0 means success)

                            // Record memory access for function profiling using new profiler
                            if (function_profiler && simulator) {
                                size_t func_idx = function_profiler->get_current_function_index(core_id, pc);
                                if (func_idx != static_cast<size_t>(-1)) {
                                    uint64_t instruction_count = performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
                                    function_profiler->record_memory_access(core_id, func_idx, addr, true, instruction_count);
                                }
                            }
                            break;
                        }
                        default:
                            // Unsupported atomic operation
                            pc += 4;
                            pc_updated = true;
                            break;
                    }
                } else {
                    // Handle MMIO or invalid address
                    Device *dev = nullptr;
                    if (simulator) dev = simulator->get_device(addr & 0xFFFFFFF0);
                    if (dev) {
                        // For now, we'll just return 0 for MMIO atomic operations
                        regs[rd] = 0;
                    } else {
                        // Invalid address - could trigger an exception in a real implementation
                        regs[rd] = 0;
                    }
                }
            }
            pc += 4;
            pc_updated = true;
            break;
        case 0x63: // BRANCH (e.g., BEQ)
            imm = ((instr >> 8) & 0xf) << 1;
            imm |= ((instr >> 25) & 0x3f) << 5;
            imm |= ((instr >> 7) & 0x1) << 11;
            imm |= ((instr >> 31) & 0x1) << 12;
            imm = sign_extend(imm, 13);
            funct3 = (instr >> 12) & 0x7;
            rs1 = (instr >> 15) & 0x1f;
            rs2 = (instr >> 20) & 0x1f;
            if (funct3 == 0x0) { // BEQ
                if (regs[rs1] == regs[rs2]) {
#if DEBUG
                    CORE_ID_LOGF(core_id, "BEQ PC=0x%08x x%d==x%d=0x%08x TAKEN to 0x%08x", pc, rs1, rs2, regs[rs1], pc + imm);
#endif
                    pc += imm;
                    pc_updated = true;
                } else {
#if DEBUG
                    CORE_ID_LOGF(core_id, "BEQ PC=0x%08x x%d(0x%08x)!=x%d(0x%08x) NOT TAKEN", pc, rs1, regs[rs1], rs2, regs[rs2]);
#endif
                }
            }
            if (funct3 == 0x1) { // BNE
                if (regs[rs1] != regs[rs2]) {
#if DEBUG
                    CORE_ID_LOGF(core_id, "BNE PC=0x%08x x%d!=x%d=0x%08x TAKEN to 0x%08x", pc, rs1, rs2, regs[rs1], pc + imm);
#endif
                    pc += imm;
                    pc_updated = true;
                } else {
#if DEBUG
                    CORE_ID_LOGF(core_id, "BNE PC=0x%08x x%d(0x%08x)==x%d(0x%08x) NOT TAKEN", pc, rs1, regs[rs1], rs2, regs[rs2]);
#endif
                }
            }
            if (funct3 == 0x4) { // BLT
                if ((int32_t)regs[rs1] < (int32_t)regs[rs2]) {
#if DEBUG
                    CORE_ID_LOGF(core_id, "BLT PC=0x%08x x%d(%d)<x%d(%d) TAKEN to 0x%08x", pc, rs1, (int32_t)regs[rs1], rs2, (int32_t)regs[rs2], pc + imm);
#endif
                    pc += imm;
                    pc_updated = true;
                } else {
#if DEBUG
                    CORE_ID_LOGF(core_id, "BLT PC=0x%08x x%d(%d)>=x%d(%d) NOT TAKEN", pc, rs1, (int32_t)regs[rs1], rs2, (int32_t)regs[rs2]);
#endif
                }
            }
            if (funct3 == 0x5) { // BGE
                if ((int32_t)regs[rs1] >= (int32_t)regs[rs2]) {
#if DEBUG
                    CORE_ID_LOGF(core_id, "BGE PC=0x%08x x%d(%d)>=x%d(%d) TAKEN to 0x%08x", pc, rs1, (int32_t)regs[rs1], rs2, (int32_t)regs[rs2], pc + imm);
#endif
                    pc += imm;
                    pc_updated = true;
                }
            }
            if (funct3 == 0x6) { // BLTU
                if (regs[rs1] < regs[rs2]) {
                    pc += imm;
                    pc_updated = true;
                }
            }
            if (funct3 == 0x7) { // BGEU
                if (regs[rs1] >= regs[rs2]) {
                    pc += imm;
                    pc_updated = true;
                }
            }
            if (!pc_updated) {
                pc += 4;
                pc_updated = true;
            }
            break;
        case 0x6F: // JAL
            rd = (instr >> 7) & 0x1f;
            imm = ((instr >> 21) & 0x3ff) << 1;
            imm |= ((instr >> 20) & 0x1) << 11;
            imm |= ((instr >> 12) & 0xff) << 12;
            imm |= ((instr >> 31) & 0x1) << 20;
            imm = sign_extend(imm, 21);
            regs[rd] = pc + 4;
#if DEBUG
            CORE_ID_LOGF(core_id, "JAL PC=0x%08x -> 0x%08x link=x%d=0x%08x", pc, pc + imm, rd, regs[rd]);
#endif
            pc += imm;
            pc_updated = true;
            break;
        case 0x67: // JALR
            rd = (instr >> 7) & 0x1f;
            funct3 = (instr >> 12) & 0x7;
            rs1 = (instr >> 15) & 0x1f;
            imm = sign_extend(instr >> 20, 12);
            if (funct3 == 0x0) { // JALR
                uint32_t new_pc = (regs[rs1] + imm) & ~1;
                regs[rd] = pc + 4;
#if DEBUG
                CORE_ID_LOGF(core_id, "JALR PC=0x%08x -> 0x%08x via x%d+0x%x link=x%d=0x%08x", pc, new_pc, rs1, imm, rd, regs[rd]);
#endif
                // DEBUG: Uncomment to log jr (jalr x0, x1, 0) return instructions
                // if (rd == 0 && rs1 == 1 && imm == 0) {
                //     fprintf(stderr, "[JR] PC=0x%08x -> 0x%08x via x1 (ra=0x%08x)\n", pc, new_pc, regs[1]);
                // }
                pc = new_pc; // Clear LSB for instruction alignment
                pc_updated = true;
            } else {
                // Unsupported JALR funct3
                pc += 4;
                pc_updated = true;
            }
            break;
        case 0x73: // CSR instructions
            {
                rd = (instr >> 7) & 0x1f;
                funct3 = (instr >> 12) & 0x7;
                uint32_t csr_addr = (instr >> 20) & 0xFFF;
                rs1 = (instr >> 15) & 0x1f;
                uint32_t imm = (instr >> 20) & 0x1F; // For immediate CSR instructions

                uint32_t csr_value = read_csr(csr_addr);
                uint32_t write_value = 0;

                switch (funct3) {
                    case 0x0: // System instructions including WFI, MRET, SRET
                        {
                            uint32_t funct7 = (instr >> 25) & 0x7F;
                            // Check for special system instructions by matching the full instruction pattern
                            // MRET: 0011000 00010 00000 000 00000 1110011 = 0x30200073
                            // SRET: 0001000 00010 00000 000 00000 1110011 = 0x10200073
                            if (instr == 0x30200073) {  // MRET instruction
                                uint32_t mstatus = read_csr(0x300); // mstatus
                                uint32_t mepc = read_csr(0x341);    // mepc

                                CORE_ID_LOGF(core_id, "[MRET] Before: mstatus=0x%08x, mepc=0x%08x, current_priv=%d",
                                       mstatus, mepc, (int)privilege_level);

                                // Extract fields from mstatus
                                uint32_t mpie = (mstatus >> 7) & 0x1;  // Machine Previous Interrupt Enable
                                uint32_t mpp = (mstatus >> 11) & 0x3;  // Machine Previous Privilege Mode

                                CORE_ID_LOGF(core_id, "[MRET] Extracted: MPP=%d, MPIE=%d", mpp, mpie);

                                // Restore MIE from MPIE
                                mstatus = (mstatus & ~0x8) | (mpie << 3);  // MIE = MPIE
                                // Set MPIE to 1
                                mstatus = (mstatus & ~0x80) | 0x80;  // MPIE = 1
                                // Set MPP to M (machine mode)
                                mstatus = (mstatus & ~0x1800) | (0x3 << 11);  // MPP = M

                                CORE_ID_LOGF(core_id, "[MRET] After mstatus update: mstatus=0x%08x", mstatus);

                                // Write updated mstatus
                                write_csr(0x300, mstatus);

                                // Restore privilege level from MPP
                                privilege_level = static_cast<PrivilegeLevel>(mpp);

                                // fprintf(stderr, "[MRET] Before: mstatus=0x%08x, mepc=0x%08x, MPP=%d, current_PC=0x%08x\n",
                                //        mstatus, mepc, mpp, pc);

                                // Jump to mepc
                                pc = mepc;
                                pc_updated = true;

                                // fprintf(stderr, "[MRET] After: new_PC=0x%08x, new_priv=%d, pc_updated=%d\n", pc, (int)privilege_level, pc_updated);
                            } else if (funct7 == 0x105 && csr_addr == 0x105) {
                                // WFI instruction (opcode=0x73, funct3=0x0, funct7=0x105, csr_addr=0x105)
                                CORE_ID_LOGF(core_id, "Executing WFI instruction");

                                // WFI is a hint instruction. According to RISC-V spec:
                                // If interrupts are enabled and pending, implementation may immediately take the interrupt
                                // Otherwise, the core may enter a low-power state waiting for interrupt

                                // Check if there are pending interrupts right now
                                if (check_pending_interrupts()) {
                                    CORE_ID_LOGF(core_id, "WFI: Pending interrupt detected, handling immediately");
                                    handle_interrupt();
                                } else {
                                    CORE_ID_LOGF(core_id, "WFI: Entering wait state");
                                    is_waiting_for_interrupt = true;
                                }

                                // WFI writes 0 to rd if rd != x0
                                if (rd != 0) {
                                    regs[rd] = 0;
                                }
                            } else if (funct7 == 0x18 && rs1 == 2 && funct3 == 0x0 && rd == 0) {
                                // MRET instruction
                                // Official encoding: 0011000 00010 00000 000 00000 1110011
                                // funct7[31:25]=0x18, rs1[24:20]=0x02, funct3[14:12]=0x0, rd[11:7]=0x00, opcode[6:0]=0x73
                                uint32_t mstatus = read_csr(0x300); // mstatus
                                uint32_t mepc = read_csr(0x341);    // mepc

                                CORE_ID_LOGF(core_id, "Executing MRET: mstatus=0x%08x, mepc=0x%08x", mstatus, mepc);

                                // Extract fields from mstatus
                                uint32_t mpie = (mstatus >> 7) & 0x1;  // Machine Previous Interrupt Enable
                                uint32_t mpp = (mstatus >> 11) & 0x3;  // Machine Previous Privilege Mode

                                // Restore MIE from MPIE
                                mstatus = (mstatus & ~0x8) | (mpie << 3);  // MIE = MPIE
                                // Set MPIE to 1
                                mstatus = (mstatus & ~0x80) | 0x80;  // MPIE = 1
                                // Set MPP to M (machine mode)
                                mstatus = (mstatus & ~0x1800) | (0x3 << 11);  // MPP = M

                                // Write updated mstatus
                                write_csr(0x300, mstatus);

                                // Restore privilege level from MPP
                                privilege_level = static_cast<PrivilegeLevel>(mpp);

                                CORE_ID_LOGF(core_id, "MRET: jumping to mepc=0x%08x, new privilege=%d", mepc, (int)privilege_level);

                                // Jump to mepc
                                pc = mepc;
                                pc_updated = true;
                            } else if (rd == 0 && rs1 == 0 && funct7 == 0 && csr_addr == 0) {
                                // ECALL instruction
                                // Environment call (ecall): opcode=0x73, funct3=0x0, rd=x0, rs1=x0, funct7=x0
                                // Full encoding: 0000000 00000 00000 000 00000 1110011 = 0x00000073

                                uint32_t ecall_cause;
                                // Determine exception cause based on current privilege level
                                switch (privilege_level) {
                                    case PRV_U:
                                        ecall_cause = 8;  // User-mode ecall
                                        CORE_ID_LOGF(core_id, "[ECALL] From User mode, PC=0x%08x", pc);
                                        // fprintf(stderr, "[ECALL] From User mode, PC=0x%08x\n", pc);
                                        break;
                                    case PRV_S:
                                        ecall_cause = 9;  // Supervisor-mode ecall
                                        CORE_ID_LOGF(core_id, "[ECALL] From Supervisor mode (SBI call), PC=0x%08x", pc);
                                        // fprintf(stderr, "[ECALL] From Supervisor mode (SBI call), PC=0x%08x\n", pc);
                                        break;
                                    case PRV_M:
                                        ecall_cause = 11; // Machine-mode ecall
                                        CORE_ID_LOGF(core_id, "[ECALL] From Machine mode, PC=0x%08x", pc);
                                        // fprintf(stderr, "[ECALL] From Machine mode, PC=0x%08x\n", pc);
                                        break;
                                    default:
                                        ecall_cause = 9;  // Default to supervisor ecall
                                        CORE_ID_LOGF(core_id, "[ECALL] From unknown mode, defaulting to Supervisor, PC=0x%08x", pc);
                                        // fprintf(stderr, "[ECALL] From unknown mode, defaulting to Supervisor, PC=0x%08x\n", pc);
                                        break;
                                }

                                // Take the exception - this will switch to machine mode and jump to trap handler
                                take_exception(ecall_cause);
                                pc_updated = true;
                            } else {
                                // Other system instructions - treat as no-op for now
                                CORE_ID_LOGF(core_id, "Unknown system instruction: funct7=0x%x, csr_addr=0x%x", funct7, csr_addr);
                                if (rd != 0) {
                                    regs[rd] = 0; // Many system instructions return 0
                                }
                            }
                        }
                        break;
                    case 0x1: // CSRRW
                        write_value = regs[rs1];
                        if (rd != 0) regs[rd] = csr_value;
                        // if (csr_addr == 0x300) {
                        //     fprintf(stderr, "[CSR] mstatus write: old=0x%08x, new=0x%08x, PC=0x%08x\n", csr_value, write_value, pc);
                        // }
                        write_csr(csr_addr, write_value);
                        break;
                    case 0x2: // CSRRS
                        write_value = csr_value | regs[rs1];
                        if (rd != 0) regs[rd] = csr_value;
                        write_csr(csr_addr, write_value);
                        break;
                    case 0x3: // CSRRC
                        write_value = csr_value & ~regs[rs1];
                        if (rd != 0) regs[rd] = csr_value;
                        write_csr(csr_addr, write_value);
                        break;
                    case 0x5: // CSRRWI
                        write_value = imm;
                        if (rd != 0) regs[rd] = csr_value;
                        write_csr(csr_addr, write_value);
                        break;
                    case 0x6: // CSRRSI
                        write_value = csr_value | imm;
                        if (rd != 0) regs[rd] = csr_value;
                        write_csr(csr_addr, write_value);
                        break;
                    case 0x7: // CSRRCI
                        write_value = csr_value & ~imm;
                        if (rd != 0) regs[rd] = csr_value;
                        write_csr(csr_addr, write_value);
                        break;
                    default:
                        // Invalid CSR instruction - treat as no-op
                        break;
                }
                // Only increment PC if it wasn't already updated by the instruction
                // (e.g., MRET sets pc directly, so we shouldn't increment it)
                if (!pc_updated) {
                    pc += 4;
                    pc_updated = true;
                    // fprintf(stderr, "[CSR] PC incremented to 0x%08x (pc_updated was false)\n", pc);
                } else {
                    // fprintf(stderr, "[CSR] PC NOT incremented, keeping at 0x%08x (pc_updated was true)\n", pc);
                }
            }
            break;
        default:
            pc += 4;
            pc_updated = true;
            break;
    }

    // Update performance counter if enabled
    update_performance_counter();

    regs[0] = 0;
    (void)pc_updated;
}

uint32_t Core::read_instruction(uint32_t pc_addr) {
    // Fetch instruction through instruction cache if available
    uint32_t instr;
    if (instruction_cache) {
        instruction_cache->read(pc_addr, instr, true, core_id);  // true indicates instruction fetch, pass core_id
    } else {
        // Let cache/MMU handle address translation
        instr = load32(memory, pc_addr);
    }
    return instr;
}



// New profiling methods using the profiler classes
uint64_t Core::get_instruction_count() const {
    return performance_profiler ? performance_profiler->get_instruction_count(core_id) : 0;
}

void Core::set_function_profiling(bool enable) {
    if (function_profiler) {
        function_profiler->enable_profiling(enable);
    }
}

bool Core::get_function_profiling() const {
    return function_profiler ? function_profiler->is_profiling_enabled() : false;
}

void Core::add_function_profile(const std::string& name, uint64_t start_pc, uint64_t end_pc) {
    if (function_profiler) {
        function_profiler->add_function_profile(name, start_pc, end_pc);
    }
}

void Core::print_function_profiling_results() {
    if (function_profiler) {
        function_profiler->print_function_profiling_results();
    }
}

// CSR Implementation
void Core::init_csrs() {
    // Initialize CSR registers with zeros
    // We'll use a sparse representation - only allocate when needed
    // For now, we'll pre-allocate common CSRs
    csr_registers.resize(4096, 0); // 12-bit CSR address space

    // Initialize SATP CSR (address 0x180)
    // Default: MODE = 0 (Bare), PPN = 0
    csr_registers[0x180] = 0x00000000;

    // Initialize Machine-mode interrupt CSRs

    // mstatus (0x300) - Machine Status Register
    // Initial state: MIE=0 (machine interrupts disabled), MPIE=1, MPP=3 (machine mode)
    csr_registers[0x300] = 0x1800; // MPIE=1, MPP=11 (binary 11 = machine mode)

    // mie (0x304) - Machine Interrupt Enable Register
    // Enable machine software, timer, and external interrupts
    csr_registers[0x304] = 0x888; // MSIE=1, MTIE=1, MEIE=1

    // mip (0x344) - Machine Interrupt Pending Register (read-only)
    // Will be updated by interrupt controllers

    // mtvec (0x305) - Machine Trap Vector Base Address
    // Default to address 0, will be set by bootloader
    csr_registers[0x305] = 0;

    // mscratch (0x340) - Machine Scratch Register
    // Initial value 0
    csr_registers[0x340] = 0;

    // mepc (0x341) - Machine Exception Program Counter
    // Will be set when traps occur

    // mcause (0x342) - Machine Cause Register
    // Will be set when traps occur

    // mtval (0x343) - Machine Trap Value Register
    // Will be set when traps occur
}

uint32_t Core::read_csr(uint32_t csr_addr) {
    if (csr_addr >= csr_registers.size()) {
        return 0; // Invalid CSR address
    }

    // Handle special CSR reads
    switch (csr_addr) {
        // Supervisor-mode CSRs
        case 0x100: // sstatus - Supervisor Status Register
            // sstatus is a subset of mstatus
            {
                uint32_t mstatus = csr_registers[0x300];
                uint32_t sstatus = 0;
                // Copy relevant bits from mstatus to sstatus
                sstatus |= (mstatus & 0x1);    // SD (dirty)
                sstatus |= (mstatus & 0x2);    // WP (watchpoint)
                sstatus |= (mstatus & 0x4) << 3; // TS (trap sighandler)
                sstatus |= (mstatus & 0x8);    // FS (float status)
                sstatus |= (mstatus & 0x10) << 1; // XS (extension status)
                sstatus |= (mstatus & 0x20) << 1; // UXL (user xlen)
                sstatus |= (mstatus & 0x40) << 1; // SDL (dirty previous)
                sstatus |= (mstatus & 0x80);    // SPIE (supervisor previous interrupt enable)
                sstatus |= (mstatus & 0x100) << 4; //UBE (user big endian)
                sstatus |= (mstatus & 0x200) << 8; // SPIE
                sstatus |= (mstatus & 0x400) << 4; // SPP (supervisor previous privilege)
                sstatus |= (mstatus & 0x80000); // UIE (user interrupt enable)
                sstatus |= (mstatus & 0x100000); // SIE (supervisor interrupt enable)
                return sstatus;
            }
        case 0x104: // sie - Supervisor Interrupt Enable Register
            return csr_registers[0x104];
        case 0x105: // stvec - Supervisor Trap Vector Base Address
            return csr_registers[0x105];
        case 0x140: // sscratch - Supervisor Scratch Register
            return csr_registers[0x140];
        case 0x141: // sepc - Supervisor Exception Program Counter
            return csr_registers[0x141];
        case 0x142: // scause - Supervisor Cause Register
            return csr_registers[0x142];
        case 0x143: // stval - Supervisor Trap Value Register
            return csr_registers[0x143];
        case 0x302: // medeleg - Machine Exception Delegation Register
            return csr_registers[0x302];
        case 0x304: // mie - Machine Interrupt Enable Register
            return csr_registers[0x304];
        case 0x30A: // mideleg - Machine Interrupt Delegation Register
            return csr_registers[0x30A];
        // Machine-mode interrupt CSRs
        case 0x300: // mstatus - Machine Status Register
            return csr_registers[0x300];
        case 0x344: // mip - Machine Interrupt Pending Register (read-only)
            return csr_registers[0x344];
        case 0x305: // mtvec - Machine Trap Vector Base Address
            return csr_registers[0x305];
        case 0x340: // mscratch - Machine Scratch Register
            return csr_registers[0x340];
        case 0x341: // mepc - Machine Exception Program Counter
            return csr_registers[0x341];
        case 0x342: // mcause - Machine Cause Register
            return csr_registers[0x342];
        case 0x343: // mtval - Machine Trap Value Register
            return csr_registers[0x343];
        case 0x180: // SATP
            return get_satp_value();
        case 0xF14: // mhartid - Hardware thread ID
            CORE_ID_LOGF(core_id, "mhartid CSR read: returning core_id=%d", core_id);
            return core_id;
        case 0xC01: // time - Timer/counter
            // Return a realistic cycle counter that simulates timing
            // Increment by a varying amount to simulate realistic instruction timing
            csr_registers[0xC01] += 50 + (core_id * 5); // Base 50 cycles + core variation
            return csr_registers[0xC01];
        case 0xB00: // PMC - Performance Monitor Counter (per-core)
            return performance_counter;
        case 0xB01: // PMCTRL - Performance Monitor Control (per-core)
            return performance_control;
        case 0xB02: // PMES - Performance Monitor Event Select (per-core)
            return performance_event_select;
        case 0xB10: // Global Instruction Counter (read-only)
            return read_global_counter(0);
        case 0xB11: // Global Memory Access Counter (read-only)
            return read_global_counter(1);
        case 0xB12: // Global Branch Counter (read-only)
            return read_global_counter(2);
        case 0xB13: // Global Cache Hit Counter (read-only)
            return read_global_counter(3);
        case 0xB14: // Global Performance Control Register
            return csr_registers[0xB14];
        default:
            return csr_registers[csr_addr];
    }
}

void Core::write_csr(uint32_t csr_addr, uint32_t value) {
    if (csr_addr >= csr_registers.size()) {
        return; // Invalid CSR address
    }

    // Handle special CSR writes
    switch (csr_addr) {
        // Supervisor-mode CSRs
        case 0x100: // sstatus - Supervisor Status Register
            // sstatus writes affect mstatus
            {
                uint32_t mstatus = csr_registers[0x300];
                // Update mstatus from sstatus (only writable fields)
                mstatus = (mstatus & ~0x622) | (value & 0x622);  // Update SIE, SPIE, SPP fields
                csr_registers[0x300] = mstatus;
                CORE_ID_LOGF(core_id, "sstatus CSR write: value=0x%08x, mstatus updated to 0x%08x", value, mstatus);
            }
            break;
        case 0x104: // sie - Supervisor Interrupt Enable Register
            csr_registers[0x104] = value;
            CORE_ID_LOGF(core_id, "sie CSR write: value=0x%08x", value);
            break;
        case 0x105: // stvec - Supervisor Trap Vector Base Address
            csr_registers[0x105] = value;
            CORE_ID_LOGF(core_id, "stvec CSR write: value=0x%08x", value);
            break;
        case 0x140: // sscratch - Supervisor Scratch Register
            csr_registers[0x140] = value;
            break;
        case 0x141: // sepc - Supervisor Exception Program Counter
            csr_registers[0x141] = value;
            break;
        case 0x142: // scause - Supervisor Cause Register
            csr_registers[0x142] = value;
            break;
        case 0x143: // stval - Supervisor Trap Value Register
            csr_registers[0x143] = value;
            break;
        // Machine-mode interrupt CSRs
        case 0x300: // mstatus - Machine Status Register
            // Preserve read-only bits, handle interrupt enable changes
            csr_registers[0x300] = value;
            CORE_ID_LOGF(core_id, "mstatus CSR write: value=0x%08x", value);
            break;
        case 0x304: // mie - Machine Interrupt Enable Register
            csr_registers[0x304] = value;
            CORE_ID_LOGF(core_id, "mie CSR write: value=0x%08x", value);
            break;
        case 0x302: // medeleg - Machine Exception Delegation Register
            csr_registers[0x302] = value;
            CORE_ID_LOGF(core_id, "medeleg CSR write: value=0x%08x", value);
            break;
        case 0x30A: // mideleg - Machine Interrupt Delegation Register
            csr_registers[0x30A] = value;
            CORE_ID_LOGF(core_id, "mideleg CSR write: value=0x%08x", value);
            break;
        case 0x344: // mip - Machine Interrupt Pending Register
            // According to RISC-V spec, only certain bits are writable:
            // - SSIP (bit 1): writable, software interrupt pending
            // - STIP (bit 5): writable if stimecmp not implemented (our case)
            // - SEIP (bit 9): writable with special semantics
            // - MTIP (bit 7): read-only, controlled by timer hardware
            // We mask to only allow writable bits (SSIP, STIP, SEIP)
            {
                uint32_t writable_mask = (1U << 1) | (1U << 5) | (1U << 9); // SSIP | STIP | SEIP
                uint32_t current_mip = csr_registers[0x344];
                // Clear writable bits, then set them from value
                csr_registers[0x344] = (current_mip & ~writable_mask) | (value & writable_mask);
                CORE_ID_LOGF(core_id, "mip CSR write: value=0x%08x, result=0x%08x", value, csr_registers[0x344]);
            }
            break;
        case 0x340: // mscratch - Machine Scratch Register
            csr_registers[0x340] = value;
            break;
        case 0x341: // mepc - Machine Exception Program Counter
            csr_registers[0x341] = value;
            break;
        case 0x342: // mcause - Machine Cause Register
            csr_registers[0x342] = value;
            break;
        case 0x343: // mtval - Machine Trap Value Register
            csr_registers[0x343] = value;
            break;
        case 0x180: // SATP
            csr_registers[csr_addr] = value;
          CORE_ID_LOGF(core_id, "SATP CSR write: value=0x%08x", value);
            update_mmu_from_satp(value);
            CORE_ID_LOG(core_id, "SATP update completed");
            break;
        case 0xF14: // mhartid - Hardware thread ID (read-only)
            // mhartid is read-only, ignore writes to preserve correctness
            CORE_ID_LOGF(core_id, "Attempted write to read-only mhartid CSR (value=0x%08x) - ignored", value);
            break;
        case 0xB01: // PMCTRL - Performance Monitor Control
            performance_control = value;
            // Bit 0: Enable/disable counter
            // Bit 1: Reset counter
            if (value & 0x2) { // Reset bit
                performance_counter = 0;
            }
            break;
        case 0xB02: // PMES - Performance Monitor Event Select
            performance_event_select = value;
            break;
        case 0xB00: // PMC - Performance Monitor Counter (read-only)
            // Don't allow direct writes to counter
            break;
        case 0xB10: // Global counters (read-only)
        case 0xB11: // Global counters (read-only)
        case 0xB12: // Global counters (read-only)
        case 0xB13: // Global counters (read-only)
            // Global counters are read-only to prevent corruption
            break;
        case 0xB14: // Global Performance Control Register
            // Bit 0: Enable global counters (not implemented - they're always enabled)
            // Bit 1: Reset all global counters
            if (value & 0x2) { // Reset bit
                std::lock_guard<std::mutex> lock(performance_counter_mutex);
                global_instruction_counter = 0;
                global_memory_access_counter = 0;
                global_branch_counter = 0;
                global_cache_hit_counter = 0;
            }
            break;
        default:
            csr_registers[csr_addr] = value;
            break;
    }
}

// SATP CSR specific methods
void Core::update_mmu_from_satp(uint32_t satp_value) {
#if DEBUG
    CORE_ID_LOGF(core_id, "update_mmu_from_satp called with value=0x%08x", satp_value);
#endif

    if (!mmu) {
#if DEBUG
        CORE_LOG("No MMU connected");
#endif
        return; // No MMU connected
    }

    // Extract MODE field (bit 31 for RV32 Sv32)
    uint32_t mode = (satp_value >> 31) & 0x1;

#if DEBUG
    CORE_ID_LOGF(core_id, "SATP mode=%d", mode);
#endif

    if (mode == 0) {
        // Bare mode: disable virtual memory (identity mapping)
        mmu->set_page_table_base(0);
#if DEBUG
        CORE_LOG("Set bare mode (page_table_base=0)");
#endif
    } else if (mode == 1) {
        // Sv32 mode: extract PPN and set page table base
        uint32_t ppn = satp_value & 0x3FFFFF; // Bits 21:0
        uint32_t page_table_base = ppn << 12; // PPN << 12 = physical address
        mmu->set_page_table_base(page_table_base);
#if DEBUG
        CORE_ID_LOGF(core_id, "Set Sv32 mode (page_table_base=0x%08x)", page_table_base);
#endif
    }
    // Other modes are not supported in RV32
}

uint32_t Core::get_satp_value() const {
    if (csr_registers.size() <= 0x180) {
        return 0;
    }
    return csr_registers[0x180];
}

uint32_t Core::handle_exception(ExceptionType ex, uint32_t fault_pc) {
    // Enhanced exception handler for sandbox security
    // In a real RISC-V implementation, this would:
    // 1. Save the current PC to EPC (Exception Program Counter) CSR
    // 2. Set the cause in CAUSE CSR
    // 3. Set the appropriate trap vector in STVEC CSR
    // 4. Jump to the trap handler

#if DEBUG
    CORE_ID_LOGF(core_id, "SECURITY: Exception %d at PC 0x%08x - Sandbox violation detected!", static_cast<int>(ex), fault_pc);
#endif

    // For critical security violations, halt execution
    if (ex == ExceptionType::LOAD_ACCESS_FAULT || ex == ExceptionType::STORE_AMO_ACCESS_FAULT) {
#if DEBUG
        CORE_ID_LOGF(core_id, "SECURITY: Memory access violation - halting core for security");
#endif
        // Set PC to infinite loop to halt execution (security measure)
        // In a real system, this would trigger a trap handler or system panic
        return fault_pc; // Stay at fault PC to create infinite loop
    }

    // For other exceptions, continue execution at the next instruction
    // This is not correct behavior but allows the simulation to continue for debugging
    return fault_pc + 4;
}

// Performance Monitor Counter implementation
void Core::update_performance_counter() {
    // Always increment global counters (they're always enabled for system-wide monitoring)
    increment_global_counter(0); // Always count instructions

    // Check if local performance counter is enabled (bit 0 of control register)
    if (!(performance_control & 0x1)) {
        return; // Local counter disabled
    }

    // Increment local counter based on selected event
    switch (performance_event_select) {
        case 0: // Count all instructions
            increment_performance_counter();
            break;
        case 1: // Count memory accesses (loads and stores)
            // This would require tracking memory accesses during instruction execution
            // For now, we'll count them as they happen
            increment_performance_counter();
            increment_global_counter(1); // Also count in global memory access counter
            break;
        case 2: // Count branch instructions
            // This would require checking if current instruction is a branch
            increment_performance_counter();
            increment_global_counter(2); // Also count in global branch counter
            break;
        case 3: // Count cache hits
            // This would require cache hit statistics
            increment_performance_counter();
            increment_global_counter(3); // Also count in global cache hit counter
            break;
        default:
            // Unknown event - don't count
            break;
    }
}

void Core::increment_performance_counter() {
    if (performance_counter < 0xFFFFFFFF) { // Prevent overflow
        performance_counter++;
    }
}

// Multi-core global counter methods with synchronization
void Core::increment_global_counter(uint32_t event_type) {
    std::lock_guard<std::mutex> lock(performance_counter_mutex);

    switch (event_type) {
        case 0: // Instructions
            if (global_instruction_counter < 0xFFFFFFFF) {
                global_instruction_counter++;
            }
            break;
        case 1: // Memory accesses
            if (global_memory_access_counter < 0xFFFFFFFF) {
                global_memory_access_counter++;
            }
            break;
        case 2: // Branches
            if (global_branch_counter < 0xFFFFFFFF) {
                global_branch_counter++;
            }
            break;
        case 3: // Cache hits
            if (global_cache_hit_counter < 0xFFFFFFFF) {
                global_cache_hit_counter++;
            }
            break;
        default:
            break;
    }
}

uint32_t Core::read_global_counter(uint32_t event_type) {
    std::lock_guard<std::mutex> lock(performance_counter_mutex);

    switch (event_type) {
        case 0: return global_instruction_counter;
        case 1: return global_memory_access_counter;
        case 2: return global_branch_counter;
        case 3: return global_cache_hit_counter;
        default: return 0;
    }
}

// Interrupt Handling Implementation
void Core::handle_interrupt() {
    // Check for pending interrupts
    if (check_pending_interrupts()) {
        uint32_t mstatus = read_csr(0x300); // mstatus
        uint32_t mie = (mstatus >> 3) & 0x1;  // MIE bit
        uint32_t sstatus = read_csr(0x100); // sstatus
        uint32_t sie = (sstatus >> 1) & 0x1;  // SIE bit

        update_mip_csr();
        uint32_t mip = read_csr(0x344);    // mip
        uint32_t mie_reg = read_csr(0x304); // mie
        uint32_t mideleg = read_csr(0x30A); // mideleg
        uint32_t sie_reg = read_csr(0x104); // sie (S-mode interrupt enable)

        // Handle M-mode interrupts (only if MIE is set)
        if (mie) {
            // Check for pending and enabled M-mode interrupts (priority order)
            uint32_t m_interrupts = mip & mie_reg & ~mideleg;  // Non-delegated interrupts

            if (m_interrupts & 0x800) { // Machine software interrupt (bit 11)
                take_interrupt(ExceptionType::MACHINE_SOFTWARE_INTERRUPT);
                return;
            } else if (m_interrupts & 0x200) { // Machine timer interrupt (bit 9)
                take_interrupt(ExceptionType::MACHINE_TIMER_INTERRUPT);
                return;
            } else if (m_interrupts & 0x100) { // Machine external interrupt (bit 8)
                take_interrupt(ExceptionType::MACHINE_EXTERNAL_INTERRUPT);
                return;
            }
        }

        // Handle S-mode delegated interrupts (only if SIE is set)
        if (sie) {
            // Check for delegated interrupts that are pending and enabled in S-mode
            // MIP bits: MSIP=11, MTIP=9, MEIP=8
            // sie bits: SSIP=1,  STIP=5, SEIP=9
            uint32_t delegated = mip & mideleg;

            // Convert MIP bits to sie bits for checking
            uint32_t delegated_for_sie = 0;
            if (delegated & 0x800) delegated_for_sie |= (1 << 1);  // MSIP -> SSIE
            if (delegated & 0x200) delegated_for_sie |= (1 << 5);  // MTIP -> STIE
            if (delegated & 0x100) delegated_for_sie |= (1 << 9);  // MEIP -> SEIE

            uint32_t s_interrupts = delegated_for_sie & sie_reg;

            if (s_interrupts & (1 << 1)) { // Software interrupt (SSIP)
                take_supervisor_interrupt(0x1);
                return;
            } else if (s_interrupts & (1 << 5)) { // Timer interrupt (STIP)
                take_supervisor_interrupt(0x5);
                return;
            } else if (s_interrupts & (1 << 9)) { // External interrupt (SEIP)
                take_supervisor_interrupt(0x9);
                return;
            }
        }
    }
}

bool Core::check_pending_interrupts() {
    // Update MIP register with current interrupt status
    update_mip_csr();

    uint32_t mip = read_csr(0x344);
    uint32_t mie_reg = read_csr(0x304);
    uint32_t mideleg = read_csr(0x30A);
    uint32_t sstatus = read_csr(0x100);
    uint32_t sie_reg = (sstatus >> 1) & 0x1;  // SIE bit

    // Check if any machine interrupts are pending and enabled
    bool machine_interrupt = (mip & mie_reg & 0xB00) != 0;

    // Check if any supervisor interrupts are pending and enabled (via delegation)
    bool supervisor_interrupt = false;
    if (sie_reg) {
        uint32_t delegated = mip & mideleg;
        uint32_t sie = read_csr(0x104);
        supervisor_interrupt = (delegated & sie & 0x222) != 0; // Check bits 1, 5, and 9
    }

    return machine_interrupt || supervisor_interrupt;
}

void Core::update_mip_csr() {
    uint32_t mip_value = 0;

    // Check CLINT for local interrupts
    if (clint) {
        if (clint->has_software_interrupt(core_id)) {
            mip_value |= 0x800; // Set MSIP bit (bit 11)
        }
        if (clint->has_timer_interrupt(core_id)) {
            mip_value |= 0x200; // Set MTIP bit (bit 9)
        }
    }

    // Check PLIC for external interrupts
    if (plic && plic->has_pending_interrupts(core_id)) {
        mip_value |= 0x100; // Set MEIP bit (bit 8)
    }

    // Update MIP CSR (read-only, but we need to store it for read operations)
    csr_registers[0x344] = mip_value;
}

void Core::take_interrupt(ExceptionType interrupt_type) {
    CORE_ID_LOGF(core_id, "Taking interrupt: type=%d", static_cast<int>(interrupt_type));

    uint32_t mstatus = read_csr(0x300); // mstatus
    uint32_t mie = (mstatus >> 3) & 0x1;   // Current MIE bit

    // Save current state
    uint32_t mepc = pc; // Save current PC

    // Update mstatus: clear MIE, set MPIE, set MPP
    mstatus = (mstatus & ~0x8) |      // Clear MIE (bit 3)
              (mie << 7) |            // Set MPIE to old MIE (bit 7)
              (0x3 << 11);            // Set MPP to machine mode (bits 11-12 = 11)
    write_csr(0x300, mstatus);

    // Set mepc and mcause
    write_csr(0x341, mepc); // mepc

    uint32_t cause = 0x80000000; // Interrupt bit (31) set
    switch (interrupt_type) {
        case ExceptionType::MACHINE_SOFTWARE_INTERRUPT:
            cause |= 0x3;  // Machine software interrupt
            break;
        case ExceptionType::MACHINE_TIMER_INTERRUPT:
            cause |= 0x7;  // Machine timer interrupt
            break;
        case ExceptionType::MACHINE_EXTERNAL_INTERRUPT:
            cause |= 0xB;  // Machine external interrupt
            break;
        default:
            cause |= 0x3;  // Default to machine software interrupt
            break;
    }
    write_csr(0x342, cause); // mcause

    // Clear mtval for interrupts
    write_csr(0x343, 0); // mtval

    // Get trap vector base and mode
    uint32_t mtvec = read_csr(0x305);
    uint32_t base = mtvec & ~0x3;
    uint32_t mode = mtvec & 0x3;

    // Jump to trap handler
    if (mode == 0) {
        // Direct mode: jump to base address
        pc = base;
    } else if (mode == 1) {
        // Vector mode: jump to base + (cause * 4)
        uint32_t cause_code = cause & 0xFFF;
        pc = base + (cause_code * 4);
    } else {
        // Reserved modes, treat as direct mode
        pc = base;
    }

    CORE_ID_LOGF(core_id, "Interrupt handling complete: jumping to PC=0x%08x", pc);
}

void Core::take_supervisor_interrupt(uint32_t cause) {
    CORE_ID_LOGF(core_id, "Taking supervisor interrupt: cause=0x%x", cause);

    uint32_t sstatus = read_csr(0x100); // sstatus
    uint32_t sie = (sstatus >> 1) & 0x1;   // SIE bit

    // Save current state
    uint32_t sepc = pc; // Save current PC

    // Update sstatus: clear SIE, set SPIE, set SPP
    sstatus = (sstatus & ~0x2) |      // Clear SIE (bit 1)
              (sie << 5) |            // Set SPIE to old SIE (bit 5)
              (0x1 << 8);             // Set SPP to supervisor mode (bit 8)
    write_csr(0x100, sstatus);

    // Set sepc and scause
    write_csr(0x141, sepc); // sepc

    uint32_t scause = 0x80000000 | cause; // Interrupt bit (31) set + cause code
    write_csr(0x142, scause); // scause

    // Clear stval for interrupts
    write_csr(0x143, 0); // stval

    // Get trap vector base and mode
    uint32_t stvec = read_csr(0x105);
    uint32_t base = stvec & ~0x3;
    uint32_t mode = stvec & 0x3;

    // Jump to trap handler
    if (mode == 0) {
        // Direct mode: jump to base address
        pc = base;
    } else if (mode == 1) {
        // Vector mode: jump to base + (cause * 4)
        uint32_t cause_code = scause & 0xFFF;
        pc = base + (cause_code * 4);
    } else {
        // Reserved modes, treat as direct mode
        pc = base;
    }

    CORE_ID_LOGF(core_id, "Supervisor interrupt handling complete: jumping to PC=0x%08x", pc);
}

void Core::take_exception(uint32_t cause) {
    // Check if exception is delegated to S-mode
    uint32_t medeleg = read_csr(0x302); // Machine Exception Delegation
    uint32_t cause_code = cause & 0xFFF; // Extract cause code (without interrupt bit)
    bool delegated = (medeleg >> cause_code) & 0x1;

    // If delegated and current privilege is S-mode or U-mode, use S-mode trap handling
    if (delegated && privilege_level <= PRV_S) {
        // S-mode exception handling
        uint32_t sstatus = read_csr(0x100); // sstatus
        uint32_t sie = (sstatus >> 1) & 0x1;   // Current SIE bit

        // Save current PC
        uint32_t sepc = pc;

        // Update sstatus: clear SIE, set SPIE, set SPP
        // First, clear the SPP bit (8)
        sstatus = sstatus & ~0x100;
        // Clear SIE (bit 1)
        sstatus = sstatus & ~0x2;
        // Set SPIE to old SIE (bit 5)
        sstatus = sstatus | (sie << 5);
        // Set SPP to current privilege level (bit 8)
        sstatus = sstatus | ((uint32_t)privilege_level << 8);

        write_csr(0x100, sstatus);

        // Set sepc and scause
        write_csr(0x141, sepc); // sepc
        write_csr(0x142, cause); // scause

        // Clear stval for ecall
        write_csr(0x143, 0); // stval

        // Get trap vector base and mode from stvec
        uint32_t stvec = read_csr(0x105);
        uint32_t base = stvec & ~0x3;
        uint32_t mode = stvec & 0x3;

        // Jump to trap handler
        if (mode == 0) {
            // Direct mode: jump to base address
            pc = base;
        } else if (mode == 1) {
            // Vector mode: jump to base + (cause * 4)
            pc = base + (cause_code * 4);
        } else {
            // Reserved modes, treat as direct mode
            pc = base;
        }

        // Switch to supervisor mode
        privilege_level = PRV_S;

        CORE_ID_LOGF(core_id, "S-mode exception: jumping to PC=0x%08x, sepc=0x%08x, stvec=0x%08x", pc, sepc, stvec);
    } else {
        // M-mode exception handling (original logic)
        uint32_t mstatus = read_csr(0x300); // mstatus
        uint32_t mie = (mstatus >> 3) & 0x1;   // Current MIE bit

        // Save current PC
        uint32_t mepc = pc;

        // Update mstatus: clear MIE, set MPIE, set MPP
        // First, clear the MPP bits (11-12)
        mstatus = mstatus & ~0x1800;
        // Clear MIE (bit 3)
        mstatus = mstatus & ~0x8;
        // Set MPIE to old MIE (bit 7)
        mstatus = mstatus | (mie << 7);
        // Set MPP to current privilege level (bits 11-12)
        mstatus = mstatus | ((uint32_t)privilege_level << 11);

        write_csr(0x300, mstatus);

        // Set mepc and mcause
        write_csr(0x341, mepc); // mepc
        write_csr(0x342, cause); // mcause

        // Clear mtval for ecall
        write_csr(0x343, 0); // mtval

        // Get trap vector base and mode from mtvec
        uint32_t mtvec = read_csr(0x305);
        uint32_t base = mtvec & ~0x3;
        uint32_t mode = mtvec & 0x3;

        // Jump to trap handler
        if (mode == 0) {
            // Direct mode: jump to base address
            pc = base;
        } else if (mode == 1) {
            // Vector mode: jump to base + (cause * 4)
            pc = base + (cause_code * 4);
        } else {
            // Reserved modes, treat as direct mode
            pc = base;
        }

        // Switch to machine mode
        privilege_level = PRV_M;

        CORE_ID_LOGF(core_id, "M-mode exception: jumping to PC=0x%08x, mepc=0x%08x, mtvec=0x%08x", pc, mepc, mtvec);
    }
}

// Debugger support methods
uint32_t Core::get_reg(uint32_t reg_num) const {
    if (reg_num < 32) {
        return regs[reg_num];
    }
    return 0;
}

void Core::set_reg(uint32_t reg_num, uint32_t value) {
    if (reg_num < 32 && reg_num != 0) {  // x0 is read-only
        regs[reg_num] = value;
    }
}

bool Core::check_debug_breakpoint() {
    if (simulator && simulator->is_debug_mode()) {
        if (simulator->check_breakpoint(core_id, pc)) {
            return true;
        }
    }
    return false;
}
