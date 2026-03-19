#include <stdint.h>
#include <stdio.h>

extern uint32_t get_core_id(void);

void main(void) {
    uint32_t core_id = get_core_id();

    printf("RV32I printf test - Core#%lu\n", (unsigned long)core_id);

    // Test various printf formats
    printf("Decimal: %d, %u\n", -123, 456);
    printf("Hex: 0x%08X, 0x%x\n", 0x1234ABCD, 0xDEF);
    printf("String: %s\n", "Hello RV32I!");
    printf("Char: %c\n", 'A');

    // Test large numbers
    printf("Large numbers: %lu, %d\n", 1234567890UL, -987654321);

    printf("RV32I printf test completed successfully!\n");
}