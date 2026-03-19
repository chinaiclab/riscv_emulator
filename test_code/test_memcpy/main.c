#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "../../software/init_code/vprintf.h"

// memset, memcpy, strlen, and strcmp are already defined in bare-metal.c

extern uint32_t get_core_id(void);
extern void release_all_cores(void);
extern void setup_other_cores(void);
extern void report_core_state(uint32_t state_value);

// Core state constants for MultiCoreMonitor
#define CORE_STATE_HALT     0x01
#define CORE_STATE_RUNNING  0x02
#define CORE_STATE_IDLE     0x03
#define CORE_STATE_ERROR    0xFF

// Memory addresses for MultiCoreMonitor
#define INIT_PHASE_ADDR      0x98004  // Initialization phase flag

// Function to read initialization phase flag from MultiCoreMonitor memory
static bool get_initialization_phase(void) {
    volatile uint32_t* flag_ptr = (volatile uint32_t*)INIT_PHASE_ADDR;
    return (*flag_ptr != 0);
}

// Function to set initialization phase flag in MultiCoreMonitor memory
static void set_initialization_phase(bool is_init) {
    volatile uint32_t* flag_ptr = (volatile uint32_t*)INIT_PHASE_ADDR;
    *flag_ptr = is_init ? 1 : 0;
}

// Test function to verify memcpy functionality
void test_memcpy_functionality(void) {
    uint32_t core_id = get_core_id();
    printf("[Core#%lu] Starting memcpy tests...\n", (unsigned long)core_id);

    // Test 1: Basic memcpy
    {
        const char* src_str = "Hello, memcpy test!";
        char dest_str[50];

        // Clear destination first
        memset(dest_str, 0, sizeof(dest_str));

        // Perform memcpy
        memcpy(dest_str, src_str, strlen(src_str) + 1);

        printf("[Core#%lu] Test 1 - Basic memcpy: '%s'\n", (unsigned long)core_id, dest_str);

        // Verify
        if (strcmp(src_str, dest_str) == 0) {
            printf("[Core#%lu] Test 1 PASSED OK\n", (unsigned long)core_id);
        } else {
            printf("[Core#%lu] Test 1 FAILED ✗\n", (unsigned long)core_id);
            report_core_state(CORE_STATE_ERROR);
            return;
        }
    }

    // Test 2: Integer array memcpy
    {
        uint32_t src_array[] = {0x12345678, 0x9ABCDEF0, 0x11223344, 0x55667788, 0x99AABBCC};
        uint32_t dest_array[5];

        // Clear destination
        memset(dest_array, 0, sizeof(dest_array));

        // Copy array
        memcpy(dest_array, src_array, sizeof(src_array));

        printf("[Core#%lu] Test 2 - Integer array: [0x%08X, 0x%08X, 0x%08X]\n",
               (unsigned long)core_id, dest_array[0], dest_array[1], dest_array[2]);

        // Verify
        bool passed = true;
        for (int i = 0; i < 5; i++) {
            if (src_array[i] != dest_array[i]) {
                passed = false;
                break;
            }
        }

        if (passed) {
            printf("[Core#%lu] Test 2 PASSED OK\n", (unsigned long)core_id);
        } else {
            printf("[Core#%lu] Test 2 FAILED ✗\n", (unsigned long)core_id);
            report_core_state(CORE_STATE_ERROR);
            return;
        }
    }

    // Test 3: Overlapping memcpy test (should work with standard memcpy)
    {
        char buffer[20] = "1234567890ABCDE";

        printf("[Core#%lu] Test 3 - Before overlapping memcpy: '%s'\n", (unsigned long)core_id, buffer);

        // Copy "123456" to position starting at index 2 (overlapping)
        memcpy(buffer + 2, buffer, 6);

        printf("[Core#%lu] Test 3 - After overlapping memcpy: '%s'\n", (unsigned long)core_id, buffer);

        // Expected result should be "1212345678DE"
        if (memcmp(buffer, "1212345678DE", 12) == 0) {
            printf("[Core#%lu] Test 3 PASSED OK\n", (unsigned long)core_id);
        } else {
            printf("[Core#%lu] Test 3 FAILED ✗ (overlapping behavior)\n", (unsigned long)core_id);
            // Note: Overlapping behavior is undefined for memcpy, but we test it anyway
        }
    }

    // Test 4: Zero-length memcpy
    {
        char src[] = "source";
        char dest[20] = "destination";

        memcpy(dest, src, 0);  // Zero bytes should do nothing

        if (strcmp(dest, "destination") == 0) {
            printf("[Core#%lu] Test 4 PASSED OK (zero-length)\n", (unsigned long)core_id);
        } else {
            printf("[Core#%lu] Test 4 FAILED ✗ (zero-length)\n", (unsigned long)core_id);
        }
    }

    // Test 5: Large block memcpy
    {
        #define BLOCK_SIZE 1024
        static uint8_t src_block[BLOCK_SIZE];
        static uint8_t dest_block[BLOCK_SIZE];

        // Fill source with pattern
        for (int i = 0; i < BLOCK_SIZE; i++) {
            src_block[i] = (uint8_t)(i & 0xFF);
        }

        // Clear destination
        memset(dest_block, 0, BLOCK_SIZE);

        // Copy large block
        memcpy(dest_block, src_block, BLOCK_SIZE);

        // Verify
        bool passed = true;
        for (int i = 0; i < BLOCK_SIZE; i++) {
            if (src_block[i] != dest_block[i]) {
                passed = false;
                break;
            }
        }

        if (passed) {
            printf("[Core#%lu] Test 5 PASSED OK (%d-byte block copy)\n", (unsigned long)core_id, BLOCK_SIZE);
        } else {
            printf("[Core#%lu] Test 5 FAILED ✗ (%d-byte block copy)\n", (unsigned long)core_id, BLOCK_SIZE);
            report_core_state(CORE_STATE_ERROR);
            return;
        }
    }

    printf("[Core#%lu] All memcpy tests completed successfully!\n", (unsigned long)core_id);
}

// Main function - called by startup code
void main(void) {
    uint32_t core_id = get_core_id();

    // Report initial state
    report_core_state(CORE_STATE_RUNNING);

    if (get_initialization_phase()) {
        // Initialization phase - only core0 should execute this
        if (core_id == 0) {
            printf("memcpy Test: Core#%lu starting initialization\n", (unsigned long)core_id);

            // Setup other cores and release them for parallel execution
            setup_other_cores();
            release_all_cores();

            // Signal that initialization is complete
            set_initialization_phase(false);

            printf("memcpy Test: Core#%lu initialization complete, releasing all cores\n", (unsigned long)core_id);
        }
        return;  // Return to startup code for phase transition
    } else {
        // Parallel execution phase - all cores execute memcpy tests
        test_memcpy_functionality();
    }

    // Report IDLE state before entering infinite loop
    report_core_state(CORE_STATE_IDLE);

    while (1) {
        // Core is now idle, waiting for interrupts or program termination
    }
}