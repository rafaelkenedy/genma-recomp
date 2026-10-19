# Genma Onimusha - PC port (Xbox static recompilation)

An unofficial native Windows port of the original Xbox version of **Genma Onimusha**, made by statically recompiling the game's executable to C++ with [Xbox XBE Recomp](https://github.com/TGP482/XboxXBERecomp).

> [!IMPORTANT]
> This repository and its releases contain **no game code and no game assets**. You need your own legally acquired copy of the game. The release builds the game's code on your PC, from your copy, on the first start.

## Playing

1. Download the latest `GenmaOnimusha-*-win64.zip` from [Releases](../../releases) and extract it to a folder you can write to.
2. Run `GenmaOnimusha.exe`.
3. On the first start it asks for your copy of the game: pick the disc image (`.iso`, a full redump image or an XISO), or `default.xbe` in a folder with the game's files. You can also put the `.iso` in the `assets` folder beforehand.
4. It then recompiles and compiles the game for your PC with the bundled compiler, which takes a few minutes, and starts it. This happens once; later starts go straight to the game.

The release is made for **Genma Onimusha (USA)**, `default.xbe` SHA-1 `08bfe432eb55877b5af2dd401b713743c5b751cd` (redump). Another release of the game gets a warning before it is built.

Press **F1** in the game for the settings menu: display mode and resolution, aspect (4:3, stretch), render scale, frame rate cap, V-Sync, texture filtering and the port's fixes, applied at once; **Save** writes them to `game/settings.toml`. The first-start log is `work/build.log`, the game's logs are in `game/logs`.

### Requirements

- Windows 10 or 11, 64-bit
- A GPU with Direct3D 12
- About 1.5 GB of free disk space for the first build

## Improvements over the original

- Rendering at the output's resolution (or above, as supersampling), shown in the original 4:3 with bars; the game has no widescreen mode, its backgrounds are pre-rendered 640x480 images
- Precise depth for models: no flickering where layers of a character overlap (`docs/depth-precision.md`)
- Stable point-sampled backgrounds when upscaled (`docs/point-sampling-nudge.md`)
- Xbox, PlayStation and Switch controllers with hot plugging and rumble

## Building from source

There are two builds:

| Build | Needs the game | What it makes |
| --- | --- | --- |
| Development (`build.bat`) | yes, in `assets/` | the full game exe, for working on the port |
| Release (`-DXBE_RELEASE=ON`) | no | the launcher package the CI publishes |

Clone the repository; the SDK is included in `sdk/`.

### Development build

1. Put your game in `assets/`: `default.xbe` with the game's folders beside it (extract the disc image).
2. Install Visual Studio 2022 with **Desktop development with C++**, **C++ Clang tools for Windows** and **C++ CMake tools for Windows**.
3. Run `build.bat`. The exe is written to `out\build\release`.

### Release build

Uses [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) `20260922` (ucrt, x86_64), the toolchain the release bundles:

```
cmake -S . -B out/build/pkg -G Ninja -DCMAKE_BUILD_TYPE=Release -DXBE_RELEASE=ON ^
      -DCMAKE_TOOLCHAIN_FILE=cmake/llvm-mingw.cmake -DLLVM_MINGW=<path to llvm-mingw> ^
      "-DCMAKE_C_FLAGS=-march=x86-64-v2" "-DCMAKE_CXX_FLAGS=-march=x86-64-v2"
cmake --build out/build/pkg --target package_GenmaOnimusha
```

The package is laid out in `out/build/pkg/release/GenmaOnimusha`. `.github/workflows/build.yml` does the same on every push, and publishes a release for every `v*` tag.

## Layout

| Path | What |
| --- | --- |
| `sdk/` | Xbox XBE Recomp with this port's changes (`sdk/UPSTREAM.md`): recompiler, runtime, release launcher |
| `config/game.toml` | the recompiler's input for this game: addresses and XDK symbols, no game bytes |
| `src/` | game-specific host code and hooks |
| `cmake/llvm-mingw.cmake` | the release toolchain |
| `docs/` | investigation notes (Portuguese) |
| `tools/` | development scripts (captures, frame shots, disassembly helpers) |

## Credits

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBERecomp) by TGP482, the recompiler and runtime this port is built on.
- [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw), the compiler the release bundles.
- Third-party licenses are in `sdk/THIRD_PARTY.txt` of a release.

Genma Onimusha is a trademark of Capcom. This project is not affiliated with or endorsed by Capcom or Microsoft.
