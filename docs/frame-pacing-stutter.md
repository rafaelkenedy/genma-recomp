# Frame pacing and traversal stutter

The guest calls `D3DDevice_BlockUntilVerticalBlank` at `0xE3D8B`. The original
XDK import at `0x184D90` clears the device's VBlank event and waits for its next
signal. Our HLE implementation instead slept for 16 ms. Guest work was added to
that delay, and the host's separate FPS limiter could drift against it.

The HLE call now waits for the SDK's next 60 Hz VBlank pulse. Windows uses a
high resolution waitable timer, with an ordinary timer fallback and a short
final spin. Presentation skips a second host cap of 60 or higher when the
guest already waited for VBlank; lower caps still apply. Late host deadlines
rebase instead of generating short catch-up frames. Linux shares this clock,
and Vulkan no longer applies the host limiter a second time inside `Present`.

Unlimited removes the host cap. It does not remove the game's own VBlank wait
or establish support for gameplay above 60 FPS.

## Traversal cache

D3D12 keeps compiled DXBC and previously successful pipeline recipes in
`shader-cache-d3d12/v1/` beside the executable. Sources, entry points and shader
profiles identify shader entries. Recipes contain the initialized `PsoKey`,
including its shader hashes, rather than pointers or native driver binaries.
They are rebuilt using the current device, root signature and pipeline code.

At device initialization, the renderer prepares up to 128 known recipes,
checking a three-second budget between recipes. A driver call already in
progress cannot be interrupted by this budget. This moves repeat pipeline
creation out of traversal and adds work at startup. New areas can still compile
shaders or pipelines during gameplay. The Vulkan cache is unchanged.

Entries have a version, type, key, size limit and checksum. DXBC is also checked
with shader reflection. Missing, truncated, incompatible or damaged entries
fall back to compilation. Writes replace complete files through unique temporary
files; a failed write does not prevent rendering. A failed warmup does not retain
a null pipeline in the live map. Deleting this generated directory is safe; it
will be learned again. Invalidate its version when changing the serialized format.

Caching bytecode alone was insufficient locally: time moved into driver PSO
creation. Native driver blob caching also failed to demonstrate a benefit, so
the implementation prepares known portable recipes before gameplay instead.

## Local measurements, 29 October 2026

Hardware: i7-4790K, RTX 3050, 32 GB RAM, Windows 10. Optimized LLVM-MinGW builds,
HD replacements enabled (23 GB pack), isolated copies of the same room 005 save.
Autopilot loads the save, then moves up/down/up. Runs last 90 seconds, without
frame captures or concurrent builds. The reference is `bd30555` plus the same
CSV instrumentation and bounded-run timer; the updated cache is learned first.

At 1280 x 720, 100% render scale, V-Sync off, cap 60:

| Measurement | Reference | Updated, prepared cache |
| --- | ---: | ---: |
| Traversal cadence, 45–63 s | 57.97 FPS | 59.89 FPS |
| Later cadence, 65–85 s | 58.12 FPS | 59.85 FPS |
| Later 99th-percentile interval | 20.22 ms | 20.64 ms |
| Later intervals over 33.334 ms | 0 | 2 |
| Largest loading interval, 30–45 s | 906.62 ms | 131.36 ms |
| HLSL compilation in loading window | 813.25 ms / 15 shaders | 0 ms / 0 shaders |
| Pipelines created in loading window | 14 | 0 |

The updated startup prepared 25/25 recipes in 2363 ms. The loading window is
deliberately wide enough to include the transition with that startup shift.
CSV time starts at the first backend presentation, whereas autopilot input is
scheduled from process launch. These are overlapping samples of the same save
and input sequence, not frame-by-frame deterministic replays.

Later investigation found that the extra interaction command after loading
could leave a dialogue open and block the movement commands in these early
runs. They establish presentation cadence and loading behavior, not verified
camera traversal. The subsequent texture-cache tests omit that command and
record positions and camera IDs, confirming cuts between cameras 4, 3 and 5.

These intervals measure CPU-side completion of backend presentation, not
displayed frames from PresentMon. The later updated sample still had two
intervals above 33.334 ms (maximum 47.23 ms). The loading peak still included
synchronous replacement texture work. This is evidence of an improvement in
the tested sequence, not evidence that all traversal or presentation stutter
has been eliminated.

At 2560 x 1440, 200% render scale and V-Sync on, the same traversal sample
improved from 43.41 to 54.20 FPS; the later sample improved from 38.58 to
51.19 FPS. This machine did not sustain 60 FPS in that configuration. There
were no new shaders or pipelines during the updated samples, so cache hits
do not explain away the remaining fluctuations.

Additional checks: the 30 FPS cap produced 29.8–29.9 FPS in the initial screens.
Unlimited completed the gameplay sequence but measured only 47.55 FPS in its
later traversal sample; it is not recommended as a stutter workaround on this
evidence. These settings checks are not a controlled comparison across identical
system load. A separate boot with every shader entry deliberately truncated
rejected all 25 warmup recipes, recompiled the required shaders and exited
normally. The healthy benchmark cache and original save were preserved.

## Reproducing and regression coverage

`XBE_FRAME_TRACE` selects a CSV output path for Windows D3D12. It records
per-frame intervals, draw/compile/pipeline/texture/synchronization times,
uploads, cache hits and DXGI Present duration. It works with or without
`XBE_PERF`; that variable additionally enables the one-second summary log.
The trace is buffered and flushed every 60 frames. `XBE_RUN_SECONDS` accepts
1–3600 seconds on Windows to stop through the normal host shutdown path.
Keep screenshots and other GPU work outside timed runs.

`test_frame_clock` checks deadlines, late frames, cap changes, guest-paced
frames and VBlank broadcast/retained edges. `test_disk_cache` checks identity,
replacement, checksum rejection, truncation and competing atomic writes.
Both run through CTest on Linux and the Windows package workflow.

Final local validation passed both new Windows tests and the modern controls
regression, all nine Linux core tests, the Windows distribution package build
and Linux runtime compilation. A bounded Linux/Dozen boot with Vulkan validation
enabled initialized the Vulkan swapchain and exited with code 0 without VUID or
validation errors. Dozen's existing non-conformance warning remains; this boot
does not validate Linux gameplay performance or visual parity.
