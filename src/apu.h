#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <mutex>

class APU {
public:
    APU();

    void cpuWrite(uint16_t addr, uint8_t val);
    uint8_t cpuRead(uint16_t addr);

    // Called at CPU rate (~1.789 MHz)
    void clock();

    // Fill audio buffer for SDL callback
    void fillBuffer(float* buffer, int numSamples);

    // Sample rate
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr double CPU_CLOCK = 1789773.0;

private:
    // Frame counter
    uint8_t frameCounterMode = 0; // 0 = 4-step, 1 = 5-step
    bool frameIRQ = false;
    bool inhibitIRQ = false;
    int frameClock = 0;

    // =========== Pulse Channel ===========
    struct Pulse {
        bool enabled = false;

        // Duty cycle
        uint8_t duty = 0;
        uint8_t dutyPos = 0;
        static constexpr bool dutyTable[4][8] = {
            {0,1,0,0,0,0,0,0},
            {0,1,1,0,0,0,0,0},
            {0,1,1,1,1,0,0,0},
            {1,0,0,1,1,1,1,1},
        };

        // Timer
        uint16_t timerPeriod = 0;
        uint16_t timerValue = 0;

        // Length counter
        uint8_t lengthCounter = 0;
        bool lengthHalt = false;

        // Envelope
        bool envelopeStart = false;
        bool envelopeLoop = false;
        bool constantVolume = false;
        uint8_t envelopeVolume = 0;
        uint8_t envelopeDecay = 0;
        uint8_t envelopeDivider = 0;

        // Sweep
        bool sweepEnabled = false;
        bool sweepNegate = false;
        bool sweepReload = false;
        uint8_t sweepPeriod = 0;
        uint8_t sweepShift = 0;
        uint8_t sweepDivider = 0;
        bool isChannel1 = false; // for negate difference

        void clockTimer();
        void clockEnvelope();
        void clockLengthCounter();
        void clockSweep();
        uint8_t output() const;
        uint16_t sweepTarget() const;
    };

    Pulse pulse1, pulse2;

    // =========== Triangle Channel ===========
    struct Triangle {
        bool enabled = false;

        // Timer
        uint16_t timerPeriod = 0;
        uint16_t timerValue = 0;

        // Length counter
        uint8_t lengthCounter = 0;
        bool lengthHalt = false; // also linear counter control

        // Linear counter
        uint8_t linearCounterLoad = 0;
        uint8_t linearCounter = 0;
        bool linearReload = false;

        // Sequence
        uint8_t sequencePos = 0;
        static constexpr uint8_t sequence[32] = {
            15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0,
            0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
        };

        void clockTimer();
        void clockLinearCounter();
        void clockLengthCounter();
        uint8_t output() const;
    };

    Triangle triangle;

    // =========== Noise Channel ===========
    struct Noise {
        bool enabled = false;

        // Timer
        uint16_t timerPeriod = 0;
        uint16_t timerValue = 0;
        bool mode = false; // short mode

        // Shift register (LFSR)
        uint16_t shiftReg = 1;

        // Length counter
        uint8_t lengthCounter = 0;
        bool lengthHalt = false;

        // Envelope
        bool envelopeStart = false;
        bool envelopeLoop = false;
        bool constantVolume = false;
        uint8_t envelopeVolume = 0;
        uint8_t envelopeDecay = 0;
        uint8_t envelopeDivider = 0;

        void clockTimer();
        void clockEnvelope();
        void clockLengthCounter();
        uint8_t output() const;
    };

    Noise noise;

    // =========== DMC Channel (stub) ===========
    struct DMC {
        bool enabled = false;
        uint8_t output_level = 0;
    };
    DMC dmc;

    // Length counter lookup table
    static constexpr uint8_t lengthTable[32] = {
        10,254,20, 2,40, 4,80, 6,160, 8,60,10,14,12,26,14,
        12, 16,24,18,48,20,96,22,192,24,72,26,16,28,32,30
    };

    // Noise period lookup table (NTSC)
    static constexpr uint16_t noisePeriodTable[16] = {
        4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068
    };

    // Frame counter
    void clockQuarterFrame();
    void clockHalfFrame();

    // Mixing
    float mix() const;

    // Sample buffer for audio thread
    static constexpr int BUFFER_SIZE = 8192;
    std::array<float, BUFFER_SIZE> sampleBuffer{};
    int sampleWritePos = 0;
    int sampleReadPos = 0;
    std::mutex bufferMutex;

    // Sampling
    double sampleAccumulator = 0.0;
    static constexpr double SAMPLES_PER_CPU_CLOCK = SAMPLE_RATE / CPU_CLOCK;

    // Sample averaging to reduce aliasing
    double sampleSum = 0.0;
    int sampleCount = 0;

    // Low-pass filter state
    float prevSample = 0.0f;
    static constexpr float LPF_ALPHA = 0.65f;

    // Last valid output for underrun interpolation
    float lastOutputSample = 0.0f;

    uint64_t cpuClock = 0;
};
