#include "modern_controls.h"
#include "app/app.h"
#include "core/input.h"
#include "xdk/hle.h"
#include <chrono>
#include <cstring>
#include <cstdlib>

bool g_modernControls = true; // Accessed under app::UiLock, including the settings checkbox.

namespace {
// USA retail XBE: verified against the generated instruction listings.
constexpr uint32_t kActions = 0x543050, kPad = 0x542C60, kPort = 0x543070;
constexpr uint32_t kPlayer = 0x547440, kCamera = 0x546AEC, kView = 0x53E600;
constexpr uint32_t kControlFlags = 0x54365C, kGameFlags = 0x543644;
using Clock = std::chrono::steady_clock;
thread_local modern::Movement movement;
thread_local uint32_t previousDirections = 0;
thread_local bool synthesized = false;
thread_local int previousPort = -1;
thread_local Clock::time_point lastUpdate;

float ReadFloat(uint8_t* base, uint32_t address) {
    float v;
    std::memcpy(&v, base + address, sizeof(v));
    return v;
}
// Only the direction bits are temporary. Preserve native writes to all other
// actions/buttons; menus and the next input poll see the original directions.
struct ScopedDirections {
    uint32_t pad, actions[4], raw;
    explicit ScopedDirections(uint32_t p) : pad(p) {
        for (int i = 0; i < 4; ++i) actions[i] = kernel::R32(kActions + 4 * i) & 15;
        raw = kernel::R32(pad) & 0xF0000;
    }
    ~ScopedDirections() {
        for (int i = 0; i < 4; ++i) {
            const uint32_t a = kActions + 4 * i;
            kernel::W32(a, (kernel::R32(a) & ~15u) | actions[i]);
        }
        kernel::W32(pad, (kernel::R32(pad) & ~0xF0000u) | raw);
    }
};
}

X86_EXTERN_FUNC(__imp__sub_001109A0);
X86_FUNC(sub_001109A0) {
    const uint32_t player = X86_MEM32(ctx.esp + 4);
    bool enabled;
    {
        std::lock_guard lock(app::UiLock());
        enabled = g_modernControls && !app::MenuOpen();
    }
    const unsigned port = X86_MEM16(kPort);
    if (player != kPlayer || port >= 4) {
        movement.Reset();
        synthesized = false;
        __imp__sub_001109A0(ctx, base);
        return;
    }
    const auto now = Clock::now();
    const float elapsed = lastUpdate.time_since_epoch().count() ?
        std::chrono::duration<float>(now - lastUpdate).count() : 1.0f / 60;
    lastUpdate = now;
    if (previousPort != int(port) || elapsed > 0.25f) {
        movement.Reset();
        synthesized = false;
    }
    previousPort = int(port);
    const uint32_t pad = kPad + port * 0xFC;
    const float rightX = ReadFloat(base, kView), rightZ = ReadFloat(base, kView + 0x20);
    const bool validCamera = std::isfinite(rightX) && std::isfinite(rightZ) &&
                            std::hypot(rightX, rightZ) > 0.001f;
    modern::Frame f;
    f.x = int16_t(X86_MEM16(pad + 0x20));
    f.y = int16_t(X86_MEM16(pad + 0x22));
    f.camera = X86_MEM16(kCamera);
    f.cameraYaw = validCamera ? modern::Wrap(modern::Angle(rightX, rightZ) + 1024) : 0;
    f.yaw = X86_MEM16(player + 0x8E2) & 4095;
    f.state = X86_MEM8(player + 0x24);
    f.held = X86_MEM32(kActions);
    f.seconds = elapsed;
    // The guest may retain its last axes briefly after a controller disconnects.
    f.allowed = enabled && validCamera && input::Poll(int(port)).connected &&
                !(X86_MEM32(kControlFlags) & 0x1000) &&
                !(X86_MEM32(kGameFlags) & 0x800000);
    const auto result = movement.Update(f);
    const modern::Actions original{f.held, X86_MEM32(kActions + 4),
                                   X86_MEM32(kActions + 8), X86_MEM32(kActions + 12)};
    if (!result.active) {
        previousDirections = f.held & 15;
        synthesized = false;
        __imp__sub_001109A0(ctx, base);
        return;
    }
    ScopedDirections restore(pad);
    const auto actions = modern::Substitute(original, result.directions,
        synthesized ? previousDirections : original.previous);
    X86_MEM32(kActions) = actions.held;
    X86_MEM32(kActions + 4) = actions.previous;
    X86_MEM32(kActions + 8) = actions.pressed;
    X86_MEM32(kActions + 12) = actions.repeat;
    if (result.steer) X86_MEM16(player + 0x8E2) = uint16_t(result.yaw);
    // All native side-dodge reads inside this update see the same synthesized
    // left/right bits, including both character branches.
    if (result.dodge)
        X86_MEM32(pad) = (X86_MEM32(pad) & ~0xF0000u) | modern::RawDirections(result.directions);
    previousDirections = result.directions;
    synthesized = true;
    __imp__sub_001109A0(ctx, base);
    static const bool trace = std::getenv("GENMA_TRACE_CONTROLS") != nullptr;
    if (trace) {
        static thread_local unsigned count = 0;
        if ((count++ % 15) == 0)
            LOGR("modern controls: camera=%d state=%u stick=%d,%d target=%d yaw=%u dirs=%x pos=%.1f,%.1f",
                 f.camera, unsigned(f.state), f.x, f.y, result.target,
                 unsigned(X86_MEM16(player + 0x8E2) & 4095), unsigned(result.directions),
                 ReadFloat(base, player + 0x890), ReadFloat(base, player + 0x898));
    }
}
