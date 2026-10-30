# Warlords HD loading: local REA investigation

Analysis date: 2026-10-30. Read-only investigation requested to understand the remaster's treatment of the original loading screen. No Genma runtime changes were made in this investigation.

## Target and limits

Target: `H:\Steam\steamapps\common\Onimusha\game.exe`, SHA-256 `25d731b7316436e23f15dd855d1c411ed5b42e7a86514f2ba15c1a850526dbd9`.

REA/Ghidra examined the previously captured, decrypted loaded code through `out/onimusha-hd-memory-analysis.exe` (SHA-256 `7abc2b72e24e4e0279daa9100c9deb7739b42d77bff99e5ea338077788d45c11`). Container derivation and missing writable globals are described in [onimusha-hd-controls-analysis.md](onimusha-hd-controls-analysis.md). Addresses below are HD virtual addresses, not Xbox addresses. Descriptive responsibilities are analyst interpretations, not recovered symbols.

No HD room transition was traced dynamically during this investigation. This establishes parts of its loading architecture, **not the exact removal point of the Now Loading artwork, the duration of its black frames, or a comparison proving these mechanisms were newly introduced in HD**. Some mechanisms could have existed in the earlier console code.

## Observed flow

| Address | Observed behavior |
| --- | --- |
| `0x1400a2410` | Appends four-byte requests to a queue, reports `game_load_entry over!!` above eight entries, and activates a state byte at game-work `+0xa1bf`. |
| `0x1400a24c0` | Processes requests using that state byte, yields through a fiber-like external call between tasks, and resets queue/state flags on completion. Its background-change branch waits on `0x14060e874` before calling `0x140003cf0`. |
| `0x140003cf0` | Constructs a room BGS name, derives the selected background index, obtains its data, copies the required region and initializes the background resources. |
| `0x1400c57e0` | Searches a configurable set of background slots by mapped ID and readiness. A ready hit returns its existing pointer; a miss selects a slot, grows its allocation if necessary and calls `0x1400c5440` to read the required file region. |
| `0x1400c5440` | Opens a named file and loops until the requested byte range is read. This demand-miss path is synchronous; the remaster still has a path that waits for storage. Exact external API names were not independently resolved in this session. |
| `0x1400c6100` | Consults the table at game-work `+0xc60` for entries associated with the selected camera/background, preserves relevant resident slots and queues other background IDs. The tables' complete semantics were not decoded. |
| `0x14009da60` | Calls the loading dispatcher early in its update path and `0x1400c6680` near its end. |
| `0x1400c6680` | Advances a loading state machine across calls; consumes queued background IDs through `0x1400c5a70` and `0x1400c5dc0`. |
| `0x1400c5a70` | Prepares a background buffer and file region, publishes a request through shared state `0x14060e870`, signals an external synchronization operation, then polls completion using atomic transitions. |
| `0x1400c5dc0` | Similar staged loading for an associated resource. On completion, invokes another resource-processing routine. |

Together, the slot cache, camera-associated request table and staged completion form concrete evidence of background anticipation and reuse. Atomic request publication and signaling indicate a worker handoff, but the complete worker body and thread creation were not recovered here. Do not describe this as proof that all resource decoding or GPU upload happens asynchronously.

## Legacy stop/sleep messages

The dispatcher contains a case at `0x1400a26a2` that calls the logging wrapper with `nb todo : Load_stop not implemented`, writes `0xffff` at `0x140438978`, and continues queue processing. There is no wait in this particular case. Both decompilation and a separate LLVM instruction listing confirm the branch; initial Ghidra exact string xrefs missed it because the analyzed function body omitted switch-case ranges.

`0x1400c5650` and `0x140223c30` also log `Load_stop0 not implemented`, but perform additional state reset/resource work. They are not entirely empty functions.

Several fallback paths repeatedly log `Game_sleep not implemented`. Those loops are **not evidence of safely removed delays**: if reached under the decoded conditions they could hang. Their actual reachability was not measured.

No literal ASCII/UTF-16 `Now Loading` was found in the captured read-only data. This does not establish absence of an image-based or encoded loading message.

## Implications for GenmaRecomp

The supported direction is to anticipate likely camera backgrounds and retain ready resources before the native transition requests them. This is consistent with the current CPU/GPU cache work, but the HD's camera-associated table is a more precise lead than speculatively loading every resource in a room.

This evidence does not justify skipping Xbox loading functions, clearing guessed flags, removing all fades, or adding a fixed delay. To suppress the displayed screen safely, the Xbox entry/exit conditions and resource-readiness boundary still need mapping. Retaining a complete previously presented frame is a separate possible presentation technique; it was **not observed as the HD implementation**.

## Local evidence

Full REA results are retained under ignored `out/onimusha-hd-loading-*.json`, with task definitions in `out/onimusha-hd-loading-queue.json`. No game binaries or captured bytes are included in this document.

| Operation | Evidence ID |
| --- | --- |
| Dispatcher pseudocode | `ev_499ecf2fb4cdfe27fd249220293169136f9de312abec8a409658a10386883453` |
| Background initialization | `ev_84e92f98c4c13d75c31be12565070f6e5c2221bdbbc96423dd17d51be963e797` |
| Background slot cache | `ev_745f1a7471aaf9fd25e3d514f160db61ffc7dce6e7b0e07845c38f1b32ca6e5d` |
| Camera-associated queue | `ev_27c8133d5adf876b38b62f0a632b49b97630fd2cf9d12579ffffe3e8b6cc35e9` |
| Staged background request | `ev_73e98ac4d7d8270342d13f5fd2e2652e0ae1464fc53a9eea12ea3e9303cb1159` |
| Loading state machine | `ev_f702c2d50a1be6f56aaf0aa3adde32773242521fd409d434ce0a17e8f747e7a1` |
