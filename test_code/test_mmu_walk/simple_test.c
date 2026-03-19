// Simple MMU test to debug cache coherence issues
#include "../../init_code/uart_puts.h"

void write_word(uint32_t addr, uint32_t value) {
    *(volatile uint32_t*)addr = value;
}

uint32_t read_word(uint32_t addr) {
    return *(volatile uint32_t*)addr;
}

int main(void) {
    printf("=== SIMPLE MMU TEST ===\n");

    // Test basic memory first
    write_word(0x1000, 0x12345678);
    uint32_t test = read_word(0x1000);
    if (test == 0x12345678) {
        printf("Basic memory test: PASS\n");
    } else {
        printf("Basic memory test: FAIL\n");
    }

    // Simple page table test
    printf("Page table test:\n");
    uint32_t page_table_base = 0x20000;

    // Write one PTE
    uint32_t pte_addr = page_table_base + (0 * 4);  // Page 0
    uint32_t pte_value = 0x7;  // Simple PTE

    printf("Writing PTE 0x");
    printf("7");
    printf(" to address 0x20000\n");

    write_word(pte_addr, pte_value);

    // Read it back immediately
    uint32_t read_back = read_word(pte_addr);

    printf("Read back: 0x");
    if (read_back == 0x7) {
        printf("7 - SUCCESS!\n");
    } else {
        printf("00000000 - FAILED!\n");
    }

    printf("Test complete.\n");

    while (1) { }
}