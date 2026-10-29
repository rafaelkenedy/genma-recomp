#pragma once
#include <cstdint>

namespace input {

// the console controller's report, analog buttons are 0-255
struct Pad {
    bool connected = false;
    uint16_t buttons = 0;  // dpad up/down/left/right, start, back, left thumb, right thumb
    uint8_t analog[8] = {};  // A B X Y black white left trigger right trigger
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};

void Init();  // window thread, before boot
void Pump();  // Linux: window thread owns SDL event pumping and gamepad hotplug
void SetKeyboardPad(const Pad& pad);  // optional host keyboard controller, port 0
// the controller on that port, then GameInfo::pad
Pad Poll(int port);
void Rumble(int port, uint16_t left, uint16_t right);

}  // namespace input
