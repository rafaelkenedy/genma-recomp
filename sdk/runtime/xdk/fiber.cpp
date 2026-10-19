// XAPI fibers. SwitchToFiber swaps the guest stack and returns into the other fiber, which recompiled code cannot
// follow: its returns are host returns. Each guest fiber runs on a host fiber of its own instead. The guest
// structures stay XAPI's, so inlined GetCurrentFiber and GetFiberData read what they expect: the fiber object
// (parameter, stack base, stack limit, saved esp) and the thread's current fiber at XAPI's TLS block + 8.
// CreateFiber and ConvertThreadToFiber keep their recompiled bodies, which only set up guest memory.
#include "xdk/hle.h"

#if defined(SYM_SwitchToFiber) && defined(SYM_XAPI_TlsIndex)

#include <memory>
#include <mutex>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using namespace kernel;

namespace {

struct Fiber {
    uint32_t obj = 0;     // guest fiber object
    void* host = nullptr;  // host fiber
    uint32_t start = 0, param = 0;
    // callee saved guest registers and the exception list while switched away
    uint32_t ebx = 0, esi = 0, edi = 0, ebp = 0, esp = 0, seh = 0xFFFFFFFF;
};

std::mutex g_lock;
std::unordered_map<uint32_t, std::unique_ptr<Fiber>> g_fibers;

Fiber* Find(uint32_t obj) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_fibers.find(obj);
    return it == g_fibers.end() ? nullptr : it->second.get();
}

Fiber* Add(uint32_t obj) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto& f = g_fibers[obj];
    if (!f) f = std::make_unique<Fiber>();
    f->obj = obj;
    return f.get();
}

// XAPI's thread local block: [StackBase + _tls_index * 4]
uint32_t TlsBlock(X86Context& ctx) { return R32(R32(ctx.fs_base + 4) + R32(SYM(XAPI_TlsIndex)) * 4); }

// the host thread runs fibers once it has converted
void HostFiberThread() {
    if (!::IsThreadAFiber()) ::ConvertThreadToFiber(nullptr);
}

void WINAPI FiberMain(void* p) {
    auto* f = static_cast<Fiber*>(p);
    X86Context& ctx = CurrentContext();
    // the guest stack below the fiber object CreateFiber put at its top
    ctx.esp = (f->obj - 0x20) & ~15u;
    ctx.ebp = 0;
    W32(ctx.fs_base, 0xFFFFFFFF);
    LOGI("xapi: fiber %08X starts at %08X (%08X)", f->obj, f->start, f->param);
    uint32_t r = CallGuest(ctx, f->start, {f->param});
    // XAPI ends the thread when a fiber routine returns
    LOGI("xapi: fiber %08X returned %08X, the thread exits", f->obj, r);
    ExitThread(r);
}

}  // namespace

// (parameter) -> fiber object inside XAPI's TLS block
X86_EXTERN_FUNC(GUEST_ORIG(ConvertThreadToFiber));
X86_FUNC(GUEST(ConvertThreadToFiber)) {
    GUEST_ORIG(ConvertThreadToFiber)(ctx, base);
    HostFiberThread();
    Fiber* f = Add(ctx.eax);
    f->host = ::GetCurrentFiber();
    LOGI("xapi: thread converted to fiber %08X", ctx.eax);
}

// (stack size, start, parameter) -> fiber object at the top of its new stack
X86_EXTERN_FUNC(GUEST_ORIG(CreateFiber));
X86_FUNC(GUEST(CreateFiber)) {
    uint32_t start = R32(ctx.esp + 8), param = R32(ctx.esp + 12);
    GUEST_ORIG(CreateFiber)(ctx, base);
    if (!ctx.eax) return;
    Fiber* f = Add(ctx.eax);
    f->start = start;
    f->param = param;
    // recompiled code nests host frames as deep as the guest's calls, as on the guest threads
    f->host = ::CreateFiberEx(1u << 20, 64u << 20, FIBER_FLAG_FLOAT_SWITCH, FiberMain, f);
    if (!f->host) LOGE("xapi: CreateFiber %08X: no host fiber (%lu)", ctx.eax, ::GetLastError());
}

// (fiber): the stack goes back with the recompiled body
X86_EXTERN_FUNC(GUEST_ORIG(DeleteFiber));
X86_FUNC(GUEST(DeleteFiber)) {
    uint32_t obj = R32(ctx.esp + 4);
    void* host = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        auto it = g_fibers.find(obj);
        if (it != g_fibers.end()) {
            host = it->second->host;
            g_fibers.erase(it);
        }
    }
    if (host && host != ::GetCurrentFiber()) ::DeleteFiber(host);
    GUEST_ORIG(DeleteFiber)(ctx, base);
}

// (fiber): stdcall, callee saved registers and the exception list belong to each fiber
X86_FUNC(GUEST(SwitchToFiber)) {
    (void)base;
    uint32_t target = R32(ctx.esp + 4);
    uint32_t tls = TlsBlock(ctx);
    uint32_t cur = R32(tls + 8);
    Fiber* from = Find(cur);
    Fiber* to = Find(target);
    if (!to || !to->host) x86_trap(ctx, SYM(SwitchToFiber), "SwitchToFiber to a fiber the host did not make");
    if (!from) {
        // the thread's own fiber from before the first conversion the host saw
        HostFiberThread();
        from = Add(cur);
        from->host = ::GetCurrentFiber();
    }
    from->ebx = ctx.ebx;
    from->esi = ctx.esi;
    from->edi = ctx.edi;
    from->ebp = ctx.ebp;
    from->esp = ctx.esp + 8;  // the return address and the argument popped
    from->seh = R32(ctx.fs_base);
    W32(tls + 8, target);
    if (to != from) ::SwitchToFiber(to->host);
    // back on this fiber
    ctx.ebx = from->ebx;
    ctx.esi = from->esi;
    ctx.edi = from->edi;
    ctx.ebp = from->ebp;
    ctx.esp = from->esp;
    W32(ctx.fs_base, from->seh);
}

#endif
