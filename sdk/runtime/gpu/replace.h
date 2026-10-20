#pragma once
// HD texture replacement, after RT64's packs: a PNG in the textures folder beside the exe replaces the game texture
// whose level 0 bytes, as the game stores them, hash to the name (gpu::Hash, 16 hex digits):
//   textures/<hash>.png      BGRA, any size; the texture's coordinates are normalized, so a larger one fits
//   textures/<hash>.idx.png  8-bit grey: palette indices for a P8 texture, read through the game's palette at upload,
//                            so colours, alpha and palette changes stay the game's own
// XBE_DUMP_TEXTURES=1 writes each texture once to textures_dump/<hash>.png (and .idx.png for P8) to find the names.
#include <cstdint>
#include <vector>

#include "gpu/texture.h"

namespace gpu {

struct Replacement {
    uint32_t w = 0, h = 0;
    bool indexed = false;
    std::vector<uint8_t> px;  // w * h indices, or BGRA
};

// the replacement for a texture's level 0 hash, null without one; the folder is listed on the first call
const Replacement* FindReplacement(uint64_t hash);

bool DumpTextures();  // XBE_DUMP_TEXTURES=1
// level 0 decoded (with the palette for P8) to textures_dump/<hash>.png, and a P8 texture's indices to .idx.png
void DumpTexture(uint64_t hash, const TextureDesc& d, const uint8_t* guest, const uint32_t* palette);

// a replacement as BGRA level 0 (indices through the palette, grey without one), then its mip chain by 2 x 2 average
std::vector<std::vector<uint8_t>> ReplacementLevels(const Replacement& r, const uint32_t* palette);

}  // namespace gpu
