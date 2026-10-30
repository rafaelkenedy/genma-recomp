#pragma once
// HD texture replacement, after RT64's packs: a file in the textures folder beside the exe replaces the game texture
// whose level 0 bytes, as the game stores them, hash to the name (gpu::Hash, 16 hex digits):
//   textures/<hash>.dds      BC1, BC2, BC3 or BC7 (DX10 header too), with the file's mip levels, uploaded as they are
//   textures/<hash>.png      BGRA, any size; the mip chain is made from it
//   textures/<hash>.idx.png  8-bit grey: palette indices for a P8 texture, read through the game's palette at upload,
//                            so colours, alpha and palette changes stay the game's own
//   textures/<hash>.p<sig>.dds / .png  a P8 texture drawn with one palette: <sig> is PaletteSignature, the colours its
//                            indices use. A game may draw the same indices with other palettes (Genma's souls, red,
//                            yellow and blue): each palette needs its own file, the others keep the game's texture
// A DDS wins over a PNG for the same hash. The texture's coordinates are normalized, so any size fits. Files are read
// on demand and retained in a bounded CPU cache; room files can prefetch them.
// XBE_DUMP_TEXTURES=1 writes each texture once to textures_dump/<hash>.png (and .idx.png for P8) to find the names.
#include <cstdint>
#include <vector>
#include <memory>

#include "gpu/texture.h"

namespace gpu {

struct Replacement {
    enum class Kind { Bgra, Indexed, BC1, BC2, BC3, BC7 };
    Kind kind = Kind::Bgra;
    uint32_t w = 0, h = 0;
    std::vector<std::vector<uint8_t>> levels;  // level 0 first; Indexed: w * h indices, Bgra: w * h * 4, BCn: blocks
    bool Compressed() const { return kind != Kind::Bgra && kind != Kind::Indexed; }
    uint32_t BlockBytes() const { return kind == Kind::BC1 ? 8 : 16; }
    size_t CacheBytes() const {
        size_t bytes = 0;
        for (const auto& level : levels) bytes += level.size();
        return bytes;
    }
};

// a P8 texture's palette as its level 0 uses it: FNV-1a 64 over, for each index present in order, the index byte and
// its colour's 4 bytes (B G R A); tools/convert_texture_pack.py computes the same from a dump
uint64_t PaletteSignature(const uint8_t* indices, size_t count, const uint32_t* palette);

// is there a replacement for this texture: palette_sig 0 for a texture without a palette, else PaletteSignature; a P8
// texture takes its <hash>.p<sig> file, else its <hash>.idx.png. The folder is listed on the first call.
bool HasReplacement(uint64_t hash, uint64_t palette_sig);
bool ReplacementUsesPalette(uint64_t hash, uint64_t palette_sig);
// Immutable cached source, or null when it cannot be loaded.
std::shared_ptr<const Replacement> LoadReplacement(uint64_t hash, uint64_t palette_sig);
bool ReplacementCaching();
// Call at device initialization and after each presentation. Speculative reads
// wait for the current view to be presented before competing for I/O.
void StartReplacementPrefetch(bool enabled);

bool DumpTextures();  // XBE_DUMP_TEXTURES=1
// level 0 decoded to textures_dump/<hash>.png; a P8 texture to <hash>.p<palette_sig>.png, once per palette, and its
// indices to <hash>.idx.png
void DumpTexture(uint64_t hash, uint64_t palette_sig, const TextureDesc& d, const uint8_t* guest, const uint32_t* palette);

// an uncompressed replacement as BGRA levels: indices through the palette (grey without one), then the mip chain by
// 2 x 2 average; a compressed one keeps its own levels
void PrepareLevels(Replacement& r, const uint32_t* palette);

}  // namespace gpu
