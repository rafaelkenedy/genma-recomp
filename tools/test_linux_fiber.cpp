#include <cassert>
#include <cmath>
#include "xdk/fiber_linux.h"

struct Run {
    void *root = nullptr;
    void *fiber = nullptr;
    int turns = 0;
    bool complete = false;
};
void Worker(void *argument) {
    auto &run = *static_cast<Run *>(argument);
    volatile double local = 0.125;
    for (int i = 0; i < 1000; i++) {
        local += 0.25;
        run.turns++;
        assert(GetCurrentFiber() == run.fiber);
        SwitchToFiber(run.root);
        assert(local == 0.125 + (i + 1) * 0.25);
    }
    run.complete = true;
    SwitchToFiber(run.root);
    std::abort();
}
int main() {
    assert(!IsThreadAFiber());
    Run run;
    run.root = ConvertThreadToFiber(nullptr);
    assert(IsThreadAFiber());
    run.fiber = CreateFiberEx(0, 1u << 20, 0, Worker, &run);
    assert(run.fiber);
    volatile double caller = 1.5;
    for (int i = 0; i < 1000; i++) {
        SwitchToFiber(run.fiber);
        assert(run.turns == i + 1);
        assert(GetCurrentFiber() == run.root);
        assert(caller == 1.5);
    }
    assert(!run.complete);
    SwitchToFiber(run.fiber);
    assert(run.complete);
    DeleteFiber(run.fiber);
}
