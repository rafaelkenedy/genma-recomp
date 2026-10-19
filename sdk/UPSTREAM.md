# Origin

This folder is a copy of [Xbox XBE Recomp](https://github.com/TGP482/XboxXBERecomp) by TGP482 (MIT License, see `LICENSE`),
taken at commit `99078b119b5d5e0d1d6afb1390e29437c0c984c0` (2026-10-18), with this port's changes on top. It is kept
in this repository rather than as a submodule; changes to it are made here.

Changes since that commit, in short:

- XDK 3911 support: `xdk/3911/layout.toml`, render state numbering, embedded frame buffers, DirectSound mix bin masks,
  XAPI fibers on host fibers (`runtime/xdk/fiber.cpp`)
- Recompiler: push of a segment register, data section jump tables, switch bound lookback, `XBE_RECOMP_WHY`,
  `--symbols-only`
- Renderer: CPU depth import, precise depth for vertex programs, point sampling nudge, frame recorder and live debug
  switches (`shadow_bias`, `skip_draws`, `precise_depth`, `point_nudge`)
- Release without game code: LLVM-MinGW build, `add_xbe_release`, `cmake/XbePackage.cmake`, the `launcher/`, and
  `assets/game_path.txt` in `runtime/app/game_files.cpp`

The port's notes in `../docs/` describe each change.
