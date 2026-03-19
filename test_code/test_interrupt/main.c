// Simple RISC-V interrupt test program
// Tests CLINT timer interrupt functionality

#include <stdint.h>

// CSR addresses
#define MTVEC   0x305
#define MSTATUS 0x300
#define MIE     0x304
#define MIP     0x344
#define MCYCLE  0xB00
#define MCYCLEH 0xB80

// CLINT memory map
#define CLINT_BASE 0x02000000
#define MTIMECMP(core) (CLINT_BASE + 0x4000 + (core) * 8)
#define MTIME        (CLINT_BASE + 0xBFF8)

volatile uint32_t interrupt_count = 0;
volatile uint32_t timer_fired = 0;

// Simple trap handler
void trap_handler(void) {
    uint32_t mcause = 0x342; // mcause CSR address read via csrr
    uint32_t mcause_val;

    // Read mcause using csrr instruction (inline assembly will be needed)
    // For now, assume it's a timer interrupt
    timer_fired = 1;
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
    // Enable timer interrupts
    // Set MIE (Machine Interrupt Enable) bit in mstatus
    uint32_t mstatus = 0x1800; // MIE bit set
    // asm volatile ("csrw mstatus, %0" : : "r" (mstatus));

    // Enable timer interrupt in mie CSR
    uint32_t mie = 0x80; // MTIE bit (bit 7)
    // asm volatile ("csrw mie, %0" : : "r" (mie));

    // Set trap vector
    // asm volatile ("csrw mtvec, %0" : : "r" (trap_handler));

    // Set timer compare for near future
    uint64_t current_time = *((volatile uint64_t*)MTIME);
    *((volatile uint64_t*)MTIMECMP(0)) = current_time + 1000;

    // UART output
    const char* msg = "Interrupt test started\n";
    for (const char* p = msg; *p; p++) {
        *((volatile uint8_t*)0x10000000) = *p;
    }

    // Wait for interrupt
    uint32_t timeout = 1000000;
    while (timeout-- && !timer_fired) {
        delay(100);
    }

    // Report results
    if (timer_fired) {
        const char* success = "Timer interrupt detected!\n";
        for (const char* p = success; *p; p++) {
            *((volatile uint8_t*)0x10000000) = *p;
        }
    } else {
        const char* fail = "No interrupt detected\n";
        for (const char* p = fail; *p; p++) {
            *((volatile uint8_t*)0x10000000) = *p;
        }
    }

    return 0;
}