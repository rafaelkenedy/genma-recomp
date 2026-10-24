# Distant gameplay effects (issue #11)

The software mixer used `minimum / distance` for every 3D voice, ignoring
`IDirectSound_SetRolloffFactor` and the rolloff field in
`IDirectSound_SetAllParameters`. Music does not normally use this 3D path.

The local recompilation shows Genma setting rolloff to float bits `0x391D4952`
(0.00015) during sound initialization, and again through `DS3DLISTENER` when
updating the listener. Its distance factor is 0.001; that converts world units
to metres for Doppler and must not be used to rescale attenuation distances.

The mixer now follows the retail XDK 3911 attenuation routine at `0x1988F9`.
Within the maximum distance its logarithm argument is `distance/minimum * rolloff`;
beyond maximum it is `(maximum/minimum - 1) * rolloff + 1`. It converts this to
integer millibels with `-2000 * log10(argument)` and clamps positive volume to
zero.
Both listener APIs update it, and deferred rolloff changes take effect on
`CommitDeferredSettings`. Custom per-buffer rolloff curves retain their existing
path. This change does not implement Doppler, reverb, or full deferred listener
position/orientation handling.

At distance 1000 with minimum 1, the original mixer's gain was 0.001; the retail
routine gives gain 1 with Genma's rolloff. At distance 10000 it gives about 2/3.

Opt-in diagnostics
(`XBE_AUDIO_DIAGNOSTICS=1`) measured a pre-clamp peak of 150.6542, with
41751/96000 samples clipping in one second. A 3D voice had left gain -866.6379.
The port used an unnormalized camera right vector in `side/distance` and only
bounded the upper speaker multiplier, allowing negative, amplified output.
The corrected pan normalizes the right vector and source direction and clamps
to [-1, 1], with a centred fallback for degenerate vectors.

The original direction routine `0x198707` instead computes angular coordinates
using ratios of projected components; `0x19809D` normalizes source vectors.
Our stereo pan remains an approximation, not a reproduction of the Xbox HRTF.
The corrected build was played for 321 measured seconds, with 1067 3D voice
play events. Its pre-clamp peak was 1.0021, with one clipped sample out of
30,816,000 measured samples; the game exited normally (`Exit (0)`). This
establishes that the extreme clipping was eliminated in that session, not that
all rooms or every subjective balance issue have been validated.

## REA evidence (2026-10-24)

REA 6.7.0 / Ghidra 12.1.4, static analysis. Source XBE SHA-256:
`7382d3e7380c507702684047777bd68bf424f9e665a4643619a66b265d2c867c`.
REA rejected XBE and raw x86 on its Windows provider, so analysis used a derived
PE carrier preserving original section bytes and addresses. Only the audio range
was marked executable to bound auto-analysis. The carrier was never executed.
This is evidence from a derived carrier, not a direct REA XBE import or original
Xbox runtime trace. Recovered types can be inaccurate; instruction operands and
the source data constants were checked alongside the pseudocode.

- Listener update `0x173B00`: `ev_f09b703219c13ef41b64dcde108e53a759336c2ce63847d079ec1ce4746c22a7`.
- Direction `0x198707`: `ev_9f54723bcaf77dc4cc6098c1b4b6866b168fc01ffc4dc83acb4120c507905807`.
- Attenuation `0x1988F9`: `ev_888e3e1c7a971e1075224c982b4b8e93442746867ca322b3b698b7b48f50f7cd`.
- Vector normalization `0x19809D`: `ev_53d8aa6e5058b5d63b265a80164b6c2707fd06a721f552a8da3f77140b5a42e5`.

Local-only evidence, mapping script, source/carrier hashes and REA snapshot are
under `out/audio-*` and `out/map-audio-xbe.mjs`; they include game-derived content
and must not be distributed with the port.

## Verification

From a Visual Studio C++ developer environment:

```
clang++ -std=c++20 -I sdk/runtime tools/test_audio_distance.cpp -o out/test_audio_distance.exe
out\test_audio_distance.exe
cmake --build out/build/release
```

The regression test covers minimum/maximum distance, default, zero and custom
rolloff, the factor passed by Genma, scaled orientation, zero vectors and bounded
speaker gains. The development executable also builds.
For listening validation, compare footsteps and sword strikes while moving
across the same room, including camera changes, and check music and movies.

References:
- https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee418787(v=vs.85)
- https://learn.microsoft.com/en-us/previous-versions/windows/desktop/ee416810(v=vs.85)
