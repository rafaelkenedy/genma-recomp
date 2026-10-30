#include "gpu/xpr_index.h"
#include <cassert>
#include <iostream>

int main() {
    std::vector<uint8_t> bytes(72);
    auto put = [&](size_t offset, uint32_t value) { std::memcpy(bytes.data() + offset, &value, 4); };
    std::memcpy(bytes.data(), "XPR0", 4);
    put(4, 72);
    put(8, 64);
    put(12, 4 << 16);
    put(24, (0x0C << 8) | (1 << 16) | (2 << 20) | (2 << 24)); // DXT1 4x4
    put(32, 0xFFFFFFFF);
    auto levels = gpu::IndexXpr(bytes);
    assert(levels.size() == 1 && levels[0].offset == 64 && levels[0].bytes == 8);
    auto file = bytes;
    file.insert(file.end(), {9, 8, 7}); // other room data between XPR blocks
    file.insert(file.end(), bytes.begin(), bytes.end());
    levels = gpu::IndexXpr(file);
    assert(levels.size() == 2 && levels[1].offset == 139);
    put(16, 0xFFFFFFFF); // invalid data offset
    assert(gpu::IndexXpr(bytes).empty());
    put(16, 0);
    put(24, (0xFE << 8)); // unsupported format
    assert(gpu::IndexXpr(bytes).empty());
    put(8, 80); // header outside its container
    assert(gpu::IndexXpr(bytes).empty());
    bytes.resize(11);
    assert(gpu::IndexXpr(bytes).empty());
    std::cout << "PASS: concatenated XPR blocks, texture bounds, unsupported formats and truncated headers\n";
}
