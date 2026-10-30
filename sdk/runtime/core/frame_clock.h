#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace timing {
using Clock = std::chrono::steady_clock;
constexpr auto kVBlankPeriod = std::chrono::nanoseconds(16666667);

// A late frame rebases the deadline instead of producing short catch-up frames.
class FrameDeadline {
    Clock::time_point next_{};
    uint32_t cap_ = 0;

  public:
    Clock::time_point Next(Clock::time_point now, uint32_t cap, bool guest_vblank = false) {
        if (guest_vblank && cap >= 60)
            cap = 0;
        if (!cap) {
            next_ = now;
            cap_ = 0;
            return now;
        }
        auto period = std::chrono::nanoseconds(1000000000ull / cap);
        if (cap != cap_) {
            cap_ = cap;
            next_ = now;
        }
        next_ += period;
        if (next_ < now)
            next_ = now;
        return next_;
    }
};

inline void WaitUntil(Clock::time_point deadline) {
#ifdef _WIN32
    // Windows 10 1803+. Older hosts fall back to an ordinary waitable timer.
    constexpr DWORD high_resolution = 0x00000002;
    struct Timer {
        HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, high_resolution, TIMER_MODIFY_STATE | SYNCHRONIZE);
        Timer() {
            if (!handle)
                handle = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE);
        }
        ~Timer() {
            if (handle)
                CloseHandle(handle);
        }
    };
    thread_local Timer timer;
    constexpr auto margin = std::chrono::microseconds(200);
    for (;;) {
        auto left = deadline - Clock::now();
        if (left <= Clock::duration::zero())
            return;
        if (timer.handle && left > margin) {
            LARGE_INTEGER due{};
            due.QuadPart = -std::max<int64_t>(
                1, std::chrono::duration_cast<std::chrono::nanoseconds>(left - margin).count() / 100);
            if (SetWaitableTimer(timer.handle, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer.handle, INFINITE);
                continue;
            }
            CloseHandle(timer.handle);
            timer.handle = nullptr;
        }
        if (!timer.handle && left > std::chrono::milliseconds(2))
            Sleep(1);
        else
            YieldProcessor();
    }
#else
    std::this_thread::sleep_until(deadline);
#endif
}

class VBlankSignal {
    std::mutex lock_;
    std::condition_variable changed_;
    uint64_t sequence_ = 0;

  public:
    void Pulse() {
        {
            std::lock_guard guard(lock_);
            ++sequence_;
        }
        changed_.notify_all();
    }
    uint64_t Sequence() {
        std::lock_guard guard(lock_);
        return sequence_;
    }
    void WaitAfter(uint64_t sequence) {
        std::unique_lock guard(lock_);
        changed_.wait(guard, [&] { return sequence_ != sequence; });
    }
    void WaitNext() {
        std::unique_lock guard(lock_);
        uint64_t sequence = sequence_;
        changed_.wait(guard, [&] { return sequence_ != sequence; });
    }
};
} // namespace timing
