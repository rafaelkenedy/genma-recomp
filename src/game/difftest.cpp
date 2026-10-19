// Differential testing: saves the guest's committed memory and registers around one call of a recompiled function,
// so tools/difftest.py can run the original x86 code in an emulator from the same state and compare.
// Armed by a file named "dumpskel" beside the exe; the next call writes difftest_before.bin and difftest_after.bin.
// Hooked in config/game.toml: DiffBefore at the function's entry, DiffAfter at its ret.
#include <cstdio>
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "app/app.h"
#include "core/log.h"
#include "xdk/hle.h"

namespace {

bool g_armed = false;
uint32_t g_esp = 0;  // the call being recorded, by its stack

void Dump(const X86Context& ctx, const char* name) {
    auto path = app::ExeDir() / name;
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return;
    // registers: eax ecx edx ebx esp ebp esi edi, fs base, fcw
    uint32_t regs[10] = {ctx.eax, ctx.ecx, ctx.edx, ctx.ebx, ctx.esp, ctx.ebp, ctx.esi, ctx.edi, ctx.fs_base, ctx.fcw};
    fwrite(regs, 4, 10, f);
    // committed guest pages, as (address, size, bytes) blocks
    auto* base = static_cast<uint8_t*>(kernel::HostPtr(0));
    uint64_t total = 0;
    for (uint64_t a = 0; a < (1ull << 32);) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(base + a, &mi, sizeof(mi))) break;
        uint64_t start = uint64_t(static_cast<uint8_t*>(mi.BaseAddress) - base), size = mi.RegionSize;
        if (mi.State == MEM_COMMIT && !(mi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) && start < (1ull << 32)) {
            uint64_t n = std::min<uint64_t>(size, (1ull << 32) - start);
            uint32_t hdr[2] = {uint32_t(start), uint32_t(n)};
            fwrite(hdr, 4, 2, f);
            fwrite(base + start, 1, size_t(n), f);
            total += n;
        }
        a = start + size;
    }
    fclose(f);
    LOGI("difftest: %s, %llu bytes of guest memory, esp %08X", name, (unsigned long long)total, ctx.esp);
}

}  // namespace

void DiffBefore(X86Context& ctx, uint8_t* base) {
    (void)base;
    if (g_armed) return;
    std::error_code ec;
    if (!std::filesystem::remove(app::ExeDir() / "dumpskel", ec)) return;
    g_armed = true;
    g_esp = ctx.esp;
    Dump(ctx, "difftest_before.bin");
}

void DiffAfter(X86Context& ctx, uint8_t* base) {
    (void)base;
    // the same call: its ret runs with the stack it was entered with, less the registers it pushed and popped
    if (!g_armed || ctx.esp != g_esp) return;
    g_armed = false;
    Dump(ctx, "difftest_after.bin");
}

