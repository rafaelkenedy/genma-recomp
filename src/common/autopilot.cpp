// Drives pad 0 without a person at the controller, for testing.
// A file named "cmd" beside the exe holds "BUTTONS [seconds held]" (held 0.25 s by default); it is pressed once
// from when it is read, then the file is removed. Buttons joined by +:
// UP DOWN LEFT RIGHT START BACK A B X Y BLACK WHITE LT RT LS LSD LSL LSR (left stick up/down/left/right)
// RS RSD RSL RSR (right stick). A real controller still works; the script adds to it.
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>

#include "app/app.h"
#include "core/input.h"
#include "core/log.h"

namespace {

using Clock = std::chrono::steady_clock;

struct Press {
    std::string buttons;
    Clock::time_point from, to;
};

std::mutex g_lock;
Press g_live;

void Apply(const std::string& s, input::Pad& p) {
    size_t at = 0;
    while (at <= s.size()) {
        size_t e = s.find('+', at);
        if (e == std::string::npos) e = s.size();
        std::string b = s.substr(at, e - at);
        at = e + 1;
        if (b == "UP") p.buttons |= 1;
        else if (b == "DOWN") p.buttons |= 2;
        else if (b == "LEFT") p.buttons |= 4;
        else if (b == "RIGHT") p.buttons |= 8;
        else if (b == "START") p.buttons |= 16;
        else if (b == "BACK") p.buttons |= 32;
        else if (b == "A") p.analog[0] = 255;
        else if (b == "B") p.analog[1] = 255;
        else if (b == "X") p.analog[2] = 255;
        else if (b == "Y") p.analog[3] = 255;
        else if (b == "BLACK") p.analog[4] = 255;
        else if (b == "WHITE") p.analog[5] = 255;
        else if (b == "LT") p.analog[6] = 255;
        else if (b == "RT") p.analog[7] = 255;
        else if (b == "LS") p.ly = 32767;
        else if (b == "LSD") p.ly = -32767;
        else if (b == "LSL") p.lx = -32767;
        else if (b == "LSR") p.lx = 32767;
        else if (b == "RS") p.ry = 32767;
        else if (b == "RSD") p.ry = -32767;
        else if (b == "RSL") p.rx = -32767;
        else if (b == "RSR") p.rx = 32767;
    }
}

}  // namespace

void AutopilotPad(int port, input::Pad& p) {
    if (port != 0) return;
    auto now = Clock::now();
    std::lock_guard<std::mutex> lock(g_lock);
    std::string cmd = (app::CacheDir() / "cmd").string();
    if (FILE* f = std::fopen(cmd.c_str(), "r")) {
        char b[96] = {};
        double len = 0.25;
        if (std::fscanf(f, "%95s %lf", b, &len) >= 1) {
            g_live = {b, now, now + std::chrono::microseconds(int64_t(len * 1e6))};
            LOGI("autopilot: %s for %.2f s", b, len);
        }
        std::fclose(f);
        std::remove(cmd.c_str());
    }
    if (!g_live.buttons.empty() && now >= g_live.from && now <= g_live.to) {
        p.connected = true;
        Apply(g_live.buttons, p);
    }
}
