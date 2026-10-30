#include "gpu/disk_cache.h"
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>

int main() {
    namespace fs = std::filesystem;
    using namespace gpu::disk_cache;
    auto root = fs::temp_directory_path() /
                ("genma-cache-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() { fs::remove_all(path); }
    } cleanup{root};
    auto path = root / "space path" / "shader.bin";
    std::vector<uint8_t> bytes, first{1, 2, 3, 4}, second{5, 6, 7};
    assert(!Read(path, Kind::Shader, 42, bytes));
    assert(Write(path, Kind::Shader, 42, first));
    assert(Read(path, Kind::Shader, 42, bytes) && bytes == first);
    assert(!Read(path, Kind::PipelineRecipe, 42, bytes));
    assert(!Read(path, Kind::Shader, 43, bytes));
    assert(Write(path, Kind::Shader, 42, second));
    assert(Read(path, Kind::Shader, 42, bytes) && bytes == second);
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(40);
        file.put('x');
    }
    assert(!Read(path, Kind::Shader, 42, bytes) && bytes.empty());
    assert(Write(path, Kind::Shader, 42, first));
    fs::resize_file(path, 42);
    assert(!Read(path, Kind::Shader, 42, bytes));
    assert(!Write(path, Kind::Shader, 42, {}));
    auto blocked = root / "not a directory";
    {
        std::ofstream file(blocked);
        file.put('x');
    }
    assert(!Write(blocked / "entry", Kind::Shader, 42, first));
    // Reject an oversized entry before allocating its declared payload.
    fs::resize_file(path, kMaxBytes + 41);
    assert(!Read(path, Kind::Shader, 42, bytes) && bytes.empty());
    std::atomic<int> writes{0};
    // Windows may reject a competing replacement while another writer owns
    // the destination. Caching is best effort; a miss must never leave a torn entry.
    std::thread a([&] {
        for (int i = 0; i < 25; i++)
            if (Write(path, Kind::Shader, 42, first))
                writes++;
    });
    std::thread b([&] {
        for (int i = 0; i < 25; i++)
            if (Write(path, Kind::Shader, 42, second))
                writes++;
    });
    a.join();
    b.join();
    assert(writes > 0 && Read(path, Kind::Shader, 42, bytes) && (bytes == first || bytes == second));
    for (auto &entry : fs::directory_iterator(path.parent_path()))
        assert(entry.path() == path);
    std::cout << "PASS: cache identity, replacement, corruption, truncation and concurrent atomic writes\n";
}
