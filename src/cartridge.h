#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class MirrorMode { Horizontal, Vertical, FourScreen };

class Cartridge {
public:
    bool load(const std::string& path);

    // CPU-side access (PRG ROM)
    uint8_t cpuRead(uint16_t addr) const;
    void cpuWrite(uint16_t addr, uint8_t val);

    // PPU-side access (CHR ROM/RAM)
    uint8_t ppuRead(uint16_t addr) const;
    void ppuWrite(uint16_t addr, uint8_t val);

    MirrorMode mirror() const { return mirrorMode; }
    uint8_t mapperId() const { return mapper; }

private:
    std::vector<uint8_t> prgRom;
    std::vector<uint8_t> chrRom; // may be RAM if 0 CHR banks
    uint8_t prgBanks = 0;
    uint8_t chrBanks = 0;
    uint8_t mapper = 0;
    MirrorMode mirrorMode = MirrorMode::Horizontal;
};
