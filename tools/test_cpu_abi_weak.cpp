// Synthetic generated functions; no game bytes are included.
#include "cpu/x86_ops.h"
X86_FUNC(test_guest_impl) {
    ctx.eax = X86_MEM32(ctx.esp + 4) + 1;
    ctx.esp += 4;
}
X86_WEAK_FUNC(test_guest_override, test_guest_impl)
X86_WEAK_FUNC(test_guest_fallback, test_guest_impl)
X86_FUNC(test_generated_caller) {
    test_guest_override(ctx, base);
}
X86_FUNC(test_guest_throw) {
    (void)base;
    throw X86Resume{0x123456, ctx.esp};
}
