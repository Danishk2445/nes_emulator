#include "apu.h"
#include <cmath>
#include <algorithm>

APU::APU() {
    pulse1.isChannel1 = true;
    pulse2.isChannel1 = false;
    sampleBuffer.fill(0.0f);
}

// ===================== Pulse =====================

void APU::Pulse::clockTimer() {
    if (timerValue == 0) {
        timerValue = timerPeriod;
        dutyPos = (dutyPos + 1) & 7;
    } else {
        timerValue--;
    }
}

void APU::Pulse::clockEnvelope() {
    if (envelopeStart) {
        envelopeStart = false;
        envelopeDecay = 15;
        envelopeDivider = envelopeVolume;
    } else {
        if (envelopeDivider == 0) {
            envelopeDivider = envelopeVolume;
            if (envelopeDecay > 0) {
                envelopeDecay--;
            } else if (envelopeLoop) {
                envelopeDecay = 15;
            }
        } else {
            envelopeDivider--;
        }
    }
}

void APU::Pulse::clockLengthCounter() {
    if (!lengthHalt && lengthCounter > 0) {
        lengthCounter--;
    }
}

uint16_t APU::Pulse::sweepTarget() const {
    int16_t change = timerPeriod >> sweepShift;
    if (sweepNegate) {
        change = -change;
        if (isChannel1) change--; // pulse 1 uses one's complement
    }
    int16_t target = (int16_t)timerPeriod + change;
    return (uint16_t)std::max((int16_t)0, target);
}

void APU::Pulse::clockSweep() {
    if (sweepDivider == 0 && sweepEnabled && sweepShift > 0) {
        uint16_t target = sweepTarget();
        if (timerPeriod >= 8 && target <= 0x7FF) {
            timerPeriod = target;
        }
    }
    if (sweepDivider == 0 || sweepReload) {
        sweepDivider = sweepPeriod;
        sweepReload = false;
    } else {
        sweepDivider--;
    }
}

uint8_t APU::Pulse::output() const {
    if (!enabled) return 0;
    if (lengthCounter == 0) return 0;
    if (!dutyTable[duty][dutyPos]) return 0;
    if (timerPeriod < 8) return 0;
    if (sweepTarget() > 0x7FF) return 0;
    return constantVolume ? envelopeVolume : envelopeDecay;
}

// ===================== Triangle =====================

void APU::Triangle::clockTimer() {
    if (timerValue == 0) {
        timerValue = timerPeriod;
        if (lengthCounter > 0 && linearCounter > 0) {
            sequencePos = (sequencePos + 1) & 31;
        }
    } else {
        timerValue--;
    }
}

void APU::Triangle::clockLinearCounter() {
    if (linearReload) {
        linearCounter = linearCounterLoad;
    } else if (linearCounter > 0) {
        linearCounter--;
    }
    if (!lengthHalt) {
        linearReload = false;
    }
}

void APU::Triangle::clockLengthCounter() {
    if (!lengthHalt && lengthCounter > 0) {
        lengthCounter--;
    }
}

uint8_t APU::Triangle::output() const {
    if (!enabled) return 0;
    if (lengthCounter == 0) return 0;
    if (linearCounter == 0) return 0;
    if (timerPeriod < 2) return 7; // ultrasonic - output midpoint to avoid popping
    return sequence[sequencePos];
}

// ===================== Noise =====================

void APU::Noise::clockTimer() {
    if (timerValue == 0) {
        timerValue = timerPeriod;
        uint8_t bit = mode ? 6 : 1;
        uint16_t feedback = (shiftReg & 1) ^ ((shiftReg >> bit) & 1);
        shiftReg = (shiftReg >> 1) | (feedback << 14);
    } else {
        timerValue--;
    }
}

void APU::Noise::clockEnvelope() {
    if (envelopeStart) {
        envelopeStart = false;
        envelopeDecay = 15;
        envelopeDivider = envelopeVolume;
    } else {
        if (envelopeDivider == 0) {
            envelopeDivider = envelopeVolume;
            if (envelopeDecay > 0) {
                envelopeDecay--;
            } else if (envelopeLoop) {
                envelopeDecay = 15;
            }
        } else {
            envelopeDivider--;
        }
    }
}

void APU::Noise::clockLengthCounter() {
    if (!lengthHalt && lengthCounter > 0) {
        lengthCounter--;
    }
}

uint8_t APU::Noise::output() const {
    if (!enabled) return 0;
    if (lengthCounter == 0) return 0;
    if (shiftReg & 1) return 0;
    return constantVolume ? envelopeVolume : envelopeDecay;
}

// ===================== Frame Counter =====================

void APU::clockQuarterFrame() {
    pulse1.clockEnvelope();
    pulse2.clockEnvelope();
    triangle.clockLinearCounter();
    noise.clockEnvelope();
}

void APU::clockHalfFrame() {
    pulse1.clockLengthCounter();
    pulse1.clockSweep();
    pulse2.clockLengthCounter();
    pulse2.clockSweep();
    triangle.clockLengthCounter();
    noise.clockLengthCounter();
}

// ===================== Mixing =====================

float APU::mix() const {
    // Linear approximation mixing (good enough, avoids lookup table)
    uint8_t p1 = pulse1.output();
    uint8_t p2 = pulse2.output();
    uint8_t tri = triangle.output();
    uint8_t noi = noise.output();
    uint8_t dm = dmc.output_level;

    float pulseOut = 0.0f;
    if (p1 || p2) {
        pulseOut = 95.88f / (8128.0f / (float)(p1 + p2) + 100.0f);
    }

    float tndOut = 0.0f;
    float tndSum = tri / 8227.0f + noi / 12241.0f + dm / 22638.0f;
    if (tndSum > 0.0f) {
        tndOut = 159.79f / (1.0f / tndSum + 100.0f);
    }

    return pulseOut + tndOut;
}

// ===================== Register Writes =====================

void APU::cpuWrite(uint16_t addr, uint8_t val) {
    switch (addr) {
        // Pulse 1: $4000-$4003
        case 0x4000:
            pulse1.duty = (val >> 6) & 3;
            pulse1.lengthHalt = (val & 0x20) != 0;
            pulse1.envelopeLoop = (val & 0x20) != 0;
            pulse1.constantVolume = (val & 0x10) != 0;
            pulse1.envelopeVolume = val & 0x0F;
            break;
        case 0x4001:
            pulse1.sweepEnabled = (val & 0x80) != 0;
            pulse1.sweepPeriod = (val >> 4) & 7;
            pulse1.sweepNegate = (val & 0x08) != 0;
            pulse1.sweepShift = val & 7;
            pulse1.sweepReload = true;
            break;
        case 0x4002:
            pulse1.timerPeriod = (pulse1.timerPeriod & 0x700) | val;
            break;
        case 0x4003:
            pulse1.timerPeriod = (pulse1.timerPeriod & 0xFF) | ((uint16_t)(val & 7) << 8);
            if (pulse1.enabled)
                pulse1.lengthCounter = lengthTable[val >> 3];
            pulse1.envelopeStart = true;
            pulse1.dutyPos = 0;
            break;

        // Pulse 2: $4004-$4007
        case 0x4004:
            pulse2.duty = (val >> 6) & 3;
            pulse2.lengthHalt = (val & 0x20) != 0;
            pulse2.envelopeLoop = (val & 0x20) != 0;
            pulse2.constantVolume = (val & 0x10) != 0;
            pulse2.envelopeVolume = val & 0x0F;
            break;
        case 0x4005:
            pulse2.sweepEnabled = (val & 0x80) != 0;
            pulse2.sweepPeriod = (val >> 4) & 7;
            pulse2.sweepNegate = (val & 0x08) != 0;
            pulse2.sweepShift = val & 7;
            pulse2.sweepReload = true;
            break;
        case 0x4006:
            pulse2.timerPeriod = (pulse2.timerPeriod & 0x700) | val;
            break;
        case 0x4007:
            pulse2.timerPeriod = (pulse2.timerPeriod & 0xFF) | ((uint16_t)(val & 7) << 8);
            if (pulse2.enabled)
                pulse2.lengthCounter = lengthTable[val >> 3];
            pulse2.envelopeStart = true;
            pulse2.dutyPos = 0;
            break;

        // Triangle: $4008-$400B
        case 0x4008:
            triangle.lengthHalt = (val & 0x80) != 0;
            triangle.linearCounterLoad = val & 0x7F;
            break;
        case 0x400A:
            triangle.timerPeriod = (triangle.timerPeriod & 0x700) | val;
            break;
        case 0x400B:
            triangle.timerPeriod = (triangle.timerPeriod & 0xFF) | ((uint16_t)(val & 7) << 8);
            if (triangle.enabled)
                triangle.lengthCounter = lengthTable[val >> 3];
            triangle.linearReload = true;
            break;

        // Noise: $400C-$400F
        case 0x400C:
            noise.lengthHalt = (val & 0x20) != 0;
            noise.envelopeLoop = (val & 0x20) != 0;
            noise.constantVolume = (val & 0x10) != 0;
            noise.envelopeVolume = val & 0x0F;
            break;
        case 0x400E:
            noise.mode = (val & 0x80) != 0;
            noise.timerPeriod = noisePeriodTable[val & 0x0F];
            break;
        case 0x400F:
            if (noise.enabled)
                noise.lengthCounter = lengthTable[val >> 3];
            noise.envelopeStart = true;
            break;

        // DMC: $4010-$4013 (stub)
        case 0x4010: case 0x4011: case 0x4012: case 0x4013:
            if (addr == 0x4011) dmc.output_level = val & 0x7F;
            break;

        // Status: $4015
        case 0x4015:
            pulse1.enabled = (val & 0x01) != 0;
            pulse2.enabled = (val & 0x02) != 0;
            triangle.enabled = (val & 0x04) != 0;
            noise.enabled = (val & 0x08) != 0;
            dmc.enabled = (val & 0x10) != 0;
            if (!pulse1.enabled) pulse1.lengthCounter = 0;
            if (!pulse2.enabled) pulse2.lengthCounter = 0;
            if (!triangle.enabled) triangle.lengthCounter = 0;
            if (!noise.enabled) noise.lengthCounter = 0;
            break;

        // Frame counter: $4017
        case 0x4017:
            frameCounterMode = (val >> 7) & 1;
            inhibitIRQ = (val & 0x40) != 0;
            if (inhibitIRQ) frameIRQ = false;
            frameClock = 0;
            if (frameCounterMode == 1) {
                clockQuarterFrame();
                clockHalfFrame();
            }
            break;
    }
}

uint8_t APU::cpuRead(uint16_t addr) {
    if (addr == 0x4015) {
        uint8_t status = 0;
        if (pulse1.lengthCounter > 0) status |= 0x01;
        if (pulse2.lengthCounter > 0) status |= 0x02;
        if (triangle.lengthCounter > 0) status |= 0x04;
        if (noise.lengthCounter > 0) status |= 0x08;
        if (frameIRQ) status |= 0x40;
        frameIRQ = false;
        return status;
    }
    return 0;
}

// ===================== Clock =====================

void APU::clock() {
    // Triangle clocks at CPU rate
    triangle.clockTimer();

    // Pulse and noise clock at half CPU rate
    if (cpuClock % 2 == 0) {
        pulse1.clockTimer();
        pulse2.clockTimer();
        noise.clockTimer();

        // Frame counter (clocked at ~240Hz, every 3728.5 CPU half-clocks ≈ 7457 CPU clocks)
        frameClock++;
        if (frameCounterMode == 0) {
            // 4-step sequence
            switch (frameClock) {
                case 3729:  clockQuarterFrame(); break;
                case 7457:  clockQuarterFrame(); clockHalfFrame(); break;
                case 11186: clockQuarterFrame(); break;
                case 14915:
                    clockQuarterFrame(); clockHalfFrame();
                    if (!inhibitIRQ) frameIRQ = true;
                    frameClock = 0;
                    break;
            }
        } else {
            // 5-step sequence
            switch (frameClock) {
                case 3729:  clockQuarterFrame(); break;
                case 7457:  clockQuarterFrame(); clockHalfFrame(); break;
                case 11186: clockQuarterFrame(); break;
                case 14915: break; // do nothing
                case 18641:
                    clockQuarterFrame(); clockHalfFrame();
                    frameClock = 0;
                    break;
            }
        }
    }

    // Accumulate mix output for sample averaging
    sampleSum += mix();
    sampleCount++;

    // Generate sample at output sample rate
    sampleAccumulator += SAMPLES_PER_CPU_CLOCK;
    if (sampleAccumulator >= 1.0) {
        sampleAccumulator -= 1.0;

        // Average all APU outputs since the last output sample
        float sample = (sampleCount > 0)
            ? (float)(sampleSum / sampleCount)
            : prevSample;
        sampleSum = 0.0;
        sampleCount = 0;

        // First-order low-pass filter to remove aliasing hiss
        sample = LPF_ALPHA * sample + (1.0f - LPF_ALPHA) * prevSample;
        prevSample = sample;

        std::lock_guard<std::mutex> lock(bufferMutex);
        sampleBuffer[sampleWritePos] = sample;
        sampleWritePos = (sampleWritePos + 1) % BUFFER_SIZE;
    }

    cpuClock++;
}

void APU::fillBuffer(float* buffer, int numSamples) {
    std::lock_guard<std::mutex> lock(bufferMutex);
    for (int i = 0; i < numSamples; i++) {
        if (sampleReadPos != sampleWritePos) {
            lastOutputSample = sampleBuffer[sampleReadPos] * 0.5f; // master volume
            buffer[i] = lastOutputSample;
            sampleReadPos = (sampleReadPos + 1) % BUFFER_SIZE;
        } else {
            // Repeat last sample instead of silence to avoid pops
            buffer[i] = lastOutputSample;
        }
    }
}
