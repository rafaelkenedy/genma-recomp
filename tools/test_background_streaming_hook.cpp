// Exercise the production observer against a synthetic guest directory. Native
// routines are stubbed, but retain real return addresses and cdecl stack rules.
#include "../src/game/background_streaming.cpp"
#include <cassert>
#include <stdexcept>
#include <vector>

namespace kernel {
uint8_t* g_base = nullptr;
}
namespace mem {
bool IsCommitted(uint32_t at) { return at >= 0x200000 && at < 0x210000; }
} // namespace mem
namespace {
unsigned resolved = 0, notifications = 0;
assets::BackgroundHint observed;
} // namespace
X86_FUNC(__imp__sub_00011350) {
    ++resolved;
    ctx.eax = X86_MEM32(ctx.esp + 4);
    ctx.edx = 0x1234; // native register side effects must survive
    ctx.esp += 4;
}
X86_FUNC(__imp__sub_0001D840) {
    const uint32_t saved = ctx.esp;
    for (const auto [caller, id] : {std::pair{0x1D991u, 70u}, // unrelated source: ignore
                                    {0x1DA0Cu, 9u},
                                    {0x1DA38u, 9u}, // duplicate destination
                                    {0x1DA53u, 11u},
                                    {0x1DA82u, 0xffffu}}) {
        ctx.esp = saved - 32;
        X86_MEM32(ctx.esp) = caller;
        X86_MEM32(ctx.esp + 4) = id;
        sub_00011350(ctx, base);
        assert(ctx.esp == saved - 28 && ctx.eax == id && ctx.edx == 0x1234);
    }
    ctx.esp = saved + 4;
    ctx.eax = 0xabcdef;
}
int main() {
    std::vector<uint8_t> memory(0x600000);
    auto* base = memory.data();
    kernel::g_base = base;
    X86Context ctx{};
    X86_MEM16(0x546AE8) = 0;
    X86_MEM16(0x546AEA) = 5;
    X86_MEM16(0x546AEC) = 4;
    X86_MEM32(0x54347C) = 0x200000;
    for (unsigned id : {2u, 9u, 11u, 70u}) {
        const unsigned at = 0x200000 + id * 40;
        X86_MEM32(at) = id * 8192;
        X86_MEM32(at + 4) = 2049; // align independently, not after adding sizes
        X86_MEM32(at + 12) = 1;
    }
    assets::on_background = [](const assets::BackgroundHint& hint) {
        ++notifications;
        observed = hint;
    };
    const auto before = memory;
    const auto invoke = [&] {
        ctx.esp = 0x1000;
        X86_MEM32(ctx.esp + 4) = 2;
        sub_0001D840(ctx, base);
        assert(ctx.esp == 0x1004 && ctx.eax == 0xabcdef && collecting == nullptr);
    };
    invoke();
    assert(resolved == 5 && notifications == 1 && observed.count == 3);
    assert(observed.stage == 0 && observed.room == 5 && observed.camera == 4);
    assert(observed.ranges[0].offset == 2 * 8192 && observed.ranges[0].bytes == 6144);
    assert(observed.ranges[1].offset == 9 * 8192 && observed.ranges[2].offset == 11 * 8192);
    assert(observed.ranges[0].Contains(2 * 8192 + 100, 6044));
    assert(!observed.ranges[0].Contains(2 * 8192 + 100, 6045));
    assert(!observed.ranges[0].Contains(1, ~uint64_t{0}));
    // Observation never modifies the native directory.
    assert(std::equal(memory.begin() + 0x200000, memory.begin() + 0x210000, before.begin() + 0x200000));
    X86_MEM32(0x54347C) = 0xfffffff0; // never read invalid/wrapped pointers
    invoke();
    assert(observed.count == 0);
    assets::on_background = [](const assets::BackgroundHint&) { throw std::runtime_error("optional"); };
    invoke(); // observer exceptions cannot break native execution
    assert(assets::camera_context == 5ull * 65536 + 4);
    assets::on_background = nullptr;
}
