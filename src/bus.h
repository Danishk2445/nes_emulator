#pragma once
#include <cstdint>
#include <array>

class CPU;
class PPU;
class APU;
class Cartridge;
class Controller;

class Bus {
public:
    Bus();

    void connectCPU(CPU* c) { cpu = c; }
    void connectPPU(PPU* p) { ppu = p; }
    void connectAPU(APU* a) { apu = a; }
    void connectCartridge(Cartridge* c) { cartridge = c; }
    void connectController(Controller* c1, Controller* c2) { ctrl1 = c1; ctrl2 = c2; }

    // CPU reads/writes go through the bus
    uint8_t cpuRead(uint16_t addr);
    void cpuWrite(uint16_t addr, uint8_t val);

    // System clock
    void clock();

    uint64_t totalCycles() const { return systemClock; }

private:
    CPU* cpu = nullptr;
    PPU* ppu = nullptr;
    APU* apu = nullptr;
    Cartridge* cartridge = nullptr;
    Controller* ctrl1 = nullptr;
    Controller* ctrl2 = nullptr;

    // 2KB internal RAM
    std::array<uint8_t, 2048> ram{};

    uint64_t systemClock = 0;

    // DMA
    bool dmaActive = false;
    bool dmaSync = true;
    uint8_t dmaPage = 0;
    uint8_t dmaAddr = 0;
    uint8_t dmaData = 0;
};
