#include "uart.h"

// UART output mutex - simple spinlock implementation
volatile uint32_t uart_mutex = 0;

// Function to acquire UART lock
void uart_lock(void) {
    uint32_t expected = UART_UNLOCKED;
    uint32_t desired = UART_LOCKED;
    // Simple test-and-set loop using inline assembly
    do {
        expected = UART_UNLOCKED;
        // Atomic swap: acquire lock by swapping UART_LOCKED into memory location
        __asm__ volatile("amoswap.w %0, %1, 0(%2)"
                        : "=r"(expected) : "r"(desired), "r"(&uart_mutex));
    } while (expected != UART_UNLOCKED);
}

// Function to release UART lock
void uart_unlock(void) {
    // Store 0 directly to uart_mutex - we use a simple store here
    // since the acquire side (uart_lock) uses cache-bypassing atomic operations
    // The fence ensures the write is visible before returning
    uart_mutex = UART_UNLOCKED;

    // Memory barrier to ensure the write completes and is visible to all cores
    __asm__ volatile("fence" ::: "memory");
}

// Simple retarget of write syscall to UART for bare-metal environment
ssize_t uart_write(const void *ptr, size_t len) {
    const uint8_t *cptr = (const uint8_t *)ptr;

    // Acquire UART lock to prevent output conflicts between cores
    uart_lock();

    // Use physical UART address 0x10000000 directly
    volatile uint32_t *uart = (volatile uint32_t*)0x10000000;
    for (size_t i = 0; i < len; ++i) {
        *uart = (uint32_t)cptr[i];
    }

    // Release UART lock
    uart_unlock();
    return (ssize_t)len;
}