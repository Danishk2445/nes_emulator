#include "bus.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "cartridge.h"
#include "controller.h"

Bus::Bus() {
    ram.fill(0);
}

uint8_t Bus::cpuRead(uint16_t addr) {
    if (addr < 0x2000) {
        return ram[addr & 0x07FF];
    } else if (addr < 0x4000) {
        return ppu ? ppu->cpuRead(addr) : 0;
    } else if (addr == 0x4015) {
        return apu ? apu->cpuRead(addr) : 0;
    } else if (addr == 0x4016) {
        return ctrl1 ? ctrl1->read() : 0;
    } else if (addr == 0x4017) {
        return ctrl2 ? ctrl2->read() : 0;
    } else if (addr < 0x4020) {
        // Other APU/IO registers
        return 0;
    } else {
        return cartridge ? cartridge->cpuRead(addr) : 0;
    }
}

void Bus::cpuWrite(uint16_t addr, uint8_t val) {
    if (addr < 0x2000) {
        ram[addr & 0x07FF] = val;
    } else if (addr < 0x4000) {
        if (ppu) ppu->cpuWrite(addr, val);
    } else if (addr == 0x4014) {
        // OAM DMA
        dmaPage = val;
        dmaAddr = 0;
        dmaActive = true;
        dmaSync = true;
    } else if (addr == 0x4016) {
        if (ctrl1) ctrl1->write(val);
        if (ctrl2) ctrl2->write(val);
    } else if (addr < 0x4020) {
        // APU registers ($4000-$4013, $4015, $4017)
        if (apu) apu->cpuWrite(addr, val);
    } else {
        if (cartridge) cartridge->cpuWrite(addr, val);
    }
}

void Bus::clock() {
    // PPU runs at 3x CPU speed
    if (ppu) ppu->clock();

    if (systemClock % 3 == 0) {
        // Handle DMA
        if (dmaActive) {
            if (dmaSync) {
                if (systemClock % 2 == 1) {
                    dmaSync = false;
                }
            } else {
                if (systemClock % 2 == 0) {
                    // Read
                    dmaData = cpuRead((uint16_t)dmaPage << 8 | dmaAddr);
                } else {
                    // Write to OAM
                    if (ppu) ppu->getOAM()[dmaAddr] = dmaData;
                    dmaAddr++;
                    if (dmaAddr == 0) { // wrapped around = done
                        dmaActive = false;
                    }
                }
            }
        } else {
            if (cpu) cpu->clock();
        }

        // Clock APU at CPU rate
        if (apu) apu->clock();
    }

    // Check for NMI
    if (ppu && cpu && ppu->nmiOccurred()) {
        ppu->clearNMI();
        cpu->nmi();
    }

    systemClock++;
}
