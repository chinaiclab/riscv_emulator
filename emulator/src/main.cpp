#include "../include/system/Simulator.h"
#include "../include/device/UART.h"
#include "../include/utils/FunctionDetector.h"
#include "../include/utils/DebugLogger.h"
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <csignal>
#include <unistd.h>

// Global flag for timeout handling
static volatile bool timeout_triggered = false;

// Signal handler for timeout
void timeout_handler(int signal) {
    if (signal == SIGALRM) {
        timeout_triggered = true;
        std::cerr << "\n\n[TIMEOUT] Simulation exceeded maximum time limit!" << std::endl;
        std::cerr << "[TIMEOUT] This indicates a potential infinite loop in the emulator." << std::endl;
    }
}

int main(int argc, char **argv) {
#ifdef DEBUG
    // Initialize debug logger to separate debug output from program output
    DebugLogger::getInstance().setLogFile("debug_output.log");
    DEBUG_LOG("=== RISC-V Simulator Debug Log ===\n");
    DEBUG_LOG("Debug logging enabled\n");
#endif

    // Default configuration
    uint32_t num_cores = 2;  // Changed default to 2 cores
    bool trace = false;
    bool profiling = false;
    bool func_profiling = false;
    bool debug_mode = false;
    const char* func_profile_file = nullptr;
    uint32_t cycles = 1000; // default cycles
    const char* program_name = "hello_rv32"; // default program
    uint32_t memory_size = 0x200000; // Default 2MB memory (to support larger payloads)

    // Parse command-line options
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--trace") == 0) {
            trace = true;
        } else if (std::strcmp(argv[i], "--debug") == 0) {
            debug_mode = true;
        } else if (std::strcmp(argv[i], "--profiling") == 0) {
            profiling = true;
        } else if (std::strcmp(argv[i], "--func-profiling") == 0) {
            func_profiling = true;
        } else if (std::strcmp(argv[i], "--func-profile-file") == 0 && i + 1 < argc) {
            func_profile_file = argv[++i];
        } else if (std::strcmp(argv[i], "--cores") == 0 && i + 1 < argc) {
            num_cores = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--cycles") == 0 && i + 1 < argc) {
            cycles = static_cast<uint32_t>(std::stoi(argv[++i]));
        } else if (std::strcmp(argv[i], "--memory") == 0 && i + 1 < argc) {
            // Parse memory size (supports decimal, hex with 0x prefix, or K/M/G suffix)
            std::string mem_str = argv[++i];
            size_t mem_val = std::stoull(mem_str, nullptr, 0);
            // Check for K/M/G suffix
            if (mem_str.find('K') != std::string::npos || mem_str.find('k') != std::string::npos) {
                mem_val *= 1024;
            } else if (mem_str.find('M') != std::string::npos || mem_str.find('m') != std::string::npos) {
                mem_val *= 1024 * 1024;
            } else if (mem_str.find('G') != std::string::npos || mem_str.find('g') != std::string::npos) {
                mem_val *= 1024 * 1024 * 1024;
            }
            memory_size = static_cast<uint32_t>(mem_val);
        } else if (std::strcmp(argv[i], "--program") == 0 && i + 1 < argc) {
            program_name = argv[++i];
        } else if (argv[i][0] != '-') {
            // Positional argument: treat as program name if no cores specified yet
            // or if cores and cycles are already specified
            if (i + 2 < argc && argv[i+1][0] != '-' && argv[i+2][0] != '-') {
                // Format: program_name cores cycles
                program_name = argv[i];
                num_cores = static_cast<uint32_t>(std::stoi(argv[++i]));
                cycles = static_cast<uint32_t>(std::stoi(argv[++i]));
            } else if (i + 1 < argc && argv[i+1][0] != '-') {
                // Format: program_name cores (use default cycles)
                program_name = argv[i];
                num_cores = static_cast<uint32_t>(std::stoi(argv[++i]));
            } else {
                // Format: program_name (use default cores and cycles)
                program_name = argv[i];
            }
        }
    }

    // Create simulator with configured core count and memory size
    Simulator sim(num_cores, memory_size);
    // Initialize callbacks after the simulator object is fully constructed
    sim.initialize_callbacks();
    if (trace) sim.set_trace(true);
    if (debug_mode) sim.set_debug_mode(true);
    if (profiling) sim.set_profiling(true);
    if (func_profiling) sim.set_function_profiling(true);

    // Load the specified program into memory at address 0
    bool program_loaded = sim.load_program(program_name);

    if (!program_loaded) {
        std::cerr << "Error: Failed to load program: " << program_name << std::endl;
        return 1;
    }

    // If function profiling is enabled and a profile file is provided, load function profiles
    if (func_profiling && func_profile_file) {
        std::ifstream file(func_profile_file);
        if (file.is_open()) {
            std::string line;
            // Skip comment lines and read function profiles
            while (std::getline(file, line)) {
                if (line.empty() || line[0] == '#') continue; // Skip empty lines and comments

                std::istringstream iss(line);
                std::string core_id_str, func_name, start_addr_str, end_addr_str;
                if (std::getline(iss, core_id_str, ',') &&
                    std::getline(iss, func_name, ',') &&
                    std::getline(iss, start_addr_str, ',') &&
                    std::getline(iss, end_addr_str)) {

                    uint32_t core_id = std::stoi(core_id_str);
                    uint64_t start_addr = std::stoull(start_addr_str, nullptr, 16); // Parse as hex
                    uint64_t end_addr = std::stoull(end_addr_str, nullptr, 16); // Parse as hex

                    sim.add_function_profile(core_id, func_name, start_addr, end_addr);
                }
            }
            file.close();
        } else {
            std::cerr << "Warning: Could not open function profile file: " << func_profile_file << std::endl;
        }
    }
    // If no function profile file is provided but function profiling is enabled,
    // try to automatically detect functions from the loaded program
    else if (func_profiling && program_loaded) {
        // Try to extract function names from the ELF file first
        std::string elf_filename = std::string(program_name) + ".elf";
        auto functions_from_elf = extract_functions_from_elf(elf_filename);

        if (!functions_from_elf.empty()) {
            std::cout << "Extracted " << functions_from_elf.size() << " functions from ELF file:" << std::endl;
            for (const auto& func : functions_from_elf) {
                std::cout << "  " << func.second << " at 0x" << std::hex << func.first << std::dec << std::endl;
                // Add the function profile for both cores
                // Assume each function is 256 bytes or until the next function
                uint64_t end_addr = func.first + 0x100; // Default to 256 bytes
                auto next_func_it = std::find_if(functions_from_elf.begin(), functions_from_elf.end(),
                    [func](const std::pair<uint32_t, std::string>& f) { return f.first > func.first; });
                if (next_func_it != functions_from_elf.end()) {
                    end_addr = std::min<uint64_t>(end_addr, next_func_it->first);
                }

                // Add the function profile for all cores
                for (uint32_t core_id = 0; core_id < num_cores; core_id++) {
                    sim.add_function_profile(core_id, func.second, func.first, end_addr);
                }
            }
        } else {
            // Fallback to binary analysis if no ELF symbols found
            // Get the loaded program data from simulator memory
            std::vector<uint8_t> program_data(sim.get_memory_ptr(),
                                             sim.get_memory_ptr() + sim.get_memory_size());

            // Find the actual program size by looking for the end of valid instructions
            size_t actual_size = 0;
            for (size_t i = 0; i < sim.get_memory_size(); i += 4) {
                // Check if this is a valid instruction (not all zeros or 0xFF)
                uint32_t word = program_data[i] |
                               (program_data[i+1] << 8) |
                               (program_data[i+2] << 16) |
                               (program_data[i+3] << 24);
                if (word != 0 && word != 0xFFFFFFFF) {
                    actual_size = i + 4;
                } else if (actual_size != 0) {
                    // If we've seen valid instructions and now see padding, stop
                    break;
                }
            }

            if (actual_size > 0) {
                program_data.resize(actual_size);
                auto detected_functions = detect_functions(program_data);

                if (!detected_functions.empty()) {
                    std::cout << "Automatically detected " << detected_functions.size() << " functions:" << std::endl;
                    for (const auto& func : detected_functions) {
                        std::cout << "  " << func.second << " at 0x" << std::hex << func.first << std::dec << std::endl;
                        // Add the function profile for all cores
                        for (uint32_t core_id = 0; core_id < num_cores; core_id++) {
                            sim.add_function_profile(core_id, func.second, func.first, func.first + 0x100); // Assume 256-byte functions
                        }
                    }
                } else {
                    std::cout << "No functions automatically detected in the program." << std::endl;
                    // Add a default function profile for the start of the program
                    for (uint32_t core_id = 0; core_id < num_cores; core_id++) {
                        sim.add_function_profile(core_id, "main", 0x0, 0x1000);
                    }
                }
            } else {
                std::cout << "Could not determine program size for function detection." << std::endl;
                // Add a default function profile for the start of the program
                for (uint32_t core_id = 0; core_id < num_cores; core_id++) {
                    sim.add_function_profile(core_id, "main", 0x0, 0x1000);
                }
            }
        }
    }

    // Run the simulation (will execute the specified program)
    if (debug_mode) {
        // Enter interactive debugging mode (no timeout for interactive debugging)
        sim.enter_debug_loop();
    } else {
        // Set up timeout mechanism (300 seconds for safety) only in non-debug mode
        timeout_triggered = false;
        signal(SIGALRM, timeout_handler);
        alarm(300);  // Set 300-second timeout

        sim.run(cycles);

        // Cancel the timeout alarm if simulation completed normally
        alarm(0);
        signal(SIGALRM, SIG_DFL);

        // Check if timeout was triggered
        if (timeout_triggered) {
            std::cerr << "[ERROR] Simulation failed to complete within timeout limit!" << std::endl;
            std::cerr << "[INFO] This suggests an infinite loop in the multi-core execution logic." << std::endl;
            return 1;
        }
    }

    // Print profiling results if enabled
    if (profiling) {
        sim.print_profiling_results();
        sim.print_cache_stats();
        sim.print_mmu_stats();
    }

    // Print function profiling results if enabled
    if (func_profiling) {
        sim.print_function_profiling_results();
    }

    return 0;
}