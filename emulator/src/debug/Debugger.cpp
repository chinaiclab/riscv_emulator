#include "../include/debug/Debugger.h"
#include "../include/system/Simulator.h"
#include "../include/core/Core.h"
#include "../include/debug/Disassembler.h"
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

Debugger::Debugger(Simulator* sim)
    : simulator(sim), focus_core(0), step_mode(false), continue_execution(false), active(false) {
}

// Breakpoint management
void Debugger::set_breakpoint(uint32_t pc) {
    breakpoints.insert(pc);
}

void Debugger::remove_breakpoint(uint32_t pc) {
    breakpoints.erase(pc);
}

void Debugger::clear_breakpoints() {
    breakpoints.clear();
}

bool Debugger::should_break(uint32_t pc) const {
    return breakpoints.find(pc) != breakpoints.end();
}

void Debugger::list_breakpoints() const {
    if (breakpoints.empty()) {
        std::cout << "No breakpoints set." << std::endl;
        return;
    }

    std::cout << "Breakpoints:" << std::endl;
    int idx = 1;
    for (uint32_t bp : breakpoints) {
        std::cout << "  " << idx++ << ". 0x" << std::hex << std::setfill('0')
                  << std::setw(8) << bp << std::dec << std::endl;
    }
}

// Tokenize command line
std::vector<std::string> Debugger::tokenize(const std::string& cmd) {
    std::vector<std::string> tokens;
    std::istringstream iss(cmd);
    std::string token;
    while (iss >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

// Parse address string (supports decimal and hex)
uint32_t Debugger::parse_address(const std::string& addr_str) {
    if (addr_str.empty()) {
        return 0;
    }

    // Check for hex prefix
    if (addr_str.size() > 2 && addr_str[0] == '0' && (addr_str[1] == 'x' || addr_str[1] == 'X')) {
        return static_cast<uint32_t>(std::stoul(addr_str, nullptr, 16));
    }

    // Check for hex suffix
    if (addr_str.back() == 'h' || addr_str.back() == 'H') {
        return static_cast<uint32_t>(std::stoul(addr_str.substr(0, addr_str.size() - 1), nullptr, 16));
    }

    // Default to decimal
    return static_cast<uint32_t>(std::stoul(addr_str));
}

// Read memory through simulator
uint32_t Debugger::read_memory_word(uint32_t addr) {
    return simulator->read_word(addr);
}

uint8_t Debugger::read_memory_byte(uint32_t addr) {
    uint32_t word = simulator->read_word(addr & ~0x3);
    int byte_offset = addr & 0x3;
    return static_cast<uint8_t>((word >> (byte_offset * 8)) & 0xFF);
}

// Print debugger prompt
void Debugger::print_prompt() {
    std::cout << "(dbg) ";
}

// Print breakpoint hit message
void Debugger::print_breakpoint_hit(uint32_t pc, uint32_t core_id) {
    std::cout << std::endl << "Breakpoint hit at Core#" << core_id
              << " PC=0x" << std::hex << std::setfill('0') << std::setw(8) << pc
              << std::dec << std::endl;
}

// Main command loop
void Debugger::command_loop() {
    active = true;
    std::cout << std::endl << "RISC-V Debugger - Type 'help' for commands" << std::endl;

    while (active) {
        print_prompt();

        std::string cmd_line;
        if (!std::getline(std::cin, cmd_line)) {
            // EOF reached
            break;
        }

        // Skip empty lines
        if (cmd_line.empty() || cmd_line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }

        handle_command(cmd_line);

        // Check if we should continue execution
        if (continue_execution || step_mode) {
            break;
        }
    }
}

// Handle command
void Debugger::handle_command(const std::string& cmd) {
    std::vector<std::string> tokens = tokenize(cmd);
    if (tokens.empty()) {
        return;
    }

    const std::string& command = tokens[0];
    std::vector<std::string> args(tokens.begin() + 1, tokens.end());

    // Command aliases
    if (command == "b" || command == "break") {
        cmd_break(args);
    } else if (command == "d" || command == "delete") {
        cmd_delete(args);
    } else if (command == "l" || command == "list") {
        cmd_list(args);
    } else if (command == "s" || command == "step") {
        cmd_step(args);
    } else if (command == "n" || command == "next") {
        cmd_next(args);
    } else if (command == "c" || command == "continue") {
        cmd_continue(args);
    } else if (command == "r" || command == "regs") {
        cmd_regs(args);
    } else if (command == "mem") {
        cmd_mem(args);
    } else if (command == "disasm") {
        cmd_disasm(args);
    } else if (command == "h" || command == "help") {
        cmd_help(args);
    } else if (command == "q" || command == "quit") {
        cmd_quit(args);
    } else {
        std::cout << "Unknown command: " << command << ". Type 'help' for available commands." << std::endl;
    }
}

// Command: break <address>
void Debugger::cmd_break(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: break <address>" << std::endl;
        std::cout << "Example: break 0x1000" << std::endl;
        return;
    }

    try {
        uint32_t addr = parse_address(args[0]);
        set_breakpoint(addr);

        // Get breakpoint number
        int bp_num = static_cast<int>(breakpoints.size());
        std::cout << "Breakpoint " << bp_num << " set at 0x"
                  << std::hex << std::setfill('0') << std::setw(8) << addr
                  << std::dec << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Invalid address: " << args[0] << std::endl;
    }
}

// Command: delete <number>
void Debugger::cmd_delete(const std::vector<std::string>& args) {
    if (args.empty()) {
        // Delete all breakpoints
        clear_breakpoints();
        std::cout << "All breakpoints deleted." << std::endl;
        return;
    }

    try {
        int bp_num = std::stoi(args[0]);
        if (bp_num < 1 || bp_num > static_cast<int>(breakpoints.size())) {
            std::cout << "Invalid breakpoint number." << std::endl;
            return;
        }

        auto it = breakpoints.begin();
        std::advance(it, bp_num - 1);
        uint32_t addr = *it;
        breakpoints.erase(it);

        std::cout << "Deleted breakpoint " << bp_num << " at 0x"
                  << std::hex << std::setfill('0') << std::setw(8) << addr
                  << std::dec << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Invalid breakpoint number: " << args[0] << std::endl;
    }
}

// Command: list
void Debugger::cmd_list(const std::vector<std::string>& args) {
    (void)args;  // Unused
    list_breakpoints();
}

// Command: step [count]
void Debugger::cmd_step(const std::vector<std::string>& args) {
    int count = 1;
    if (!args.empty()) {
        try {
            count = std::stoi(args[0]);
        } catch (...) {
            std::cout << "Invalid count. Using 1." << std::endl;
            count = 1;
        }
    }

    std::cout << "Stepping " << count << " instruction(s)..." << std::endl;
    // Execute the specified number of steps
    simulator->debug_run(count);
}

// Command: next
void Debugger::cmd_next(const std::vector<std::string>& args) {
    (void)args;  // Unused
    std::cout << "Next instruction..." << std::endl;
    // Execute one step (same as step)
    simulator->debug_run(1);
}

// Command: continue
void Debugger::cmd_continue(const std::vector<std::string>& args) {
    (void)args;  // Unused
    std::cout << "Continuing execution (until breakpoint or program end)..." << std::endl;

    // Run until breakpoint or completion
    while (true) {
        // Execute one step
        simulator->debug_run(1);

        // Check if any core hit a breakpoint
        bool hit_breakpoint = false;
        for (uint32_t i = 0; i < simulator->get_cores().size(); i++) {
            Core* core = simulator->get_core(i);
            if (core && should_break(core->get_pc())) {
                std::cout << std::endl << "Breakpoint hit at Core#" << i
                          << " PC=0x" << std::hex << std::setfill('0') << std::setw(8) << core->get_pc()
                          << std::dec << std::endl;
                hit_breakpoint = true;
                break;
            }
        }

        if (hit_breakpoint) {
            break;
        }

        // Check if all cores finished
        bool all_finished = true;
        for (uint32_t i = 0; i < simulator->get_cores().size(); i++) {
            Core* core = simulator->get_core(i);
            if (core && !core->get_finished()) {
                all_finished = false;
                break;
            }
        }

        if (all_finished) {
            std::cout << std::endl << "Program finished." << std::endl;
            break;
        }
    }
}

// Command: regs [core_id]
void Debugger::cmd_regs(const std::vector<std::string>& args) {
    uint32_t core_id = focus_core;

    if (!args.empty()) {
        try {
            core_id = static_cast<uint32_t>(std::stoi(args[0]));
        } catch (...) {
            std::cout << "Invalid core ID. Using current focus core." << std::endl;
        }
    }

    // Access core through simulator
    Core* core = simulator->get_core(core_id);
    if (!core) {
        std::cout << "Invalid core ID: " << core_id << std::endl;
        return;
    }

    std::cout << "Core#" << core_id << " Registers:" << std::endl;

    // Print PC
    std::cout << "PC: 0x" << std::hex << std::setfill('0') << std::setw(8)
              << core->get_pc() << std::dec << std::endl;

    // Print register groups (4 per row)
    const uint32_t* regs = core->get_regs();
    for (int i = 0; i < 32; i += 4) {
        for (int j = 0; j < 4 && (i + j) < 32; j++) {
            std::cout << " x" << std::setw(2) << (i + j) << ": 0x"
                      << std::hex << std::setfill('0') << std::setw(8) << regs[i + j]
                      << std::dec;
        }
        std::cout << std::endl;
    }
}

// Command: mem <address> [count]
void Debugger::cmd_mem(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: mem <address> [count]" << std::endl;
        std::cout << "Example: mem 0x10000 16" << std::endl;
        return;
    }

    try {
        uint32_t addr = parse_address(args[0]);
        int count = 16;  // Default

        if (args.size() > 1) {
            count = std::stoi(args[1]);
        }

        // Limit count
        count = std::min(256, std::max(1, count));

        std::cout << "Memory at 0x" << std::hex << std::setfill('0') << std::setw(8) << addr
                  << std::dec << ":" << std::endl;

        // Print in hex dump format
        for (int i = 0; i < count; i += 16) {
            std::cout << "  0x" << std::hex << std::setfill('0') << std::setw(8) << (addr + i)
                      << std::dec << ": ";

            // Hex bytes
            for (int j = 0; j < 16 && (i + j) < count; j++) {
                uint8_t byte = read_memory_byte(addr + i + j);
                std::cout << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(byte)
                          << " " << std::dec;
            }

            // Fill if needed
            for (int j = count - i; j < 16; j++) {
                std::cout << "   ";
            }

            // ASCII representation
            std::cout << " |";
            for (int j = 0; j < 16 && (i + j) < count; j++) {
                uint8_t byte = read_memory_byte(addr + i + j);
                char c = (byte >= 32 && byte < 127) ? static_cast<char>(byte) : '.';
                std::cout << c;
            }
            std::cout << "|" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "Error reading memory: " << e.what() << std::endl;
    }
}

// Command: disasm <address> [count]
void Debugger::cmd_disasm(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cout << "Usage: disasm <address> [count]" << std::endl;
        std::cout << "Example: disasm 0x0 10" << std::endl;
        return;
    }

    try {
        uint32_t addr = parse_address(args[0]);
        int count = 10;  // Default

        if (args.size() > 1) {
            count = std::stoi(args[1]);
        }

        // Limit count
        count = std::min(100, std::max(1, count));

        std::cout << "Disassembly from 0x" << std::hex << std::setfill('0') << std::setw(8) << addr
                  << std::dec << ":" << std::endl;

        for (int i = 0; i < count; i++) {
            uint32_t pc = addr + (i * 4);
            uint32_t instr = read_memory_word(pc);
            std::string disasm = Disassembler::disassemble(instr, pc);

            std::cout << "  0x" << std::hex << std::setfill('0') << std::setw(8) << pc
                      << std::dec << ":  " << disasm << std::endl;
        }
    } catch (const std::exception& e) {
        std::cout << "Error disassembling: " << e.what() << std::endl;
    }
}

// Command: help
void Debugger::cmd_help(const std::vector<std::string>& args) {
    (void)args;  // Unused
    std::cout << "Available commands:" << std::endl;
    std::cout << "  break <addr>    - Set breakpoint at address (alias: b)" << std::endl;
    std::cout << "  delete [num]    - Delete breakpoint by number or all (alias: d)" << std::endl;
    std::cout << "  list            - List all breakpoints (alias: l)" << std::endl;
    std::cout << "  step [count]    - Step execution (default: 1) (alias: s)" << std::endl;
    std::cout << "  next            - Execute next instruction (alias: n)" << std::endl;
    std::cout << "  continue        - Continue execution (alias: c)" << std::endl;
    std::cout << "  regs [core]     - Show registers (alias: r)" << std::endl;
    std::cout << "  mem <addr> [n]  - Show memory (default: 16 bytes)" << std::endl;
    std::cout << "  disasm <addr> [n] - Disassemble instructions (default: 10)" << std::endl;
    std::cout << "  help            - Show this help (alias: h)" << std::endl;
    std::cout << "  quit            - Exit debugger (alias: q)" << std::endl;
    std::cout << std::endl;
    std::cout << "Address formats:" << std::endl;
    std::cout << "  0x1000          - Hexadecimal with prefix" << std::endl;
    std::cout << "  1000h           - Hexadecimal with suffix" << std::endl;
    std::cout << "  4096            - Decimal" << std::endl;
}

// Command: quit
void Debugger::cmd_quit(const std::vector<std::string>& args) {
    (void)args;  // Unused
    std::cout << "Exiting debugger..." << std::endl;
    active = false;
    continue_execution = true;  // Allow program to finish
    step_mode = false;
}
