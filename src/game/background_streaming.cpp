#include "core/streaming.h"
#include "kernel/memory.h"
#include "xdk/hle.h"
#include <algorithm>

namespace {
// USA retail, verified in generated listings: 1D840 builds the camera's native
// prefetch queue; 11350 resolves story-dependent background aliases. Observe the
// original resolver results rather than invoking it again (it has side effects).
struct Selection {
    std::array<int, 17> ids{};
    size_t count = 0;
    void Add(int id) {
        if (id >= 0 && id < 128 && count < ids.size() &&
            std::find(ids.begin(), ids.begin() + count, id) == ids.begin() + count)
            ids[count++] = id;
    }
};
thread_local Selection* collecting = nullptr;
} // namespace

X86_EXTERN_FUNC(__imp__sub_00011350);
X86_FUNC(sub_00011350) {
    const uint32_t caller = X86_MEM32(ctx.esp);
    __imp__sub_00011350(ctx, base);
    // Destination resolutions and the current camera inside 1D840. Exclude the
    // source-camera comparison at 1D991: it enumerates unrelated table rows.
    if (collecting &&
        (caller == 0x1D8EB || caller == 0x1DA0C || caller == 0x1DA38 || caller == 0x1DA53 || caller == 0x1DA82))
        collecting->Add(int16_t(ctx.eax));
}

X86_EXTERN_FUNC(__imp__sub_0001D840);
X86_FUNC(sub_0001D840) {
    Selection selection;
    // Caller already supplies the mapped current background (11BE2, DAFD7).
    selection.Add(int(X86_MEM32(ctx.esp + 4)));
    auto* previous = collecting;
    collecting = &selection;
    struct Restore {
        Selection* previous;
        ~Restore() { collecting = previous; }
    } restore{previous};
    __imp__sub_0001D840(ctx, base);
    collecting = previous;
    assets::BackgroundHint hint;
    hint.stage = X86_MEM16(0x546AE8);
    hint.room = X86_MEM16(0x546AEA);
    hint.camera = X86_MEM16(0x546AEC);
    if (hint.stage > 5 || hint.room > 99)
        return;
    // BGS directory records have a 40-byte stride. Native 1D030 reads offset
    // +0 and the independently sector-aligned sizes +4/+12 for each background.
    const uint32_t table = X86_MEM32(0x54347C);
    for (size_t i = 0; table && i < selection.count; ++i) {
        const uint64_t entry = uint64_t(table) + uint32_t(selection.ids[i]) * 40;
        if (entry + 16 > 0x100000000ull || !mem::IsCommitted(uint32_t(entry)) ||
            !mem::IsCommitted(uint32_t(entry + 15)))
            continue;
        const uint64_t first = (uint64_t(X86_MEM32(entry + 4)) + 2047) & ~2047ull;
        const uint64_t second = (uint64_t(X86_MEM32(entry + 12)) + 2047) & ~2047ull;
        const uint64_t offset = X86_MEM32(entry);
        if (!first || first + second > (64ull << 20) || offset + first + second > (64ull << 20))
            continue;
        hint.ranges[hint.count++] = {offset, first + second};
    }
    assets::Backgrounds(hint);
}
