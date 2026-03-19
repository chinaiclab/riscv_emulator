#include "../include/device/UART.h"
#include "../include/utils/DebugLogger.h"
#include <iostream>

// Static mutex definition
std::mutex UART::uart_mutex;

UART::UART() {
    // Initialize registers: Data Register cleared, Flag Register set to RXFE (receive FIFO empty)
    dr = 0;
    fr = 0x90; // Bit0 (RXFE) = 1, Bit5 (TXFF) = 0, others cleared

    // Initialize UART16550 compatible registers
    lcr = 0;   // Line Control Register - DLAB=0 by default
    lsr = 0x60; // Line Status Register - THRE=1, TEMT=1 (transmitter ready)
}

void UART::write(uint32_t offset, uint32_t value) {
    // UART16550 compatible registers
    switch (offset) {
        case 0: // THR (Transmit Holding Register) - when DLAB=0
        case 1: // DLL (Divisor Latch Low) - when DLAB=1
        case 2: // FCR (FIFO Control Register) - write only
            // FCR is write-only, ignored for minimal implementation
            break;
        case 3: // LCR (Line Control Register)
            lcr = value & 0xFF; // Store LCR for DLAB checking
            break;
        case 4: // MCR (Modem Control Register)
            // Ignored for minimal implementation
            break;
        default:
            // Other write registers ignored
            break;
    }

    // For THR write (offset 0 when DLAB=0), output the character
    if (offset == 0 && (lcr & 0x80) == 0) {
        dr = value & 0xFF;
        output_char(static_cast<uint8_t>(dr));
        // After sending, set THRE bit (transmitter holding register empty)
        lsr |= 0x60; // Set THRE (bit5) and TEMT (bit6)
    }
}

uint32_t UART::read(uint32_t offset) {
    // UART16550 compatible registers
    switch (offset) {
        case 0: // RBR (Receive Buffer Register) - when DLAB=0
        case 1: // DLL (Divisor Latch Low) - when DLAB=1
            // std::cerr << "[UART] read offset=" << offset << " returning dr=0x" << std::hex << dr << std::endl;
            return dr;
        case 5: // LSR (Line Status Register)
            // std::cerr << "[UART] read LSR offset=" << offset << " returning lsr=0x" << std::hex << lsr << std::endl;
            return lsr;
        case 4: // Legacy PL011 FR (Flag Register) - for backward compatibility
            return fr;
        default:
            // std::cerr << "[UART] read offset=" << offset << " returning 0" << std::endl;
            return 0;
    }
}

void UART::output_char(uint8_t ch) {
    // Use mutex to ensure thread-safe UART access
    std::lock_guard<std::mutex> lock(uart_mutex);
    std::cout << static_cast<char>(ch) << std::flush;
#if DEBUG
    // Debug output to stderr to see if UART is being accessed
    UART_LOGF("Output char: %c (0x%02x)", static_cast<char>(ch), static_cast<int>(ch));
#endif
}
