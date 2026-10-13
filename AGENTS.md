# Repository guidance

## Purpose and current scope

This is a personal-use native port of StarCraft 64 (NTSC-U): N64Recomp-generated
C, librecomp/ultramodern, a custom HLE renderer, and an authored SDL3 shell for
presentation/input (ADR-074). Authored native code is
C++20; generated game code is C17; tools are Python 3.10+. RT64 is excluded from
the shipped runtime.

Read [project status](docs/project-status.md), [the development guide](docs/development.md)
and [the implementation plan](docs/implementation-plan.md) before changing scope. Stages 0–4 have documented macOS desktop acceptance;
Stage 5 closes the user-scoped personal Android profile on 2026-10-09 (ADR-055).
Read [its acceptance checklist](docs/stage5-acceptance.md): expanded device checks
and long Adreno/Mali performance/thermal runs are explicitly deferred. Stage 6
was withdrawn at the user's request on 2026-10-09 (ADR-060). All HD/filter/scale
choices and settings persistence are removed. Presentation always uses the
original raster and aspect-preserving Fit, with one viewport for rendering/input.
No widescreen guest patch is enabled or requested. The SDL3 shell (ADR-074)
also cross-builds Windows/Linux x86_64; their user acceptance is D-054. Check current documents
rather than carrying forward an old receipt as proof for a new build or platform.

User-facing discussion is normally Russian. All project documentation is English.

## Private data boundary

- Never track or upload ROMs, extracted assets, assembly/microcode dumps,
  generated game/RSP C, game archives, captures, saves, signing keys or personal
  bundles/APKs. Keep them under ignored `local/`; preserve existing private data.
- `recipe/ntsc-u/` is the one tracked ROM-derived exception (ADR-084): symbol names,
  addresses, sizes, hook text and SHA-256 hashes, no game bytes or generated code.
  `make recipe-export` refreshes it and `tools/recompile/recipe.py check` rejects
  anything outside its whitelist; never add other analysis output to it.
- Only reviewed authored source, documentation, metadata and synthetic fixtures
  belong in Git. Ignore rules do not protect already tracked or force-added files.
  Inspect the diff and stage explicit paths; do not use `git add -A` or `git add -f`
  for private outputs.
- `make icon` converts one cropped title frame into `local/icons/`; builds embed it
  in personal artifacts only (ADR-076). Never commit or publish those files.
- Normalize ROM byte order and validate NTSC-U SHA-1
  `a36d91b5aac7bb95df0d7de5f9814f5270e25c2b` before guest startup.
- Build locally (a local Docker container counts as local). Do not submit
  game-derived code/libraries to cloud CI or any remote build service. Public
  dependency downloads are separate from private build inputs.
- The default APK excludes the ROM. `BUNDLE_ROM=1` is an authorized opt-in for
  personal use in the same APK/application ID. Read its verified asset into owned
  memory without creating another on-disk ROM copy; saves remain private.

## Where to work

- `native/gfx_core/`: platform-independent GBI/S2DEX2 interpretation, TMEM and
  software rasterization; owned DrawList contract in `gfx.hpp`.
- `native/runtime/`: Flash device, VI/FPU helpers and guarded read-only UI feedback.
- `native/host/`: standalone native capture host and file PCM output.
- `native/shell/`: SDL3 application (`app.cpp`), session, lifecycle, audio, mailbox,
  ROM import, presentation selection (`native.cpp`), video/overlay, host scripts.
- `native/shell/launcher_screen.*`: the start screen (icon, title, Select ROM / Select game module buttons; ADR-091).
- `native/shell/input/`: SDL-free gesture/menu/HUD/minimap/touch-pad adapters.
- `tools/desktop/package.py`, `notices.py`: release launcher packages and their license texts
  (ADR-088). Launchers never contain the ROM, generated code, the game module or the game icon.
- `native/setup/`: `sc64_setup` and its library, ROM to game module with the prebuilt N64Recomp and a
  probed compiler or pinned zig (ADR-087); the shell builds the module on first run through it.
  `--target android-arm64` cross-builds it for a phone with the NDK (ADR-089).
- `native/module/`: the game-module ABI (`sc64_module.h`, `sc64_imports.def`), `module_main.c`
  and the shim `librecomp/sections.h`; the generated C is built into `sc64-game.*` (ADR-086).
- `native/android/`: NDK profile, manifest, `Sc64Activity` and haptics JNI helper.
  `make build-android` links the game statically (default); `make build-android-launcher` builds the APK
  without game code that loads the module of `sc64_setup --target android-arm64` (ADR-090).
- `cmake/`: shared shell build (`shell.cmake`) and cross toolchain files.
- `tools/sc64.py` (cross-platform command entry point, ADR-082) and `tools/hostenv.py`
  (host detection and tool discovery); `tools/{research,recompile,renderer,desktop,input,android}/`:
  authored analysis, dependency patches, builds and evidence checks. `tools/research_libultra/`
  maintains the reference inventory; `tools/rom_info.py` analyzes ROM metadata.
- `tests/`: synthetic Python regressions and `tests/native/` C++ fixtures.
- `local/`: ignored dependencies, generated code, build trees, reports and storage.
  See [repository-layout.md](docs/repository-layout.md) for actual versus proposed
  modules; do not create the proposed tree merely to match the design.

## Architecture invariants

- Only the shell main thread may call SDL video/render APIs. No direct
  OpenGL/Vulkan/Metal/Direct3D calls in the port. `gfx_core` and
  `native/shell/input` include no SDL headers.
- The current backend rasterizes guest tasks in software and uploads a native
  RGBA image through `SDL_Renderer`. Do not describe it as a GPU combiner implementation.
- Presentation uses the completed original image with nearest sampling and a
  fixed Fit viewport shared by rendering/input. Keep removed HD/filter/scale
  choices out of the runtime. Fit is not a widescreen guest view; new typography
  or camera patches require an explicit request and original behavior evidence.
- Snapshot required guest bytes coherently before scheduler checkpoints. Parse
  and rasterize owned data; never retain mutable guest pointers in published frames.
  Preserve cooperative audio-service checkpoints during expensive graphics work.
- Process every task's guest-visible RDP memory effects before completion.
  Only complete disposable presentation packets may be superseded; mailbox
  eviction must not discard guest RAM writes or persistent state dependencies.
- Emulate 8 MiB RDRAM and use verified endian-aware access. Preserve original
  fixed-point texture/rectangle arithmetic, command ordering and checked bounds.
  Unknown used opcodes must fail explicitly rather than silently render nothing.
- VI fields follow the guest clock independently of shell presentation. Do not
  equate VI fields, graphics tasks, displayed frames and audio-device callbacks.
- Mouse/touch normally translates to original controller events using fresh,
  owned UI/cursor feedback. Keep menu/HUD contact ownership and stale-context
  cancellation. New guest writes or gameplay patches require original evidence
  and address/instruction guards, not guessed structures.
- Keep lifecycle causes distinct: focus loss pauses missions, iconification pauses
  the app, and manual pause remains independent. Background input probes are
  diagnostics, not lifecycle acceptance. Pause/resume/shutdown must settle queues,
  clear transient input, rebase clocks and join producers safely.

## Verify APIs and game facts

SDL is pinned to the **3.4.18** source release (SHA-256 in `tools/desktop/bootstrap.py`
and [toolchain-sources.md](docs/toolchain-sources.md)). Inspect its headers and
sources under `local/toolchains/sdl3/SDL3-3.4.18/` before using video, render,
event, dialog, IO or Android APIs; per-platform behaviour (for example Android
lifecycle or renderer backends) requires its own check. Inspect prepared N64ModernRuntime
and N64Recomp sources under `local/` for runtime signatures and TOML syntax;
do not invent APIs or update dependency pins implicitly.

Record new game facts in [research.md](docs/research.md) with the verification
method, scene/revision and evidence scope. Mark unverified interpretations as
**HYPOTHESIS** and propose a discriminating experiment. The measured game uses
S2DEX2, CPU CI8 rendering and CPU PCM; initial F3D/audio-RSP guesses are not
implementation requirements. Check the ledger for precise boundaries.

Add architecture decisions to [decisions.md](docs/decisions.md): context →
decision → consequences. Record stubs and unresolved work in
[debt.md](docs/debt.md); every TODO must reference a debt entry. Make dependency
changes through reproducible authored patch tools, preserving guarded separate
host/shell runtime copies instead of relying on unexplained local edits.

## Build and verification

Run from the repository root. The Make targets forward to `python tools/sc64.py
<command>`, which also runs on Linux and Windows (`doctor` lists missing tools).
Builds need prepared private game/runtime inputs. `build` makes the package of
the host: the macOS arm64 app, a Linux x86_64 package (host compiler; Docker
elsewhere or with `DOCKER=1`) or, from an MSYS2 MINGW64 shell, the Windows x86_64
package. Windows is also cross-built with mingw-w64 from macOS/Linux; Android
delivery is arm64-v8a, API 28+. Only macOS arm64 is verified as a build host
(D-057).

```sh
make help
make build                           # macOS app: local/dist/StarCraft64.app
make run ROM="/path/to/your.z64"      # runs an existing build
make test-python                     # synthetic Python suite
make test-native                     # private desktop configure/build + CTest
make test                            # both suites
make package                         # release launcher without game code: local/dist/launcher/
make build-windows                   # local/dist/StarCraft64-windows.zip
make build-linux                     # local/dist/StarCraft64-linux.tar.gz
make build-appimage                  # local/dist/StarCraft64-x86_64.AppImage
make build-android                   # local/dist/StarCraft64-android.apk
make check-apk
make devices
make install-android SERIAL=DEVICE_SERIAL
make run-android SERIAL=DEVICE_SERIAL
```

The Linux AppImage takes `BUNDLE_ROM=1 ROM=...` the same way and then writes the
distinct `StarCraft64-x86_64-bundled-rom.AppImage` (personal use, ADR-081).
For personal ROM bundling, use
`make build-android BUNDLE_ROM=1 ROM="/path/to/your.z64"`. Also pass
`BUNDLE_ROM=1` to APK audit/install/smoke targets for that artifact. Select an
explicit adb serial; do not assume the first device is the intended target.
`make smoke-android` restarts the app and performs a Home/return probe;
`make audio-android` samples an already running app. See
[android.md](docs/android.md) for prerequisites and interpretation.

For native renderer/bridge/input fixtures without private generated game code:

```sh
cmake -S . -B local/build/renderer -G Ninja \
  -DSC64_BUILD_HOST=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build local/build/renderer -j8
ctest --test-dir local/build/renderer --output-on-failure
```

Choose checks appropriate to the change. Logic fixes need a regression that
reproduces the defect; graphics changes also need the fenced original-frame
gate (RGB PSNR ≥ 40 dB per frame) and visual inspection. Timing comparisons use
`make benchmark-renderer BASELINE_REPLAY=/path/to/preserved/binary`; offline
timings do not certify phone cadence or thermal behavior. See
[renderer.md](docs/renderer.md), [desktop.md](docs/desktop.md),
[input.md](docs/input.md) and [performance.md](docs/performance.md).

Use fresh ignored output paths for evidence; existing receipts bind artifact
hashes and must not be overwritten or hand-edited. Distinguish source analysis,
synthetic fixtures, successful builds, native input recipes, OS event delivery
and user/device acceptance. A sleeping device or recipe bypassing physical touch
cannot certify touch controls. A 16 KiB-aligned APK on a 4 KiB device does not
certify a 16 KiB runtime. Preserve saves and signing identity during rebuilds.

Before each stage, state the plan, risks and unresolved user inputs. After it,
document build/run commands, completed checks, acceptance checklist and remaining
limits. Do not cross a stage gate without its required evidence and accepted
scope. For documentation-only edits, verify paths/commands and `git diff --check`;
a game rebuild is unnecessary.
