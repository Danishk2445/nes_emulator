#pragma once
#include <cstdint>

class Controller {
public:
    enum Button : uint8_t {
        A      = 0x01,
        B      = 0x02,
        Select = 0x04,
        Start  = 0x08,
        Up     = 0x10,
        Down   = 0x20,
        Left   = 0x40,
        Right  = 0x80,
    };

    void setButtonState(uint8_t state) { buttons = state; }
    uint8_t getButtonState() const { return buttons; }

    void write(uint8_t val);
    uint8_t read();

private:
    uint8_t buttons = 0;   // current button state
    uint8_t shifter = 0;   // shift register
    bool strobe = false;
};
