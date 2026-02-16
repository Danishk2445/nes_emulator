#include "bus.h"
#include "cpu.h"
#include "ppu.h"
#include "apu.h"
#include "cartridge.h"
#include "controller.h"

#include <SDL3/SDL.h>
#include <iostream>
#include <chrono>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: ./nes <rom.nes>\n";
        return 1;
    }

    // Load ROM
    Cartridge cartridge;
    if (!cartridge.load(argv[1])) {
        return 1;
    }

    // Create components
    CPU cpu;
    PPU ppu;
    APU apuUnit;
    Bus bus;
    Controller ctrl1, ctrl2;

    // Wire everything together
    bus.connectCPU(&cpu);
    bus.connectPPU(&ppu);
    bus.connectAPU(&apuUnit);
    bus.connectCartridge(&cartridge);
    bus.connectController(&ctrl1, &ctrl2);
    cpu.connectBus(&bus);
    ppu.connectCartridge(&cartridge);

    // Reset CPU
    cpu.reset();

    // Initialize SDL3
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    // Setup audio
    SDL_AudioSpec audioSpec{};
    audioSpec.freq = APU::SAMPLE_RATE;
    audioSpec.format = SDL_AUDIO_F32;
    audioSpec.channels = 1;

    SDL_AudioStream* audioStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audioSpec,
        [](void* userdata, SDL_AudioStream* stream, int additional_amount, int /*total_amount*/) {
            APU* apu = static_cast<APU*>(userdata);
            int numSamples = additional_amount / sizeof(float);
            if (numSamples <= 0) return;
            std::vector<float> buffer(numSamples);
            apu->fillBuffer(buffer.data(), numSamples);
            SDL_PutAudioStreamData(stream, buffer.data(), numSamples * sizeof(float));
        },
        &apuUnit);

    if (!audioStream) {
        std::cerr << "Warning: Audio init failed: " << SDL_GetError() << "\n";
    } else {
        SDL_ResumeAudioStreamDevice(audioStream);
    }

    const int SCALE = 3;
    const int WIDTH = 256 * SCALE;
    const int HEIGHT = 240 * SCALE;

    SDL_Window* window = SDL_CreateWindow("NES Emulator", WIDTH, HEIGHT, 0);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << "\n";
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_Texture* texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        256, 240);
    if (!texture) {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << "\n";
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);

    bool running = true;

    // Frame timing
    using Clock = std::chrono::high_resolution_clock;
    auto frameStart = Clock::now();
    const double frameTime = 1.0 / 60.0; // 60 FPS

    while (running) {
        // Handle events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        // Read keyboard state
        const bool* keys = SDL_GetKeyboardState(nullptr);
        uint8_t btnState = 0;

        if (keys[SDL_SCANCODE_Z] || keys[SDL_SCANCODE_X])      btnState |= Controller::A;
        if (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_S])      btnState |= Controller::B;
        if (keys[SDL_SCANCODE_RSHIFT] || keys[SDL_SCANCODE_BACKSPACE]) btnState |= Controller::Select;
        if (keys[SDL_SCANCODE_RETURN])                          btnState |= Controller::Start;
        if (keys[SDL_SCANCODE_UP])                              btnState |= Controller::Up;
        if (keys[SDL_SCANCODE_DOWN])                            btnState |= Controller::Down;
        if (keys[SDL_SCANCODE_LEFT])                            btnState |= Controller::Left;
        if (keys[SDL_SCANCODE_RIGHT])                           btnState |= Controller::Right;

        ctrl1.setButtonState(btnState);

        // Run emulation until frame complete
        ppu.clearFrameReady();
        while (!ppu.isFrameReady()) {
            bus.clock();
        }

        // Update texture with framebuffer
        SDL_UpdateTexture(texture, nullptr, ppu.getFrameBuffer(), 256 * sizeof(uint32_t));

        // Render
        SDL_RenderClear(renderer);

        SDL_FRect dst = { 0, 0, (float)WIDTH, (float)HEIGHT };
        SDL_RenderTexture(renderer, texture, nullptr, &dst);
        SDL_RenderPresent(renderer);

        // Frame timing - wait for remaining time
        auto frameEnd = Clock::now();
        double elapsed = std::chrono::duration<double>(frameEnd - frameStart).count();
        if (elapsed < frameTime) {
            SDL_Delay((uint32_t)((frameTime - elapsed) * 1000.0));
        }
        frameStart = Clock::now();
    }

    if (audioStream) {
        SDL_DestroyAudioStream(audioStream);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
