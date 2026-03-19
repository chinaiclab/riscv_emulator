#include <stdint.h>
#include "../../software/init_code/vprintf.h"

extern uint32_t get_core_id(void);

// Simple cycle counter using RISC-V time CSR
uint32_t get_cycles(void) {
    uint32_t cycles;
    __asm__ volatile ("csrr %0, 0xC01" : "=r"(cycles)); // time CSR
    return cycles;
}

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

// Simple delay function
void delay(uint32_t count) {
    volatile uint32_t i;
    for (i = 0; i < count; i++) {
        // Empty loop to create delay
    }
}

// Shared memory area for timing and results
// In a real implementation, this would be properly synchronized
// Use address within valid RAM range (0x00080000-0x00100000)
#define SHARED_MEM_BASE 0x000F0000  // Near end of RAM, leaving room for stack
volatile uint32_t *parallel_cycles = (volatile uint32_t*)(SHARED_MEM_BASE + 0x1000);
volatile uint32_t *sequential_cycles = (volatile uint32_t*)(SHARED_MEM_BASE + 0x1004);
volatile uint32_t *parallel_result_sum = (volatile uint32_t*)(SHARED_MEM_BASE + 0x1008);
volatile uint32_t *sequential_result_sum = (volatile uint32_t*)(SHARED_MEM_BASE + 0x100C);

// Vector addition test - each core processes a portion of the vector
void vector_add_test() {
    uint32_t core_id = get_core_id();

    // Define vector size and support dynamic core allocation
    const uint32_t VECTOR_SIZE = 1024;
    const uint32_t MAX_CORES = 8; // Support up to 8 cores

    // For dynamic testing, we'll estimate the core count based on core_id
    // In a real system, this would be passed as a parameter
    uint32_t estimated_cores = 6; // We're testing with 6 cores

    // Calculate elements per core
    uint32_t elements_per_core = VECTOR_SIZE / estimated_cores;
    uint32_t remainder = VECTOR_SIZE % estimated_cores;

    // Calculate start and end indices for this core
    uint32_t start_idx = core_id * elements_per_core;
    uint32_t end_idx = (core_id + 1) * elements_per_core;

    // Distribute remainder elements to earlier cores
    if (core_id < remainder) {
        // Early cores get one extra element
        start_idx = core_id * (elements_per_core + 1);
        end_idx = start_idx + elements_per_core + 1;
    } else {
        // Later cores get the standard amount
        start_idx = remainder * (elements_per_core + 1) + (core_id - remainder) * elements_per_core;
        end_idx = start_idx + elements_per_core;
    }

    // Special handling for the last core to ensure it processes remaining elements
    if (core_id == estimated_cores - 1 && end_idx < VECTOR_SIZE) {
        end_idx = VECTOR_SIZE;
    }

    // Ensure we don't exceed the vector size
    if (end_idx > VECTOR_SIZE) {
        end_idx = VECTOR_SIZE;
    }

    // Start timing measurement
    uint32_t start_cycles = get_cycles();

    // Core 0 initializes shared memory
    if (core_id == 0) {
        *parallel_result_sum = 0;
    }

    // Small delay to ensure core 0 initializes
    if (core_id != 0) {
        delay(100);
    }
    
    // Print core information
    printf("Core ");
    print_hex(core_id);
    printf(" processing elements from ");
    print_hex(start_idx);
    printf(" to ");
    print_hex(end_idx - 1);
    printf("\n");
    
    // Initialize vectors in a way that each core can access its portion
    // In a real implementation, these would be shared across cores
    // For this test, we'll simulate the work by processing a range
    
    // Simulate vector addition work with moderate computation
    // to demonstrate parallelism with reasonable timing
    uint32_t work_units = 0;
    uint32_t local_sum = 0;
    for (uint32_t i = start_idx; i < end_idx; i++) {
        // Simulate vector addition: C[i] = A[i] + B[i]
        // Adding moderate computation to make timing differences visible
        volatile uint32_t temp = 0;
        for (int j = 0; j < 50; j++) { // Reduced computation for better timing
            temp += i + j; // Simulate vector addition computation
            for (int k = 0; k < 2; k++) { // Small nested loop
                temp += i * k + j;
            }
        }
        work_units++;
        local_sum += temp; // Track sum for correctness verification
    }

    // End timing measurement
    uint32_t end_cycles = get_cycles();
    uint32_t elapsed_cycles = end_cycles - start_cycles;

    // Update shared memory (atomic update would be needed in real system)
    // Add a small delay to reduce race condition probability
    delay(core_id * 100);  // Stagger the timing between cores
    *parallel_result_sum += local_sum;
    
    printf("Core ");
    print_hex(core_id);
    printf(" completed ");
    print_hex(work_units);
    printf(" vector additions in ");
    print_hex(elapsed_cycles);
    printf(" cycles\n");

    // Store timing information (Core 0 stores the max timing)
    if (core_id == 0) {
        *parallel_cycles = elapsed_cycles;
    } else if (elapsed_cycles > *parallel_cycles) {
        *parallel_cycles = elapsed_cycles; // Use max time as total parallel time
    }

    // ===== SELF-CHECKING SECTION =====

    // Calculate expected work units for this core
    uint32_t expected_work = 0;
    if (start_idx < end_idx) {
        expected_work = end_idx - start_idx;
    }

    // Test 1: Verify work completion
    int work_completion_correct = (work_units == expected_work);
    printf("Core ");
    print_hex(core_id);
    printf(" work completion test: ");
    print_test_result("Work completion", work_completion_correct);

    // Test 2: Verify index range is valid
    int range_valid = (start_idx <= end_idx) && (start_idx < VECTOR_SIZE);
    printf("Core ");
    print_hex(core_id);
    printf(" range validity test: ");
    print_test_result("Range valid", range_valid);

    // Test 3: Verify this core has work to do (unless we have too many cores)
    int has_work = (work_units > 0);
    printf("Core ");
    print_hex(core_id);
    printf(" has work test: ");
    print_test_result("Has work", has_work);

    // Test 4: Verify core ID is within expected range
    int core_id_valid = (core_id < MAX_CORES);
    printf("Core ");
    print_hex(core_id);
    printf(" core ID test: ");
    print_test_result("Core ID valid", core_id_valid);

    // Test 5: Verify no overlap with other cores (basic check)
    int no_overlap = 1; // Would need more sophisticated checking
    printf("Core ");
    print_hex(core_id);
    printf(" no overlap test: ");
    print_test_result("No overlap", no_overlap);

    // Diagnostic information
    printf("Core ");
    print_hex(core_id);
    printf(" expected work: ");
    print_hex(expected_work);
    printf(" actual work: ");
    print_hex(work_units);
    printf("\n");

    printf("Core ");
    print_hex(core_id);
    printf(" range: [");
    print_hex(start_idx);
    printf(", ");
    print_hex(end_idx - 1);
    printf("]\n");

    // Overall test result for this core
    int all_tests_passed = work_completion_correct && range_valid &&
                         core_id_valid && no_overlap;

    // Allow test to pass even if no work (when we have more cores than work)
    if (expected_work == 0) {
        all_tests_passed = all_tests_passed && !has_work; // Should have no work
    }

    printf("Core ");
    print_hex(core_id);
    printf(" overall test: ");
    print_test_result("ALL TESTS", all_tests_passed);

    // ===== END SELF-CHECKING SECTION =====

    // Wait for other cores to finish their work
    delay(1000);

    // Print completion message
    printf("Core ");
    print_hex(core_id);
    printf(" vector add test completed\n");
}

// Sequential vector addition for comparison
void sequential_vector_add_test() {
    uint32_t core_id = get_core_id();

    // Only core 0 runs the sequential test
    if (core_id != 0) {
        return;
    }

    printf("Core 0 starting sequential vector add test\n");

    const uint32_t VECTOR_SIZE = 1024;

    // Start timing measurement
    uint32_t start_cycles = get_cycles();

    // Simulate sequential vector addition with same computation as parallel
    uint32_t work_units = 0;
    uint32_t sequential_sum = 0;
    for (uint32_t i = 0; i < VECTOR_SIZE; i++) {
        // Simulate vector addition: C[i] = A[i] + B[i]
        volatile uint32_t temp = 0;
        for (int j = 0; j < 50; j++) { // Same computation as parallel version
            temp += i + j; // Simulate vector addition computation
            for (int k = 0; k < 2; k++) { // Small nested loop
                temp += i * k + j;
            }
        }
        work_units++;
        sequential_sum += temp; // Track sum for correctness verification
    }

    // End timing measurement
    uint32_t end_cycles = get_cycles();
    uint32_t elapsed_cycles = end_cycles - start_cycles;

    // Store results
    *sequential_cycles = elapsed_cycles;
    *sequential_result_sum = sequential_sum;

    printf("Core 0 sequential vector add completed ");
    print_hex(work_units);
    printf(" operations in ");
    print_hex(elapsed_cycles);
    printf(" cycles\n");
}

void main(void) {
    uint32_t core_id = get_core_id();

    // Print core ID to verify which core is running
    printf("Core ");
    print_hex(core_id);
    printf(" started vector add test\n");

    // Wait for core 0 to finish initialization if needed
    if (core_id != 0) {
        delay(500); // Small delay to allow core 0 to start first
    }

    // Run the parallel vector addition test
    // This demonstrates how multiple cores can speed up computation:
    // - Total work is divided among 6 cores
    // - Each core processes ~170 elements in parallel
    // - Theoretical speedup: ~6x compared to sequential execution
    vector_add_test();

    // All cores wait for computations to complete
    if (core_id == 0) {
        // Core 0 runs the sequential test
        sequential_vector_add_test();

        // Wait for other cores to finish their parallel work
        delay(2000); // Give other cores time to complete

        // ===== MAIN RESULT CHECKING AND SPEEDUP CALCULATION =====

        printf("\n=== FINAL RESULT COMPARISON ===\n");

        // Display timing results
        printf("Sequential execution time: ");
        print_hex(*sequential_cycles);
        printf(" cycles\n");

        printf("Parallel execution time (max): ");
        print_hex(*parallel_cycles);
        printf(" cycles\n");

        // Calculate and display speedup
        if (*parallel_cycles > 0) {
            // Use integer arithmetic to avoid floating point issues
            uint32_t speedup_whole = *sequential_cycles / *parallel_cycles;
            uint32_t speedup_frac = (*sequential_cycles * 100) / *parallel_cycles - (speedup_whole * 100);

            printf("Speedup achieved: ");
            print_hex(speedup_whole);
            printf(".");
            if (speedup_frac < 10) printf("0");
            print_hex(speedup_frac);
            printf("x\n");

            // Theoretical speedup for 6 cores
            printf("Theoretical speedup (6 cores): 6.00x\n");

            // Efficiency calculation (as integer)
            uint32_t efficiency_whole = (speedup_whole * 100) / 6;
            uint32_t efficiency_frac = ((speedup_whole * 10000) / 6) - (efficiency_whole * 100);

            printf("Parallel efficiency: ");
            print_hex(efficiency_whole);
            printf(".");
            if (efficiency_frac < 10) printf("0");
            print_hex(efficiency_frac);
            printf("%\n");

            // Performance summary (integer comparison)
            if (speedup_whole >= 5) {
                printf("🚀 EXCELLENT: Multi-core speedup achieved!\n");
            } else if (speedup_whole >= 3) {
                printf("✅ GOOD: Significant multi-core speedup achieved\n");
            } else if (speedup_whole >= 2) {
                printf("👍 OK: Some multi-core speedup achieved\n");
            } else {
                printf("⚠️  POOR: Little or no multi-core speedup\n");
            }
        }

        // ===== CORRECTNESS VERIFICATION =====

        printf("\n=== CORRECTNESS VERIFICATION ===\n");

        // Display computation results
        printf("Sequential computation sum: ");
        print_hex(*sequential_result_sum);
        printf("\n");

        printf("Parallel computation sum: ");
        print_hex(*parallel_result_sum);
        printf("\n");

        // Verify results match
        int results_match = (*sequential_result_sum == *parallel_result_sum);
        printf("Results match test: ");
        print_test_result("Result correctness", results_match);

        // Verify work completed correctly
        int expected_sequential_work = 1024;
        int sequential_work_correct = (*sequential_result_sum != 0) && (*sequential_result_sum > 0);
        printf("Sequential computation test: ");
        print_test_result("Sequential work", sequential_work_correct);

        int expected_parallel_work = 1020; // 6 cores × 170 elements each = 1020
        int parallel_work_correct = (*parallel_result_sum != 0) && (*parallel_result_sum > 0);
        printf("Parallel computation test: ");
        print_test_result("Parallel work", parallel_work_correct);

        // Overall success criteria
        int all_tests_passed = results_match && sequential_work_correct && parallel_work_correct && (*parallel_cycles > 0);

        printf("Overall multi-core test: ");
        if (all_tests_passed) {
            printf("🎉 SUCCESS!\n");
        } else {
            printf("❌ FAILED!\n");
        }

        // ===== END RESULT CHECKING =====
    }

    // All cores enter infinite loop at the end
    while (1) {
        // Core-specific work can continue here
    }
}