// Differential testing: saves the guest's committed memory and registers around one call of a recompiled function,
// so tools/difftest.py can run the original x86 code in an emulator from the same state and compare.
// Armed by a file named "dumpskel" beside the exe; the next call writes difftest_before.bin and difftest_after.bin.
// Hooked in config/game.toml: DiffBefore at the function's entry, DiffAfter at its ret.
#include <cstdio>
#include <filesystem>
#include <algorithm>
#include <fstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#include "app/app.h"
#include "core/log.h"
#include "xdk/hle.h"

namespace {

bool g_armed = false;
uint32_t g_esp = 0;  // the call being recorded, by its stack

void Dump(const X86Context& ctx, const char* name) {
    auto path = app::CacheDir() / name;
#ifdef _WIN32
    FILE* f = _wfopen(path.c_str(), L"wb");
#else
    FILE* f = std::fopen(path.c_str(), "wb");
#endif
    if (!f) return;
    // registers: eax ecx edx ebx esp ebp esi edi, fs base, fcw
    uint32_t regs[10] = {ctx.eax, ctx.ecx, ctx.edx, ctx.ebx, ctx.esp, ctx.ebp, ctx.esi, ctx.edi, ctx.fs_base, ctx.fcw};
    fwrite(regs, 4, 10, f);
    // committed guest pages, as (address, size, bytes) blocks
    auto* base = static_cast<uint8_t*>(kernel::HostPtr(0));
    uint64_t total = 0;
#ifdef _WIN32
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
#else
    // Linux describes committed/readable host mappings through procfs. Clip each
    // mapping to the 4 GiB guest reservation and preserve the existing dump format.
    std::ifstream maps("/proc/self/maps");
    uintptr_t first = reinterpret_cast<uintptr_t>(base);
    for (std::string line; std::getline(maps, line);) {
        unsigned long long low = 0, high = 0;
        char permissions[5] = {};
        if (std::sscanf(line.c_str(), "%llx-%llx %4s", &low, &high, permissions) != 3 || permissions[0] != 'r') continue;
        uint64_t start = std::max<uint64_t>(low, first), end = std::min<uint64_t>(high, first + (1ull << 32));
        if (end <= start) continue;
        uint32_t header[2] = {uint32_t(start - first), uint32_t(end - start)};
        std::fwrite(header, 4, 2, f);
        std::fwrite(reinterpret_cast<const void*>(uintptr_t(start)), 1, size_t(end - start), f);
        total += end - start;
    }
#endif
    fclose(f);
    LOGI("difftest: %s, %llu bytes of guest memory, esp %08X", name, (unsigned long long)total, ctx.esp);
}

}  // namespace

void DiffBefore(X86Context& ctx, uint8_t* base) {
    (void)base;
    if (g_armed) return;
    std::error_code ec;
    if (!std::filesystem::remove(app::CacheDir() / "dumpskel", ec)) return;
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

