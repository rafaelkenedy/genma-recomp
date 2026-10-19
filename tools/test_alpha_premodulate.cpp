// Regression from the original XDK 3948 combiner at 18D800/18D5E0.
// Build against the SDK's actual shader generator; no game execution needed.
#include "gpu/shaders.h"
#include <cassert>
#include <iostream>

int main() {
    gpu::PixelKey k{};
    k.tex[0].kind = gpu::kTex2D;
    const uint32_t hair[] = {5, 2, 0, 1, 17, 2, 3, 1, 1};
    for (int j = 0; j < 9; ++j) k.ff[0][j] = hair[j];
    k.ff[1][0] = k.ff[1][4] = 1;
    k.alphakill = 1;
    const auto premod = gpu::PixelHlsl(k);
    k.ff[0][4] = 2;
    // The original emits identical NV2A alpha inputs/outputs for these states.
    assert(premod == gpu::PixelHlsl(k));
    assert(premod.find("float c1 = saturate((t0).a)") != std::string::npos);
    assert(premod.find("if (t0.a == 0) discard;") != std::string::npos);
    k.ff[0][4] = 4;
    assert(premod != gpu::PixelHlsl(k));
    // With the next alpha stage active, the original multiplies Arg1
    // by this stage's texture, replacing the configured Arg2.
    k.ff[0][4] = 17;
    k.ff[1][4] = 2;
    assert(gpu::PixelHlsl(k).find("saturate((t0).a * t0.a)") != std::string::npos);
    // Later combiners select Arg1, including its argument modifiers.
    k.ff[0][4] = 2;
    for (int j = 0; j < 9; ++j) k.ff[1][j] = hair[j];
    k.ff[1][5] = 2 | 0x10; // complement TEXTURE alpha
    k.tex[1].kind = gpu::kTex2D;
    const auto later = gpu::PixelHlsl(k);
    k.ff[1][4] = 2;
    assert(later == gpu::PixelHlsl(k));
    std::cout << "PASS: PREMODULATE matches original SELECTARG1 behavior; texture alpha and alpha kill retained\n";
}
