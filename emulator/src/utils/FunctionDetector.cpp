#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <cstdint>
#include <cstdio>
#include <algorithm>

// ELF symbol type macros
#define ELF32_ST_BIND(info) ((info)>>4)
#define ELF32_ST_TYPE(info) ((info)&0xf)
#define ELF32_ST_INFO(bind, type) (((bind)<<4)+((type)&0xf))

// ELF header structure (simplified for 32-bit little-endian RISC-V)
struct Elf32_Ehdr {
    unsigned char e_ident[16];  // ELF identification
    uint16_t e_type;            // Object file type
    uint16_t e_machine;         // Architecture
    uint32_t e_version;         // Object file version
    uint32_t e_entry;           // Entry point virtual address
    uint32_t e_phoff;           // Program header table file offset
    uint32_t e_shoff;           // Section header table file offset
    uint32_t e_flags;           // Processor-specific flags
    uint16_t e_ehsize;          // ELF header size
    uint16_t e_phentsize;       // Size of program header entry
    uint16_t e_phnum;           // Number of program header entries
    uint16_t e_shentsize;       // Size of section header entry
    uint16_t e_shnum;           // Number of section header entries
    uint16_t e_shstrndx;        // Section name string table index
};

// Section header structure
struct Elf32_Shdr {
    uint32_t sh_name;      // Section name (index into string table)
    uint32_t sh_type;      // Section type
    uint32_t sh_flags;     // Section attributes
    uint32_t sh_addr;      // Virtual address in memory
    uint32_t sh_offset;    // Offset in file
    uint32_t sh_size;      // Size of section
    uint32_t sh_link;      // Link to other section
    uint32_t sh_info;      // Miscellaneous information
    uint32_t sh_addralign; // Address alignment boundary
    uint32_t sh_entsize;   // Size of entries, if section has table
};

// Symbol table entry
struct Elf32_Sym {
    uint32_t st_name;  // Symbol name (index into string table)
    uint32_t st_value; // Symbol value (address)
    uint32_t st_size;  // Symbol size
    unsigned char st_info;  // Type and binding
    unsigned char st_other; // Visibility
    uint16_t st_shndx; // Section index
};

// Function to read ELF file and extract function names
std::map<uint32_t, std::string> extract_functions_from_elf(const std::string& elf_filename) {
    std::map<uint32_t, std::string> functions;
    
    std::ifstream file(elf_filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Could not open ELF file: " << elf_filename << std::endl;
        return functions;
    }
    
    // Read ELF header
    Elf32_Ehdr ehdr;
    file.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    
    if (ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' || 
        ehdr.e_ident[2] != 'L' || ehdr.e_ident[3] != 'F') {
        std::cerr << "Error: Not a valid ELF file: " << elf_filename << std::endl;
        return functions;
    }
    
    // Read section headers
    std::vector<Elf32_Shdr> section_headers(ehdr.e_shnum);
    file.seekg(ehdr.e_shoff);
    file.read(reinterpret_cast<char*>(section_headers.data()), 
              sizeof(Elf32_Shdr) * ehdr.e_shnum);
    
    // Find the string table for section names
    std::vector<char> shstrtab(section_headers[ehdr.e_shstrndx].sh_size);
    file.seekg(section_headers[ehdr.e_shstrndx].sh_offset);
    file.read(shstrtab.data(), section_headers[ehdr.e_shstrndx].sh_size);
    
    // Find symbol table and string table sections
    uint32_t symtab_idx = 0;
    uint32_t strtab_idx = 0;
    
    for (int i = 0; i < ehdr.e_shnum; ++i) {
        const char* section_name = shstrtab.data() + section_headers[i].sh_name;
        
        if (section_headers[i].sh_type == 2) { // SHT_SYMTAB (symbol table)
            symtab_idx = i;
        } else if (section_headers[i].sh_type == 3) { // SHT_STRTAB (string table)
            // Check if this is the string table for symbols (not section names)
            if (std::string(section_name) == ".strtab") {
                strtab_idx = i;
            }
        }
    }
    
    if (symtab_idx == 0 || strtab_idx == 0) {
        std::cerr << "Warning: No symbol table found in ELF file: " << elf_filename << std::endl;
        // If no symbol table, try to load from binary as fallback
        return functions;
    }
    
    // Read symbol table
    std::vector<Elf32_Sym> symtab(section_headers[symtab_idx].sh_size / sizeof(Elf32_Sym));
    file.seekg(section_headers[symtab_idx].sh_offset);
    file.read(reinterpret_cast<char*>(symtab.data()), section_headers[symtab_idx].sh_size);
    
    // Read string table
    std::vector<char> strtab(section_headers[strtab_idx].sh_size);
    file.seekg(section_headers[strtab_idx].sh_offset);
    file.read(strtab.data(), section_headers[strtab_idx].sh_size);
    
    // Extract function symbols
    for (const auto& sym : symtab) {
        // Check if symbol is a function (STT_FUNC)
        if (ELF32_ST_TYPE(sym.st_info) == 2) { // STT_FUNC
            std::string name(strtab.data() + sym.st_name);
            
            // Filter out some common non-user functions
            if (name != "" && 
                name.substr(0, 4) != "gcc_" &&
                name.substr(0, 3) != "__c" &&
                name.substr(0, 4) != "__st" &&
                name.substr(0, 5) != "frame" &&
                name.substr(0, 4) != "__i" &&
                name.substr(0, 3) != "__v" &&
                name.substr(0, 4) != "__t") {
                
                functions[sym.st_value] = name;
            }
        }
    }
    
    file.close();
    return functions;
}

// RISC-V instruction decoding functions
uint32_t get_opcode(uint32_t instr) {
    return instr & 0x7f;
}

uint32_t get_rd(uint32_t instr) {
    return (instr >> 7) & 0x1f;
}

uint32_t get_rs1(uint32_t instr) {
    return (instr >> 15) & 0x1f;
}

uint32_t get_rs2(uint32_t instr) {
    return (instr >> 20) & 0x1f;
}

uint32_t get_funct3(uint32_t instr) {
    return (instr >> 12) & 0x7;
}

uint32_t get_funct7(uint32_t instr) {
    return (instr >> 25) & 0x7f;
}

// Function to check if an instruction is a return (JALR with rd=x0 and rs1=x1)
bool is_return_instruction(uint32_t instr) {
    uint32_t opcode = get_opcode(instr);
    if (opcode == 0x67) { // JALR
        uint32_t rd = get_rd(instr);
        uint32_t rs1 = get_rs1(instr);
        // Return is typically JALR x0, 0(x1) - jump to x1 (ra) and don't save return address
        return (rd == 0 && rs1 == 1); // rd=x0 (no return address saved), rs1=x1 (return address)
    }
    return false;
}

// Function to check if an instruction is a function entry pattern
bool is_function_entry(uint32_t instr1, uint32_t instr2) {
    // Common function entry pattern: addi sp, sp, -imm (adjust stack pointer)
    uint32_t opcode1 = get_opcode(instr1);
    uint32_t funct3_1 = get_funct3(instr1);
    uint32_t rd1 = get_rd(instr1);
    uint32_t rs1_1 = get_rs1(instr1);

    // Also check for patterns in instr2 to identify function entry
    uint32_t opcode2 = get_opcode(instr2);
    uint32_t funct3_2 = get_funct3(instr2);
    uint32_t rd2 = get_rd(instr2);
    uint32_t rs1_2 = get_rs1(instr2);

    // Check for ADDI instruction that adjusts stack pointer
    // ADDI opcode = 0x13, funct3 = 0x0
    if (opcode1 == 0x13 && funct3_1 == 0x0 && rd1 == 2 && rs1_1 == 2) { // addi sp, sp, -imm
        return true;
    }

    // Check for store instruction that saves return address (another function entry pattern)
    if (opcode2 == 0x23 && funct3_2 == 0x2 && rs1_2 == 2 && rd2 == 1) { // sw x1, offset(x2) - save ra to stack
        return true;
    }

    return false;
}

// Function to analyze a function and identify its type based on characteristics
std::string identify_function_type(const std::vector<uint8_t>& program, uint32_t start_addr) {
    // For binary analysis (when no ELF symbols are available), return a generic function name
    // The real function names should come from ELF symbols, not heuristics
    (void)program; // Suppress unused parameter warning
    char buf[16];
    snprintf(buf, sizeof(buf), "func_%x", start_addr);
    return std::string(buf);
}

// Function to detect functions in a RISC-V binary
std::map<uint32_t, std::string> detect_functions(const std::vector<uint8_t>& program) {
    std::map<uint32_t, std::string> functions;
    std::set<uint32_t> potential_function_starts;
    
    // Ensure we have enough bytes for at least one instruction
    if (program.size() < 4) {
        return functions;
    }
    
    // Scan through the program to find potential function starts
    for (size_t i = 0; i < program.size(); i += 4) {
        if (i + 7 < program.size()) { // Need at least 2 instructions
            // Read two 32-bit instructions
            uint32_t instr1 = program[i] |
                             (program[i+1] << 8) |
                             (program[i+2] << 16) |
                             (program[i+3] << 24);
            uint32_t instr2 = program[i+4] |
                             (program[i+5] << 8) |
                             (program[i+6] << 16) |
                             (program[i+7] << 24);
            
            // Check for function entry patterns
            if (is_function_entry(instr1, instr2)) {
                potential_function_starts.insert(static_cast<uint32_t>(i));
            }
        }
    }
    
    // Additional heuristic: look for return instructions followed by other instructions
    for (size_t i = 0; i < program.size(); i += 4) {
        if (i + 3 < program.size()) {
            uint32_t instr = program[i] |
                            (program[i+1] << 8) |
                            (program[i+2] << 16) |
                            (program[i+3] << 24);
            
            if (is_return_instruction(instr)) {
                // The next instruction after a return might be the start of a new function
                uint32_t next_addr = static_cast<uint32_t>(i + 4);
                if (next_addr < program.size()) {
                    potential_function_starts.insert(next_addr);
                }
            }
        }
    }
    
    // Assign function names to detected addresses based on analysis
    for (uint32_t addr : potential_function_starts) {
        functions[addr] = identify_function_type(program, addr);
    }
    
    // If no functions were detected, add a default function for the start of the program
    if (functions.empty() && !program.empty()) {
        functions[0] = "main";
    }
    
    return functions;
}

// Function to save function profiles to a file
void save_function_profiles(const std::map<uint32_t, std::string>& functions, const std::string& filename) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not create function profile file: " << filename << std::endl;
        return;
    }
    
    file << "# Automatically generated function profiles\n";
    
    // For each function, we'll assume it continues until the next function or a reasonable end point
    auto it = functions.begin();
    while (it != functions.end()) {
        uint32_t start_addr = it->first;
        std::string func_name = it->second;
        
        // Find the end address (next function start or a reasonable default)
        uint32_t end_addr;
        ++it;
        if (it != functions.end()) {
            end_addr = it->first - 4; // End just before next function
        } else {
            end_addr = start_addr + 0x100; // Default to 256 bytes if no next function
        }
        
        // Add profiles for both cores
        file << "0," << func_name << "," << std::hex << start_addr << "," << std::hex << end_addr << std::dec << "\n";
        file << "1," << func_name << "," << std::hex << start_addr << "," << std::hex << end_addr << std::dec << "\n";
    }
    
    file.close();
    std::cout << "Function profiles saved to " << filename << std::endl;
}