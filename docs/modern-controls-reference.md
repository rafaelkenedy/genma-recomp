# Modern controls reference: genma-pc v0.2.6 r2

Analysis date: 2026-10-24. Reference: https://github.com/w1284603830/genma-pc

The GitHub main tree currently exposes the README. Implementation analysis used the locally retained public v0.2.6 r2 source archive, dated 2026-10-17, SHA-256 `8d9b3e031c7b95a86e3d5ad32399ba4d340a23755fb25ccf2e16f327a6400f95`, matching the retained release checksum list. Source archive URL: https://github.com/w1284603830/genma-pc/releases/download/v0.2.6/genma-pc_v0.2.6_._20261017_r2.zip

This is static source analysis, not a new gameplay validation. The reference supports the Japanese XBE; this port uses the USA XBE. Addresses below belong to the reference and must be remapped and verified before implementation here. The reference source declares GPL-2.0-or-later; this report contains an independent description, not copied implementation code.

## Source entry points

- `loader/src/genma_stick.cpp`: movement, camera-cut handling, attack steering, soft lock, guard/dodge and native patch sites.
- `loader/src/genma_patches.cpp`: installs the plugin when `left_stick` is enabled.
- `loader/src/common.h`, `settings.cpp`: defaults, limits and INI persistence.
- `loader/src/input/xinput.cpp`: configurable controller/keyboard mapping. WASD maps to the left stick; arrow keys map to the original D-pad.
- `docs/修复分类_移植方法与MOD开发指南.txt`, sections B1-B3, and `docs/验证范围.md`: dodge/input-lock regressions and validation limits.

## Integration approach

This is not just a controller remap. The reference executes the original x86 code and patches 16 instruction sites plus six raw-pad reads. It validates original instruction bytes before installing those patches.

Before the player update consumes actions, the plugin creates a private four-word copy: held, previous, pressed and repeat. It replaces only the direction bits in that copy. The original pad structures, menu input and other buttons remain available to the original game. D-pad directions override synthesized stick movement.

Separate hooks replace the player's yaw increments, suppress native auto-facing while stick aiming owns the heading, and customize attack/guard target selection. Two input representations must be handled: action directions use up/right/down/left, while the side-dodge handlers read raw pad bits in up/down/left/right order.

## Direction and movement

1. Read the active player's left-stick axes. Apply a per-axis deadzone of 10836/32767 (about 33%), subtracting the deadzone from the surviving component.
2. Compute the stick angle using the game's own angle routine. Angles wrap through 4096 units per revolution.
3. Read the horizontal screen-right direction from the view matrix. Add a quarter turn for camera-forward; subtract the stick angle to get the desired world heading.
4. Compare that heading with the player's yaw. In free movement states 0-4, synthesize forward plus a left/right turn action, with hooks steering the actual yaw.
5. From standing, pivot before running if the desired heading is more than 90 degrees away. While already running, continue forward while turning. Within roughly 2.1 degrees, stop emitting a turn bit.

The configured movement turn cap defaults to 1200 degrees/second, scaled by elapsed time with elapsed time capped at 50 ms. Turn magnitude also depends on remaining angle. The plugin does not implement proportional walking speed: stick magnitude activates movement and aiming, while locomotion stays in the original action/state machinery.

## Camera cuts

The plugin tracks camera index, the previous stick angle and a movement reference heading. When the camera changes while the stick angle remains within 140/4096 turns (about 12.3 degrees), the reference initially retains the previous camera heading and blends toward the new one by 14 yaw units per update (about 1.23 degrees/update).

Releasing the stick or changing its direction beyond that tolerance immediately adopts the current camera. This is a gradual transition, not an indefinite heading lock until release. Attacks, target selection and dodges use the current camera directly, rather than the movement blend.

The blend constant is per update; unlike turn-rate caps, it is not multiplied by elapsed time. A port should explicitly decide whether to preserve that behavior or make blend duration independent of update rate.

## Behavior by player state

| State/context | Stick behavior |
| --- | --- |
| Free movement, states 0-4 | Forward movement and steering toward the screen-relative heading |
| Guard held while standing/moving | Never synthesize forward, because the native stand state checks forward before guard |
| Guard/dodge states 5/6/7/13/14 | With `stick_dodge`, choose native forward/back/left/right dodge relative to player facing |
| Attacks, states 12/17 | Select a target in the stick cone and steer the attack, or turn toward the stick if none qualifies |
| Other actions | Only allow forward within 45 degrees of player facing; do not inject backward/side actions at the end of an animation |
| Native input lock | No synthesized movement or soft lock |

For dodges, directions within 45 degrees count as forward; at least 135 degrees counts as backward; the remaining sectors select left/right. Keeping the stick held does not create a fresh press each time guard resumes. The six patched raw-pad reads cover Samanosuke and Kaede branches; the reference docs explicitly do not claim full Kaede combat validation.

## Soft lock

Default activation is at least 50% raw stick deflection, subject to movement activation and the game's no-auto-facing flag. Candidates come from the game's own eligible-target list, which already applies native visibility/eligibility rules.

Defaults: 45-degree half-cone, radius 2500 world units, and 540 degrees/second steering when no enemy qualifies. Picking an enemy allows a three-times-faster turn toward it. Candidate score combines angular misalignment and distance; an existing player/attack target receives a 15% score discount to avoid flickering between near-equal choices.

When stick aiming owns the heading, the plugin suppresses the native auto-facing function so the two systems do not fight. Attack object selection also rejects objects outside the stick cone. With `stick_dodge=0`, the left stick instead influences guard target selection; right-stick target selection stays native.

## Important regression already solved in the reference

Reading raw stick values bypasses native action filtering unless explicitly gated. The reference checks both control-lock bit `0x1000` and game-lock bit `0x800000`. Earlier versions injected movement during magic, exiting its handler before cleanup and leaving attacks/other actions locked. The fix respects those locks rather than forcibly clearing game state.

The dodge fix is equally important: changing only the action copy did not cover native state-7 raw-pad reads, causing left and right to dodge the same way. All six corresponding reads must be accounted for.

## Reference-only address map

| Purpose | Japanese XBE address/offset |
| --- | --- |
| Action words | `0x542F50` |
| Pad base / active port | `0x542B60`, stride `0xFC` / `0x542F70` |
| Left-stick axes inside native pad | `+0x20`, `+0x22` |
| Player / state / yaw | `0x547340` / `+0x24` / `+0x8E2` |
| Player position / target / flags | `+0x890` / `+0xEB8` / `+0x1DF4` |
| Camera index / view matrix | `0x5469EC` / `0x53E500` |
| Game angle function | `0xAA8C0` |
| Player update / run turning | `0x110990` / `0x110E80` |
| Eligible-target list / attack target | `0x4B9DE4` / `0x4BA0B4` |
| Native auto-facing | `0x10AA70` |
| Control flags / game flags | `0x54355C` / `0x543544` |

## Official Warlords HD remaster: verification limits

Update, 2026-10-25: a subsequent local REA investigation recovered useful HD input, heading and camera-retention functions from loaded code. See [onimusha-hd-controls-analysis.md](onimusha-hd-controls-analysis.md) for the target hash, addresses, thresholds, evidence boundary and comparison with our implementation. The paragraph below describes the earlier public-source check.

Public-source check on 2026-10-24: Capcom's launch announcement confirms analog-stick support in the January 2019 remaster, but does not describe the movement conversion, dead zone, turning rate, targeting rules or camera-cut handling. Source: https://www.capcomfrance.fr/onimushasortie/ .

Steam's official support page links its manual to https://game.capcom.com/manual/onimusha . That manual returned HTTP 403 during this check, including platform-specific pages. No remaster executable was analyzed or gameplay tested. Therefore the camera blend, thresholds and soft-lock behavior documented above are findings from genma-pc, not verified properties of the official remaster. Establishing equivalence requires a controlled gameplay comparison or analysis of the remaster executable.

## Resident Evil research

The [Classic Trilogy FusionFix](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix) documents screen-relative movement, retaining heading through cuts until release or direction change, and native D-pad controls. It modifies the original trilogy, not the HD remaster. [RE1 Analog 3D Controls](https://github.com/Rebrond/re1-analog-3d) documents four camera-cut policies and an input hook deriving angles from RCUT camera data. These are independent implementations inspired by the HD controls, not proof of the HD's internal algorithm. [BhdTool](https://github.com/eleval/BhdTool) and [rehd-nodoors](https://github.com/FluffyQuack/rehd-nodoors) demonstrate public reverse engineering of RE1/RE0 HD but do not document that movement algorithm in the material reviewed.

Warlords HD gameplay reviewers also describe camera-cut disorientation: [Push Square](https://www.pushsquare.com/reviews/ps4/onimusha_warlords), [Pixel Judge](https://pixeljudge.com/reviews/onimushawarlords/?game_profile=38182) and [Maximum Utmost](https://maxutmost.com/review-onimusha-warlords-2019-remaster/). These observations cannot establish a precise camera-retention threshold or formula.

## GenmaRecomp implementation

`src/game/modern_controls.cpp` now wraps the USA player update `0x1109A0` with an independent policy in `modern_controls.h`. Confirmed USA operands in the generated listings: action words `0x543050`; pad base `0x542C60`, stride `0xFC`, axes `+0x20/+0x22`; active port `0x543070`; player `0x547440`; camera index `0x546AEC`; view matrix `0x53E600`; control/game flags `0x54365C/0x543644`. These data addresses were checked individually, rather than extrapolated from a presumed global delta. `0x106BD0` shows the input-lock masks; `0x1109A0` supplies the action pointer to both state tables and reads the same global held word on exit. The native side-dodge handlers read raw direction bits `0x40000/0x80000`.

The left stick uses a radial activation dead zone of 8192 with a 6500 release threshold. The 2026-10-25 experiment retains the character's actual heading at a camera cut, including when it is still turning toward the previous target. Small stick changes leave this world heading unchanged. Release or a cumulative stick-angle change exceeding 140/4096 of a turn (12.3 degrees) immediately adopts the current view; there is no timed protection interval. Subsequent cuts keep the original heading and intent anchor. Retention only starts between free-movement updates and ends on guard or other action states, so combat cannot leave a stale movement heading. This replaces the v0.5.0 camera-reference retention, following the local HD analysis. Free movement still steers at up to 1200 degrees/second, with elapsed time capped at 50 ms; native forward locomotion retains collision and animation ownership. From rest, large reversals pivot before running.

The D-pad takes priority. Guard entry receives no synthetic forward. Native guard/dodge states use directions relative to player facing and the current camera. Only direction bits in the action words and (for guard/dodge) raw pad word are replaced for the duration of player update, then restored; native writes to other bits survive. This also covers native left/right dodge reads without copying JP patch sites. Soul absorption (state 12) and attacks (state 17), when guard is not held, receive held forward as a native movement-cancel request, without yaw steering or direct state writes. The attack handler still enforces its animation cancel window. Other action states receive no synthetic directions or yaw steering. Native control locks, invalid camera data, paused settings, disabled controls, port changes and long update gaps reset movement ownership. The feature does not add soft lock or proportional walk speed. See [action cancellation](modern-action-cancel.md) for issues #19/#24 and the validation boundary.

Enabled by default, with the persisted **Modern analog controls** option in F1. `tools/test_modern_controls.cpp` checks the movement policy and `tools/test_modern_controls_hook.cpp` executes the production wrapper against simulated guest memory with only the original callee stubbed, checking ABI, action history, restoration and native fallback paths. Build and automated tests do not establish full gameplay validation of both characters, combat or every camera cut.

Validation commands (Windows, from the repository root):

```powershell
.\dev.bat clang++ -std=c++20 -Wall -Wextra tools/test_modern_controls.cpp -o out/test_modern_controls.exe
.\out\test_modern_controls.exe
.\dev.bat clang++ -std=c++20 -Wall -Wextra -D_CRT_SECURE_NO_WARNINGS -Isdk/runtime -Igenerated/game tools/test_modern_controls_hook.cpp -o out/test_modern_controls_hook.exe
.\out\test_modern_controls_hook.exe
.\dev.bat cmake --build out/build/release
cmake --build out/build/pkg --target GenmaOnimusha_runtime
```

For runtime diagnostics set `GENMA_TRACE_CONTROLS=1` before launching the game; it reports camera, state, stick, target heading, yaw, synthesized directions and position every 15 active updates. This is off normally.

The same smoke run entered native attack state 17 with synthetic directions suppressed, then returned to free movement and native run state 2 on a new stick command. Both native side-dodge directions were observed moving in opposite world directions. The run reported no error-level entries; it was stopped after the checks without saving the test game.

Runtime smoke check on 2026-10-24, USA XBE: booted a new game and reached the first outdoor area. Screen-right input on the left stick (`LSR` in the autopilot) produced native run state 2 and changed player position, with yaw matching target 2804. Holding stick-up crossed camera 1 to camera 0 while target and yaw remained 3828; subsequent movement after release used camera 0's reference (forward 2970). Guard plus a rear-relative direction entered native back-step state 5 and returned to guard 14. Guard plus opposite side-relative directions entered native side-dodge state 7 with action directions 2 and 8, moved in opposite world directions, and returned to guard 14. These are short Samanosuke checks, not full coverage of Kaede, magic recovery or all combat/camera combinations.
