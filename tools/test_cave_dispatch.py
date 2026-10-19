"""Check the ROOM006 dispatch override against the XBE and emitted C++.

Run after recompilation: python -P tools/test_cave_dispatch.py
Requires the local assets/default.xbe; no third-party Python packages.
"""
from pathlib import Path
import re
import struct
import tomllib

ROOT = Path(__file__).resolve().parents[1]
image = (ROOT / "assets/default.xbe").read_bytes()
base = struct.unpack_from("<I", image, 0x104)[0]
count, headers = struct.unpack_from("<II", image, 0x11C)


def read_va(address, size):
    for index in range(count):
        _, va, _, raw, raw_size = struct.unpack_from(
            "<5I", image, headers - base + index * 56
        )
        if va <= address and address + size <= va + raw_size:
            return image[raw + address - va:raw + address - va + size]
    raise AssertionError(f"Unmapped XBE address: {address:#x}")


# Verify the instruction and all original destinations, independently of TOML.
assert read_va(0x90E45, 7) == bytes.fromhex("ff248d24100900")
original = list(struct.unpack("<11I", read_va(0x91024, 11 * 4)))
config = tomllib.loads((ROOT / "config/game.toml").read_text(encoding="utf-8"))
overrides = [s for s in config["switch"] if s["at"] == 0x90E45]
assert len(overrides) == 1, "Missing or duplicate cave dispatch override"
assert overrides[0]["targets"] == original, "Override differs from original XBE"

sources = list((ROOT / "generated/game").glob("x86_recomp.*.cpp"))
dispatches = []
for path in sources:
    source = path.read_text(encoding="utf-8")
    match = re.search(r"// 00090E45 jmp[^\n]*\n(.*?)\n\t\}", source, re.S)
    if match:
        dispatches.append((source, match[1]))
assert len(dispatches) == 1, "Regenerate the recompilation sources first"
source, dispatch = dispatches[0]
cases = re.findall(r"case 0x([0-9A-F]+)u: ([^\n]+)", dispatch)
assert [int(address, 16) for address, _ in cases] == original, (
    "Generated dispatch omits original states (including crash states 3 and 9)"
)
all_sources = "\n".join(p.read_text(encoding="utf-8") for p in sources)
for address, branch in cases:
    assert "x86_icall" not in branch, f"Unresolved state target {address}"
    target = re.search(r"goto (loc_[0-9A-F]+);", branch)
    if target:
        assert f"{target[1]}:" in source, f"Missing local target {address}"
    else:
        target = re.search(r"(sub_[0-9A-F]+)\(ctx, base\)", branch)
        assert target, f"Unexpected dispatch for {address}: {branch}"
        assert f"X86_FUNC(__imp__{target[1]}) {{" in all_sources, (
            f"Missing recompiled target {address}"
        )
        assert f"X86_WEAK_FUNC({target[1]}, __imp__{target[1]})" in all_sources
print("PASS: all 11 original cave states have compiled dispatch targets; states 3/9 included")
