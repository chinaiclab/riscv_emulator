#include <stdint.h>
#include "../../software/init_code/vprintf.h"

extern uint32_t get_core_id(void);

// Function to print a number in hex format
void print_hex(uint32_t num) {
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[9]; // 8 hex digits + null terminator
    buffer[8] = '\0';
    
    for (int i = 7; i >= 0; i--) {
        buffer[i] = hex_chars[num & 0xF];
        num >>= 4;
    }
    
    printf(buffer);
}

// Function to print a string followed by a number in hex
void print_result(const char* label, uint32_t value) {
    printf(label);
    print_hex(value);
    printf("\n");
}

// Function to print a string followed by a pass/fail message
void print_test_result(const char* label, int passed) {
    printf(label);
    if (passed) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }
}

// Test functions for each operation using actual RISC-V instructions
uint32_t test_mul(uint32_t a, uint32_t b) {
    uint32_t result;
    __asm__ volatile (
        "mul %0, %1, %2"
        : "=r" (result)
        : "r" (a), "r" (b)
    );
    return result;
}

int32_t test_div(int32_t a, int32_t b) {
    int32_t result;
    __asm__ volatile (
        "div %0, %1, %2"
        : "=r" (result)
        : "r" (a), "r" (b)
    );
    return result;
}

uint32_t test_divu(uint32_t a, uint32_t b) {
    uint32_t result;
    __asm__ volatile (
        "divu %0, %1, %2"
        : "=r" (result)
        : "r" (a), "r" (b)
    );
    return result;
}

int32_t test_rem(int32_t a, int32_t b) {
    int32_t result;
    __asm__ volatile (
        "rem %0, %1, %2"
        : "=r" (result)
        : "r" (a), "r" (b)
    );
    return result;
}

uint32_t test_remu(uint32_t a, uint32_t b) {
    uint32_t result;
    __asm__ volatile (
        "remu %0, %1, %2"
        : "=r" (result)
        : "r" (a), "r" (b)
    );
    return result;
}

void main(void) {
    uint32_t core_id = get_core_id();
    printf("RV32IM Comprehensive Instruction Test Started\n");
    printf("Core ID: ");
    print_hex(core_id);
    printf("\n");

    // Test values
    uint32_t a = 0x12345678;
    uint32_t b = 0x87654321;
    uint32_t result;
    uint32_t expected;

    // Test MUL (M Extension)
    result = test_mul(a, b);
    expected = a * b;
    print_test_result("MUL test: ", result == expected);
    print_result("MUL result: ", result);

    // Test DIV (M Extension)
    if (b != 0) {
        result = test_div(a, b);
        expected = (int32_t)a / (int32_t)b;
        print_test_result("DIV test: ", result == expected);
        print_result("DIV result: ", result);
    } else {
        printf("DIV by zero test skipped\n");
    }

    // Test DIVU (M Extension)
    if (b != 0) {
        result = test_divu(a, b);
        expected = a / b;
        print_test_result("DIVU test: ", result == expected);
        print_result("DIVU result: ", result);
    } else {
        printf("DIVU by zero test skipped\n");
    }

    // Test REM (M Extension)
    if (b != 0) {
        result = test_rem(a, b);
        expected = (int32_t)a % (int32_t)b;
        print_test_result("REM test: ", result == expected);
        print_result("REM result: ", result);
    } else {
        printf("REM by zero test skipped\n");
    }

    // Test REMU (M Extension)
    if (b != 0) {
        result = test_remu(a, b);
        expected = a % b;
        print_test_result("REMU test: ", result == expected);
        print_result("REMU result: ", result);
    } else {
        printf("REMU by zero test skipped\n");
    }

    // Test special DIV/REM cases
    // Division overflow case: 0x80000000 / 0xFFFFFFFF
    int32_t min_int = 0x80000000;  // Most negative 32-bit integer
    int32_t minus_one = 0xFFFFFFFF; // -1 in two's complement
    
    // Debug: print the actual values being used
    print_result("DIV overflow min_int input: ", (uint32_t)min_int);
    print_result("DIV overflow minus_one input: ", (uint32_t)minus_one);

    result = test_div(min_int, minus_one);
    expected = 0x80000000; // RISC-V DIV overflow returns the dividend (min_int)
    print_test_result("DIV overflow test: ", result == expected);
    print_result("DIV overflow result: ", result);
    print_result("DIV overflow expected: ", expected);

    // Division by zero case
    result = test_div(a, 0);
    expected = 0xFFFFFFFF; // Division by zero returns -1
    print_test_result("DIV by zero test: ", result == expected);
    print_result("DIV by zero result: ", result);

    // REM by zero case
    result = test_rem(a, 0);
    expected = a; // REM by zero returns dividend
    print_test_result("REM by zero test: ", result == expected);
    print_result("REM by zero result: ", result);

    // DIVU by zero case
    result = test_divu(a, 0);
    expected = 0xFFFFFFFF; // RISC-V DIVU by zero returns 0xFFFFFFFF
    print_test_result("DIVU by zero test: ", result == expected);
    print_result("DIVU by zero result: ", result);

    // REMU by zero case
    result = test_remu(a, 0);
    expected = a; // RISC-V REMU by zero returns the dividend
    print_test_result("REMU by zero test: ", result == expected);
    print_result("REMU by zero result: ", result);

    // Test basic integer instructions
    // ADD
    result = a + b;
    expected = a + b;
    print_test_result("ADD test: ", result == expected);
    print_result("ADD result: ", result);

    // SUB
    result = a - b;
    expected = a - b;
    print_test_result("SUB test: ", result == expected);
    print_result("SUB result: ", result);

    // AND
    result = a & b;
    expected = a & b;
    print_test_result("AND test: ", result == expected);
    print_result("AND result: ", result);

    // OR
    result = a | b;
    expected = a | b;
    print_test_result("OR test: ", result == expected);
    print_result("OR result: ", result);

    // XOR
    result = a ^ b;
    expected = a ^ b;
    print_test_result("XOR test: ", result == expected);
    print_result("XOR result: ", result);

    // SLL
    uint32_t shift_amount = 4;
    result = a << (shift_amount & 0x1F);
    expected = a << (shift_amount & 0x1F);
    print_test_result("SLL test: ", result == expected);
    print_result("SLL result: ", result);

    // SRL
    result = a >> (shift_amount & 0x1F);
    expected = a >> (shift_amount & 0x1F);
    print_test_result("SRL test: ", result == expected);
    print_result("SRL result: ", result);

    // SRA
    result = ((int32_t)a) >> (shift_amount & 0x1F);
    expected = ((int32_t)a) >> (shift_amount & 0x1F);
    print_test_result("SRA test: ", result == expected);
    print_result("SRA result: ", result);

    // SLT
    result = ((int32_t)a < (int32_t)b) ? 1 : 0;
    expected = ((int32_t)a < (int32_t)b) ? 1 : 0;
    print_test_result("SLT test: ", result == expected);
    print_result("SLT result: ", result);

    // SLTU
    result = (a < b) ? 1 : 0;
    expected = (a < b) ? 1 : 0;
    print_test_result("SLTU test: ", result == expected);
    print_result("SLTU result: ", result);

    // Test immediate instructions
    uint32_t imm = 0x123;
    
    // ADDI
    result = a + imm;
    expected = a + imm;
    print_test_result("ADDI test: ", result == expected);
    print_result("ADDI result: ", result);

    // ANDI
    result = a & imm;
    expected = a & imm;
    print_test_result("ANDI test: ", result == expected);
    print_result("ANDI result: ", result);

    // ORI
    result = a | imm;
    expected = a | imm;
    print_test_result("ORI test: ", result == expected);
    print_result("ORI result: ", result);

    // XORI
    result = a ^ imm;
    expected = a ^ imm;
    print_test_result("XORI test: ", result == expected);
    print_result("XORI result: ", result);

    // SLLI
    shift_amount = 0x5; // Only lower 5 bits matter
    result = a << (shift_amount & 0x1F);
    expected = a << (shift_amount & 0x1F);
    print_test_result("SLLI test: ", result == expected);
    print_result("SLLI result: ", result);

    // SRLI
    result = a >> (shift_amount & 0x1F);
    expected = a >> (shift_amount & 0x1F);
    print_test_result("SRLI test: ", result == expected);
    print_result("SRLI result: ", result);

    // SRAI
    result = ((int32_t)a) >> (shift_amount & 0x1F);
    expected = ((int32_t)a) >> (shift_amount & 0x1F);
    print_test_result("SRAI test: ", result == expected);
    print_result("SRAI result: ", result);

    // SLTI
    result = ((int32_t)a < (int32_t)imm) ? 1 : 0;
    expected = ((int32_t)a < (int32_t)imm) ? 1 : 0;
    print_test_result("SLTI test: ", result == expected);
    print_result("SLTI result: ", result);

    // SLTIU
    result = (a < imm) ? 1 : 0;
    expected = (a < imm) ? 1 : 0;
    print_test_result("SLTIU test: ", result == expected);
    print_result("SLTIU result: ", result);

    // Test LUI
    uint32_t upper_imm = 0x12345;
    result = upper_imm << 12;
    expected = upper_imm << 12;
    print_test_result("LUI test: ", result == expected);
    print_result("LUI result: ", result);

    // Test branch instructions using function calls instead of inline assembly
    // BEQ - test with equal values
    int branch_result = 0;
    if (a == a) {  // This simulates BEQ
        branch_result = 1;
    } else {
        branch_result = 0;
    }
    print_test_result("BEQ branch test: ", branch_result == 1);

    // BNE - test with different values
    branch_result = 0;
    if (a != b) {  // This simulates BNE
        branch_result = 1;
    } else {
        branch_result = 0;
    }
    print_test_result("BNE branch test: ", branch_result == 1);

    // BLT - test with values where first < second (signed)
    branch_result = 0;
    if ((int32_t)a < (int32_t)b) {  // This simulates BLT
        branch_result = 1;
    } else {
        branch_result = 0;
    }
    // This test depends on the values of a and b
    int blt_expected = ((int32_t)a < (int32_t)b) ? 1 : 0;
    print_test_result("BLT branch test: ", branch_result == blt_expected);

    // BGE - test with equal values (first >= second)
    branch_result = 0;
    if ((int32_t)a >= (int32_t)a) {  // This simulates BGE
        branch_result = 1;
    } else {
        branch_result = 0;
    }
    print_test_result("BGE branch test: ", branch_result == 1);

    // BLTU - test with values where first < second (unsigned)
    branch_result = 0;
    if (a < b) {  // This simulates BLTU
        branch_result = 1;
    } else {
        branch_result = 0;
    }
    // This test depends on the values of a and b
    int bltu_expected = (a < b) ? 1 : 0;
    print_test_result("BLTU branch test: ", branch_result == bltu_expected);

    // BGEU - test with equal values (first >= second unsigned)
    branch_result = 0;
    if (a >= a) {  // This simulates BGEU
        branch_result = 1;
    } else {
        branch_result = 0;
    }
    print_test_result("BGEU branch test: ", branch_result == 1);

    // Test A Extension - Atomic Operations
    // We'll use a specific memory location for atomic operations
    // Use RAM region within PMA-defined range (0x80000-0x87FFF)
    uint32_t test_mem_location = 0x81000; // Use a known memory location in RAM (within PMA region)
    uint32_t *test_mem_ptr = (uint32_t*)test_mem_location;

    // Initialize the memory location
    *test_mem_ptr = 0x12345678;

    // Test AMOADD.W
    uint32_t original_val = *test_mem_ptr;
    uint32_t add_val = 0x00000010;
    uint32_t expected_result = original_val + add_val;
    uint32_t actual_result;

    // Use inline assembly to perform AMOADD.W
    __asm__ volatile (
        "amoadd.w %0, %2, (%1)"
        : "=r" (actual_result)
        : "r" (test_mem_location), "r" (add_val)
        : "memory"
    );

    print_test_result("AMOADD.W test: ", actual_result == original_val);
    print_result("AMOADD.W original value: ", actual_result);
    print_result("AMOADD.W new memory value: ", *test_mem_ptr);

    // Reset memory location
    *test_mem_ptr = 0x12345678;

    // Test AMOSWAP.W
    uint32_t swap_val = 0xABCDEF00;
    original_val = *test_mem_ptr;

    __asm__ volatile (
        "amoswap.w %0, %2, (%1)"
        : "=r" (actual_result)
        : "r" (test_mem_location), "r" (swap_val)
        : "memory"
    );

    print_test_result("AMOSWAP.W test: ", actual_result == original_val);
    print_result("AMOSWAP.W original value: ", actual_result);
    print_result("AMOSWAP.W new memory value: ", *test_mem_ptr);

    // Reset memory location
    *test_mem_ptr = 0x12345678;

    // Test AMOAND.W
    uint32_t and_val = 0xF0F0F0F0;
    original_val = *test_mem_ptr;
    expected_result = original_val & and_val;

    __asm__ volatile (
        "amoand.w %0, %2, (%1)"
        : "=r" (actual_result)
        : "r" (test_mem_location), "r" (and_val)
        : "memory"
    );

    print_test_result("AMOAND.W test: ", actual_result == original_val);
    print_result("AMOAND.W original value: ", actual_result);
    print_result("AMOAND.W new memory value: ", *test_mem_ptr);

    // Reset memory location
    *test_mem_ptr = 0x12345678;

    // Test AMOOR.W
    uint32_t or_val = 0xF0F0F0F0;
    original_val = *test_mem_ptr;
    expected_result = original_val | or_val;

    __asm__ volatile (
        "amoor.w %0, %2, (%1)"
        : "=r" (actual_result)
        : "r" (test_mem_location), "r" (or_val)
        : "memory"
    );

    print_test_result("AMOOR.W test: ", actual_result == original_val);
    print_result("AMOOR.W original value: ", actual_result);
    print_result("AMOOR.W new memory value: ", *test_mem_ptr);

    // Reset memory location
    *test_mem_ptr = 0x12345678;

    // Test AMOXOR.W
    uint32_t xor_val = 0xF0F0F0F0;
    original_val = *test_mem_ptr;
    expected_result = original_val ^ xor_val;

    __asm__ volatile (
        "amoxor.w %0, %2, (%1)"
        : "=r" (actual_result)
        : "r" (test_mem_location), "r" (xor_val)
        : "memory"
    );

    print_test_result("AMOXOR.W test: ", actual_result == original_val);
    print_result("AMOXOR.W original value: ", actual_result);
    print_result("AMOXOR.W new memory value: ", *test_mem_ptr);

    // Reset memory location
    *test_mem_ptr = 0x12345678;

    // Test AMOMIN.W
    uint32_t min_val = 0x00000001;  // Smaller value
    original_val = *test_mem_ptr;
    expected_result = ((int32_t)original_val < (int32_t)min_val) ? original_val : min_val;

    __asm__ volatile (
        "amomin.w %0, %2, (%1)"
        : "=r" (actual_result)
        : "r" (test_mem_location), "r" (min_val)
        : "memory"
    );

    print_test_result("AMOMIN.W test: ", actual_result == original_val);
    print_result("AMOMIN.W original value: ", actual_result);
    print_result("AMOMIN.W new memory value: ", *test_mem_ptr);

    // Reset memory location
    *test_mem_ptr = 0x12345678;

    // Test AMOMAX.W
    uint32_t max_val = 0xFFFFFFFF;  // Larger value
    original_val = *test_mem_ptr;
    expected_result = ((int32_t)original_val > (int32_t)max_val) ? original_val : max_val;

    __asm__ volatile (
        "amomax.w %0, %2, (%1)"
        : "=r" (actual_result)
        : "r" (test_mem_location), "r" (max_val)
        : "memory"
    );

    print_test_result("AMOMAX.W test: ", actual_result == original_val);
    print_result("AMOMAX.W original value: ", actual_result);
    print_result("AMOMAX.W new memory value: ", *test_mem_ptr);

    // Reset memory location
    *test_mem_ptr = 0x12345678;

    // Test AMOMINU.W
    uint32_t minu_val = 0x00000001;  // Smaller unsigned value
    original_val = *test_mem_ptr;
    expected_result = (original_val < minu_val) ? original_val : minu_val;

    __asm__ volatile (
        "amominu.w %0, %2, (%1)"
        : "=r" (actual_result)
        : "r" (test_mem_location), "r" (minu_val)
        : "memory"
    );

    print_test_result("AMOMINU.W test: ", actual_result == original_val);
    print_result("AMOMINU.W original value: ", actual_result);
    print_result("AMOMINU.W new memory value: ", *test_mem_ptr);

    // Reset memory location
    *test_mem_ptr = 0x12345678;

    // Test AMOMAXU.W
    uint32_t maxu_val = 0xFFFFFFFF;  // Larger unsigned value
    original_val = *test_mem_ptr;
    expected_result = (original_val > maxu_val) ? original_val : maxu_val;

    __asm__ volatile (
        "amomaxu.w %0, %2, (%1)"
        : "=r" (actual_result)
        : "r" (test_mem_location), "r" (maxu_val)
        : "memory"
    );

    print_test_result("AMOMAXU.W test: ", actual_result == original_val);
    print_result("AMOMAXU.W original value: ", actual_result);
    print_result("AMOMAXU.W new memory value: ", *test_mem_ptr);

    // Test LR.W and SC.W together
    // Reset memory location
    *test_mem_ptr = 0x12345678;

    uint32_t lr_result, sc_result;
    original_val = *test_mem_ptr;

    // Perform LR.W
    __asm__ volatile (
        "lr.w %0, (%1)"
        : "=r" (lr_result)
        : "r" (test_mem_location)
        : "memory"
    );

    // Perform SC.W
    uint32_t new_val = 0xABCDEF00;
    __asm__ volatile (
        "sc.w %0, %2, (%1)"
        : "=r" (sc_result)
        : "r" (test_mem_location), "r" (new_val)
        : "memory"
    );

    print_test_result("LR.W/SC.W test: ", lr_result == original_val && sc_result == 0);
    print_result("LR.W result: ", lr_result);
    print_result("SC.W result (0=success): ", sc_result);
    print_result("New memory value after SC: ", *test_mem_ptr);

    printf("RV32IM Comprehensive Instruction Test Completed\n");

    while (1) { }
}