#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace assets {
struct BackgroundRange {
    uint64_t offset = 0, bytes = 0;
    bool operator==(const BackgroundRange&) const = default;
    bool Contains(uint64_t at, uint64_t size) const {
        return at >= offset && at - offset <= bytes && size <= bytes - (at - offset);
    }
};
struct BackgroundHint {
    uint16_t stage = 0, room = 0, camera = 0;
    // Current background first, then native camera-associated destinations.
    std::array<BackgroundRange, 17> ranges{};
    uint32_t count = 0;
    bool operator==(const BackgroundHint&) const = default;
};
using BackgroundObserver = void (*)(const BackgroundHint&);
inline std::atomic<BackgroundObserver> on_background{nullptr};
inline std::atomic<uint64_t> camera_context{~uint64_t{0}};
inline void Backgrounds(const BackgroundHint& hint) noexcept {
    camera_context.store(uint64_t(hint.stage) << 32 | uint64_t(hint.room) << 16 | hint.camera);
    try {
        if (auto observer = on_background.load())
            observer(hint);
    } catch (...) { /* Optional streaming cannot affect guest execution. */
    }
}
// Cumulative guest read durations, captured on the producer before Present is
// queued. These measure host CPU I/O calls, including OS cache hits, not disk time.
inline std::atomic<uint64_t> read_ns{0}, read_bytes{0};
struct StreamingFrame {
    uint64_t camera = ~uint64_t{0}, read_ns = 0, read_bytes = 0;
};
inline StreamingFrame StreamingSnapshot() { return {camera_context.load(), read_ns.load(), read_bytes.load()}; }
} // namespace assets
