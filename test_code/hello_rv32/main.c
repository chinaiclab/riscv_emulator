#include <stdint.h>
#include <stdbool.h>
#include "../../software/init_code/vprintf.h"

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
#define INIT_PHASE_ADDR      0x98004  // Initialization phase flag (1=init, 0=normal execution)

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


// Main function - called by startup code
void main(void) {
    uint32_t core_id = get_core_id();

    // Report initial state
    report_core_state(CORE_STATE_RUNNING);

    if (get_initialization_phase()) {
        // Initialization phase - only core0 should execute this
        if (core_id == 0) {
            printf("Hello, world!\n");
            printf("Hello, this is core#%lu - initializing and preparing to release all cores\n", (unsigned long)core_id);

            // Setup other cores and release them for parallel execution
            setup_other_cores();
            release_all_cores();

            // Signal that initialization is complete by writing to MultiCoreMonitor memory
            set_initialization_phase(false);
        }
        return;  // Return to startup code for phase transition
    } else {
        // Parallel execution phase - all cores execute this
        if (core_id == 0) {
            printf("Core #%lu is executing the main program\n", (unsigned long)core_id);
        } else if (core_id == 1) {
            printf("Core #%lu is executing the main program\n", (unsigned long)core_id);
        } else if (core_id == 2) {
            printf("Core #%lu is executing the main program\n", (unsigned long)core_id);
        } else if (core_id == 3) {
            printf("Core #%lu is executing the main program\n", (unsigned long)core_id);
        }
    }

    // Report IDLE state before entering infinite loop
    report_core_state(CORE_STATE_IDLE);

    while (1) {
        // Core is now idle, waiting for interrupts or program termination
        // In a real system, this could be a low-power state
    }
}
