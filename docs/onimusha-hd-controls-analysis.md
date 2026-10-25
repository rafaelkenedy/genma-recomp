# Warlords HD controls: local REA investigation

Analysis date: 2026-10-25. This supplements the earlier public-source research in [modern-controls-reference.md](modern-controls-reference.md). It documents observed implementation details and proposed improvements; it does not change GenmaRecomp's controls.

## Target and evidence boundary

Target supplied by the user: `H:\Steam\steamapps\common\Onimusha\game.exe`, 5,755,472 bytes, x86-64 PE, preferred image base `0x140000000`. SHA-256 before and after investigation:

```text
25d731b7316436e23f15dd855d1c411ed5b42e7a86514f2ba15c1a850526dbd9
```

Tools: REA 6.7.0, Ghidra 12.1.4, and REA's Frida observation lane. No REA registration or game executable was rewritten. The remaster was launched for observation and restarted itself; REA attached to the resulting `game.exe` instance at the supplied path. That process was stopped after capture.

The disk executable is encoded/packed: the main file-backed sections have entropy close to eight bits/byte, and Ghidra's entry-point decompilation at `0x14a66b380` observes a loop XORing `0x60b` bytes with `0x2c`, followed by undecodable control flow. No particular protection vendor is inferred.

REA/Frida captured the loaded module's headers, code at RVA `0x1000` for `0x32b000` bytes, and read-only data at RVA `0x32c000` for `0x78000` bytes. Separate PE **analysis containers** preserve these RVAs; their headers, section metadata and analysis entry seed are synthesized. They are not runnable replacements, and their file offsets must not be attributed to the original executable. Writable globals were not captured in those containers: variable names/types in pseudocode remain decompiler inferences.

The broad Ghidra analysis timed out with default switch analysis. Re-analysis with `Decompiler Switch Analysis` and `Decompiler Parameter ID` disabled recovered the functions below; selected-page analysis provided an additional bounded check. Missing pages/callees and disabled analyzers limit whole-program conclusions. This investigation did not exercise the HD controls interactively or measure its update frequency.

## Useful functions

Addresses are loaded virtual addresses for this exact remaster build, not Xbox hook addresses. Descriptive names below are ours; the binary has no recovered source names for these functions.

| Address | Interpreted responsibility | REA evidence from the loaded-code analysis |
| --- | --- | --- |
| `0x1400d7df0` | XInput report conversion | `ev_8ec2c564de5fa336c5a3a444ba743f46c2f12048aff1a6719705996288c8e98e` |
| `0x1401c9200` | Raw pad update and radial filtering | `ev_3e79cd15cffd0141ce8c0d204ad9772c3200919944a2a1637af209c5cbf95ebe` |
| `0x1401c96a0` | Modern action mapping, guard directions, retention release and native input locks | `ev_014cdce2decc3bb4440435af2b6b6556e7bcf002aeecc56abfcafcdc89f870cc` |
| `0x140006180` | Camera-update wrapper that initializes retained movement state | `ev_d6bdcd716e3dc6128d62414ad0baf38535412017819e7a31dbd7a2c2032c594f` |
| `0x1401cb9c0` | Writes the player's analog movement heading | `ev_73fa280d50aa4184d373c5f92a6ca3462e5b7c1a090178fa1bf4f9e4b5aa5ddb` |
| `0x1401d6e70` | Wrapped angular distance | `ev_67f6784a9dbaac42694735ae5861d95bb9f60834f51e8054226cf78c4f37ba94` |

The local complete records are retained as `out/onimusha-hd-fast-*.json`, with additional bounded records under `out/onimusha-hd-focus-*.json`. Frida observations are retained under `out/onimusha-hd-rea-observe-*.json`; memory-container derivation is recorded in `out/onimusha-hd-focused-mapping.json`. These local artifacts are not committed or redistributed with the report.

## Input thresholds

`0x1400d7df0` calls the loaded `XInputGetState` entry through slot `0x14032c4f8`. Each signed 16-bit stick component is divided by 256 with truncation toward zero and stored as a signed byte, giving a range of -128 through 127. Triggers become digital buttons above 30/255. The update clears its raw report before reading devices, avoiding retention of stale report contents.

`0x1401c9200` computes `M2 = x*x + y*y` separately for both sticks. It passes the raw components onward when `M2 >= 1024`, and zeroes both components below that threshold. This is a radial cutoff of 32 in byte units, approximately 25% of a cardinal-axis range, without dead-zone subtraction in this layer. The digital direction aliases use separate per-axis comparisons against +32/-32.

`0x1401c96a0` applies a **second** threshold: analog action synthesis requires `M2 > 4096`, approximately half cardinal-axis travel. Retention is cancelled at `M2 <= 4096`. Thus a partially tilted stick can survive the initial filtering without producing the same forward action as a stronger tilt. The inspected running-state code does not establish proportional speed from stick magnitude; these thresholds must not be presented as proof of analog walking speed.

GenmaRecomp currently uses a roughly 25% activation threshold and a roughly 20% release threshold, and starts native locomotion above activation. The HD therefore supplies a useful reference for separating orientation sensitivity from movement activation, rather than simply increasing one global dead zone.

## Heading calculation and camera retention

The common angle call at `0x14032a3fa` is an indirect thunk through `0x14032c5d0`. Its loaded pointer was resolved against the observed `ucrtbase.dll` module base and that DLL's PE export table to **`atan2`**. The read-only constants are `3.1415927410125732` at `0x14036ea78` and `2048.0` at `0x14036eae0`: headings use approximately 4096 units per revolution, as in the Xbox game.

The ordinary desired heading is formed from `atan2(filteredX, -filteredY)` plus the current camera-reference angle, then converted to game units. Axis/sign conventions belong to the HD camera representation and should not be copied over the Xbox view-matrix conversion without verification.

The camera wrapper initializes the following state only when retention is not already active:

| Address | Observed use |
| --- | --- |
| `0x1403e9a04` | Saved player heading, read from the shared game-work pointer plus `0xb0c6` |
| `0x1403e9a08` | Protection counter, initialized to 30 |
| `0x1403e9a0c` | Saved camera reference angle |
| `0x1403e9a10` | Stick intent converted using the saved camera reference at capture time |
| `0x1403e9a14` | Retention-active flag |
| `0x1403fd4b4` | Current camera reference angle used outside retention |

The modern input mapper decrements the counter once per call while retention is active. The heading writer uses the **saved actual player heading**, rather than continuously following small stick changes using the old camera. It keeps that heading while the protected counter is nonzero or the requested old-camera heading is within 192 game units (approximately 16.875 degrees) of the saved heading. Once the counter is zero and that difference exceeds 192, it clears retention and writes the current-camera heading. Reducing the raw magnitude to half travel or less clears retention through the input mapper independently of the counter.

The guard-direction branch also uses the retained reference while active. There its release comparison uses the saved stick intent, and directional sectors are classified against the saved player heading. Consequently, the heading-writer and guard comparisons are related but not identical. Repeated camera-wrapper calls do not replace the original saved values while retention remains active.

Thirty calls are not a verified wall-clock duration. At 60 calls/second they would span about 0.5 seconds; other update rates change that. The distance helper folds a wrapped 12-bit difference using `0xfff - difference` above half a revolution, which differs by one unit from an ideal 4096-unit fold. That rounding detail is not a reason to reproduce it in our port.

GenmaRecomp v0.5.0 instead retains the camera reference indefinitely across cuts and releases it after a cumulative stick-angle change greater than 140 units (about 12.3 degrees), or a return below its release threshold. Small stick changes still alter that version's target heading immediately. It also caps turning at 1200 degrees/second. The inspected HD heading writer assigns the requested/saved yaw directly; it has no corresponding gradual turning cap in that function.

The local 2026-10-25 experiment implements candidate 1 below: actual-heading retention with a separate stick-intent anchor, retaining our immediate 140-unit release and existing dead zones and turning cap. Guard and action states end the movement latch. Candidates 2–4 remain unimplemented; the new retention still needs a gameplay comparison.

## Native movement and guard integration

The remaster's player update dispatches through state tables at `0x140355c80` and `0x140355da0`. The latter's state-2 entry is `0x1401cc670`. Its decompilation calls the analog heading writer after the original turn-bit adjustments, then invokes native motion processing. State 1 also has a call to the heading writer in the captured assembly. This supports a localized heading override while retaining native animation and movement, rather than a replacement locomotion system.

The modern input mapper uses sectors within 512 units (45 degrees) of forward and each side for guarded input, falling back to backward. It applies the same important native restrictions already respected by our implementation: the control lock at game-work `+0x1028`, mask `0x1000`, and the game-action restriction at `+0x1010`, mask `0x800000` (which retains only actions `0x6000`). No new enemy soft-lock selection was established by these functions.

## Improvements worth testing in GenmaRecomp

1. **Retain actual heading at a camera transition.** This can prevent small stick noise from rotating the player during the transition. Keep the captured stick direction as a separate intent anchor, as the HD does, instead of conflating heading and input intent.
2. **Add a configurable, time-based protection interval.** Test it against our immediate intentional-direction-change response. Copying 30 update calls would introduce frame-rate dependence and could noticeably delay an intentional reversal.
3. **Separate orientation and locomotion activation.** Compare the HD's roughly 25%/50% two-stage thresholds with our current activation/release hysteresis. This may reduce accidental movement while preserving fine orientation, but should be an explicit feel comparison rather than an assumed universal improvement.
4. **Keep guard directions consistent through camera transitions.** Test a retained-reference guard policy against our current-camera guard policy, including both dodge sides, new enemy-facing angles, entering/exiting guard and attack recovery.

Our current bounded turning, D-pad priority, input-lock handling and disconnect protection remain deliberate port choices. The analysis supplies concrete comparison candidates; Kaede, spells, wall-clock timing and the complete HD combat behavior still require targeted gameplay validation before changing defaults.
