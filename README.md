# Genma Onimusha - PC port (Xbox static recompilation)

An unofficial native Windows port of the original Xbox version of **Genma Onimusha**, made by statically recompiling the game's executable to C++ with [Xbox XBE Recomp](https://github.com/TGP482/XboxXBERecomp).

**Latest release: [v0.3.0](../../releases/tag/v0.3.0)** · [all releases](../../releases)

> [!IMPORTANT]
> This repository and its releases contain **no game code and no game assets**. You need your own legally acquired copy of the game. The release builds the game's code on your PC, from your copy, on the first start.

## Playing

1. Download the latest `GenmaOnimusha-*-win64.zip` from [Releases](../../releases) and extract it to a folder you can write to.
2. Run `GenmaOnimusha.exe`.
3. On the first start it asks for your copy of the game: pick the disc image (`.iso`, a full redump image or an XISO), or `default.xbe` in a folder with the game's files. You can also put the `.iso` in the `assets` folder beforehand.
4. It then recompiles and compiles the game for your PC with the bundled compiler, behind a progress window (about a minute on an 8-thread CPU, longer on slower ones), and starts it. This happens once; later starts go straight to the game. The disc image is read where it is, never copied.

The release is made for **Genma Onimusha (USA)**, `default.xbe` SHA-1 `08bfe432eb55877b5af2dd401b713743c5b751cd` (redump). Another release of the game gets a warning before it is built.

### Keys

| Key | What |
| --- | --- |
| **F1** | settings menu (mouse or keyboard) |
| **Esc** | quit, after asking: Enter quits, Esc goes back |

The game itself is played with a controller: Xbox, PlayStation and Switch controllers work, with hot plugging and rumble.

### Settings (F1)

Changes apply at once; **Save** writes them to `game/settings.toml`.

| Setting | Choices |
| --- | --- |
| Mode | Fullscreen, Borderless, Windowed |
| Resolution | Desktop, or any mode of the monitor |
| Picture | **Fill screen** (default): the black frame the game drew for TV overscan is cut, so the picture reaches the top and bottom of the screen. **Original**: the console's whole frame, Capcom's border kept. **Custom**: 0-10% cut per edge |
| Aspect | **4:3** (default), with bars at the sides. **Stretch** fills the screen out of proportion. The game has no widescreen mode, so **16:9** and **Auto** stretch it too |
| Render scale | 25-200% of the output's height; above 100% is supersampling |
| Anisotropic filtering | as the game asks, 2x-16x |
| FPS cap, V-Sync | 30-240 or unlimited (60 by default), V-Sync on or off |
| Port fixes | Precise depth and Stable point sampling, on by default (see below); off shows the original rendering |
| HD textures | on by default: uses the textures in `game/textures/` (see below); off shows the original ones |

Logs: the first start writes `work/build.log`, the game writes `game/logs/`.

### HD textures (optional)

A separate download: [GenmaOnimusha-HD-textures.zip](https://drive.google.com/file/d/1oE0Cfq00ih_nQ7XcCPSXXHFNbc89uzmk/view?usp=sharing) (about 9.9 GB). It is a conversion of the "4K Texture Pack AI-Enhanced" (Nexus Mods) for Onimusha: Warlords by **Evgeshajk**, matched to the Xbox textures: about 1,180 of the 1,284 backgrounds, plus the animations.

1. Start the game once, so the `game` folder exists.
2. Extract the zip into the `game` folder: the files end up in `game/textures/`.
3. Start the game again. **F1 → HD textures** turns them on and off.

They need v0.3.0 or later. The textures are not part of this repository or its releases; `docs/texture-packs.md` explains how they were made.

### Requirements

- Windows 10 or 11, 64-bit
- A CPU with SSE4.2 (x86-64-v2: Intel from 2008, AMD from 2011)
- A GPU with Direct3D 12
- About 500 MB of free disk space (the release, the toolchain and the built game)

## Improvements over the original

- Rendering at the output's resolution (or above, as supersampling), shown in the original 4:3 with bars; the game has no widescreen mode, its backgrounds are pre-rendered 640x480 images
- The black frame the game leaves for TV overscan is cut by default (3.3% per edge), so the picture reaches the top and bottom of the screen; **Picture: Original** keeps it
- Precise depth for models: no flickering where layers of a character overlap (`docs/depth-precision.md`)
- Stable point-sampled backgrounds when upscaled: no shimmering columns in animated backgrounds (`docs/point-sampling-nudge.md`)
- Xbox, PlayStation and Switch controllers with hot plugging and rumble

## Changes

| Version | What |
| --- | --- |
| [v0.3.0](../../releases/tag/v0.3.0) | HD texture replacement (DDS, per palette), with the optional HD texture download; an HD font for menus and text; **F1 → HD textures** |
| [v0.2.1](../../releases/tag/v0.2.1) | fixes the crash when entering room 100 |
| [v0.2.0](../../releases/tag/v0.2.0) | F1 settings menu; 4:3 with bars by default (it was stretched on wide screens); overscan frame cut with the Original option; Esc asks before quitting |
| [v0.1.1](../../releases/tag/v0.1.1) | fixes the crash when entering the cave |
| [v0.1.0](../../releases/tag/v0.1.0) | first release: the launcher builds the game from your own disc image |

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
3. Run `build.bat`. The exe is written to `out\build\release`, with `GenmaOnimusha.pdb` beside it for a debugger.

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
| `.github/workflows/build.yml` | CI: builds and packages the release on every push, publishes `v*` tags |
| `docs/` | investigation notes (Portuguese) |
| `tools/` | development scripts (captures, frame shots, disassembly helpers) |

## Credits

- [Xbox XBE Recomp](https://github.com/TGP482/XboxXBERecomp) by TGP482, the recompiler and runtime this port is built on.
- Evgeshajk, for the Onimusha: Warlords 4K texture pack the HD textures are converted from.
- [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw), the compiler the release bundles.
- Third-party licenses are in `sdk/THIRD_PARTY.txt` of a release.

Genma Onimusha is a trademark of Capcom. This project is not affiliated with or endorsed by Capcom or Microsoft.
