#include "cartridge.h"
#include <fstream>
#include <iostream>

bool Cartridge::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open ROM: " << path << "\n";
        return false;
    }

    // iNES header (16 bytes)
    uint8_t header[16];
    file.read(reinterpret_cast<char*>(header), 16);

    // Verify "NES\x1A" magic
    if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S' || header[3] != 0x1A) {
        std::cerr << "Invalid iNES header\n";
        return false;
    }

    prgBanks = header[4];
    chrBanks = header[5];

    uint8_t flags6 = header[6];
    uint8_t flags7 = header[7];

    mapper = (flags7 & 0xF0) | (flags6 >> 4);

    if (flags6 & 0x08)
        mirrorMode = MirrorMode::FourScreen;
    else if (flags6 & 0x01)
        mirrorMode = MirrorMode::Vertical;
    else
        mirrorMode = MirrorMode::Horizontal;

    // Skip trainer if present
    if (flags6 & 0x04) {
        file.seekg(512, std::ios::cur);
    }

    // Read PRG ROM
    prgRom.resize(prgBanks * 16384);
    file.read(reinterpret_cast<char*>(prgRom.data()), prgRom.size());

    // Read CHR ROM (or allocate CHR RAM)
    if (chrBanks == 0) {
        chrRom.resize(8192, 0); // 8KB CHR-RAM
    } else {
        chrRom.resize(chrBanks * 8192);
        file.read(reinterpret_cast<char*>(chrRom.data()), chrRom.size());
    }

    std::cout << "Loaded ROM: PRG=" << (int)prgBanks << "x16KB, CHR=" << (int)chrBanks
              << "x8KB, Mapper=" << (int)mapper << "\n";

    if (mapper != 0) {
        std::cerr << "Warning: Only Mapper 0 (NROM) is supported\n";
    }

    return true;
}

uint8_t Cartridge::cpuRead(uint16_t addr) const {
    // Mapper 0: $8000-$BFFF = first 16KB, $C000-$FFFF = last 16KB (or mirror)
    if (addr >= 0x8000) {
        uint32_t mapped = addr - 0x8000;
        if (prgBanks == 1) mapped &= 0x3FFF; // Mirror 16KB
        return prgRom[mapped % prgRom.size()];
    }
    return 0;
}

void Cartridge::cpuWrite(uint16_t addr, uint8_t val) {
    // Mapper 0 has no writable PRG
    (void)addr; (void)val;
}

uint8_t Cartridge::ppuRead(uint16_t addr) const {
    if (addr < 0x2000) {
        return chrRom[addr % chrRom.size()];
    }
    return 0;
}

void Cartridge::ppuWrite(uint16_t addr, uint8_t val) {
    // CHR-RAM is writable
    if (addr < 0x2000 && chrBanks == 0) {
        chrRom[addr % chrRom.size()] = const_cast<uint8_t&>(chrRom[addr % chrRom.size()]) = val;
    }
}
