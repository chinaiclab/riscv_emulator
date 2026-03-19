#pragma once

#include <cstdint>

// Simple MMIO device interface
class Device {
public:
    virtual ~Device() = default;
    // Write 32‑bit value to device register at offset
    virtual void write(uint32_t offset, uint32_t value) = 0;
    // Read 32‑bit value from device register at offset
    virtual uint32_t read(uint32_t offset) = 0;
    // Optional interrupt request; return true if device raised an interrupt
    virtual bool poll_interrupt() { return false; }
};
