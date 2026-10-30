#include "core/frame_clock.h"
#include <cassert>
#include <future>
#include <iostream>

int main() {
    using namespace std::chrono_literals;
    using timing::Clock;
    timing::FrameDeadline clock;
    Clock::time_point now{};
    auto period = std::chrono::nanoseconds(1000000000 / 60);
    assert(clock.Next(now, 60) == now + period);
    // Missed frame: no shortened catch-up interval on the following frame.
    now += 40ms;
    assert(clock.Next(now, 60) == now);
    assert(clock.Next(now + 1ms, 60) == now + period);
    assert(clock.Next(now, 0) == now);
    assert(clock.Next(now, 120) == now + std::chrono::nanoseconds(1000000000 / 120));
    assert(clock.Next(now, 60, true) == now);
    assert(clock.Next(now, 120, true) == now); // native clock stays at 60, no second limiter
    assert(clock.Next(now, 30, true) == now + std::chrono::nanoseconds(1000000000 / 30));
    assert(clock.Next(now, 60, false) == now + period); // unsynchronized paths retain their limit
    timing::VBlankSignal signal;
    auto ticket = signal.Sequence();
    auto first = std::async(std::launch::async, [&] { signal.WaitAfter(ticket); });
    auto second = std::async(std::launch::async, [&] { signal.WaitAfter(ticket); });
    signal.Pulse();
    assert(first.wait_for(1s) == std::future_status::ready);
    assert(second.wait_for(1s) == std::future_status::ready);
    // A pulse arriving before the waiter starts must not be lost.
    signal.WaitAfter(ticket);
    ticket = signal.Sequence();
    auto pending = std::async(std::launch::async, [&] { signal.WaitAfter(ticket); });
    assert(pending.wait_for(10ms) == std::future_status::timeout);
    signal.Pulse();
    assert(pending.wait_for(1s) == std::future_status::ready);
    std::cout << "PASS: frame deadlines, cap changes, guest-paced frames, broadcast and retained VBlank edges\n";
}
