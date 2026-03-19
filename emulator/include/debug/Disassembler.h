#pragma once

#include <cstdint>
#include <string>

class Disassembler {
public:
    // Disassemble a single instruction
    static std::string disassemble(uint32_t instr, uint32_t pc);

    // Format instruction with operands
    static std::string format_instruction(uint32_t instr, uint32_t pc, const std::string& mnemonic);

private:
    // Helper functions for decoding different instruction formats
    static std::string decode_r_type(uint32_t instr, const std::string& mnemonic);
    static std::string decode_i_type(uint32_t instr, const std::string& mnemonic);
    static std::string decode_s_type(uint32_t instr, const std::string& mnemonic);
    static std::string decode_b_type(uint32_t instr, const std::string& mnemonic, uint32_t pc);
    static std::string decode_u_type(uint32_t instr, const std::string& mnemonic, uint32_t pc);
    static std::string decode_j_type(uint32_t instr, const std::string& mnemonic, uint32_t pc);

    // Extract fields from instruction
    static uint32_t get_opcode(uint32_t instr) { return instr & 0x7F; }
    static uint32_t get_rd(uint32_t instr) { return (instr >> 7) & 0x1F; }
    static uint32_t get_funct3(uint32_t instr) { return (instr >> 12) & 0x7; }
    static uint32_t get_rs1(uint32_t instr) { return (instr >> 15) & 0x1F; }
    static uint32_t get_rs2(uint32_t instr) { return (instr >> 20) & 0x1F; }
    static uint32_t get_funct7(uint32_t instr) { return (instr >> 25) & 0x7F; }

    // Sign extend helpers
    static int32_t sign_extend_i_type(uint32_t instr);
    static int32_t sign_extend_s_type(uint32_t instr);
    static int32_t sign_extend_b_type(uint32_t instr);
    static int32_t sign_extend_u_type(uint32_t instr);
    static int32_t sign_extend_j_type(uint32_t instr);

    // Register name helper
    static std::string reg_name(uint32_t reg_num);
};
