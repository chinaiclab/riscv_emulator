#include "../include/debug/Disassembler.h"
#include <sstream>
#include <iomanip>
#include <unordered_map>

// Register name mapping
std::string Disassembler::reg_name(uint32_t reg_num) {
    static const char* reg_names[] = {
        "zero", "ra", "sp", "gp", "tp",   // x0-x4
        "t0", "t1", "t2",                  // x5-x7
        "s0/fp", "s1",                     // x8-x9
        "a0", "a1", "a2", "a3", "a4", "a5", // x10-x15
        "a6", "a7",                         // x16-x17
        "s2", "s3", "s4", "s5", "s6", "s7", "s8", "s9", "s10", "s11", // x18-x27
        "t3", "t4", "t5", "t6"              // x28-x31
    };
    if (reg_num < 32) {
        return reg_names[reg_num];
    }
    return "x" + std::to_string(reg_num);
}

// Sign extend I-type immediate
int32_t Disassembler::sign_extend_i_type(uint32_t instr) {
    int16_t imm = (instr >> 20) & 0xFFF;
    return static_cast<int32_t>(imm);
}

// Sign extend S-type immediate
int32_t Disassembler::sign_extend_s_type(uint32_t instr) {
    int16_t imm = ((instr >> 7) & 0x1F) | ((instr >> 25) << 5);
    return static_cast<int32_t>(imm);
}

// Sign extend B-type immediate
int32_t Disassembler::sign_extend_b_type(uint32_t instr) {
    int16_t imm = ((instr >> 8) & 0xF) | ((instr >> 25) & 0x3F) << 4 |
                  ((instr >> 7) & 0x1) << 10 | ((instr >> 31) & 0x1) << 11;
    return static_cast<int32_t>(imm << 4) >> 3;  // Sign extend and scale
}

// Sign extend U-type immediate
int32_t Disassembler::sign_extend_u_type(uint32_t instr) {
    return static_cast<int32_t>(instr & 0xFFFFF000);
}

// Sign extend J-type immediate
int32_t Disassembler::sign_extend_j_type(uint32_t instr) {
    int32_t imm = ((instr >> 21) & 0x3FF) | ((instr >> 20) & 0x1) << 10 |
                  ((instr >> 12) & 0xFF) << 11 | ((instr >> 31) & 0x1) << 19;
    return (imm << 12) >> 11;  // Sign extend and scale
}

// Decode R-type instruction
std::string Disassembler::decode_r_type(uint32_t instr, const std::string& mnemonic) {
    std::ostringstream oss;
    oss << mnemonic << " " << reg_name(get_rd(instr)) << ", "
        << reg_name(get_rs1(instr)) << ", " << reg_name(get_rs2(instr));
    return oss.str();
}

// Decode I-type instruction
std::string Disassembler::decode_i_type(uint32_t instr, const std::string& mnemonic) {
    std::ostringstream oss;
    int32_t imm = sign_extend_i_type(instr);
    oss << mnemonic << " " << reg_name(get_rd(instr)) << ", "
        << reg_name(get_rs1(instr)) << ", " << imm;
    return oss.str();
}

// Decode S-type instruction
std::string Disassembler::decode_s_type(uint32_t instr, const std::string& mnemonic) {
    std::ostringstream oss;
    int32_t imm = sign_extend_s_type(instr);
    oss << mnemonic << " " << reg_name(get_rs2(instr)) << ", "
        << imm << "(" << reg_name(get_rs1(instr)) << ")";
    return oss.str();
}

// Decode B-type instruction
std::string Disassembler::decode_b_type(uint32_t instr, const std::string& mnemonic, uint32_t pc) {
    std::ostringstream oss;
    int32_t imm = sign_extend_b_type(instr);
    uint32_t target = pc + imm;
    oss << mnemonic << " " << reg_name(get_rs1(instr)) << ", "
        << reg_name(get_rs2(instr)) << ", 0x" << std::hex << target << std::dec;
    return oss.str();
}

// Decode U-type instruction
std::string Disassembler::decode_u_type(uint32_t instr, const std::string& mnemonic, uint32_t pc) {
    std::ostringstream oss;
    int32_t imm = sign_extend_u_type(instr);
    uint32_t target = pc + imm;
    oss << mnemonic << " " << reg_name(get_rd(instr)) << ", 0x"
        << std::hex << target << std::dec;
    return oss.str();
}

// Decode J-type instruction
std::string Disassembler::decode_j_type(uint32_t instr, const std::string& mnemonic, uint32_t pc) {
    std::ostringstream oss;
    int32_t imm = sign_extend_j_type(instr);
    uint32_t target = pc + imm;
    oss << mnemonic << " " << reg_name(get_rd(instr)) << ", 0x"
        << std::hex << target << std::dec;
    return oss.str();
}

// Main disassemble function
std::string Disassembler::disassemble(uint32_t instr, uint32_t pc) {
    uint32_t opcode = get_opcode(instr);
    uint32_t funct3 = get_funct3(instr);
    uint32_t funct7 = get_funct7(instr);

    switch (opcode) {
        // LUI (Upper Immediate)
        case 0x37:
            return decode_u_type(instr, "lui", pc);

        // AUIPC (Add Upper Immediate to PC)
        case 0x17:
            return decode_u_type(instr, "auipc", pc);

        // JAL (Jump and Link)
        case 0x6F:
            return decode_j_type(instr, "jal", pc);

        // JALR (Jump and Link Register)
        case 0x67:
            if (funct3 == 0x0) {
                int32_t imm = sign_extend_i_type(instr);
                std::ostringstream oss;
                oss << "jalr " << reg_name(get_rd(instr)) << ", "
                    << imm << "(" << reg_name(get_rs1(instr)) << ")";
                return oss.str();
            }
            break;

        // Branch instructions
        case 0x63:
            switch (funct3) {
                case 0x0: return decode_b_type(instr, "beq", pc);
                case 0x1: return decode_b_type(instr, "bne", pc);
                case 0x4: return decode_b_type(instr, "blt", pc);
                case 0x5: return decode_b_type(instr, "bge", pc);
                case 0x6: return decode_b_type(instr, "bltu", pc);
                case 0x7: return decode_b_type(instr, "bgeu", pc);
            }
            break;

        // Load instructions
        case 0x03:
            switch (funct3) {
                case 0x0: return decode_i_type(instr, "lb");
                case 0x1: return decode_i_type(instr, "lh");
                case 0x2: return decode_i_type(instr, "lw");
                case 0x4: return decode_i_type(instr, "lbu");
                case 0x5: return decode_i_type(instr, "lhu");
            }
            break;

        // Store instructions
        case 0x23:
            switch (funct3) {
                case 0x0: return decode_s_type(instr, "sb");
                case 0x1: return decode_s_type(instr, "sh");
                case 0x2: return decode_s_type(instr, "sw");
            }
            break;

        // OP (Register-Register operations)
        case 0x33:
            if (funct7 == 0x00) {
                switch (funct3) {
                    case 0x0: return decode_r_type(instr, "add");
                    case 0x1: return decode_r_type(instr, "sll");
                    case 0x2: return decode_r_type(instr, "slt");
                    case 0x3: return decode_r_type(instr, "sltu");
                    case 0x4: return decode_r_type(instr, "xor");
                    case 0x5: return decode_r_type(instr, "srl");
                    case 0x6: return decode_r_type(instr, "or");
                    case 0x7: return decode_r_type(instr, "and");
                }
            } else if (funct7 == 0x01) {
                // M extension instructions
                switch (funct3) {
                    case 0x0: return decode_r_type(instr, "mul");
                    case 0x1: return decode_r_type(instr, "mulh");
                    case 0x2: return decode_r_type(instr, "mulhsu");
                    case 0x3: return decode_r_type(instr, "mulhu");
                    case 0x4: return decode_r_type(instr, "div");
                    case 0x5: return decode_r_type(instr, "divu");
                    case 0x6: return decode_r_type(instr, "rem");
                    case 0x7: return decode_r_type(instr, "remu");
                }
            } else if (funct7 == 0x20) {
                switch (funct3) {
                    case 0x0: return decode_r_type(instr, "sub");
                    case 0x5: return decode_r_type(instr, "sra");
                }
            }
            break;

        // OP-IMM (Immediate operations)
        case 0x13:
            switch (funct3) {
                case 0x0: return decode_i_type(instr, "addi");
                case 0x1: return decode_i_type(instr, "slli");
                case 0x2: return decode_i_type(instr, "slti");
                case 0x3: return decode_i_type(instr, "sltiu");
                case 0x4: return decode_i_type(instr, "xori");
                case 0x5:
                    if (funct7 == 0x00) return decode_i_type(instr, "srli");
                    if (funct7 == 0x20) return decode_i_type(instr, "srai");
                    break;
                case 0x6: return decode_i_type(instr, "ori");
                case 0x7: return decode_i_type(instr, "andi");
            }
            break;

        // FENCE (memory ordering)
        case 0x0F:
            if (funct3 == 0x0) return "fence";
            break;

        // ECALL / EBREAK
        case 0x73:
            if ((instr & 0xFFFFFFFF) == 0x00000073) return "ecall";
            if ((instr & 0xFFFFFFFF) == 0x00100073) return "ebreak";
            break;

        // Atomic operations (A extension)
        case 0x2F:
            switch (funct3) {
                case 0x2:
                    if (funct7 == 0x00) return decode_r_type(instr, "lr.w");
                    if ((funct7 & 0x3F) == 0x02) {
                        uint32_t aq = (funct7 >> 6) & 0x1;
                        uint32_t rl = (funct7 >> 5) & 0x1;
                        std::string suffix;
                        if (aq) suffix += ".aq";
                        if (rl) suffix += ".rl";
                        return decode_r_type(instr, "sc.w" + suffix);
                    }
                    if ((funct7 & 0x3F) == 0x01) {
                        uint32_t aq = (funct7 >> 6) & 0x1;
                        uint32_t rl = (funct7 >> 5) & 0x1;
                        std::string suffix;
                        if (aq) suffix += ".aq";
                        if (rl) suffix += ".rl";
                        return decode_r_type(instr, "amoswap.w" + suffix);
                    }
                    if ((funct7 & 0x3F) == 0x00) {
                        uint32_t aq = (funct7 >> 6) & 0x1;
                        uint32_t rl = (funct7 >> 5) & 0x1;
                        std::string suffix;
                        if (aq) suffix += ".aq";
                        if (rl) suffix += ".rl";
                        return decode_r_type(instr, "amoadd.w" + suffix);
                    }
                    break;
            }
            break;
    }

    // Unknown instruction
    std::ostringstream oss;
    oss << "unknown 0x" << std::hex << std::setfill('0') << std::setw(8) << instr << std::dec;
    return oss.str();
}

// Format instruction with full details
std::string Disassembler::format_instruction(uint32_t instr, uint32_t pc, const std::string& mnemonic) {
    return disassemble(instr, pc);
}
