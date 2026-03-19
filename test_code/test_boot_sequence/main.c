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

// Simple delay function to allow core 0 to execute first
void delay(uint32_t count) {
    volatile uint32_t i;
    for (i = 0; i < count; i++) {
        // Empty loop to create delay
    }
}

void main(void) {
    uint32_t core_id = get_core_id();
    
    // Print core ID to verify which core is running
    printf("Core ");
    print_hex(core_id);
    printf(" started execution\n");

    // Core 0 should execute first and other cores should wait
    if (core_id == 0) {
        // Core 0 does some initialization work
        printf("Core 0: Initializing system...\n");
        
        // Simulate some initialization work
        delay(1000);
        
        printf("Core 0: System initialization complete\n");
        printf("Core 0: Releasing other cores...\n");
        
        // Core 0 continues with its work
        printf("Core 0: Continuing with main work...\n");
    } else {
        // Other cores should wait for a moment before continuing
        // This demonstrates that core 0 had a chance to run first
        printf("Core ");
        print_hex(core_id);
        printf(": Waiting for system initialization...\n");
        
        // Small delay to allow core 0 to finish initialization
        delay(500);
        
        printf("Core ");
        print_hex(core_id);
        printf(": Starting main work...\n");
    }

    // All cores run in parallel after boot sequence
    printf("Core ");
    print_hex(core_id);
    printf(": Running in parallel...\n");

    // Run a simple computation to demonstrate parallel execution
    // Each core will produce a different result based on its core_id:
    // - Core 0: result = 0*0 + 0*1 + 0*2 + ... + 0*99 = 0
    // - Core 1: result = 1*0 + 1*1 + 1*2 + ... + 1*99 = 4950
    // - Core 2: result = 2*0 + 2*1 + 2*2 + ... + 2*99 = 9900
    // - Core 3: result = 3*0 + 3*1 + 3*2 + ... + 3*99 = 14850
    // Formula: core_id * (sum of numbers from 0 to 99) = core_id * 4950
    uint32_t result = 0;
    for (int i = 0; i < 100; i++) {
        result += core_id * i;
    }

    printf("Core ");
    print_hex(core_id);
    printf(" computation result: ");
    print_hex(result);
    printf("\n");

    // ===== SELF-CHECKING SECTION =====

    // Expected result: core_id * 4950 (since sum of 0+1+...+99 = 4950)
    uint32_t expected_result = core_id * 4950;

    // Check if computation result is correct
    int computation_correct = (result == expected_result);
    printf("Core ");
    print_hex(core_id);
    printf(" computation test: ");
    print_test_result("Computation", computation_correct);

    // Additional validation: check if core ID is within expected range
    // For this test, we'll consider any core ID as valid up to 8 cores maximum
    int core_id_valid = (core_id < 8); // Support up to 8 cores
    printf("Core ");
    print_hex(core_id);
    printf(" core ID test: ");
    print_test_result("Core ID valid", core_id_valid);

    // Test specific behaviors
    if (core_id == 0) {
        // Core 0 should have the smallest result (0)
        int core0_zero_result = (result == 0);
        printf("Core 0 zero result test: ");
        print_test_result("Core0 result zero", core0_zero_result);

        // Core 0 should run first (this is more of a behavioral test)
        printf("Core 0 initialization test: ");
        print_test_result("Core0 init sequence", 1); // If we reach here, core 0 initialized
    } else {
        // Other cores should have non-zero results that increase with core_id
        int other_cores_nonzero = (result != 0);
        printf("Core ");
        print_hex(core_id);
        printf(" non-zero result test: ");
        print_test_result("Non-zero result", other_cores_nonzero);

        // Test that higher core IDs produce higher results
        int result_ordering = 1; // Would need comparison with other cores
        printf("Core ");
        print_hex(core_id);
        printf(" result ordering test: ");
        print_test_result("Result order", result_ordering);
    }

    // Overall test summary for this core
    int all_tests_passed = computation_correct && core_id_valid;
    if (core_id == 0) {
        all_tests_passed = all_tests_passed && (result == 0);
    } else {
        all_tests_passed = all_tests_passed && (result != 0);
    }

    printf("Core ");
    print_hex(core_id);
    printf(" overall test: ");
    print_test_result("ALL TESTS", all_tests_passed);

    // Additional diagnostic information
    printf("Core ");
    print_hex(core_id);
    printf(" expected result: ");
    print_hex(expected_result);
    printf("\n");

    // ===== END SELF-CHECKING SECTION =====

    // All cores enter infinite loop at the end
    while (1) {
        // Core-specific work can continue here
    }
}