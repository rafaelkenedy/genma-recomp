// Exercise the production hook against guest memory, with only its original
// callee stubbed. This verifies ABI, input isolation and native fallback paths.
#include "../src/game/modern_controls.cpp"
#include <cassert>
#include <iostream>
#include <vector>

namespace kernel { uint8_t* g_base = nullptr; }
namespace input {
bool connected = true;
Pad Poll(int) { Pad p; p.connected = connected; return p; }
}
namespace app {
std::recursive_mutex& UiLock() { static std::recursive_mutex lock; return lock; }
bool menu = false;
bool MenuOpen() { return menu; }
}
namespace logx { void Write(Level, const char*, ...) {} }
namespace {
modern::Actions observed;
uint32_t observedPad;
unsigned calls = 0;
}
X86_FUNC(__imp__sub_001109A0) {
    ++calls;
    assert(X86_MEM32(ctx.esp + 4) == kPlayer);
    observed = {X86_MEM32(kActions), X86_MEM32(kActions + 4),
                X86_MEM32(kActions + 8), X86_MEM32(kActions + 12)};
    const uint32_t pad = kPad + X86_MEM16(kPort) * 0xFC;
    observedPad = X86_MEM32(pad);
    // Native changes to non-direction bits must survive the scope restoration.
    X86_MEM32(kActions) |= 0x10000000;
    X86_MEM32(pad) |= 0x80000000;
    ctx.eax = 123;
    ctx.esp += 4; // cdecl return, argument stays on stack
}
int main() {
    std::vector<uint8_t> memory(0x600000);
    auto* base = memory.data();
    kernel::g_base = base;
    X86Context ctx{};
    X86_MEM16(kPort) = 0;
    X86_MEMF32(kView) = 1; // LookAtRH screen-right +X -> camera forward -Z
    X86_MEM16(kPlayer + 0x8E2) = 2048;
    X86_MEM16(kPad + 0x22) = 32767;
    X86_MEM32(kActions) = 0x100;
    const auto invoke = [&] {
        ctx.esp = 0x1000;
        X86_MEM32(ctx.esp + 4) = kPlayer;
        sub_001109A0(ctx, base);
        assert(ctx.esp == 0x1004 && ctx.eax == 123);
    };
    invoke();
    assert(observed.held == 0x101 && observed.pressed == 1);
    assert(X86_MEM32(kActions) == 0x10000100); // original direction + native writes
    assert((X86_MEM32(kPad) & 0x800F0000) == 0x80000000);
    invoke();
    assert(observed.previous == 1 && observed.pressed == 0);
    // A camera cut preserves the guest's actual facing, not its prior target.
    X86_MEM16(kPlayer + 0x8E2) = 1900;
    X86_MEM16(kCamera) = 1;
    X86_MEMF32(kView) = -1;
    invoke();
    assert(X86_MEM16(kPlayer + 0x8E2) == 1900);
    assert((observed.held & 15) == 1 && observed.pressed == 0);
    X86_MEMF32(kView) = 1;
    // Both native side-dodge reads receive the correct raw direction.
    X86_MEM8(kPlayer + 0x24) = 13;
    X86_MEM32(kActions) = 0x10;
    X86_MEM16(kPad + 0x22) = 0;
    X86_MEM16(kPad + 0x20) = 32767;
    invoke();
    assert((observed.held & 15) == 2 && (observedPad & 0xF0000) == 0x80000);
    assert((X86_MEM32(kPad) & 0xF0000) == 0);
    X86_MEM16(kPad + 0x20) = uint16_t(-32767);
    invoke();
    assert((observed.held & 15) == 8 && (observedPad & 0xF0000) == 0x40000);
    // Native attack/absorb handlers must receive held movement intent, without
    // us rotating the character or modifying its state/animation to force a cancel.
    for (int state : {12, 17}) {
        X86_MEM8(kPlayer + 0x24) = uint8_t(state);
        X86_MEM16(kPlayer + 0x8E2) = 1900;
        X86_MEM32(kActions) = 0x200; // retain the absorb action, when held
        X86_MEM32(kActions + 4) = 0x200;
        X86_MEM32(kActions + 8) = 0;
        X86_MEM32(kActions + 12) = 0;
        synthesized = false;
        invoke();
        assert(observed.held == 0x201 && observed.pressed == 1);
        assert(X86_MEM16(kPlayer + 0x8E2) == 1900);
        assert(X86_MEM8(kPlayer + 0x24) == state);
        assert(X86_MEM32(kActions) == 0x10000200);
        invoke();
        assert(observed.previous == 0x201 && observed.pressed == 0);
        for (uint32_t flags : {0x1000u, 0x800000u}) {
            X86_MEM32(kControlFlags) = flags == 0x1000 ? flags : 0;
            X86_MEM32(kGameFlags) = flags == 0x800000 ? flags : 0;
            invoke();
            assert((observed.held & 15) == 0);
        }
        X86_MEM32(kControlFlags) = X86_MEM32(kGameFlags) = 0;
    }
    for (int reason = 0; reason < 7; ++reason) {
        X86_MEM32(kActions) = 0x100;
        X86_MEM32(kControlFlags) = reason == 0 ? 0x1000 : 0;
        X86_MEM32(kGameFlags) = reason == 1 ? 0x800000 : 0;
        g_modernControls = reason != 2;
        app::menu = reason == 3;
        input::connected = reason != 6;
        X86_MEMF32(kView) = 1;
        X86_MEM8(kPlayer + 0x24) = 0;
        X86_MEM16(kPlayer + 0x8E2) = 0;
        if (reason == 4) X86_MEM32(kActions) |= 4; // D-pad priority
        if (reason == 5) X86_MEMF32(kView) = 0; // uninitialized camera
        invoke();
        assert((observed.held & 15) == (reason == 4 ? 4u : 0u));
        assert(X86_MEM16(kPlayer + 0x8E2) == 0);
    }
    assert(calls == 20);
    std::cout << "PASS: production hook ABI, history, restoration, soul/attack cancel intent and locks, raw dodge sides, menu, option, D-pad and camera\n";
}
