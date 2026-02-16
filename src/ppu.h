#pragma once
#include <cstdint>
#include <array>

class Cartridge;

class PPU {
public:
    PPU();

    void connectCartridge(Cartridge* cart) { cartridge = cart; }

    // CPU-facing register access
    uint8_t cpuRead(uint16_t addr);
    void cpuWrite(uint16_t addr, uint8_t val);

    // Clock one PPU cycle
    void clock();

    // Framebuffer access
    const uint32_t* getFrameBuffer() const { return frameBuffer.data(); }
    bool isFrameReady() const { return frameReady; }
    void clearFrameReady() { frameReady = false; }

    // NMI signal
    bool nmiOccurred() const { return nmiOutput && nmiRaised; }
    void clearNMI() { nmiRaised = false; }

    // OAM DMA
    uint8_t* getOAM() { return oam.data(); }

private:
    Cartridge* cartridge = nullptr;

    // Internal memory
    std::array<uint8_t, 2048> vram{};       // 2KB nametable VRAM
    std::array<uint8_t, 32>   palette{};     // palette RAM
    std::array<uint8_t, 256>  oam{};         // OAM (sprite data)

    // Framebuffer (256 x 240, ARGB)
    std::array<uint32_t, 256 * 240> frameBuffer{};
    bool frameReady = false;

    // Scanline / cycle counters
    int scanline = -1;  // -1 = pre-render, 0-239 = visible, 241 = post/vblank
    int cycle = 0;

    // PPU registers
    uint8_t ctrl = 0;      // $2000 PPUCTRL
    uint8_t mask = 0;      // $2001 PPUMASK
    uint8_t status = 0;    // $2002 PPUSTATUS

    uint8_t oamAddr = 0;   // $2003 OAMADDR

    // Scrolling / address
    uint16_t vramAddr = 0; // current VRAM address (loopy v)
    uint16_t tempAddr = 0; // temporary VRAM address (loopy t)
    uint8_t  fineX = 0;    // fine X scroll
    bool     writeToggle = false; // address latch (w)

    // Data buffer for $2007 reads
    uint8_t dataBuffer = 0;

    // NMI
    bool nmiOutput = false;  // controlled by PPUCTRL bit 7
    bool nmiRaised = false;

    // Background rendering latches
    uint8_t  ntByte = 0;
    uint8_t  atByte = 0;
    uint8_t  bgLo = 0;
    uint8_t  bgHi = 0;
    uint16_t bgShiftLo = 0;
    uint16_t bgShiftHi = 0;
    uint16_t atShiftLo = 0;
    uint16_t atShiftHi = 0;
    uint8_t  atLatchLo = 0;
    uint8_t  atLatchHi = 0;

    // Sprite rendering
    struct Sprite {
        uint8_t y;
        uint8_t tile;
        uint8_t attr;
        uint8_t x;
    };
    std::array<Sprite, 8> spriteLine{};
    std::array<uint8_t, 8> spriteShiftLo{};
    std::array<uint8_t, 8> spriteShiftHi{};
    int spriteCount = 0;
    bool sprite0OnLine = false;
    bool sprite0Hit = false;

    // Internal read/write to VRAM
    uint8_t ppuRead(uint16_t addr);
    void ppuWrite(uint16_t addr, uint8_t val);

    // Nametable mirroring
    uint16_t mirrorNametable(uint16_t addr);

    // Rendering helpers
    void renderPixel();
    void loadBackgroundShifters();
    void updateShifters();
    void evaluateSprites();

    void incrementX();
    void incrementY();
    void transferX();
    void transferY();

    // NES palette -> ARGB
    static uint32_t nesColor(uint8_t idx);
};
