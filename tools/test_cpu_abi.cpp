#include "cpu/x86_ops.h"
#include <cassert>
#include <iostream>
#include <vector>
X86_EXTERN_FUNC(test_guest_override);
X86_EXTERN_FUNC(test_guest_fallback);
X86_EXTERN_FUNC(test_generated_caller);
X86_EXTERN_FUNC(test_guest_throw);
X86_FUNC(test_guest_override) {
    ctx.eax = X86_MEM32(ctx.esp + 4) + 100;
    ctx.esp += 4;
}
int main() {
    std::vector<uint8_t> memory(0x2000);
    auto* base = memory.data();
    X86Context ctx{};
    ctx.esp = 0x1000;
    X86_MEM32(ctx.esp + 4) = 23;
    test_generated_caller(ctx, base);
    assert(ctx.eax == 123 && ctx.esp == 0x1004);
    ctx.esp = 0x1000;
    test_guest_fallback(ctx, base);
    assert(ctx.eax == 24 && ctx.esp == 0x1004);
    bool caught = false;
    try { test_guest_throw(ctx, base); }
    catch (const X86Resume& resume) {
        caught = true;
        assert(resume.target == 0x123456 && resume.esp == 0x1004);
    }
    assert(caught);
    std::cout << "PASS: strong/weak hooks, guest ABI and continuation exceptions across translation units\n";
}
