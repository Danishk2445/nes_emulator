#pragma once
#include <cstdint>
#include <string>

class Bus;

class CPU {
public:
    CPU();

    void connectBus(Bus* bus) { this->bus = bus; }
    void reset();
    void clock();
    void nmi();
    void irq();

    // For DMA stalling
    int stallCycles = 0;

private:
    Bus* bus = nullptr;

    // Registers
    uint8_t  a = 0;     // Accumulator
    uint8_t  x = 0;     // X index
    uint8_t  y = 0;     // Y index
    uint8_t  sp = 0;    // Stack pointer
    uint16_t pc = 0;    // Program counter

    // Status flags
    bool flagC = false; // Carry
    bool flagZ = false; // Zero
    bool flagI = false; // Interrupt disable
    bool flagD = false; // Decimal (unused on NES)
    bool flagB = false; // Break
    bool flagV = false; // Overflow
    bool flagN = false; // Negative

    int cycles = 0; // cycles remaining for current instruction

    // Memory access
    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t val);

    // Stack
    void push(uint8_t val);
    void push16(uint16_t val);
    uint8_t pull();
    uint16_t pull16();

    // Flags
    uint8_t getStatus() const;
    void setStatus(uint8_t val);
    void setZN(uint8_t val);

    // Addressing modes (return address)
    enum class AddrMode {
        IMP, ACC, IMM, ZP, ZPX, ZPY,
        ABS, ABX, ABY, IND, IZX, IZY, REL
    };

    // Execute one instruction
    void execute();

    // Page cross check
    bool pageCross(uint16_t a, uint16_t b) { return (a & 0xFF00) != (b & 0xFF00); }
};
