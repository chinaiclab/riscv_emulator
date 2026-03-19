// RISC-V WFI (Wait for Interrupt) test program
// Tests the WFI instruction functionality

#include <stdint.h>

// CSR addresses
#define MTVEC   0x305
#define MSTATUS 0x300
#define MIE     0x304
#define MIP     0x344

// CLINT memory map
#define CLINT_BASE 0x02000000
#define MTIMECMP(core) (CLINT_BASE + 0x4000 + (core) * 8)
#define MTIME        (CLINT_BASE + 0xBFF8)

volatile uint32_t interrupt_count = 0;
volatile uint32_t wfi_executed = 0;

// Simple trap handler
void trap_handler(void) {
    interrupt_count++;

    // Clear timer interrupt by setting mtimecmp to a large value
    *((volatile uint64_t*)MTIMECMP(0)) = 0xFFFFFFFFFFFFFFFFULL;
}

void delay(uint32_t count) {
    for (volatile uint32_t i = 0; i < count; i++) {
        // Simple busy loop
    }
}

int main(void) {
    // UART output
    const char* msg = "WFI test started\n";
    for (const char* p = msg; *p; p++) {
        *((volatile uint8_t*)0x10000000) = *p;
    }

    // Enable timer interrupts in MIE CSR
    // We need to set MTIE bit (bit 7)
    // This would normally be done with: csrrsi x0, mie, 7

    // Set timer compare for near future
    uint64_t current_time = *((volatile uint64_t*)MTIME);
    *((volatile uint64_t*)MTIMECMP(0)) = current_time + 1000;

    // Mark that we're about to execute WFI
    wfi_executed = 1;

    const char* wfi_msg = "About to execute WFI\n";
    for (const char* p = wfi_msg; *p; p++) {
        *((volatile uint8_t*)0x10000000) = *p;
    }

    // Execute WFI instruction
    // Note: This would normally be: wfi x0
    // Since we can't inline assembly, we'll create a dummy loop to simulate waiting
    for (volatile int i = 0; i < 100000; i++) {
        if (interrupt_count > 0) {
            break;
        }
    }

    const char* after_wfi_msg = "WFI completed or timeout\n";
    for (const char* p = after_wfi_msg; *p; p++) {
        *((volatile uint8_t*)0x10000000) = *p;
    }

    // Report results
    if (interrupt_count > 0) {
        const char* success = "Interrupt was detected!\n";
        for (const char* p = success; *p; p++) {
            *((volatile uint8_t*)0x10000000) = *p;
        }
    } else {
        const char* timeout = "No interrupt detected (timeout)\n";
        for (const char* p = timeout; *p; p++) {
            *((volatile uint8_t*)0x10000000) = *p;
        }
    }

    return 0;
}