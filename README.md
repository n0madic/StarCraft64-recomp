# StarCraft 64 native port

A native desktop and Android port of **StarCraft 64** (Nintendo 64, NTSC-U),
playable with a mouse, keyboard, gamepad or touch screen.

The game's original MIPS code is translated ahead of time to C with
[N64Recomp](https://github.com/N64Recomp/N64Recomp) and runs on the
[N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime). It is not an
emulator: graphics are produced by a purpose-built software renderer for this
game's display lists, and an [SDL3](https://libsdl.org) shell provides the window,
audio and input.

> **You need your own ROM.** This repository contains no game data, and there are
> no prebuilt binaries: you build the app yourself from a dump of a cartridge you
> own. See the [legal notice](#legal-notice).

## Status

| Platform | Status |
|---|---|
| macOS (Apple Silicon) | Built and accepted by the author |
| Android (arm64-v8a, Android 9 / API 28+) | Accepted on one physical phone and on emulators; wider device and performance testing has not been done |
| Windows x86_64, Linux x86_64 (tar.gz, AppImage) | Cross-built (Windows menu and audio checked under Wine); not yet tried by a user |

What works: the single-player campaign, original menus and the Encyclopedia, save
and load, sound, mouse/touch play (the game still sees a normal N64 controller).

Not supported: two-player (split-screen) mode, other regions or ROM revisions, and
widescreen or other graphics options. The picture is always the original raster
scaled to fit the window (nearest sampling, black bars, same transform for mouse
and touch). See [project status](docs/project-status.md) for the detailed history.

## What you need

- A **StarCraft 64 (USA)** ROM dump. `z64`, `v64` and `n64` byte orders are accepted;
  the image must match SHA-1 `a36d91b5aac7bb95df0d7de5f9814f5270e25c2b` after
  normalization, otherwise the app refuses to start.
- A build machine: **macOS, Linux or Windows**, with CMake, Ninja and Python 3.10+
  (NumPy, Pillow). The desktop package for your own machine needs a C++ compiler
  (Homebrew LLVM on macOS, GCC or Clang on Linux, MSYS2 MINGW64 GCC on Windows).
  Other desktop targets need `mingw-w64` (Windows package from macOS/Linux) or
  Docker (Linux package from macOS, Windows or a non-x86_64 Linux machine), and the
  Android APK needs the Android SDK/NDK and OpenJDK 21. `python tools/sc64.py
  doctor` lists what your machine has and how to install the rest. Exact versions
  are in [desktop](docs/desktop.md) and [Android](docs/android.md).

## Build and run

Everything is built locally, and nothing derived from the game is uploaded
anywhere. Generated game code and the finished apps stay under the ignored
`local/` directory and are for your personal use only.

**1. Prepare the game code.** The ROM is recompiled to C once, privately, with one
command:

```sh
make recompile ROM="/path/to/your.z64"   # writes local/generated/game
```

It patches and builds N64Recomp, analyzes the ROM, adds the UI observers, generates
the C code and validates it, so the result is ready for `make build`. The ROM must
be in `z64` (big-endian) byte order. Before the first run, check out N64Recomp and
N64ModernRuntime at the revisions pinned in
[toolchain-sources](docs/toolchain-sources.md) into `third_party/`, create the
project `.venv` (`make venv`: the pinned spimdisasm/rabbitizer), and prepare the libultra
inventory once ([libultra inventory](docs/libultra-inventory.md)); the command
verifies the inventory but does not build it. The individual steps are described
in [the recompilation tools](tools/recompile/README.md) and the
[native runtime guide](docs/native-runtime.md).

**2. Build and start the desktop app.**

```sh
make doctor                         # which tools this machine has / lacks
make bootstrap                      # fetch pinned public dependencies (SDL3, fonts, miniaudio)
make build                          # the package for the OS you run on, see below
make build-macos                    # explicitly: the macOS arm64 app (macOS only)
make run ROM="/path/to/your.z64"
```

`make build` produces `local/dist/StarCraft64.app` on macOS,
`local/dist/StarCraft64-linux/` on Linux x86_64 and `local/dist/StarCraft64-windows/`
on Windows. Every `make` target is a wrapper over `python tools/sc64.py <command>`,
which runs unchanged on all three systems, so a Windows machine without `make` uses
the same commands (`python tools\sc64.py build`, `... run --rom C:\path\game.z64`).
On Windows run it from an MSYS2 MINGW64 shell with the MINGW64 Python.

Without `ROM=`, click the window (or press `O`) to pick your ROM; it is copied
into the app's private storage and reused on later launches.

**3. Other platforms.**

```sh
make build-windows                  # local/dist/StarCraft64-windows.zip (mingw-w64)
make build-linux                    # local/dist/StarCraft64-linux.tar.gz (DOCKER=1 forces Docker)
make build-appimage                 # local/dist/StarCraft64-x86_64.AppImage (builds Linux first)

make build-android                  # local/dist/StarCraft64-android.apk
make devices                        # find your device serial
make install-android SERIAL=DEVICE_SERIAL
make run-android SERIAL=DEVICE_SERIAL
```

The Linux AppImage needs glibc 2.38 or newer (Debian 13, Ubuntu 24.04+) and FUSE.
Add `BUNDLE_ROM=1 ROM="/path/to/your.z64"` to `make build-appimage` to pack the ROM
and start straight into the game (`StarCraft64-x86_64-bundled-rom.AppImage`); like the
bundled APK it is strictly personal.

On Android, tap the start screen to choose your ROM through the system picker.
To skip the picker, build with `BUNDLE_ROM=1 ROM="/path/to/your.z64"`; the ROM is
then packed into that APK, which makes it strictly personal and never shareable.
For an Android emulator use `make build-android-opengles` instead (the default
build uses Vulkan, which crashes on emulators). Full options and troubleshooting
are in the [development guide](docs/development.md).

## Controls

**Keyboard and gamepad (desktop)**

| Key | Action |
|---|---|
| X / C / Enter / Z | A / B / Start / Z |
| Q / E | L / R |
| I / K / J / L | C-up / down / left / right |
| Arrow keys / W A S D | D-pad / analog stick |
| Esc | B (Start inside a mission: pause menu) |
| P | Pause or resume |
| O | Choose a ROM (before the game starts) |

A gamepad (SDL mapping) controls player 1; the keyboard does too. Two-player is
not supported.

**Mouse and touch**

| Gesture | Effect |
|---|---|
| Click / tap | Select, press buttons, advance messages |
| Drag | Rectangle-select units |
| Right click / two-finger tap / double tap on the map | Order: move, attack, harvest |
| Double tap or double click an already selected unit | Select all units of that type on screen |
| Middle mouse drag / two-finger drag | Scroll the map |
| Tap the top-left corner in a mission | Pause menu |
| Tap the bottom-left corner in a mission | Show or hide the minimap |
| Tap or drag on the minimap | Move the camera |
| Long press | Open a small panel with Continue, Back and menu arrows |
| Android Back / Esc | Back |

Commands such as Build appear as the original on-screen hints; click or tap them
directly. The complete list, including the Encyclopedia gestures, is in
[the input guide](docs/input.md).

## Saves and data

The game's FlashRAM save is kept in the app's private storage and survives updates:

- macOS: `~/Library/Application Support/StarCraft64` (or `local/private/desktop/default`
  when started with `make run`)
- Windows: `%APPDATA%\StarCraft64`
- Linux: `~/.local/share/StarCraft64`
- Android: the app's private storage

Back up that folder before wiping it. The Android signing key used by
`make build-android` lives in `local/private/android`; keep it, because a build
signed with a different key cannot update an installed app.

## Documentation

- [Development guide](docs/development.md): all make targets and variables, testing,
  device workflow, private-data rules, document index.
- [Desktop](docs/desktop.md), [Android](docs/android.md), [input](docs/input.md):
  platform details and acceptance records.
- [Project status](docs/project-status.md), [implementation plan](docs/implementation-plan.md),
  [decisions](docs/decisions.md), [debt](docs/debt.md): history and open work.

## Legal notice

This is an unofficial, non-commercial fan project for personal interoperability
and research. It is not affiliated with, authorized or endorsed by Blizzard
Entertainment, Nintendo, Mass Media or any other rights holder. StarCraft is a
trademark of Blizzard Entertainment; Nintendo 64 is a trademark of Nintendo.
All other names are the property of their respective owners.

- **No game content is distributed.** This repository contains no ROM, no
  extracted assets and no code, microcode or data derived from the game. You must
  supply a cartridge image dumped from a copy you legally own; the runtime accepts
  only the NTSC-U image with the expected SHA-1.
- **Builds are personal.** Recompiled game code and any build that contains it
  (including an APK or AppImage with the ROM bundled) are for the owner's private use only and
  must not be shared or uploaded.
- **Licensing.** Original source and documentation are MIT-licensed ([LICENSE](LICENSE)).
  The licence grants no rights to the game, its ROM, assets or trademarks.
  `tools/bolt_extract.py` and `tools/research/bolt_audit.py` are ports of
  [BOLTextract](https://github.com/heinermann/BOLTextract) and are
  GPL-3.0-or-later instead ([text](https://www.gnu.org/licenses/gpl-3.0.html)).
- **Third-party components keep their own licenses.** Dependencies fetched at
  build time (N64Recomp, N64ModernRuntime, SDL3 and others) and the reference
  emulator sources used by research tools are not covered by this repository's
  terms.
- **No warranty.** The software is provided "as is", without warranty of any kind.
  You are responsible for complying with the laws that apply to you.
