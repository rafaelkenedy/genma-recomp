#include "kernel/disc.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <thread>

namespace fs = std::filesystem;
int main() {
    const auto dir = fs::temp_directory_path() /
        ("genma-disc-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(fs::create_directory(dir));
    struct Cleanup { fs::path path; ~Cleanup() { fs::remove_all(path); } } cleanup{dir};
    const auto path = dir / fs::path(u8"synthetic disc á.iso");
    std::vector<uint8_t> image(36 * 2048);
    const char magic[] = "MICROSOFT*XBOX*MEDIA";
    auto put32 = [&](size_t at, uint32_t value) { std::memcpy(image.data() + at, &value, 4); };
    const size_t volume = 32 * 2048;
    std::memcpy(image.data() + volume, magic, 20);
    std::memcpy(image.data() + volume + 0x7ec, magic, 20);
    put32(volume + 0x14, 33); put32(volume + 0x18, 2048);
    auto entry = [&](size_t at, uint32_t sector, uint32_t bytes, uint8_t flags, const char* name) {
        put32(at + 4, sector); put32(at + 8, bytes);
        image[at + 12] = flags; image[at + 13] = uint8_t(std::strlen(name));
        std::memcpy(image.data() + at + 14, name, std::strlen(name));
    };
    entry(33 * 2048, 34, 2048, 0x10, "DATA");
    entry(34 * 2048, 35, 8, 0, "TeSt.BIN");
    std::memcpy(image.data() + 35 * 2048, "abc12345", 8);
    {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(image.data()), image.size());
        assert(file.good());
    }
    kernel::Disc disc;
    std::string error;
    assert(disc.Open(path, error));
    const auto* file = disc.Find("data\\TEST.bin");
    assert(file && !file->dir && file->size == 8);
    assert(!disc.Find("DATA\\missing.bin"));
    std::vector<uint8_t> loaded;
    assert(disc.Load("DaTa\\test.BIN", loaded));
    assert(std::string(loaded.begin(), loaded.end()) == "abc12345");
    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) readers.emplace_back([&] {
        for (int j = 0; j < 100; ++j) {
            char out[8]{};
            assert(disc.Read(*file, 5, out, sizeof(out)) == 3);
            assert(std::memcmp(out, "345", 3) == 0);
            assert(disc.Read(*file, 8, out, sizeof(out)) == 0);
        }
    });
    for (auto& reader : readers) reader.join();
    const auto invalid = dir / "invalid.iso";
    { std::ofstream bad(invalid); bad << "not a disc"; }
    kernel::Disc bad;
    assert(!bad.Open(invalid, error) && !error.empty());
    std::cout << "PASS: synthetic XISO, Unicode/spaces, case-insensitive lookup, concurrent reads and EOF\n";
}
