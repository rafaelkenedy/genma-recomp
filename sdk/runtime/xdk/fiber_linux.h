#pragma once
// Windows fiber operations needed by fiber.cpp, backed by glibc ucontext on Linux x86-64.
#include <ucontext.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <memory>
#include <thread>
#include "core/log.h"
#define WINAPI
constexpr unsigned FIBER_FLAG_FLOAT_SWITCH = 0;
namespace linux_fiber {
struct Host {
    ucontext_t context{};
    void *allocation = nullptr;
    size_t bytes = 0;
    void (*entry)(void *) = nullptr;
    void *arg = nullptr;
    std::thread::id owner = std::this_thread::get_id();
    ~Host() {
        if (allocation)
            munmap(allocation, bytes);
    }
};
inline thread_local Host root;
inline thread_local Host *current = nullptr;
inline void Enter(uint32_t lo, uint32_t hi) {
    auto *p = reinterpret_cast<Host *>(uintptr_t(lo) | uintptr_t(hi) << 32);
    p->entry(p->arg);
    std::abort();
}
} // namespace linux_fiber
inline bool IsThreadAFiber() {
    return linux_fiber::current != nullptr;
}
inline void *ConvertThreadToFiber(void *) {
    return linux_fiber::current = &linux_fiber::root;
}
inline void *GetCurrentFiber() {
    return linux_fiber::current;
}
inline unsigned long GetLastError() {
    return static_cast<unsigned long>(errno);
}
inline void *CreateFiberEx(size_t, size_t reserve, unsigned, void (*entry)(void *), void *arg) {
    auto p = std::make_unique<linux_fiber::Host>();
    size_t page = size_t(sysconf(_SC_PAGESIZE));
    size_t usable = (reserve + page - 1) & ~(page - 1);
    p->bytes = usable + 2 * page;
    void *mem = mmap(nullptr, p->bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (mem == MAP_FAILED)
        return nullptr;
    p->allocation = mem;
    void *stack = static_cast<char *>(mem) + page;
    if (mprotect(stack, usable, PROT_READ | PROT_WRITE) || getcontext(&p->context))
        return nullptr;
    p->context.uc_stack.ss_sp = stack;
    p->context.uc_stack.ss_size = usable;
    p->context.uc_link = nullptr;
    p->entry = entry;
    p->arg = arg;
    uintptr_t value = reinterpret_cast<uintptr_t>(p.get());
    makecontext(&p->context, reinterpret_cast<void (*)()>(linux_fiber::Enter), 2, uint32_t(value),
                uint32_t(value >> 32));
    return p.release();
}
inline void SwitchToFiber(void *value) {
    auto *from = linux_fiber::current;
    auto *to = static_cast<linux_fiber::Host *>(value);
    if (!from || to->owner != std::this_thread::get_id()) {
        LOGE("fiber: cross-thread or unconverted context switch");
        std::abort();
    }
    linux_fiber::current = to;
    if (swapcontext(&from->context, &to->context))
        std::abort();
    linux_fiber::current = from;
}
inline void DeleteFiber(void *value) {
    delete static_cast<linux_fiber::Host *>(value);
}
