#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Independent movement policy; no guest addresses or host input APIs.
namespace modern {
constexpr int Wrap(int a) { return a & 4095; }
constexpr int Difference(int a, int b) { return ((a - b + 2048) & 4095) - 2048; }
inline int Angle(float x, float z) {
    return Wrap(static_cast<int>(std::atan2(x, z) * (4096.0 / 6.283185307179586)));
}
struct Frame {
    int x = 0, y = 0, camera = 0, cameraYaw = 0, yaw = 0;
    uint8_t state = 0;
    uint32_t held = 0;
    bool allowed = true;
    float seconds = 1.0f / 60;
};
struct Result {
    bool active = false, steer = false, dodge = false;
    int yaw = 0, target = 0;
    uint32_t directions = 0;
};
class Movement {
    bool active_ = false, retained_ = false, wasFree_ = false;
    int camera_ = 0, retainedYaw_ = 0, cutStick_ = 0, lastStick_ = 0;
public:
    void Reset() { active_ = retained_ = wasFree_ = false; }
    Result Update(const Frame& f) {
        Result r;
        r.yaw = f.yaw;
        r.directions = f.held & 15;
        const double magnitude = std::hypot(double(f.x), double(f.y));
        if (!f.allowed || (f.held & 15) || magnitude <= (active_ ? 6500 : 8192)) {
            Reset();
            return r;
        }
        const int stick = Angle(float(f.x), float(f.y));
        const bool guard = (f.held & 0x10) != 0;
        const bool free = f.state <= 4 && !guard;
        if (!active_ || !free || !wasFree_) {
            retained_ = false;
        } else {
            if (f.camera != camera_ && !retained_) {
                retained_ = true;
                retainedYaw_ = Wrap(f.yaw);
                cutStick_ = lastStick_;
            }
            // Compare with the cut's anchor, not the preceding sample: slow,
            // deliberate changes must also release the retained heading.
            if (retained_ && std::abs(Difference(stick, cutStick_)) > 140)
                retained_ = false;
        }
        active_ = r.active = true;
        wasFree_ = free;
        camera_ = f.camera;
        lastStick_ = stick;
        // Preserve the actual facing at a cut, even during a gradual turn.
        // Small stick corrections cannot bend this retained world heading.
        r.target = retained_ ? retainedYaw_ : Wrap(f.cameraYaw - stick);
        r.directions = 0;
        if (free) {
            r.steer = true;
            const float dt = std::clamp(f.seconds, 0.0f, 0.05f);
            const int cap = std::max(1, int(1200.0f * 4096 / 360 * dt));
            r.yaw = Wrap(f.yaw + std::clamp(Difference(r.target, f.yaw), -cap, cap));
            // Let native locomotion own collision and animation. Turn in place
            // before starting a run when the requested direction is behind us.
            if (f.state == 2 || std::abs(Difference(r.target, r.yaw)) <= 1024)
                r.directions = 1;
        } else if (guard && (f.state == 5 || f.state == 6 || f.state == 7 ||
                            f.state == 13 || f.state == 14)) {
            r.dodge = true;
            const int relative = Difference(Wrap(f.cameraYaw - stick), f.yaw);
            const int distance = std::abs(relative);
            r.directions = distance <= 512 ? 1 : distance >= 1536 ? 4 : relative > 0 ? 8 : 2;
        }
        // Guard entry must not receive forward (which takes priority over guard).
        // Other action states receive no synthetic direction: magic, attacks,
        // recovery, interactions and scripted motion remain native.
        return r;
    }
};
inline uint32_t RawDirections(uint32_t action) {
    return ((action & 1) << 16) | ((action & 4) << 15) |
           ((action & 8) << 15) | ((action & 2) << 18);
}
struct Actions { uint32_t held, previous, pressed, repeat; };
inline Actions Substitute(Actions original, uint32_t directions, uint32_t previous) {
    const uint32_t pressed = directions & ~previous & 15;
    return {(original.held & ~15u) | directions,
            (original.previous & ~15u) | (previous & 15),
            (original.pressed & ~15u) | pressed,
            (original.repeat & ~15u) | pressed};
}
} // namespace modern
