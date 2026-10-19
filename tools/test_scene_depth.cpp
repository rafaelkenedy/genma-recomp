// CPU scene depth consumed by the D3D12 depth-import pass: Xbox D24S8
// stores depth in the high 24 bits, unlike the destination D3D12 layout.
#include "gpu/texture.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <cstdlib>

// Runtime-only references in texture.cpp; this test exercises the pure decoder.
// Abort if the test accidentally starts reading guest process memory.
namespace kernel { uint8_t* g_base = nullptr; }
namespace mem { uint32_t VirtualFromPhysical(uint32_t) { std::abort(); } }

int main() {
    gpu::TextureDesc d{};
    d.format = 0x2E;
    d.width = d.height = 2;
    d.pitch = 16;
    d.linear = true;
    uint32_t pixels[8] = {0xFFFFFF00, 0xFFE00000, 0, 0,
                          0x00000000, 0x80000000, 0, 0};
    std::vector<uint8_t> out;
    uint32_t pitch;
    gpu::DecodeLevel(d, 0, 0, reinterpret_cast<uint8_t*>(pixels), nullptr, out, pitch);
    assert(pitch == 8 && out.size() == 16);
    float z[4];
    std::memcpy(z, out.data(), sizeof(z));
    assert(z[0] == 1.0f && z[2] == 0.0f);
    assert(std::abs(z[1] - double(0xFFE000) / 0xFFFFFF) < 1e-7);
    assert(std::abs(z[3] - double(0x800000) / 0xFFFFFF) < 1e-7);
    std::cout << "PASS: scene D24S8 becomes normalized R32 depth, preserving row pitch and precision\n";
}
