# Stage 5: texture processing optimization

Current status: Stage 5 is closed for the personal Android scope (ADR-055).
[stage5-acceptance.md](stage5-acceptance.md) records the accepted evidence and
deferred device/thermal checks. The plan below retains the ADR-052 slice's
historical entry status; its timing remains a macOS offline measurement.

## Plan and entry gate (2026-10-09)

Stage 4 desktop input is accepted. Stage 5 Android acceptance remains open;
this is its next implementation slice, not entry into Stage 6. ADR-051's phone
menu audio fix is the current baseline. Its interpreter wall time reaches
60–71 ms, including cooperative service of higher-priority guest threads.

1. Profile the current desktop renderer with the fenced original scene corpus.
   Preserve a baseline executable and bind measurements to its hash and inputs.
2. Optimize only confirmed interpreter work. Keep texture keys, decoded bytes,
   ordered RDP stores and the coherent snapshot/checkpoint contract unchanged.
3. Compare repeated baseline/candidate interpretations on the same inputs;
   compare full rendered RAM, content keys and command accounting exactly.
4. Rerun the frozen original RGB gate and native/Lua/Python regressions, then
   build and run the desktop application before building Android.
5. Audit the personal bundled-ROM APK and collect bounded phone telemetry if
   the selected physical device is connected. Keep stage-wide thermal,
   alternate-device/backend and 16 KiB runtime acceptance separate.

## Risks and open questions

Hash optimization can silently change resource identity. Reuse before atlas
creation can skip collision checks or source-dependent texture loads. Longer
uninterrupted loops can reopen audio starvation. Preserve the slow independent
hash in regression fixtures, semantic payload comparison, bounded checkpoints
and input validation. No guest-memory or graphics API changes are needed.

Device availability is checked locally. No additional user input is needed for
the desktop experiment. Physical touch-only mission completion, other devices,
headphone routing and sustained thermal sessions are still separate open gates.

## Slice acceptance

- [x] Profile identifies interpreter work worth changing.
- [x] Baseline/candidate full RAM, texture keys and command accounting match on
      all 20 fenced menu/briefing/1P/2P cases.
- [x] Repeated offline menu interpretation median improves by at least 20%.
- [x] Original RGB gate remains at least 40 dB for every case.
- [x] Hash, palette/state invalidation, atlas reuse and checkpoint fixtures pass.
- [x] Desktop application starts and runs with original PCM and no fatal error.
- [x] Android APK build/private-data/signature/alignment audit passes.
- [ ] Named-device bounded menu audio/cadence regression is recorded if connected.

Timings from offline replay exclude guest execution, audio service, GPU work,
thermal conditions and input latency. They are not a claim of phone frame rate.

## Implemented result and reproduction

ADR-052 reduces the historical eight-byte FNV channel encoding to one exact
unsigned operation and reuses a semantically identical texture before decoding
its atlas again. Pixel formats, keys, collision checks, snapshots, checkpoints,
RDP stores, guest completion and graphics ownership stay the same.

The private run directory is `local/reports/texture-processing-01/`:

| Receipt | Result |
|---|---|
| `baseline-sample.txt` | 650/831 samples on channel hashing; 774 in texture storage overall |
| `comparison-final/benchmark.json` | All 20 full-RAM/content-key/command comparisons exact; 25 repetitions after one warm-up; main-menu median reduction 75.78% |
| `comparison/benchmark.json` | Independent initial run of the same binaries: 75.88% reduction |
| `original-rgb/comparison.json` | All 20 original cases pass frozen 40 dB; minimum 41.61048 dB |
| `ctest.log`, `python.log` | 12 native/Lua checks and 144 Python/Lua checks pass |
| `desktop-live-02/run.json` | Actual 55.53-second desktop run exits 0; binary/resources unchanged; original intro/menu observed, zero software PCM underruns/drops |
| `desktop-live-02/audio/audio.json` | 110080 source stereo frames, 480768 pre-device output frames; neither lane truncated, no starved output block |
| `android-build.json`, `install.json` | Local bundled-ROM APK audited and installed on selected phone without deleting app data |
| `phone-install-check.json` | Installed APK SHA-256 matches the candidate receipt; foreground execution remains unverified |

The 110-second `phone-stream` collection is excluded: the device is dozing and
the app is waiting for its window. It contains no advancing SC64 game telemetry.
The user was asked to unlock the phone and leave the main menu open; the
candidate's physical menu/cadence/audio regression is still pending. The
previous APK's accepted phone measurements remain historical evidence only.

The first sandboxed desktop launch could not access Metal and failed before
game startup. It is retained as `desktop-live`; the successful native launch
is `desktop-live-02`. This is a bounded boot/PCM regression, not a mission,
physical input, perceptual listening or lifecycle acceptance run.

APK SHA-256: `3106249675609b26d319ac70a12089807999d778edeadd5ab0781068818dcc13`.
Native library SHA-256:
`18b531ffac8f6b6bfeb1fa8dfb94efefaca73415e95c0c55d12320122d4215ce`.
Size: 44533246 bytes. The build checks signing, exact ROM/private-data identity,
final library identity, ELF LOAD alignment and ZIP alignment to 16 KiB. Physical
16 KiB-page execution is still open.

For a future optimization, preserve its baseline **before** changing the core:

```sh
cmake -S . -B local/build/renderer -G Ninja -DSC64_BUILD_HOST=OFF \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build local/build/renderer --target sc64_gfx_replay -j8
mkdir -p local/reports/my-optimization/baseline
cp local/build/renderer/native/gfx_core/sc64_gfx_replay \
  local/reports/my-optimization/baseline/sc64_gfx_replay
# Implement the change, then rebuild and compare it against that baseline.
make benchmark-renderer \
  BASELINE_REPLAY=local/reports/my-optimization/baseline/sc64_gfx_replay \
  REPORT=local/reports/my-optimization/comparison
cmake -S . -B local/build/renderer -DSC64_BUILD_GFX_ORACLE=ON
cmake --build local/build/renderer --target sc64_rdp_oracle -j8
python3 tools/renderer/compare.py local/inputs/renderer/manifest.json \
  local/reports/my-optimization/original-rgb
make test
make build
make build-android BUNDLE_ROM=1 ROM="/path/to/my/StarCraft64.z64"
make audio-android ADB=/path/to/adb SERIAL=EXPLICIT_DEVICE_SERIAL
```

Every output directory must be new. The replay's `--repeat 1..100` measures
interpretation only. The benchmark checks input, manifest and binary hashes
before/after execution and fails on full-RAM/key/command differences or less
than 20% main-menu improvement. Keep these receipts private. The shipped core
does not run the offline benchmark or its slow reference hash.

## User menu comfort report (2026-10-09)

The user reports no noticeable difference in menu speed, but finds its
performance comfortable on the phone. This is qualitative usability evidence;
it does not confirm the offline percentage on Android or replace the pending
foreground telemetry, thermal and backend gates.

## Parallel software rasterization (2026-10-10)

ADR-063 splits each graphics task's rasterization by RDRAM address range across
a persistent `RasterPool` (default `min(3, cores-2)` helpers plus the guest
thread). Every range replays all rectangles in command order, so blends that
read earlier stores and aliased targets keep serial semantics; `writes` are
canonical sorted, merged ranges. Tasks under 4096 candidate pixels, odd target
addresses and hosts without helpers use the single-range serial path.

Offline macOS arm64 evidence (25 repetitions after one warm-up, alternating
order, same 20-case fenced manifest):

| Report under `local/reports/renderer/` | Comparison | Result |
|---|---|---|
| `raster-workers-{1,2,3}-*` | new serial vs 1/2/3 helpers | menu median reduction 47.1% / 58.4% / 65.5%; all equivalent |
| `raster-final-w3-181948` | previous rasterizer vs 3 helpers | menu 64.5%, overall 65.2%; all equivalent; 20% gate passed |
| `raster-final-w0-*` | previous rasterizer vs new serial path | -2% to -10% (noise band); all equivalent |
| `raster-parallel-gate-*` | fenced original RGB gate | passed; rendered RAM byte-identical to the review-fix evidence for all 20 cases |

The previous-code baseline replay is preserved at
`local/reports/renderer/raster-parallel-baseline/sc64_gfx_replay`. A 40-second
desktop session (`local/reports/desktop/raster-parallel-live-182116`) ran 1153
tasks with three helpers, no error and no audio underrun; live `software_ms`
median fell to 1.38 ms (5.1 ms in `make-desktop-20261009-073355`). ThreadSanitizer
fixture and corpus replay runs are clean. phone cadence, audio and thermal
effects are not measured yet (D-047).

Reproduce the raster comparison with one replay binary at different helper
counts (or pass a preserved baseline replay as `--baseline` without
`--baseline-workers`):

```sh
python3 tools/renderer/benchmark.py local/inputs/renderer/manifest.json \
  local/reports/renderer/my-raster-workers --stage raster \
  --baseline local/build/renderer/native/gfx_core/sc64_gfx_replay \
  --candidate local/build/renderer/native/gfx_core/sc64_gfx_replay \
  --baseline-workers 0 --candidate-workers 3
```

Live telemetry adds `software_service_ms` (guest servicing inside `software_ms`)
and `raster_helpers`.

## Game module optimization level: -O2 against -O3 (2026-10-13)

Question: should the game module (ADR-086/087) be compiled with `-O3` instead of `-O2`?

**Method.** The first mission ("1. Wasteland", `guest_mode 3`, `ui_mode game`) is reached with a
field-timed controller recipe plus one `ui_click` on the campaign list item (coordinates from the
`ui_items` telemetry) and `A` for every advisor message. The module was built twice with the same
Homebrew clang 23 and flags (`-std=c17 -fno-strict-aliasing -ffp-contract=off -fexceptions
-fvisibility=hidden`), only `-O2` or `-O3` differing; `SC64_GAME_MODULE` selected it. After fields 4800
the process was sampled by macOS `sample` for 30 s (1 ms interval, 1801-1802 fields); the leaf samples
whose image is `sc64-game.dylib` are the guest compute. Three runs per level, interleaved. A time-based
total (process CPU) cannot be used: the scene is dominated by the software rasterizer and by waiting.

| | -O2 | -O3 |
|---|---|---|
| Module leaf samples per 30 s, three runs | 380, 457, 394 | 390, 451, 414 |
| Mean (run-to-run spread) | 410 ms (10%) | 418 ms (7%) |
| Per field | 0.228 ms | 0.232 ms |
| Share of one core | 1.37% | 1.39% |
| `sc64_app` samples (rasterizer, host) | about 14 800 | about 14 950 |
| Module size, compile time (8 jobs) | 3.28 MB, 2.3 s | 3.29 MB, 2.0 s |

**Result.** Guest code is about 1.4% of one core in this scene (the host is about 50%), and `-O3`
differs from `-O2` by +1.9%, less than the run-to-run spread: no measurable gain, no size change. `-O2`
stays (ADR-087). Frame equality of `-O3`: the intro frames matched `-O2` for every task index traced
(28/28 and 29/29, two `-O2` runs as control); the menu and the mission differ even between two `-O2` runs
(cursor and game time follow the host clock), so equality could not be shown there.

**Limits.** One calm opening scene (idle marines, no combat); a battle may load the guest more but cannot
make it comparable to the 50% of the host. macOS arm64 and clang only (GCC and zig were not compared);
the sampler resolves 1 ms and counts only symbols with at least 5 samples per symbol list.
