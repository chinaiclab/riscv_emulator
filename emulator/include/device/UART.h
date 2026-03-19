#pragma once

#include "system/Device.h"
#include <cstdint>
#include <iostream>
#include <mutex>

// PL011‑like UART device simulation.
// Supports a minimal subset of registers sufficient for printing characters.
// Register offsets are in bytes (word‑addressable, offset divided by 4).
class UART : public Device {
public:
    UART();
    // Write a 32‑bit value to a register at the given offset.
    // Offset 0 corresponds to the Data Register (DR).
    void write(uint32_t offset, uint32_t value) override;
    // Read a 32‑bit value from a register at the given offset.
    uint32_t read(uint32_t offset) override;
private:
    // Data Register – holds the last written byte.
    uint32_t dr = 0;
    // Flag Register – bit0: RXFE (receive FIFO empty), bit5: TXFF (transmit FIFO full).
    uint32_t fr = 0x90; // RXFE=1 (bit0), TXFF=0 (bit5 cleared).

    // UART16550 compatible registers
    uint32_t lcr = 0;  // Line Control Register - used for DLAB (Divisor Latch Access Bit)
    uint32_t lsr = 0x60; // Line Status Register - THRE (bit5) and TEMT (bit6) set by default

    // Mutex for thread-safe UART access
    static std::mutex uart_mutex;

    // Helper to output a character.
    void output_char(uint8_t ch);
};
