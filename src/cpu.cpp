#include "cpu.h"
#include "bus.h"

CPU::CPU() {
    reset();
}

uint8_t CPU::read(uint16_t addr) {
    return bus ? bus->cpuRead(addr) : 0;
}

void CPU::write(uint16_t addr, uint8_t val) {
    if (bus) bus->cpuWrite(addr, val);
}

void CPU::push(uint8_t val) {
    write(0x0100 + sp, val);
    sp--;
}

void CPU::push16(uint16_t val) {
    push((val >> 8) & 0xFF);
    push(val & 0xFF);
}

uint8_t CPU::pull() {
    sp++;
    return read(0x0100 + sp);
}

uint16_t CPU::pull16() {
    uint16_t lo = pull();
    uint16_t hi = pull();
    return (hi << 8) | lo;
}

uint8_t CPU::getStatus() const {
    uint8_t s = 0x20; // bit 5 always set
    if (flagC) s |= 0x01;
    if (flagZ) s |= 0x02;
    if (flagI) s |= 0x04;
    if (flagD) s |= 0x08;
    if (flagB) s |= 0x10;
    if (flagV) s |= 0x40;
    if (flagN) s |= 0x80;
    return s;
}

void CPU::setStatus(uint8_t val) {
    flagC = val & 0x01;
    flagZ = val & 0x02;
    flagI = val & 0x04;
    flagD = val & 0x08;
    flagB = val & 0x10;
    flagV = val & 0x40;
    flagN = val & 0x80;
}

void CPU::setZN(uint8_t val) {
    flagZ = (val == 0);
    flagN = (val & 0x80) != 0;
}

void CPU::reset() {
    a = 0; x = 0; y = 0;
    sp = 0xFD;
    setStatus(0x24); // I flag set
    pc = read(0xFFFC) | ((uint16_t)read(0xFFFD) << 8);
    cycles = 8;
}

void CPU::nmi() {
    push16(pc);
    flagB = false;
    push(getStatus() | 0x20);
    flagI = true;
    pc = read(0xFFFA) | ((uint16_t)read(0xFFFB) << 8);
    cycles = 7;
}

void CPU::irq() {
    if (flagI) return;
    push16(pc);
    flagB = false;
    push(getStatus() | 0x20);
    flagI = true;
    pc = read(0xFFFE) | ((uint16_t)read(0xFFFF) << 8);
    cycles = 7;
}

void CPU::clock() {
    if (cycles > 0) {
        cycles--;
        return;
    }
    execute();
    cycles--;  // account for the cycle we just used
}

void CPU::execute() {
    uint8_t opcode = read(pc++);

    // Decode addressing mode and get operand address
    uint16_t addr = 0;
    bool extraCycle = false;

    // Helper lambdas for common addressing
    auto imm = [&]() -> uint16_t { return pc++; };
    auto zp  = [&]() -> uint16_t { return read(pc++) & 0xFF; };
    auto zpx = [&]() -> uint16_t { return (read(pc++) + x) & 0xFF; };
    auto zpy = [&]() -> uint16_t { return (read(pc++) + y) & 0xFF; };
    auto abs_ = [&]() -> uint16_t {
        uint16_t lo = read(pc++);
        uint16_t hi = read(pc++);
        return (hi << 8) | lo;
    };
    auto abx = [&](bool checkPage = true) -> uint16_t {
        uint16_t lo = read(pc++);
        uint16_t hi = read(pc++);
        uint16_t base = (hi << 8) | lo;
        uint16_t result = base + x;
        if (checkPage && pageCross(base, result)) extraCycle = true;
        return result;
    };
    auto aby = [&](bool checkPage = true) -> uint16_t {
        uint16_t lo = read(pc++);
        uint16_t hi = read(pc++);
        uint16_t base = (hi << 8) | lo;
        uint16_t result = base + y;
        if (checkPage && pageCross(base, result)) extraCycle = true;
        return result;
    };
    auto izx = [&]() -> uint16_t {
        uint8_t ptr = read(pc++) + x;
        uint16_t lo = read(ptr & 0xFF);
        uint16_t hi = read((ptr + 1) & 0xFF);
        return (hi << 8) | lo;
    };
    auto izy = [&](bool checkPage = true) -> uint16_t {
        uint8_t ptr = read(pc++);
        uint16_t lo = read(ptr & 0xFF);
        uint16_t hi = read((ptr + 1) & 0xFF);
        uint16_t base = (hi << 8) | lo;
        uint16_t result = base + y;
        if (checkPage && pageCross(base, result)) extraCycle = true;
        return result;
    };

    switch (opcode) {
        // ===== ADC =====
        case 0x69: { addr = imm(); cycles = 2; goto do_adc; }
        case 0x65: { addr = zp();  cycles = 3; goto do_adc; }
        case 0x75: { addr = zpx(); cycles = 4; goto do_adc; }
        case 0x6D: { addr = abs_(); cycles = 4; goto do_adc; }
        case 0x7D: { addr = abx(); cycles = 4; goto do_adc; }
        case 0x79: { addr = aby(); cycles = 4; goto do_adc; }
        case 0x61: { addr = izx(); cycles = 6; goto do_adc; }
        case 0x71: { addr = izy(); cycles = 5;
            do_adc: {
                uint8_t m = read(addr);
                uint16_t sum = a + m + (flagC ? 1 : 0);
                flagC = sum > 0xFF;
                flagV = (~(a ^ m) & (a ^ sum) & 0x80) != 0;
                a = sum & 0xFF;
                setZN(a);
                if (extraCycle) cycles++;
                break;
            }
        }

        // ===== SBC =====
        case 0xE9: { addr = imm(); cycles = 2; goto do_sbc; }
        case 0xE5: { addr = zp();  cycles = 3; goto do_sbc; }
        case 0xF5: { addr = zpx(); cycles = 4; goto do_sbc; }
        case 0xED: { addr = abs_(); cycles = 4; goto do_sbc; }
        case 0xFD: { addr = abx(); cycles = 4; goto do_sbc; }
        case 0xF9: { addr = aby(); cycles = 4; goto do_sbc; }
        case 0xE1: { addr = izx(); cycles = 6; goto do_sbc; }
        case 0xF1: { addr = izy(); cycles = 5;
            do_sbc: {
                uint8_t m = read(addr) ^ 0xFF;
                uint16_t sum = a + m + (flagC ? 1 : 0);
                flagC = sum > 0xFF;
                flagV = (~(a ^ m) & (a ^ sum) & 0x80) != 0;
                a = sum & 0xFF;
                setZN(a);
                if (extraCycle) cycles++;
                break;
            }
        }

        // ===== AND =====
        case 0x29: { addr = imm(); cycles = 2; goto do_and; }
        case 0x25: { addr = zp();  cycles = 3; goto do_and; }
        case 0x35: { addr = zpx(); cycles = 4; goto do_and; }
        case 0x2D: { addr = abs_(); cycles = 4; goto do_and; }
        case 0x3D: { addr = abx(); cycles = 4; goto do_and; }
        case 0x39: { addr = aby(); cycles = 4; goto do_and; }
        case 0x21: { addr = izx(); cycles = 6; goto do_and; }
        case 0x31: { addr = izy(); cycles = 5;
            do_and:
                a &= read(addr);
                setZN(a);
                if (extraCycle) cycles++;
                break;
        }

        // ===== ORA =====
        case 0x09: { addr = imm(); cycles = 2; goto do_ora; }
        case 0x05: { addr = zp();  cycles = 3; goto do_ora; }
        case 0x15: { addr = zpx(); cycles = 4; goto do_ora; }
        case 0x0D: { addr = abs_(); cycles = 4; goto do_ora; }
        case 0x1D: { addr = abx(); cycles = 4; goto do_ora; }
        case 0x19: { addr = aby(); cycles = 4; goto do_ora; }
        case 0x01: { addr = izx(); cycles = 6; goto do_ora; }
        case 0x11: { addr = izy(); cycles = 5;
            do_ora:
                a |= read(addr);
                setZN(a);
                if (extraCycle) cycles++;
                break;
        }

        // ===== EOR =====
        case 0x49: { addr = imm(); cycles = 2; goto do_eor; }
        case 0x45: { addr = zp();  cycles = 3; goto do_eor; }
        case 0x55: { addr = zpx(); cycles = 4; goto do_eor; }
        case 0x4D: { addr = abs_(); cycles = 4; goto do_eor; }
        case 0x5D: { addr = abx(); cycles = 4; goto do_eor; }
        case 0x59: { addr = aby(); cycles = 4; goto do_eor; }
        case 0x41: { addr = izx(); cycles = 6; goto do_eor; }
        case 0x51: { addr = izy(); cycles = 5;
            do_eor:
                a ^= read(addr);
                setZN(a);
                if (extraCycle) cycles++;
                break;
        }

        // ===== CMP =====
        case 0xC9: { addr = imm(); cycles = 2; goto do_cmp; }
        case 0xC5: { addr = zp();  cycles = 3; goto do_cmp; }
        case 0xD5: { addr = zpx(); cycles = 4; goto do_cmp; }
        case 0xCD: { addr = abs_(); cycles = 4; goto do_cmp; }
        case 0xDD: { addr = abx(); cycles = 4; goto do_cmp; }
        case 0xD9: { addr = aby(); cycles = 4; goto do_cmp; }
        case 0xC1: { addr = izx(); cycles = 6; goto do_cmp; }
        case 0xD1: { addr = izy(); cycles = 5;
            do_cmp: {
                uint8_t m = read(addr);
                flagC = a >= m;
                setZN(a - m);
                if (extraCycle) cycles++;
                break;
            }
        }

        // ===== CPX =====
        case 0xE0: { addr = imm(); cycles = 2; goto do_cpx; }
        case 0xE4: { addr = zp();  cycles = 3; goto do_cpx; }
        case 0xEC: { addr = abs_(); cycles = 4;
            do_cpx: {
                uint8_t m = read(addr);
                flagC = x >= m;
                setZN(x - m);
                break;
            }
        }

        // ===== CPY =====
        case 0xC0: { addr = imm(); cycles = 2; goto do_cpy; }
        case 0xC4: { addr = zp();  cycles = 3; goto do_cpy; }
        case 0xCC: { addr = abs_(); cycles = 4;
            do_cpy: {
                uint8_t m = read(addr);
                flagC = y >= m;
                setZN(y - m);
                break;
            }
        }

        // ===== BIT =====
        case 0x24: { addr = zp();  cycles = 3; goto do_bit; }
        case 0x2C: { addr = abs_(); cycles = 4;
            do_bit: {
                uint8_t m = read(addr);
                flagZ = (a & m) == 0;
                flagV = (m & 0x40) != 0;
                flagN = (m & 0x80) != 0;
                break;
            }
        }

        // ===== LDA =====
        case 0xA9: { addr = imm(); cycles = 2; goto do_lda; }
        case 0xA5: { addr = zp();  cycles = 3; goto do_lda; }
        case 0xB5: { addr = zpx(); cycles = 4; goto do_lda; }
        case 0xAD: { addr = abs_(); cycles = 4; goto do_lda; }
        case 0xBD: { addr = abx(); cycles = 4; goto do_lda; }
        case 0xB9: { addr = aby(); cycles = 4; goto do_lda; }
        case 0xA1: { addr = izx(); cycles = 6; goto do_lda; }
        case 0xB1: { addr = izy(); cycles = 5;
            do_lda:
                a = read(addr);
                setZN(a);
                if (extraCycle) cycles++;
                break;
        }

        // ===== LDX =====
        case 0xA2: { addr = imm(); cycles = 2; goto do_ldx; }
        case 0xA6: { addr = zp();  cycles = 3; goto do_ldx; }
        case 0xB6: { addr = zpy(); cycles = 4; goto do_ldx; }
        case 0xAE: { addr = abs_(); cycles = 4; goto do_ldx; }
        case 0xBE: { addr = aby(); cycles = 4;
            do_ldx:
                x = read(addr);
                setZN(x);
                if (extraCycle) cycles++;
                break;
        }

        // ===== LDY =====
        case 0xA0: { addr = imm(); cycles = 2; goto do_ldy; }
        case 0xA4: { addr = zp();  cycles = 3; goto do_ldy; }
        case 0xB4: { addr = zpx(); cycles = 4; goto do_ldy; }
        case 0xAC: { addr = abs_(); cycles = 4; goto do_ldy; }
        case 0xBC: { addr = abx(); cycles = 4;
            do_ldy:
                y = read(addr);
                setZN(y);
                if (extraCycle) cycles++;
                break;
        }

        // ===== STA =====
        case 0x85: { addr = zp();  cycles = 3; goto do_sta; }
        case 0x95: { addr = zpx(); cycles = 4; goto do_sta; }
        case 0x8D: { addr = abs_(); cycles = 4; goto do_sta; }
        case 0x9D: { addr = abx(false); cycles = 5; goto do_sta; }
        case 0x99: { addr = aby(false); cycles = 5; goto do_sta; }
        case 0x81: { addr = izx(); cycles = 6; goto do_sta; }
        case 0x91: { addr = izy(false); cycles = 6;
            do_sta:
                write(addr, a);
                break;
        }

        // ===== STX =====
        case 0x86: { addr = zp();  cycles = 3; goto do_stx; }
        case 0x96: { addr = zpy(); cycles = 4; goto do_stx; }
        case 0x8E: { addr = abs_(); cycles = 4;
            do_stx:
                write(addr, x);
                break;
        }

        // ===== STY =====
        case 0x84: { addr = zp();  cycles = 3; goto do_sty; }
        case 0x94: { addr = zpx(); cycles = 4; goto do_sty; }
        case 0x8C: { addr = abs_(); cycles = 4;
            do_sty:
                write(addr, y);
                break;
        }

        // ===== INC =====
        case 0xE6: { addr = zp();  cycles = 5; goto do_inc; }
        case 0xF6: { addr = zpx(); cycles = 6; goto do_inc; }
        case 0xEE: { addr = abs_(); cycles = 6; goto do_inc; }
        case 0xFE: { addr = abx(false); cycles = 7;
            do_inc: {
                uint8_t m = read(addr) + 1;
                write(addr, m);
                setZN(m);
                break;
            }
        }

        // ===== DEC =====
        case 0xC6: { addr = zp();  cycles = 5; goto do_dec; }
        case 0xD6: { addr = zpx(); cycles = 6; goto do_dec; }
        case 0xCE: { addr = abs_(); cycles = 6; goto do_dec; }
        case 0xDE: { addr = abx(false); cycles = 7;
            do_dec: {
                uint8_t m = read(addr) - 1;
                write(addr, m);
                setZN(m);
                break;
            }
        }

        // ===== INX, INY, DEX, DEY =====
        case 0xE8: x++; setZN(x); cycles = 2; break;  // INX
        case 0xC8: y++; setZN(y); cycles = 2; break;  // INY
        case 0xCA: x--; setZN(x); cycles = 2; break;  // DEX
        case 0x88: y--; setZN(y); cycles = 2; break;  // DEY

        // ===== ASL =====
        case 0x0A: // ASL A
            flagC = (a & 0x80) != 0;
            a <<= 1;
            setZN(a);
            cycles = 2;
            break;
        case 0x06: { addr = zp();  cycles = 5; goto do_asl; }
        case 0x16: { addr = zpx(); cycles = 6; goto do_asl; }
        case 0x0E: { addr = abs_(); cycles = 6; goto do_asl; }
        case 0x1E: { addr = abx(false); cycles = 7;
            do_asl: {
                uint8_t m = read(addr);
                flagC = (m & 0x80) != 0;
                m <<= 1;
                write(addr, m);
                setZN(m);
                break;
            }
        }

        // ===== LSR =====
        case 0x4A: // LSR A
            flagC = (a & 0x01) != 0;
            a >>= 1;
            setZN(a);
            cycles = 2;
            break;
        case 0x46: { addr = zp();  cycles = 5; goto do_lsr; }
        case 0x56: { addr = zpx(); cycles = 6; goto do_lsr; }
        case 0x4E: { addr = abs_(); cycles = 6; goto do_lsr; }
        case 0x5E: { addr = abx(false); cycles = 7;
            do_lsr: {
                uint8_t m = read(addr);
                flagC = (m & 0x01) != 0;
                m >>= 1;
                write(addr, m);
                setZN(m);
                break;
            }
        }

        // ===== ROL =====
        case 0x2A: { // ROL A
            bool oldC = flagC;
            flagC = (a & 0x80) != 0;
            a = (a << 1) | (oldC ? 1 : 0);
            setZN(a);
            cycles = 2;
            break;
        }
        case 0x26: { addr = zp();  cycles = 5; goto do_rol; }
        case 0x36: { addr = zpx(); cycles = 6; goto do_rol; }
        case 0x2E: { addr = abs_(); cycles = 6; goto do_rol; }
        case 0x3E: { addr = abx(false); cycles = 7;
            do_rol: {
                uint8_t m = read(addr);
                bool oldC = flagC;
                flagC = (m & 0x80) != 0;
                m = (m << 1) | (oldC ? 1 : 0);
                write(addr, m);
                setZN(m);
                break;
            }
        }

        // ===== ROR =====
        case 0x6A: { // ROR A
            bool oldC = flagC;
            flagC = (a & 0x01) != 0;
            a = (a >> 1) | (oldC ? 0x80 : 0);
            setZN(a);
            cycles = 2;
            break;
        }
        case 0x66: { addr = zp();  cycles = 5; goto do_ror; }
        case 0x76: { addr = zpx(); cycles = 6; goto do_ror; }
        case 0x6E: { addr = abs_(); cycles = 6; goto do_ror; }
        case 0x7E: { addr = abx(false); cycles = 7;
            do_ror: {
                uint8_t m = read(addr);
                bool oldC = flagC;
                flagC = (m & 0x01) != 0;
                m = (m >> 1) | (oldC ? 0x80 : 0);
                write(addr, m);
                setZN(m);
                break;
            }
        }

        // ===== Branches =====
        case 0x90: { // BCC
            int8_t off = (int8_t)read(pc++);
            cycles = 2;
            if (!flagC) {
                uint16_t newPC = pc + off;
                cycles += pageCross(pc, newPC) ? 2 : 1;
                pc = newPC;
            }
            break;
        }
        case 0xB0: { // BCS
            int8_t off = (int8_t)read(pc++);
            cycles = 2;
            if (flagC) {
                uint16_t newPC = pc + off;
                cycles += pageCross(pc, newPC) ? 2 : 1;
                pc = newPC;
            }
            break;
        }
        case 0xF0: { // BEQ
            int8_t off = (int8_t)read(pc++);
            cycles = 2;
            if (flagZ) {
                uint16_t newPC = pc + off;
                cycles += pageCross(pc, newPC) ? 2 : 1;
                pc = newPC;
            }
            break;
        }
        case 0xD0: { // BNE
            int8_t off = (int8_t)read(pc++);
            cycles = 2;
            if (!flagZ) {
                uint16_t newPC = pc + off;
                cycles += pageCross(pc, newPC) ? 2 : 1;
                pc = newPC;
            }
            break;
        }
        case 0x30: { // BMI
            int8_t off = (int8_t)read(pc++);
            cycles = 2;
            if (flagN) {
                uint16_t newPC = pc + off;
                cycles += pageCross(pc, newPC) ? 2 : 1;
                pc = newPC;
            }
            break;
        }
        case 0x10: { // BPL
            int8_t off = (int8_t)read(pc++);
            cycles = 2;
            if (!flagN) {
                uint16_t newPC = pc + off;
                cycles += pageCross(pc, newPC) ? 2 : 1;
                pc = newPC;
            }
            break;
        }
        case 0x50: { // BVC
            int8_t off = (int8_t)read(pc++);
            cycles = 2;
            if (!flagV) {
                uint16_t newPC = pc + off;
                cycles += pageCross(pc, newPC) ? 2 : 1;
                pc = newPC;
            }
            break;
        }
        case 0x70: { // BVS
            int8_t off = (int8_t)read(pc++);
            cycles = 2;
            if (flagV) {
                uint16_t newPC = pc + off;
                cycles += pageCross(pc, newPC) ? 2 : 1;
                pc = newPC;
            }
            break;
        }

        // ===== JMP =====
        case 0x4C: { // JMP absolute
            pc = abs_();
            cycles = 3;
            break;
        }
        case 0x6C: { // JMP indirect
            uint16_t ptr = abs_();
            // 6502 page boundary bug
            uint16_t lo = read(ptr);
            uint16_t hi;
            if ((ptr & 0xFF) == 0xFF)
                hi = read(ptr & 0xFF00);
            else
                hi = read(ptr + 1);
            pc = (hi << 8) | lo;
            cycles = 5;
            break;
        }

        // ===== JSR =====
        case 0x20: {
            uint16_t target = abs_();
            push16(pc - 1);
            pc = target;
            cycles = 6;
            break;
        }

        // ===== RTS =====
        case 0x60:
            pc = pull16() + 1;
            cycles = 6;
            break;

        // ===== RTI =====
        case 0x40:
            setStatus(pull());
            flagB = false;
            pc = pull16();
            cycles = 6;
            break;

        // ===== Transfers =====
        case 0xAA: x = a; setZN(x); cycles = 2; break;  // TAX
        case 0x8A: a = x; setZN(a); cycles = 2; break;  // TXA
        case 0xA8: y = a; setZN(y); cycles = 2; break;  // TAY
        case 0x98: a = y; setZN(a); cycles = 2; break;  // TYA
        case 0x9A: sp = x;          cycles = 2; break;  // TXS
        case 0xBA: x = sp; setZN(x); cycles = 2; break; // TSX

        // ===== Stack =====
        case 0x48: push(a); cycles = 3; break;                       // PHA
        case 0x68: a = pull(); setZN(a); cycles = 4; break;          // PLA
        case 0x08: push(getStatus() | 0x10); cycles = 3; break;      // PHP
        case 0x28: setStatus(pull()); flagB = false; cycles = 4; break; // PLP

        // ===== Flags =====
        case 0x18: flagC = false; cycles = 2; break; // CLC
        case 0x38: flagC = true;  cycles = 2; break; // SEC
        case 0xD8: flagD = false; cycles = 2; break; // CLD
        case 0xF8: flagD = true;  cycles = 2; break; // SED
        case 0x58: flagI = false; cycles = 2; break; // CLI
        case 0x78: flagI = true;  cycles = 2; break; // SEI
        case 0xB8: flagV = false; cycles = 2; break; // CLV

        // ===== NOP =====
        case 0xEA: cycles = 2; break;

        // ===== BRK =====
        case 0x00:
            pc++;
            push16(pc);
            push(getStatus() | 0x30);
            flagI = true;
            pc = read(0xFFFE) | ((uint16_t)read(0xFFFF) << 8);
            cycles = 7;
            break;

        // ===== Unofficial NOPs (common ones games use) =====
        case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA:
            cycles = 2; break; // NOP implied
        case 0x04: case 0x44: case 0x64:
            pc++; cycles = 3; break; // DOP zero page
        case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4:
            pc++; cycles = 4; break; // DOP zero page X
        case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2:
            pc++; cycles = 2; break; // DOP immediate
        case 0x0C:
            pc += 2; cycles = 4; break; // TOP absolute
        case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC:
            { abx(); cycles = 4; if (extraCycle) cycles++; break; } // TOP absolute X

        // ===== LAX (unofficial but used by some games) =====
        case 0xA7: { addr = zp();  cycles = 3; goto do_lax; }
        case 0xB7: { addr = zpy(); cycles = 4; goto do_lax; }
        case 0xAF: { addr = abs_(); cycles = 4; goto do_lax; }
        case 0xBF: { addr = aby(); cycles = 4; goto do_lax; }
        case 0xA3: { addr = izx(); cycles = 6; goto do_lax; }
        case 0xB3: { addr = izy(); cycles = 5;
            do_lax:
                a = x = read(addr);
                setZN(a);
                if (extraCycle) cycles++;
                break;
        }

        // ===== SAX (unofficial) =====
        case 0x87: { addr = zp();  cycles = 3; goto do_sax; }
        case 0x97: { addr = zpy(); cycles = 4; goto do_sax; }
        case 0x8F: { addr = abs_(); cycles = 4; goto do_sax; }
        case 0x83: { addr = izx(); cycles = 6;
            do_sax:
                write(addr, a & x);
                break;
        }

        // ===== DCP (unofficial) =====
        case 0xC7: { addr = zp();  cycles = 5; goto do_dcp; }
        case 0xD7: { addr = zpx(); cycles = 6; goto do_dcp; }
        case 0xCF: { addr = abs_(); cycles = 6; goto do_dcp; }
        case 0xDF: { addr = abx(false); cycles = 7; goto do_dcp; }
        case 0xDB: { addr = aby(false); cycles = 7; goto do_dcp; }
        case 0xC3: { addr = izx(); cycles = 8; goto do_dcp; }
        case 0xD3: { addr = izy(false); cycles = 8;
            do_dcp: {
                uint8_t m = read(addr) - 1;
                write(addr, m);
                flagC = a >= m;
                setZN(a - m);
                break;
            }
        }

        // ===== ISB/ISC (unofficial) =====
        case 0xE7: { addr = zp();  cycles = 5; goto do_isb; }
        case 0xF7: { addr = zpx(); cycles = 6; goto do_isb; }
        case 0xEF: { addr = abs_(); cycles = 6; goto do_isb; }
        case 0xFF: { addr = abx(false); cycles = 7; goto do_isb; }
        case 0xFB: { addr = aby(false); cycles = 7; goto do_isb; }
        case 0xE3: { addr = izx(); cycles = 8; goto do_isb; }
        case 0xF3: { addr = izy(false); cycles = 8;
            do_isb: {
                uint8_t m = read(addr) + 1;
                write(addr, m);
                m ^= 0xFF;
                uint16_t sum = a + m + (flagC ? 1 : 0);
                flagC = sum > 0xFF;
                flagV = (~(a ^ m) & (a ^ sum) & 0x80) != 0;
                a = sum & 0xFF;
                setZN(a);
                break;
            }
        }

        // ===== SLO (unofficial) =====
        case 0x07: { addr = zp();  cycles = 5; goto do_slo; }
        case 0x17: { addr = zpx(); cycles = 6; goto do_slo; }
        case 0x0F: { addr = abs_(); cycles = 6; goto do_slo; }
        case 0x1F: { addr = abx(false); cycles = 7; goto do_slo; }
        case 0x1B: { addr = aby(false); cycles = 7; goto do_slo; }
        case 0x03: { addr = izx(); cycles = 8; goto do_slo; }
        case 0x13: { addr = izy(false); cycles = 8;
            do_slo: {
                uint8_t m = read(addr);
                flagC = (m & 0x80) != 0;
                m <<= 1;
                write(addr, m);
                a |= m;
                setZN(a);
                break;
            }
        }

        // ===== RLA (unofficial) =====
        case 0x27: { addr = zp();  cycles = 5; goto do_rla; }
        case 0x37: { addr = zpx(); cycles = 6; goto do_rla; }
        case 0x2F: { addr = abs_(); cycles = 6; goto do_rla; }
        case 0x3F: { addr = abx(false); cycles = 7; goto do_rla; }
        case 0x3B: { addr = aby(false); cycles = 7; goto do_rla; }
        case 0x23: { addr = izx(); cycles = 8; goto do_rla; }
        case 0x33: { addr = izy(false); cycles = 8;
            do_rla: {
                uint8_t m = read(addr);
                bool oldC = flagC;
                flagC = (m & 0x80) != 0;
                m = (m << 1) | (oldC ? 1 : 0);
                write(addr, m);
                a &= m;
                setZN(a);
                break;
            }
        }

        // ===== SRE (unofficial) =====
        case 0x47: { addr = zp();  cycles = 5; goto do_sre; }
        case 0x57: { addr = zpx(); cycles = 6; goto do_sre; }
        case 0x4F: { addr = abs_(); cycles = 6; goto do_sre; }
        case 0x5F: { addr = abx(false); cycles = 7; goto do_sre; }
        case 0x5B: { addr = aby(false); cycles = 7; goto do_sre; }
        case 0x43: { addr = izx(); cycles = 8; goto do_sre; }
        case 0x53: { addr = izy(false); cycles = 8;
            do_sre: {
                uint8_t m = read(addr);
                flagC = (m & 0x01) != 0;
                m >>= 1;
                write(addr, m);
                a ^= m;
                setZN(a);
                break;
            }
        }

        // ===== RRA (unofficial) =====
        case 0x67: { addr = zp();  cycles = 5; goto do_rra; }
        case 0x77: { addr = zpx(); cycles = 6; goto do_rra; }
        case 0x6F: { addr = abs_(); cycles = 6; goto do_rra; }
        case 0x7F: { addr = abx(false); cycles = 7; goto do_rra; }
        case 0x7B: { addr = aby(false); cycles = 7; goto do_rra; }
        case 0x63: { addr = izx(); cycles = 8; goto do_rra; }
        case 0x73: { addr = izy(false); cycles = 8;
            do_rra: {
                uint8_t m = read(addr);
                bool oldC = flagC;
                flagC = (m & 0x01) != 0;
                m = (m >> 1) | (oldC ? 0x80 : 0);
                write(addr, m);
                // ADC
                uint16_t sum = a + m + (flagC ? 1 : 0);
                flagC = sum > 0xFF;
                flagV = (~(a ^ m) & (a ^ sum) & 0x80) != 0;
                a = sum & 0xFF;
                setZN(a);
                break;
            }
        }

        default:
            // Unknown opcode - treat as NOP
            cycles = 2;
            break;
    }
}
