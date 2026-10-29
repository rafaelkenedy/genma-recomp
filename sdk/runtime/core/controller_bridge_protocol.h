#pragma once
#include <cstdint>

// Versioned little-endian messages for the optional Windows -> WSL development bridge.
// Both endpoints target x86-64. These structures never contain pointers or SDL objects.
namespace controller_bridge {
constexpr uint32_t kStateMagic = 0x31445047;  // GPD1
constexpr uint32_t kRumbleMagic = 0x314d5552; // RUM1
struct State {
    uint32_t magic = kStateMagic;
    uint32_t sequence = 0;
    uint32_t connected = 0;
    uint16_t buttons = 0;
    uint8_t lt = 0, rt = 0;
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};
struct Rumble {
    uint32_t magic = kRumbleMagic;
    uint16_t left = 0, right = 0;
};
static_assert(sizeof(State) == 24);
static_assert(sizeof(Rumble) == 8);
} // namespace controller_bridge
