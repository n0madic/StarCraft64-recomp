# Repository layout

Tools and working directories use purpose-based names: `research`, `recompile`,
`renderer`, `desktop`, `input` and `android`. Build output lives under
`local/build/host`, `local/build/renderer`, `local/build/desktop` and
`local/build/android`, `local/build/windows`, `local/build/linux` and (host-compiler
Linux package) `local/build/linux-native`; the SDL3
shell uses the guarded runtime copy `local/deps/N64ModernRuntime-shell`. Reports, captures and private storage use
the same purpose names. Documentation and test filenames follow these names.

The 2026-10-08 local migration preserves saves, the Android signing key and
recorded evidence. `local/reports/path-renaming.json` records artifact moves.
Historical receipts retain the paths and hashes captured during their runs;
new builds produce receipts at the current locations. Previous CMake caches
are retained under `local/archive/build` and fresh build trees use the new paths.

## Present files

Stage 0 creates only authored research tools, synthetic tests, documentation,
and ignore rules. The supplied ROM remains in its original location, ignored.
`local/reports/rom-info.json` is the private generated analyzer report.
The Stage 0 work did not include a placeholder application or game library.

Stage 1 adds root `CMakeLists.txt`, `native/host` (capture, input and PCM sink),
`native/runtime` (Flash device and VI arithmetic), `tools/recompile` (section-qualified
symbols, private patch preparation, transactional generation and capture checks),
and native/synthetic regression tests. The game archive and dependency copies
exist only below ignored `local/`; no generated game C is authored source.

Stage 2 adds `native/gfx_core` (C++20 parser, TMEM and software rasterizer),
`tools/renderer` (fenced references, replay, metrics and gate),
`tests/native/test_gfx_core.cpp` and `docs/renderer.md`.
All derived RGBA images, atlases and input/post-task RAM remain under `local/`.

Stage 3 added the session/clock/PCM bridge with engine presentation and a macOS
picker, a Lua application with authored shaders, `tools/desktop`
(public dependency pins, guarded runtime copy, local SDK relink, run/GPU checks)
and desktop bridge/input tests. The current verified link profile is macOS arm64.
The personal bundle and all GPU readbacks remain ignored under `local/`.

Stage 4 added the original-controller gesture/menu/HUD adapters (then Lua),
read-only native UI feedback and guarded private recompiler observer tooling.
Stage 5 adds `native/android` (NDK link profile and Android SAF/lifecycle helper),
shared platform/FPU helpers, `tools/android` (local APK build, alignment/payload
audit and bounded device smoke), and synthetic packaging regressions. The root
`Makefile` wraps the verified build/run/test scripts. Current build hosts remain
macOS arm64; Android arm64 is a delivery target. All APKs, signing keys, personal
ROMs, generated code and emulator/device evidence remain below ignored `local/`.

## Current tree after ADR-074

ADR-074 replaced the former engine with an authored SDL3 shell: the bridge became
`native/shell`; the Lua scripts were removed and ported to C++
(`native/shell/input`, `native/shell/scripts.*`); `cmake/` holds the shared shell
build and cross toolchains. The former engine-centric proposal (extension
manifest, engine GPU backend, per-platform prebuilt `lib/` folders) is obsolete.

```
native/
  gfx_core/        # S2DEX2/TMEM/software rasterizer; no platform API
  runtime/         # Flash, VI/FPU helpers, UI observers, SHA-1/SHA-256
  host/            # Stage 1 capture host (own runtime copy)
  shell/           # SDL3 app, session, audio, presentation, scripts, platform
    input/         # SDL-free pointer/controller adapters
  module/          # game-module ABI, trampolines and shim headers (ADR-086)
  setup/           # sc64_setup: ROM -> game module (ADR-087)
  android/         # NDK profile, manifest, platform JNI, Java activity/haptics
recipe/ntsc-u/     # committed recompilation recipe: symbols, config template, generated-file hashes (ADR-084)
cmake/             # shell.cmake, game_module.cmake, setup.cmake, toolchains/{mingw-w64,linux}-x86_64.cmake
tools/             # sc64.py (command entry point), hostenv.py (host/tool discovery), research, recompile, renderer, desktop, input, android
tests/             # Python tool tests; native/ C++ fixtures
local/             # ignored: toolchains, deps, generated, build, dist, reports, private
third_party/       # ignored pinned source checkouts
```

## Module boundaries

`gfx_core` accepts canonical guest-memory snapshots and tasks and emits owned
DrawLists. It includes no SDL headers. `native/shell` converts completed images
on the main thread and is the only code calling SDL video/render. `runtime` owns
guest execution, virtual devices, task completion, timers, and canonical RDRAM
access. `native/shell/input` and the scripts own pointer/controller state, not
gameplay implementation.

Publishable source is an explicit set of reviewed files. Generated function
TOML may include more than names in some workflows; inspect it before promotion.
Keep raw assembly, bootcode, microcode bytes, dumps, reference images, and
derivative archives under `local/`, regardless of their extensions.
