# Modern controls: native movement cancellation

Issues [#19](https://github.com/rafaelkenedy/genma-recomp/issues/19) and [#24](https://github.com/rafaelkenedy/genma-recomp/issues/24) report that stick movement cannot interrupt soul absorption or leave an attack as early as the original D-pad.

The modern input policy previously cleared all direction bits in both action states. Native D-pad movement continued to reach the original handlers, so the two inputs had different cancellation behavior.

## Change

When the stick is active and guard is not held, states 12 and 17 now receive held forward (`1`) in the temporary action words. Direction is intentionally only a request: the hook does not write the action state, animation flags, or player yaw during these states. Camera-relative yaw steering resumes on the next free-movement update. This can leave the first accepted transition using the existing facing for one update; no immediate attack aiming or soft lock is added.

Guard priority, physical D-pad priority, control lock `0x1000`, game lock `0x800000`, disconnect handling and menu/disabled fallbacks remain in force. Other action states keep their previous policy. Synthetic held/previous/pressed/repeat history is retained and scoped action restoration still preserves native writes to non-direction bits.

## Original USA code evidence

- Player update `0x1109A0` dispatches input through table `0x20FE40`. State 12 selects `0x10E920`; state 17 selects `0x10FBB0` and then an animation-specific handler.
- `0x10E987` tests held forward and `0x10E98D` writes native run state 2. Absorption does not need the host to force its state or cleanup.
- The ordinary attack input handler `0x10FA80` first performs native helpers, then tests animation flag `0x01000000` at player `+0xE8C` (`0x10FAD0`). Only the native allowed window reaches the held-direction tests. Forward at `0x10FB6B` selects state 2 at `0x10FB70`. A closed window returns without that transition.

These addresses apply to the repository's USA XBE, not the Japanese executable or the 2019 remaster.

## Validation and limits

- `tools/test_modern_controls.cpp`: requests in both states, cardinal camera references, unchanged yaw, guard priority and input-lock rejection, alongside existing movement/camera tests.
- `tools/test_modern_controls_hook.cpp`: production wrapper with its native callee stubbed; verifies input delivery, held history, scoped restoration, unchanged action state/yaw and both locks in the affected states.
- `tools/test_modern_controls_native.py`: Unicorn replay of the actual original `0x10E920` and `0x10FA80` machine code loaded from the local XBE. Missing movement reproduces the lock; held forward reaches state 2; a closed attack window stays in state 17. Only unrelated helper calls are stubbed. Requires the user's XBE and the Python `unicorn` package; it embeds no game code.

The isolated replay does not validate full animation/effect cleanup, every attack variant, Kaede combat or an entire gameplay session. In-game follow-up should compare stick and D-pad during absorption, early/late normal attacks, combos, guard, magic recovery and camera transitions. Do not close the reports solely on the strength of these bounded tests.

Gameplay follow-up, 2026-10-27: the maintainer tested the compiled development game after being asked to check stick movement during soul absorption and immediately after attacking, and reported that it works. This confirms the reported scenarios in that session; broader character/combat coverage remains unverified.
