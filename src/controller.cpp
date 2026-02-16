#include "controller.h"

void Controller::write(uint8_t val) {
    strobe = (val & 1);
    if (strobe) {
        shifter = buttons;
    }
}

uint8_t Controller::read() {
    uint8_t bit = 0;
    if (strobe) {
        bit = buttons & 1;
    } else {
        bit = shifter & 1;
        shifter >>= 1;
    }
    return bit | 0x40; // open bus bits
}
