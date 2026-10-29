# Genma Onimusha — Linux x86-64 experimental

This package includes no game data or recompiled game code. Supply your own supported copy.

Extract the entire folder, then run:

```sh
./GenmaOnimusha --check-install
./GenmaOnimusha --game '/path/to/Genma Onimusha.iso'
```

An extracted folder containing `default.xbe` and the game's directories is also accepted.
The supported XBE SHA-1 is `08bfe432eb55877b5af2dd401b713743c5b751cd`.
The first start recompiles and builds your copy locally using the bundled compiler.
Later starts can use `./GenmaOnimusha` directly. `--jobs N` limits parallel compilation;
Ctrl+C cancels a build, preserving completed objects. `--build-only` prepares the game without opening it.

Requires x86-64-v2 and glibc 2.35 or newer, a Vulkan-capable GPU/driver, X11 (or XWayland),
and PulseAudio-compatible output. The package includes a Vulkan loader, compiler/sysroot and DXC;
it does not include GPU drivers. No development packages, CMake, Python, Wine or Proton are needed to play.

Settings use `$XDG_CONFIG_HOME/genma-onimusha` (default `~/.config/genma-onimusha`).
Saves use `$XDG_DATA_HOME/genma-onimusha` (default `~/.local/share/genma-onimusha`).
Generated code, compiled game and shader caches use `$XDG_CACHE_HOME/genma-onimusha`
(default `~/.cache/genma-onimusha`). `--print-paths` prints the exact directories.
The extracted package may be read-only. Cache identity changes with the runtime/toolchain;
saves and configuration remain independent of the release folder.

Keyboard: WASD = left stick; arrows = D-pad; Enter = Start; Backspace = Back;
Z/X/C/V = A/B/X/Y; 1/2 = Black/White; Q/E = triggers. SDL gamepads are supported.
F11 toggles fullscreen, F3 captures when `XBE_CAPTURE_DIR` is set, Esc exits.

This native Vulkan port is experimental. Menu/F1, HD texture replacements and render scaling
are not available yet, and full visual/audio parity and Steam Deck behavior are still being validated.
WSL/Dozen and the Windows controller test bridge are not needed on native Linux.

Updates are manual: download the new Linux tarball and extract it into a new folder.
Do not use Windows update scripts. Your XDG saves/settings remain in place; the launcher
rebuilds the game for a changed runtime. See `manifest.json`, `dependencies.lock.json`,
`LICENSE`, `THIRD_PARTY.txt`, `toolchain/usr/share/doc/` and the shader-tools licenses for provenance.
