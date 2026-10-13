# Desktop shell (SDL3)

## Scope and status

The personal desktop application is an authored C++20 shell on SDL 3.4.18
(ADR-074), replacing the former engine integration. It runs the prepared
native game archive and the Stage 2 software renderer and presents the original
image. Stage 3 (desktop integration) and Stage 4 (desktop controls) acceptance
were recorded on the former engine build; the SDL3 shell reproduces their gates as
listed below. Earlier receipts remain historical evidence for the build they
name and are not reused for the shell.

| Profile | Build host / method | Status |
|---|---|---|
| macOS arm64 | native, Homebrew LLVM (or `LLVM_BIN`) | Gates below pass |
| Windows x86_64 | mingw-w64 GCC, static: cross on macOS (Homebrew) and Linux (apt), native in an MSYS2 MINGW64 shell | Builds from macOS; menu, audio under Wine 11.17; Windows-host build and user acceptance D-054/D-057 |
| Linux x86_64 | host compiler on a Linux x86_64 machine, otherwise a local Docker image (`tools/desktop/linux-cross.Dockerfile`; `DOCKER=1` forces it); tar.gz and AppImage (ADR-081) | Docker build from macOS builds; host-compiler build D-057; user acceptance D-054 |

`python tools/sc64.py build` picks the row of the machine it runs on. A macOS app
bundle can only be built on macOS; Linux arm64 and Intel macOS have no native
profile (`build-linux` / `build-windows` still work from them). A Linux package
built with the host compiler links the glibc of the build machine, so it needs
that glibc or newer on the target; the Docker image pins Debian trixie (glibc
2.38) as a reproducible baseline.

Android uses the same shell; see [android.md](android.md).

## Implemented boundaries

| Module | Responsibility |
|---|---|
| `native/shell/session.*` | SHA-1 import, private Flash storage, native guest boot, input callbacks, strict microcode identity, owned task snapshots |
| `native/shell/clock_bridge.hpp`, `tools/desktop/patch_shell_runtime.py` | Separate prepared runtime copy (`N64ModernRuntime-shell`); cooperative guest/VI/timer gates and virtual clock |
| `native/shell/audio.*`, `pcm_*.hpp` | Independent miniaudio device, timed two-entry guest AI FIFO, bounded PCM ring and stereo resampling |
| `native/shell/native.*` | Presentation selection (VI/CPU-swap scanout), lifecycle pause causes, cursor/rumble policy, GPU probe and frame traces, telemetry |
| `native/shell/scripts.*` | Controller merge, ROM startup, input recipes, overlay model (formerly `host.script`, `touch.gui_script`, `status.gui_script`) |
| `native/shell/input/` | SDL-free gesture, menu, HUD, minimap, corner-button, touch-pad and keyboard/gamepad adapters |
| `native/shell/video.*` | `SDL_Renderer` texture upload, Fit viewport, overlay boxes and DejaVu Sans Mono text, readback |
| `native/shell/app.cpp` | `SDL_MAIN_USE_CALLBACKS` entry, event translation, frame order, shutdown |
| `native/shell/platform_sdl.cpp` | File dialog and owned ROM import copy, storage path, cursor hiding |
| `tools/desktop` | Pinned public dependency bootstrap, macOS/cross builds, launch receipts, GPU and lifecycle checks |

The parser executes on an owned canonical 8 MiB submission snapshot. Guest and
runtime threads make no SDL calls. The three-slot mailbox retains shared
immutable packets; only superseded presentation is discarded. Every task's RDP
stores complete on the submitting guest thread before SP/DP completion
(ADR-035); presentation eviction never discards those memory effects.

Each main-loop iteration runs in a fixed order: `Native::tick` (select the
scanout image, decode cursor/UI feedback, upload when changed), host merge and
`set_pad`, overlay update, status text, draw, optional readback, present with
vsync. VI fields follow the guest clock independently of this loop. An iteration gap over
200 ms (12 fields, the UI feedback freshness limit) is logged, at most once a
second, as `WARNING:SC64: SC64 loop gap N ms (tick, scripts, draw, present,
outside) [occluded,unfocused]`; minimized or hidden windows and lifecycle
transitions (including Android background) are not reported (D-056).

The backend uploads the software-rasterized RGBA image to one streaming texture
with nearest sampling and draws it into the largest aspect-preserving Fit
rectangle (floored size, centred) over a black clear. The same rectangle drives
pointer inversion. Presented rows/columns follow the VI X/Y scale (ADR-068); CPU
framebuffers swapped without an RDP task are presented while newer than the
matching task image. Exact VI filtering/field scanout remains D-029/D-037/D-051.
The overlay (diagnostic pad, hint outline, corner buttons, fallback panel,
status) is drawn after the image in the 960x720 design space, stretched over the
window; text scales uniformly.

## Prepare and build locally

Prerequisites: accepted Stages 1/2 private inputs; CMake, Ninja; Python 3.10+ with
NumPy and Pillow; a compiler for your host (macOS: Homebrew LLVM, Linux: g++ or
clang++ plus the SDL development packages listed in `linux-cross.Dockerfile`,
Windows: MSYS2 MINGW64 GCC). Cross targets additionally need mingw-w64 (Windows
package from macOS/Linux) or Docker (Linux package from anywhere but a Linux
x86_64 machine). `python tools/sc64.py doctor` checks all of this and prints the
install command for what is missing. These commands do not upload generated game
code.

```sh
make bootstrap        # SDL3, stb_truetype, DejaVu fonts, miniaudio (pinned, hashed)
make build            # the package for the OS you run on: StarCraft64.app / -linux / -windows
make build-macos      # the macOS arm64 app by name (fails on other hosts)
make build-windows    # local/dist/StarCraft64-windows.zip
make build-linux      # local/dist/StarCraft64-linux.tar.gz
make build-appimage   # local/dist/StarCraft64-x86_64.AppImage (runs build-linux first)
make test
```

Each `make` target forwards to `python tools/sc64.py <command>` (same command
names, e.g. `build-linux --docker`); use that form where `make` is unavailable.

Bootstrap downloads only the pinned public archives, checks their hashes,
refuses conflicting existing files, and extracts regular files only into ignored
`local/toolchains`. `build.py` and `cross_build.py` construct the guarded runtime
copy `local/deps/N64ModernRuntime-shell`, validate the generated observers and
link statically against the pinned SDL source. The Stage 1 capture host keeps its
own runtime copy and is excluded from shell builds.

Outputs contain the executable, the DejaVu font and its license — **no ROM file**.
`make icon ROM=/path/game.z64` first converts the original title art into
`local/icons/` (ADR-076); builds then add the icon (macOS `.icns`, Windows
resource, Linux `StarCraft64.png` beside the binary) and skip it when absent. The
`.icns` is made with `iconutil`, so it exists only when icons are generated on macOS.
New bundles/packages are staged completely before publication; a running
executable is not truncated. Receipts: `local/reports/desktop/build.json`
(schema 2), `local/reports/{windows,linux}/build.json` (schema 1).

### Linux AppImage

`make build-appimage` wraps the audited Linux package (checked against
`local/reports/linux/build.json`) into a single `local/dist/StarCraft64-x86_64.AppImage`
and writes `local/reports/appimage/<name>.json` (schema 1). `tools/desktop/appimage.py`
lays out AppDir (`AppRun`, `.desktop`, icon, `usr/bin/{StarCraft64,font,license}`),
builds the zstd squashfs with `mksquashfs` in the local cross image and joins it with
the pinned type2 runtime (`make bootstrap`). It then unpacks the finished file and
audits it again. Nothing is built or uploaded remotely. The executable keeps SDL
statically linked and opens X11/Wayland/audio libraries at run time, so the host needs
only glibc (see ADR-081 for the measured minimum) and FUSE (or run with
`--appimage-extract-and-run`). The ROM is not included; saves go to
`~/.local/share/StarCraft64` as for the tar.gz.

`make build-appimage BUNDLE_ROM=1 ROM="/path/your.z64"` packs the normalized,
SHA-1-verified ROM as `usr/bin/sc64-ntsc-u.z64` and writes
`StarCraft64-x86_64-bundled-rom.AppImage`, a different file name so the personal
artifact cannot be mistaken for the ROM-free one. `AppRun` then starts the app with
`--config=sc64.bundled_rom=1`; the shell reads the ROM from the read-only image into
owned memory and creates no second copy. Such an image is strictly personal; never
share or upload it. The embedded runtime statically links libfuse (LGPL), which
matters only if an image were ever distributed.

## Launch and controls

```sh
python3 tools/desktop/run.py --rom "/absolute/path/to/your.z64" --output local/reports/desktop/play-01
```

Use a fresh output name. The launcher keeps ROM/save storage at
`local/private/desktop/default`, records binary/resource/input hashes, the
engine log, wall duration and exit status (run receipt schema 3). Without a ROM,
click/tap or press O to open the system file chooser; the chosen file is copied
into storage, validated and persisted. A directly launched app stores data in
`SDL_GetPrefPath` (`~/Library/Application Support/StarCraft64`,
`%APPDATA%\StarCraft64`, `~/.local/share/StarCraft64`). The importer accepts
z64/v64/n64 byte order, normalizes and checks SHA-1
`a36d91b5aac7bb95df0d7de5f9814f5270e25c2b` before starting guest threads.

| Desktop action | N64 / host action |
|---|---|
| X / C / Enter / Z | A / B / Start / Z |
| Q / E | L / R |
| I / K / J / L | C-up / down / left / right |
| Arrow keys | D-pad |
| W / S / A / D | Analog stick |
| Esc | B, or Start in a mission (pause menu) |
| Left / right / middle mouse | Pointer gestures / B order at point / camera pan |
| Tab | Fallback controller panel |
| P | User pause/resume |
| O before game start | System ROM chooser |
| F3 / F4 / F6 | Diagnostic touch pad / edit layout / player |

Gamepads (SDL gamepad mapping) feed players 1/2 in connection order; the
keyboard feeds player 1. South/East/Start/shoulders/D-pad, left stick, right
stick (C buttons) and left trigger (Z) follow the former bindings. A short press
is latched for six VI fields. One game session is allowed per process.

## Time, audio and shutdown

The VI thread schedules nominal 60 fields/s independently of presentation. The
virtual clock subtracts paused wall time; guest scheduler/poll safe points, VI
and timer workers wait on the pause gate. A late VI deadline is cast to a signed
duration. Focus loss pauses only a mission; minimizing and user pause are
independent causes (ADR-041). Shutdown opens all pause gates before quit/join,
then destroys SDL objects on the main thread.

Game PCM uses miniaudio, not SDL audio: 48 kHz float stereo, linear resampling
with bounded +/-0.5% correction, a preallocated ring and try-lock callback.
`audio_underruns` counts host queue starvation or lock contention, not device
underruns. Device-loss/reopen needs separate tests (D-036).

## GPU regression, lifecycle and acceptance checklist

```sh
python3 tools/desktop/run.py --gpu-probe local/inputs/renderer/manifest.json --seconds 8 \
  --storage local/private/pixel-fixtures --output local/reports/desktop/gpu-new
python3 tools/desktop/check_gpu.py local/reports/desktop/gpu-new/gpu
python3 tools/desktop/run.py --rom /absolute/path/to/your.z64 --seconds 125 --pause-at 80 --resume-at 95 \
  --output local/reports/desktop/resume-new
python3 tools/desktop/check_live.py local/reports/desktop/resume-new --require-resume
```

The probe hashes each RAM snapshot natively, holds each image for six presents
and reads the renderer output back (top-down BGRA) before presentation. The
checker verifies provenance, exact native RGB against the accepted Stage 2 PNG
and every nearest-filter pixel of the Fit viewport and black bars. For a paused
shutdown omit `--resume-at` and pass `--require-paused-shutdown`.

SDL3 shell evidence (2026-10-11, `local/reports/sdl3-migration/`):

- [x] macOS Metal GPU probe `gpu-01`: all 20 cases accepted; 16 exact, four
  2P frames differ only at exact texel boundaries (same as the former engine baseline).
- [x] Live 55-second run `live-01`: menu reached, ~59.7 fields/s, zero
  software PCM underruns, unchanged binary/resources, exit 0.
- [x] Lifecycle `resume-01` and `paused-stop-01`: `lifecycle_accepted=true`.
- [x] Recipe gates `ui-01` (`check_ui`), `hud-01` (`check_hud`,
  `check_gestures`) pass. `check_campaign` gives identical gates on the former engine
  baseline and the shell from the same storage copy; the recalibrated
  `check_hover` (schema 2, ADR-066 cap) and `check_hud`/`check_gestures` pass on
  `hud-03` (D-055). One repeat run, `hud-02`, diverged in the menu after a main-loop stall (D-056).
- [x] Windows build `wine-07` under Wine 11.17: menu, WASAPI audio, Direct3D.
- [ ] Native Windows/Linux runs and physical mouse/gamepad on the shell (D-054).

Do not promote the open checks based only on a build or unit tests.
