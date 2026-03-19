#include <stdint.h>
#include "../../software/init_code/vprintf.h"

extern uint32_t get_core_id(void);

// Shared memory location for atomic operations
volatile uint32_t shared_counter __attribute__((section(".data"))) = 0;
volatile uint32_t test_array[4] __attribute__((section(".data"))) = {0, 0, 0, 0};

// Function to convert integer to string (improved)
void int_to_string(int value, char* str) {
    if (value == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }

    // Handle positive numbers up to 4 digits
    if (value >= 1 && value <= 9) {
        str[0] = '0' + value;
        str[1] = '\0';
        return;
    }

    if (value >= 10 && value <= 99) {
        str[0] = '0' + (value / 10);
        str[1] = '0' + (value % 10);
        str[2] = '\0';
        return;
    }

    if (value >= 100 && value <= 999) {
        str[0] = '0' + (value / 100);
        str[1] = '0' + ((value / 10) % 10);
        str[2] = '0' + (value % 10);
        str[3] = '\0';
        return;
    }

    if (value >= 1000 && value <= 9999) {
        str[0] = '0' + (value / 1000);
        str[1] = '0' + ((value / 100) % 10);
        str[2] = '0' + ((value / 10) % 10);
        str[3] = '0' + (value % 10);
        str[4] = '\0';
        return;
    }

    // Handle the case for core_id * 30 = 90
    if (value >= 10 && value <= 90) {
        int tens = value / 10;
        int ones = value % 10;
        str[0] = '0' + tens;
        str[1] = '0' + ones;
        str[2] = '\0';
        return;
    }

    // Fallback
    str[0] = '?';
    str[1] = '\0';
}

// Function to print an integer
void print_int(int value) {
    char buffer[16]; // Increased buffer size
    int_to_string(value, buffer);
    printf(buffer);
}

// Test atomic add operation
void test_atomic_add() {
    uint32_t core_id = get_core_id();
    
    // Each core adds its ID to the shared counter 100 times
    for (int i = 0; i < 100; i++) {
        // Use inline assembly for atomic add (AMOADD.W)
        uint32_t temp;
        __asm__ volatile (
            "amoadd.w %0, %2, %1"
            : "=r" (temp), "+A" (shared_counter)
            : "r" (core_id)
            : "memory"
        );
    }
    
    char core_str[16];

    printf("Core ");
    int_to_string(core_id, core_str);
    printf(core_str);
    printf(" completed atomic add test\n");
}

// Test atomic swap operation
void test_atomic_swap() {
    uint32_t core_id = get_core_id();
    uint32_t local_idx = core_id % 4;
    
    // Each core swaps its value into the test array
    uint32_t old_value;
    __asm__ volatile (
        "amoswap.w %0, %2, %1"
        : "=r" (old_value), "+A" (test_array[local_idx])
        : "r" (core_id * 10)
        : "memory"
    );
    
    char core_str[16];
    char value_str[16];

    printf("Core ");
    int_to_string(core_id, core_str);
    printf(core_str);
    printf(" swapped old value ");
    int_to_string(old_value, value_str);
    printf(value_str);
    printf(" with new value ");
    int_to_string(core_id * 10, value_str);
    printf(value_str);
    printf("\n");
}

// Test atomic compare-and-swap operation using lr.w and sc.w
void test_atomic_cas() {
    uint32_t core_id = get_core_id();
    uint32_t local_idx = core_id % 4;

    uint32_t expected = core_id * 10;
    uint32_t desired = core_id * 20;
    uint32_t result;
    uint32_t success;

    // Implement compare-and-swap using load-reserved and store-conditional
    __asm__ volatile (
        "1:\n\t"                           // Retry label
        "lr.w %0, %2\n\t"                  // Load reserved from memory location
        "bne %0, %3, 2f\n\t"               // If loaded value != expected, skip store
        "sc.w %1, %4, %2\n\t"              // Store conditional - %1 gets 0 if success, non-zero if failure
        "bnez %1, 1b\n\t"                  // If store failed (%1 != 0), retry
        "li %1, 1\n\t"                     // Mark success
        "j 3f\n\t"                         // Jump to end
        "2:\n\t"                           // Failure path
        "li %1, 0\n\t"                     // Mark failure
        "3:\n\t"                           // End
        : "=&r" (result), "=&r" (success), "+A" (test_array[local_idx])
        : "r" (expected), "r" (desired)
        : "memory"
    );

    char core_str[16];
    char result_str[16];
    char success_str[16];

    printf("Core ");
    int_to_string(core_id, core_str);
    printf(core_str);
    printf(" performed CAS operation, old value: ");
    int_to_string(result, result_str);
    printf(result_str);
    printf(", success: ");
    int_to_string(success, success_str);
    printf(success_str);
    printf("\n");
}

// Test atomic fetch-and-add operation
void test_atomic_fetch_add() {
    uint32_t core_id = get_core_id();
    
    // Each core fetches and adds to the shared counter
    for (int i = 0; i < 50; i++) {
        uint32_t old_value;
        __asm__ volatile (
            "amoadd.w %0, %2, %1"
            : "=r" (old_value), "+A" (shared_counter)
            : "r" (1)
            : "memory"
        );
    }
    
    char core_str[16];
    printf("Core ");
    int_to_string(core_id, core_str);
    printf(core_str);
    printf(" completed fetch-and-add test\n");
}

void main(void) {
    uint32_t core_id = get_core_id();

    // Print a simple message to identify the core
    if (core_id == 0) {
        printf("Starting atomic operations test on core 0\n");
    } else if (core_id == 1) {
        printf("Starting atomic operations test on core 1\n");
    } else if (core_id == 2) {
        printf("Starting atomic operations test on core 2\n");
    } else {
        printf("Starting atomic operations test on core 3\n");
    }

    // Run different tests on different cores to avoid conflicts
    if (core_id == 0) {
        test_atomic_add();
    } else if (core_id == 1) {
        test_atomic_swap();
    } else if (core_id == 2) {
        test_atomic_cas();
    } else {
        test_atomic_fetch_add();
    }

    // Print final counter value from core 0
    if (core_id == 0) {
        printf("Final shared counter value: ");
        print_int(shared_counter);
        printf("\n");

        printf("Test array values: ");
        for (int i = 0; i < 4; i++) {
            print_int(test_array[i]);
            printf(" ");
        }
        printf("\n");
    }

    // Print completion message
    if (core_id == 0) {
        printf("Core 0 completed atomic operations test\n");
    } else if (core_id == 1) {
        printf("Core 1 completed atomic operations test\n");
    } else if (core_id == 2) {
        printf("Core 2 completed atomic operations test\n");
    } else {
        printf("Core 3 completed atomic operations test\n");
    }

    // All cores enter infinite loop at the end
    while (1) {
        // Core-specific work can continue here
    }
}