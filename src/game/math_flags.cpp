#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_0017EC70);

// The CRT acos entry at 17CBE0 calls this classifier, then 17CBFD.
// 17CBFD consumes ZF before setting any flags. The generated classifier
// keeps CMP flags in local variables and loses them at RET, so publish the
// original CMP result while retaining its register and stack behavior.
X86_FUNC(sub_0017EC70) {
    const uint32_t exponent = X86_MEM32(ctx.esp + 8) & 0x7FF00000u;
    __imp__sub_0017EC70(ctx, base);
    const uint32_t result = exponent - 0x7FF00000u;
    ctx.cf = exponent < 0x7FF00000u;
    ctx.zf = result == 0;
    ctx.sf = result >> 31;
    ctx.of = ((exponent ^ 0x7FF00000u) & (exponent ^ result)) >> 31;
    ctx.pf = X86_PARITY(result);
}
