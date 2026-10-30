#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace gpu::disk_cache {
enum class Kind : uint32_t { Shader = 1, PipelineRecipe = 2 };
constexpr uint64_t kMaxBytes = 32ull << 20;
constexpr uint64_t kMagic = 0x3145484341434247ull;
inline uint64_t Checksum(std::span<const uint8_t> bytes) {
    uint64_t hash = 14695981039346656037ull;
    for (uint8_t byte : bytes)
        hash = (hash ^ byte) * 1099511628211ull;
    return hash;
}
inline bool Read(const std::filesystem::path &path, Kind kind, uint64_t key, std::vector<uint8_t> &bytes) {
    bytes.clear();
    std::error_code error;
    uint64_t size = std::filesystem::file_size(path, error);
    if (error || size < 40 || size > 40 + kMaxBytes)
        return false;
    std::ifstream file(path, std::ios::binary);
    std::array<uint64_t, 5> header{};
    if (!file.read(reinterpret_cast<char *>(header.data()), sizeof(header)))
        return false;
    if (header[0] != kMagic || header[1] != (1ull | uint64_t(kind) << 32) || header[2] != key || !header[3] ||
        header[3] != size - sizeof(header))
        return false;
    bytes.resize(size_t(header[3]));
    if (!file.read(reinterpret_cast<char *>(bytes.data()), bytes.size()) || Checksum(bytes) != header[4]) {
        bytes.clear();
        return false;
    }
    return true;
}
inline bool Write(const std::filesystem::path &path, Kind kind, uint64_t key, std::span<const uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > kMaxBytes)
        return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
        return false;
    static std::atomic<uint64_t> serial{0};
#ifdef _WIN32
    uint64_t process = GetCurrentProcessId();
#else
    uint64_t process = uint64_t(getpid());
#endif
    auto temporary = path;
    temporary += ".tmp." + std::to_string(process) + "." + std::to_string(serial.fetch_add(1));
    const std::array<uint64_t, 5> header{kMagic, 1ull | uint64_t(kind) << 32, key, bytes.size(), Checksum(bytes)};
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(header.data()), sizeof(header));
        file.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        file.close();
        if (!file) {
            std::filesystem::remove(temporary, error);
            return false;
        }
    }
#ifdef _WIN32
    bool ok = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
    std::filesystem::rename(temporary, path, error);
    bool ok = !error;
#endif
    if (!ok)
        std::filesystem::remove(temporary, error);
    return ok;
}
} // namespace gpu::disk_cache
