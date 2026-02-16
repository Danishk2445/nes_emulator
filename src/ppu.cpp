#include "ppu.h"
#include "cartridge.h"

// NES system palette - 64 colors mapped to ARGB
static const uint32_t nesPalette[64] = {
    0xFF666666, 0xFF002A88, 0xFF1412A7, 0xFF3B00A4,
    0xFF5C007E, 0xFF6E0040, 0xFF6C0600, 0xFF561D00,
    0xFF333500, 0xFF0B4800, 0xFF005200, 0xFF004F08,
    0xFF00404D, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFADADAD, 0xFF155FD9, 0xFF4240FF, 0xFF7527FE,
    0xFFA01ACC, 0xFFB71E7B, 0xFFB53120, 0xFF994E00,
    0xFF6B6D00, 0xFF388700, 0xFF0C9300, 0xFF008F32,
    0xFF007C8D, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFFFFEFF, 0xFF64B0FF, 0xFF9290FF, 0xFFC676FF,
    0xFFF36AFF, 0xFFFE6ECC, 0xFFFE8170, 0xFFEA9E22,
    0xFFBCBE00, 0xFF88D800, 0xFF5CE430, 0xFF45E082,
    0xFF48CDDE, 0xFF4F4F4F, 0xFF000000, 0xFF000000,
    0xFFFFFEFF, 0xFFC0DFFF, 0xFFD3D2FF, 0xFFE8C8FF,
    0xFFFBC2FF, 0xFFFEC4EA, 0xFFFECCC5, 0xFFF7D8A5,
    0xFFE4E594, 0xFFCFEF96, 0xFFBDF4AB, 0xFFB3F3CC,
    0xFFB5EBF2, 0xFFB8B8B8, 0xFF000000, 0xFF000000,
};

PPU::PPU() {
    frameBuffer.fill(0xFF000000);
}

uint32_t PPU::nesColor(uint8_t idx) {
    return nesPalette[idx & 0x3F];
}

uint16_t PPU::mirrorNametable(uint16_t addr) {
    addr &= 0x0FFF; // relative to $2000
    if (!cartridge) return addr & 0x07FF;

    switch (cartridge->mirror()) {
        case MirrorMode::Vertical:
            return addr & 0x07FF;
        case MirrorMode::Horizontal:
            return ((addr / 0x800) * 0x400) + (addr & 0x03FF);
        case MirrorMode::FourScreen:
        default:
            return addr & 0x0FFF;
    }
}

uint8_t PPU::ppuRead(uint16_t addr) {
    addr &= 0x3FFF;

    if (addr < 0x2000) {
        return cartridge ? cartridge->ppuRead(addr) : 0;
    } else if (addr < 0x3F00) {
        return vram[mirrorNametable(addr - 0x2000)];
    } else {
        // Palette
        uint16_t palAddr = addr & 0x1F;
        // Mirrors $3F10/$3F14/$3F18/$3F1C -> $3F00/$3F04/$3F08/$3F0C
        if ((palAddr & 0x13) == 0x10) palAddr &= 0x0F;
        return palette[palAddr];
    }
}

void PPU::ppuWrite(uint16_t addr, uint8_t val) {
    addr &= 0x3FFF;

    if (addr < 0x2000) {
        if (cartridge) cartridge->ppuWrite(addr, val);
    } else if (addr < 0x3F00) {
        vram[mirrorNametable(addr - 0x2000)] = val;
    } else {
        uint16_t palAddr = addr & 0x1F;
        if ((palAddr & 0x13) == 0x10) palAddr &= 0x0F;
        palette[palAddr] = val;
    }
}

uint8_t PPU::cpuRead(uint16_t addr) {
    uint8_t data = 0;
    switch (addr & 7) {
        case 2: // PPUSTATUS
            data = (status & 0xE0) | (dataBuffer & 0x1F);
            status &= ~0x80; // clear VBlank
            nmiRaised = false;
            writeToggle = false;
            break;
        case 4: // OAMDATA
            data = oam[oamAddr];
            break;
        case 7: // PPUDATA
            data = dataBuffer;
            dataBuffer = ppuRead(vramAddr);
            // palette reads are not buffered
            if ((vramAddr & 0x3FFF) >= 0x3F00) {
                data = ppuRead(vramAddr);
                // buffer still gets the nametable "underneath"
                dataBuffer = ppuRead(vramAddr - 0x1000);
            }
            vramAddr += (ctrl & 0x04) ? 32 : 1;
            break;
        default:
            break;
    }
    return data;
}

void PPU::cpuWrite(uint16_t addr, uint8_t val) {
    switch (addr & 7) {
        case 0: // PPUCTRL
            ctrl = val;
            nmiOutput = (val & 0x80) != 0;
            // Update nametable select in temp address
            tempAddr = (tempAddr & 0xF3FF) | ((uint16_t)(val & 0x03) << 10);
            // If in VBlank and NMI was just enabled, signal NMI
            if (nmiOutput && (status & 0x80))
                nmiRaised = true;
            break;
        case 1: // PPUMASK
            mask = val;
            break;
        case 3: // OAMADDR
            oamAddr = val;
            break;
        case 4: // OAMDATA
            oam[oamAddr++] = val;
            break;
        case 5: // PPUSCROLL
            if (!writeToggle) {
                fineX = val & 0x07;
                tempAddr = (tempAddr & 0xFFE0) | (val >> 3);
            } else {
                tempAddr = (tempAddr & 0x8C1F) |
                           ((uint16_t)(val & 0x07) << 12) |
                           ((uint16_t)(val >> 3) << 5);
            }
            writeToggle = !writeToggle;
            break;
        case 6: // PPUADDR
            if (!writeToggle) {
                tempAddr = (tempAddr & 0x00FF) | ((uint16_t)(val & 0x3F) << 8);
            } else {
                tempAddr = (tempAddr & 0xFF00) | val;
                vramAddr = tempAddr;
            }
            writeToggle = !writeToggle;
            break;
        case 7: // PPUDATA
            ppuWrite(vramAddr, val);
            vramAddr += (ctrl & 0x04) ? 32 : 1;
            break;
    }
}

void PPU::incrementX() {
    if ((vramAddr & 0x001F) == 31) {
        vramAddr &= ~0x001F;
        vramAddr ^= 0x0400; // switch horizontal nametable
    } else {
        vramAddr++;
    }
}

void PPU::incrementY() {
    if ((vramAddr & 0x7000) != 0x7000) {
        vramAddr += 0x1000;
    } else {
        vramAddr &= ~0x7000;
        int coarseY = (vramAddr & 0x03E0) >> 5;
        if (coarseY == 29) {
            coarseY = 0;
            vramAddr ^= 0x0800; // switch vertical nametable
        } else if (coarseY == 31) {
            coarseY = 0;
        } else {
            coarseY++;
        }
        vramAddr = (vramAddr & ~0x03E0) | (coarseY << 5);
    }
}

void PPU::transferX() {
    // Copy horizontal bits from t to v
    vramAddr = (vramAddr & ~0x041F) | (tempAddr & 0x041F);
}

void PPU::transferY() {
    // Copy vertical bits from t to v
    vramAddr = (vramAddr & ~0x7BE0) | (tempAddr & 0x7BE0);
}

void PPU::loadBackgroundShifters() {
    bgShiftLo = (bgShiftLo & 0xFF00) | bgLo;
    bgShiftHi = (bgShiftHi & 0xFF00) | bgHi;
    atShiftLo = (atShiftLo & 0xFF00) | ((atLatchLo) ? 0xFF : 0x00);
    atShiftHi = (atShiftHi & 0xFF00) | ((atLatchHi) ? 0xFF : 0x00);
}

void PPU::updateShifters() {
    if (mask & 0x08) { // show background
        bgShiftLo <<= 1;
        bgShiftHi <<= 1;
        atShiftLo <<= 1;
        atShiftHi <<= 1;
    }
}

void PPU::evaluateSprites() {
    spriteCount = 0;
    sprite0OnLine = false;

    int spriteHeight = (ctrl & 0x20) ? 16 : 8;

    for (int i = 0; i < 64 && spriteCount < 8; i++) {
        int diff = scanline - (int)oam[i * 4];
        if (diff >= 0 && diff < spriteHeight) {
            if (i == 0) sprite0OnLine = true;

            Sprite& s = spriteLine[spriteCount];
            s.y    = oam[i * 4 + 0];
            s.tile = oam[i * 4 + 1];
            s.attr = oam[i * 4 + 2];
            s.x    = oam[i * 4 + 3];

            // Fetch sprite pattern
            uint16_t patternAddr;
            int row = diff;

            // Vertical flip
            if (s.attr & 0x80) row = spriteHeight - 1 - row;

            if (spriteHeight == 8) {
                uint16_t table = (ctrl & 0x08) ? 0x1000 : 0x0000;
                patternAddr = table + s.tile * 16 + row;
            } else {
                // 8x16 sprites
                uint16_t table = (s.tile & 1) ? 0x1000 : 0x0000;
                uint8_t tileNum = s.tile & 0xFE;
                if (row >= 8) {
                    tileNum++;
                    row -= 8;
                }
                patternAddr = table + tileNum * 16 + row;
            }

            uint8_t lo = ppuRead(patternAddr);
            uint8_t hi = ppuRead(patternAddr + 8);

            // Horizontal flip
            if (s.attr & 0x40) {
                // Reverse bits
                auto flipByte = [](uint8_t b) -> uint8_t {
                    b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4);
                    b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
                    b = ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
                    return b;
                };
                lo = flipByte(lo);
                hi = flipByte(hi);
            }

            spriteShiftLo[spriteCount] = lo;
            spriteShiftHi[spriteCount] = hi;
            spriteCount++;
        }
    }
}

void PPU::renderPixel() {
    int x = cycle - 1;
    if (x < 0 || x >= 256 || scanline < 0 || scanline >= 240) return;

    // Background pixel
    uint8_t bgPixel = 0;
    uint8_t bgPalette = 0;

    if (mask & 0x08) {
        if ((mask & 0x02) || x >= 8) {
            uint16_t mux = 0x8000 >> fineX;
            uint8_t p0 = (bgShiftLo & mux) ? 1 : 0;
            uint8_t p1 = (bgShiftHi & mux) ? 1 : 0;
            bgPixel = (p1 << 1) | p0;

            uint8_t a0 = (atShiftLo & mux) ? 1 : 0;
            uint8_t a1 = (atShiftHi & mux) ? 1 : 0;
            bgPalette = (a1 << 1) | a0;
        }
    }

    // Sprite pixel
    uint8_t sprPixel = 0;
    uint8_t sprPalette = 0;
    uint8_t sprPriority = 0;
    bool spriteZero = false;

    if (mask & 0x10) {
        if ((mask & 0x04) || x >= 8) {
            for (int i = 0; i < spriteCount; i++) {
                int offset = x - spriteLine[i].x;
                if (offset < 0 || offset >= 8) continue;

                uint8_t p0 = (spriteShiftLo[i] >> (7 - offset)) & 1;
                uint8_t p1 = (spriteShiftHi[i] >> (7 - offset)) & 1;
                uint8_t pixel = (p1 << 1) | p0;

                if (pixel == 0) continue;

                sprPixel = pixel;
                sprPalette = (spriteLine[i].attr & 0x03) + 4;
                sprPriority = (spriteLine[i].attr & 0x20) ? 1 : 0;
                if (i == 0 && sprite0OnLine) spriteZero = true;
                break;
            }
        }
    }

    // Compositing
    uint8_t finalPixel = 0;
    uint8_t finalPalette = 0;

    if (bgPixel == 0 && sprPixel == 0) {
        finalPixel = 0; finalPalette = 0;
    } else if (bgPixel == 0 && sprPixel > 0) {
        finalPixel = sprPixel; finalPalette = sprPalette;
    } else if (bgPixel > 0 && sprPixel == 0) {
        finalPixel = bgPixel; finalPalette = bgPalette;
    } else {
        // Sprite 0 hit check
        if (spriteZero && !sprite0Hit && x < 255) {
            if ((mask & 0x18) == 0x18) {
                if (!((mask & 0x06) != 0x06 && x < 8)) {
                    sprite0Hit = true;
                    status |= 0x40;
                }
            }
        }
        if (sprPriority == 0) {
            finalPixel = sprPixel; finalPalette = sprPalette;
        } else {
            finalPixel = bgPixel; finalPalette = bgPalette;
        }
    }

    uint8_t colorIdx = ppuRead(0x3F00 + finalPalette * 4 + finalPixel) & 0x3F;
    frameBuffer[scanline * 256 + x] = nesColor(colorIdx);
}

void PPU::clock() {
    bool rendering = (mask & 0x18) != 0;

    // Pre-render scanline
    if (scanline == -1) {
        if (cycle == 1) {
            status &= ~0xE0; // clear VBlank, sprite 0 hit, overflow
            nmiRaised = false;
            sprite0Hit = false;
        }
        if (rendering) {
            if (cycle >= 280 && cycle <= 304) {
                transferY();
            }
            if (cycle == 257) {
                transferX();
            }
            // Background fetches on pre-render line
            if ((cycle >= 1 && cycle <= 256) || (cycle >= 321 && cycle <= 336)) {
                updateShifters();
                switch ((cycle - 1) % 8) {
                    case 0: {
                        loadBackgroundShifters();
                        uint16_t ntAddr = 0x2000 | (vramAddr & 0x0FFF);
                        ntByte = ppuRead(ntAddr);
                        break;
                    }
                    case 2: {
                        uint16_t atAddr = 0x23C0 | (vramAddr & 0x0C00) |
                                          ((vramAddr >> 4) & 0x38) | ((vramAddr >> 2) & 0x07);
                        atByte = ppuRead(atAddr);
                        if (vramAddr & 0x40) atByte >>= 4;
                        if (vramAddr & 0x02) atByte >>= 2;
                        atLatchLo = atByte & 1;
                        atLatchHi = (atByte >> 1) & 1;
                        break;
                    }
                    case 4: {
                        uint16_t bgTable = (ctrl & 0x10) ? 0x1000 : 0x0000;
                        uint16_t fineY = (vramAddr >> 12) & 0x07;
                        bgLo = ppuRead(bgTable + ntByte * 16 + fineY);
                        break;
                    }
                    case 6: {
                        uint16_t bgTable = (ctrl & 0x10) ? 0x1000 : 0x0000;
                        uint16_t fineY = (vramAddr >> 12) & 0x07;
                        bgHi = ppuRead(bgTable + ntByte * 16 + fineY + 8);
                        break;
                    }
                    case 7:
                        incrementX();
                        break;
                }
            }
            if (cycle == 256) incrementY();
        }
        // Odd frame cycle skip
        if (cycle == 339 && rendering) {
            // skip to cycle 0 of scanline 0 on odd frames
            static bool oddFrame = false;
            if (oddFrame) {
                cycle = 0;
                scanline = 0;
                oddFrame = !oddFrame;
                return;
            }
            oddFrame = !oddFrame;
        }
    }

    // Visible scanlines 0-239
    if (scanline >= 0 && scanline < 240) {
        if (rendering) {
            // Background fetch and rendering
            if ((cycle >= 1 && cycle <= 256) || (cycle >= 321 && cycle <= 336)) {
                updateShifters();
                switch ((cycle - 1) % 8) {
                    case 0: {
                        loadBackgroundShifters();
                        uint16_t ntAddr = 0x2000 | (vramAddr & 0x0FFF);
                        ntByte = ppuRead(ntAddr);
                        break;
                    }
                    case 2: {
                        uint16_t atAddr = 0x23C0 | (vramAddr & 0x0C00) |
                                          ((vramAddr >> 4) & 0x38) | ((vramAddr >> 2) & 0x07);
                        atByte = ppuRead(atAddr);
                        if (vramAddr & 0x40) atByte >>= 4;
                        if (vramAddr & 0x02) atByte >>= 2;
                        atLatchLo = atByte & 1;
                        atLatchHi = (atByte >> 1) & 1;
                        break;
                    }
                    case 4: {
                        uint16_t bgTable = (ctrl & 0x10) ? 0x1000 : 0x0000;
                        uint16_t fineY = (vramAddr >> 12) & 0x07;
                        bgLo = ppuRead(bgTable + ntByte * 16 + fineY);
                        break;
                    }
                    case 6: {
                        uint16_t bgTable = (ctrl & 0x10) ? 0x1000 : 0x0000;
                        uint16_t fineY = (vramAddr >> 12) & 0x07;
                        bgHi = ppuRead(bgTable + ntByte * 16 + fineY + 8);
                        break;
                    }
                    case 7:
                        incrementX();
                        break;
                }
            }

            if (cycle == 256) incrementY();
            if (cycle == 257) {
                transferX();
                evaluateSprites();
            }
        }

        // Render pixel
        if (cycle >= 1 && cycle <= 256) {
            renderPixel();
        }
    }

    // VBlank start at scanline 241
    if (scanline == 241 && cycle == 1) {
        status |= 0x80; // set VBlank
        frameReady = true;
        if (nmiOutput) {
            nmiRaised = true;
        }
    }

    // Advance cycle/scanline
    cycle++;
    if (cycle > 340) {
        cycle = 0;
        scanline++;
        if (scanline > 260) {
            scanline = -1;
        }
    }
}
