#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>
#include <sstream>
#include <iostream>

// Forward declarations
class Simulator;
class Core;

class Debugger {
public:
    explicit Debugger(Simulator* sim);
    ~Debugger() = default;

    // Breakpoint management
    void set_breakpoint(uint32_t pc);
    void remove_breakpoint(uint32_t pc);
    void clear_breakpoints();
    bool should_break(uint32_t pc) const;
    void list_breakpoints() const;

    // Interactive command loop
    void command_loop();

    // Core focus management
    void set_focus_core(uint32_t core_id) { focus_core = core_id; }
    uint32_t get_focus_core() const { return focus_core; }

    // Step execution control
    void set_step_mode(bool enable) { step_mode = enable; }
    bool get_step_mode() const { return step_mode; }
    void clear_step_mode() { step_mode = false; continue_execution = false; }

    // Continue execution
    void set_continue_flag() { continue_execution = true; step_mode = false; }
    bool should_continue() const { return continue_execution; }

    // Check if we're in debugging mode
    bool is_active() const { return active; }
    void set_active(bool state) { active = state; }

private:
    Simulator* simulator;
    std::unordered_set<uint32_t> breakpoints;
    uint32_t focus_core;
    bool step_mode;
    bool continue_execution;
    bool active;

    // Command handlers
    void handle_command(const std::string& cmd);
    void cmd_break(const std::vector<std::string>& args);
    void cmd_delete(const std::vector<std::string>& args);
    void cmd_list(const std::vector<std::string>& args);
    void cmd_step(const std::vector<std::string>& args);
    void cmd_next(const std::vector<std::string>& args);
    void cmd_continue(const std::vector<std::string>& args);
    void cmd_regs(const std::vector<std::string>& args);
    void cmd_mem(const std::vector<std::string>& args);
    void cmd_disasm(const std::vector<std::string>& args);
    void cmd_help(const std::vector<std::string>& args);
    void cmd_quit(const std::vector<std::string>& args);

    // Helper functions
    std::vector<std::string> tokenize(const std::string& cmd);
    uint32_t parse_address(const std::string& addr_str);
    void print_prompt();
    void print_breakpoint_hit(uint32_t pc, uint32_t core_id);

    // Read memory through simulator
    uint32_t read_memory_word(uint32_t addr);
    uint8_t read_memory_byte(uint32_t addr);
};
