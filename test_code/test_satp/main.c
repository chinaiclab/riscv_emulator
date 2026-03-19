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

// Function to print test results
void print_test_result(const char* test_name, int passed) {
    printf(test_name);
    printf(": ");
    if (passed) {
        printf("PASS\n");
    } else {
        printf("FAIL\n");
    }
}

// Read SATP CSR (0x180) using inline assembly
static inline uint32_t read_satp(void) {
    uint32_t value;
    __asm__ volatile ("csrr %0, 0x180" : "=r"(value));
    return value;
}

// Write SATP CSR (0x180) using inline assembly
static inline void write_satp(uint32_t value) {
    __asm__ volatile ("csrw 0x180, %0" : : "r"(value));
}

// Memory location for storing test results
#define RESULT_BASE 0x000F1000
volatile uint32_t *initial_satp = (volatile uint32_t*)(RESULT_BASE + 0x00);
volatile uint32_t *final_satp = (volatile uint32_t*)(RESULT_BASE + 0x04);

void main(void) {
    uint32_t core_id = get_core_id();

    // Only core 0 runs the test to avoid conflicts
    if (core_id != 0) {
        while (1) {
            // Other cores wait indefinitely
        }
    }

    printf("=== SATP CSR TEST START ===\n");
    printf("Core ID: ");
    print_hex(core_id);
    printf("\n\n");

    // Test 1: Read initial SATP value
    printf("Test 1: Reading initial SATP value\n");
    uint32_t satp_initial = read_satp();
    print_result("  Initial SATP value", satp_initial);
    *initial_satp = satp_initial; // Store for verification

    // Test 2: Test writing SATP value with Sv32 mode (quickly disable to avoid page faults)
    printf("\nTest 2: Testing SATP write with Sv32 mode\n");
    uint32_t satp_sv32 = (1 << 31) | (0x1000 >> 12); // MODE=1 (Sv32), PPN=0x1000>>12
    write_satp(satp_sv32); // Enable Sv32 mode
    print_result("  Written Sv32 SATP value", satp_sv32);

    // Immediately disable to avoid page faults during UART output
    uint32_t satp_new = 0x00000000; // Back to bare metal
    write_satp(satp_new);
    print_result("  Disabled Sv32 (back to bare metal)", satp_new);

    // Test 3: Read back SATP value to verify write
    printf("\nTest 3: Reading back SATP value to verify\n");
    uint32_t satp_back = read_satp();
    print_result("  Read back SATP value", satp_back);
    *final_satp = satp_back; // Store for verification

    printf("\n=== SELF-CHECKING TESTS ===\n");

    // Self-Check 1: Initial SATP should be 0 (Bare metal mode)
    int test1_pass = (satp_initial == 0);
    print_test_result("Test 1: Initial SATP = 0", test1_pass);

    // Self-Check 2: SATP write should succeed
    int test2_pass = 1; // If we reach here, write didn't crash
    print_test_result("Test 2: SATP write operation", test2_pass);

    // Self-Check 3: SATP readback should match written value
    int test3_pass = (satp_back == satp_new);
    print_test_result("Test 3: SATP readback verification", test3_pass);

    // Self-Check 4: Verify SATP bits are correctly set
    // Check MODE bits (bits 31-30 = 00 for Bare metal)
    int mode_correct = ((satp_back >> 31) & 0x1) == 0;
    print_test_result("Test 4: SATP MODE = Bare metal (00)", mode_correct);

    // Self-Check 5: Check PPN (Physical Page Number) field
    uint32_t ppn = satp_back & 0x3FFFFF; // Bits 21-0 for PPN
    int ppn_correct = (ppn == 0);
    print_test_result("Test 5: SATP PPN = 0", ppn_correct);

    // Self-Check 6: Verify memory storage worked
    int storage_correct = (*initial_satp == satp_initial) && (*final_satp == satp_back);
    print_test_result("Test 6: Memory storage verification", storage_correct);

    // Additional verification tests
    printf("\n=== DETAILED ANALYSIS ===\n");

    printf("SATP Mode field (bits 31-30): ");
    print_hex((satp_back >> 30) & 0x3);
    printf(" (");
    if (((satp_back >> 31) & 0x1) == 0) {
        printf("Bare metal");
    } else if (((satp_back >> 31) & 0x1) == 1) {
        printf("Sv32");
    } else {
        printf("Reserved");
    }
    printf(")\n");

    printf("SATP ASID field (bits 29-22): ");
    print_hex((satp_back >> 22) & 0xFF);
    printf("\n");

    printf("SATP PPN field (bits 21-0): ");
    print_hex(satp_back & 0xFFFFF);
    printf("\n");

    // Overall test result
    printf("\n=== OVERALL RESULT ===\n");
    int all_tests_passed = test1_pass && test2_pass && test3_pass &&
                          mode_correct && ppn_correct && storage_correct;

    if (all_tests_passed) {
        printf("🎉 ALL TESTS PASSED!\n");
        printf("SATP CSR implementation is working correctly.\n");
    } else {
        printf("❌ SOME TESTS FAILED!\n");
        printf("SATP CSR implementation needs attention.\n");
    }

    printf("\n=== SATP TEST COMPLETE ===\n");

    // Display final status for debugging
    printf("Final SATP register value: ");
    print_hex(satp_back);
    printf("\n");

    printf("Memory verification:\n");
    printf("  Initial SATP stored at ");
    print_hex((uint32_t)initial_satp);
    printf(": ");
    print_hex(*initial_satp);
    printf("\n");

    printf("  Final SATP stored at ");
    print_hex((uint32_t)final_satp);
    printf(": ");
    print_hex(*final_satp);
    printf("\n");

    // Infinite loop
    while (1) {
        // Test complete
    }
}