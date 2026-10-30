#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace gpu {
struct XprLevel {
    size_t offset, bytes;
};
// BGS files contain XPR0 blocks separated by other data. Index only supported
// top levels whose complete byte ranges fit inside their own XPR block.
inline std::vector<XprLevel> IndexXpr(std::span<const uint8_t> data) {
    std::vector<XprLevel> result;
    auto read = [&](size_t at) {
        uint32_t v;
        std::memcpy(&v, data.data() + at, 4);
        return v;
    };
    for (size_t base = 0; base + 12 <= data.size();) {
        if (std::memcmp(data.data() + base, "XPR0", 4)) {
            ++base;
            continue;
        }
        size_t total = read(base + 4), header = read(base + 8);
        if (header < 12 || total < header || total > data.size() - base) {
            ++base;
            continue;
        }
        for (size_t at = base + 12; at + 20 <= base + header;) {
            uint32_t common = read(at);
            if (common == 0xFFFFFFFF)
                break;
            uint32_t type = (common >> 16) & 7;
            if (type == 4) {
                uint32_t fmt = read(at + 12), f = (fmt >> 8) & 255;
                if (fmt & 4) {
                    at += 20;
                    continue;
                } // cubemaps are not replaceable
                uint64_t w = 1ull << ((fmt >> 20) & 15), h = 1ull << ((fmt >> 24) & 15);
                uint64_t bytes = 0;
                if (f == 0x0C || f == 0x0E || f == 0x0F)
                    bytes = ((w + 3) / 4) * ((h + 3) / 4) * (f == 0x0C ? 8 : 16);
                else if (f == 0x0B)
                    bytes = w * h;
                else if (f == 6 || f == 7)
                    bytes = w * h * 4;
                else if (f >= 2 && f <= 5)
                    bytes = w * h * 2;
                uint64_t offset = uint64_t(header) + read(at + 4);
                if (bytes && offset <= total && bytes <= total - offset)
                    result.push_back({base + size_t(offset), size_t(bytes)});
            }
            at += type <= 2 || type == 4 ? 20 : 4;
        }
        base += total;
    }
    return result;
}
} // namespace gpu
