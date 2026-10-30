#pragma once
#include <atomic>
#include <filesystem>

namespace assets {
// Optional host-side prefetch notification. It does not change guest I/O,
// handles, offsets, completion events or the lifetime of the opened file.
using OpenObserver = void (*)(const std::filesystem::path&);
inline std::atomic<OpenObserver> on_open{nullptr};
inline void Opened(const std::filesystem::path& path) noexcept {
    try {
        if (auto observer = on_open.load())
            observer(path);
    } catch (...) { /* Optional prefetch must not fail a successful guest open. */
    }
}
} // namespace assets
