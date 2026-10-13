# Stage 5: Android implementation and acceptance

## Current status and implementation history

**CLOSED on 2026-10-09 for the personal Android profile (ADR-055).**
[stage5-acceptance.md](stage5-acceptance.md) is the current checklist, evidence
boundary and command reference. The user accepts a touch-only first Terran
mission and save/load after restart on the phone. Expanded device checks and long
Adreno/Mali performance/thermal sessions are deferred at the user's request.
Earlier slice checklists and open-gate statements below describe their original
execution dates; they do not override the current closure scope.

## SDL3 shell APK (ADR-074, 2026-10-11)

The APK now packages the SDL3 shell; every later section of this document
describes the former engine build and remains historical evidence for it.

- **Build:** `make build-android [BUNDLE_ROM=1 ROM=...]` runs
  `tools/android/build.py`: NDK r29 CMake profile (`native/android/CMakeLists.txt`
  + `cmake/shell.cmake`) links `libmain.so` with SDL 3.4.18 statically (whole
  archive, so SDL's JNI entry points are exported). `aapt2` links the manifest
  and the launcher icon (converted from the original title art by `make icon`, ADR-076, else an authored fallback); `javac`/`d8` compile SDL's Java activity layer from the
  same release plus `Sc64Activity` and `Haptics`; the ELF is stored uncompressed,
  `zipalign -P 16` and `apksigner` use the existing private debug key. No Gradle,
  former engine build tool or remote service is involved.
- **Identity:** package `com.nomadic.sc64.personal`, the same key, versionCode 2
  (above the former build). Builds are not debuggable by default, so `run-as`
  cannot read the app data; `make build-android ANDROID_DEBUGGABLE=1` (or
  `tools/android/build.py --debuggable`) packages a debuggable APK, which also
  allows reinstalling an earlier build with `adb install -r -d` without losing
  data. Installing a release build over an earlier debuggable one with the same
  key keeps app data.
- **Configuration:** `assets/sc64/config.ini` (compact telemetry, bundled ROM,
  `render_driver = vulkan,opengles2`), overridden by the string-array extra
  `com.nomadic.sc64.ARGUMENTS`; the exported activity honors only
  `--config=sc64.render_driver=…` from it and ignores every other key.
- **Platform:** storage is `getFilesDir()` as before; the bundled ROM is read
  from assets into owned memory; ROM import uses SDL's document picker and an
  owned copy; Back maps to `android_back`; activity suspension reaches the
  session through an SDL event watch; vibration uses `Haptics` over JNI.
- **Renderer:** Vulkan by default with an OpenGL ES fallback
  (`render_driver = vulkan,opengles2`; SDL tries the list in order and the shell
  logs a warning if Vulkan was unavailable; ADR-075). It runs well on the
  physical phone (user report 2026-10-11). SDL 3.4.18's Vulkan renderer crashes
  after Home/return on surface loss on the Android **emulator** (D-053), so for
  emulator runs build the OpenGL ES variant: `make build-android-opengles` (add
  `BUNDLE_ROM=1 ROM=...` if needed) writes `local/dist/StarCraft64-android-opengles2.apk`
  and `local/reports/android/build-opengles2.json` without replacing the default
  APK or receipt. Same package, key and versionCode, so
  `make install-android ANDROID_RENDER_DRIVER=opengles2 SERIAL=...` replaces the
  installed build and keeps its saves; `install-android` refuses an `emulator-*`
  serial for an APK that is not the OpenGL ES variant. `ANDROID_RENDER_DRIVER=opengles2`
  also selects that APK for `check-apk` and `smoke-android` and makes
  `run-android` pass the driver as an intent argument. Check the active driver
  with `make smoke-android ANDROID_BACKEND=opengles|vulkan SERIAL=...`, which fails
  if telemetry reports another `render_driver`.

Evidence (`local/reports/sdl3-migration/`, 16 KiB Android 16 emulator):

- [x] `emulator-default-01`, `emulator-final`: install over the former APK keeps
  `files/sc64.fla` byte-identical; Home/return freezes and resumes guest, PCM and
  presentation (59.4–60.2 fields/s), OpenGL ES, no crash, same process.
- [x] Landscape immersive full screen; Back delivers `android_back`.
- [x] `make check-apk BUNDLE_ROM=1`: 16 KiB ELF/ZIP alignment, signature,
  bundled-ROM identity, no other private input.
- [ ] Physical phone rerun of the SDL3 APK (touch mission, audio, haptics).

At entry, the user accepts desktop Stage 4 closure (ADR-038).
The current macOS arm64 baseline has original-controller mouse/touch routing,
observed menu/HUD adapters and shortest campaign navigation. The user reports
completing a mission with the mouse alone. This permits Android implementation;
it does not certify physical touch. The implementation receipt below establishes
the first personal Android APK and emulator smoke only.

Previous complete campaign baseline engine SHA-256:
`5b51e3789840d16512beb96d61de76d7077e373f12c9e0ea86c8740b876e5ed5`.
Completed private campaign receipt: `review-remediation-native-02/campaign.json`;
eight native recipe gates, ten CTest checks and 111 Python tests passed.
ADR-039 also keeps input metadata with the selected VI image, preserves middle
pan movement over the HUD and bounds released selection completion.
The build following that baseline includes ADR-040's disabled build-cell
navigation correction, engine SHA-256
`159dbbd4814eebb76df1ec42afa23a2d4798e797b055f5a59a3b164552ee7459`.
Its ten CTest gates, 111 Python tests and bounded native grid routing pass;
the earlier eight-gate campaign receipt retains its own engine hash.
The current bundle also includes ADR-041's mission-only focus pause, SHA-256
`ac75bc8c0b106022cbe189669dc62d96ba347558b5ed4b2077efa933ed54a519`:
ten CTest checks and the bounded native intro deactivation/manual-pause check
pass. Physical mission/device lifecycle acceptance remains separately scoped.
See [input.md](input.md) and [research.md](research.md) for scope
and commands. Earlier scene/lifecycle receipts keep their own build hashes.

## First implementation slice

1. Inventory installed SDK/NDK, Java, CMake, local build tooling and the exact
   engine SDK. Read their actual headers and build profiles before choosing
   Android APIs or manifest/link flags. Pin the verified versions.
2. Audit the current macOS-only local link, native file picker, cursor and Core
   Audio paths. Define Android source selection and link a complete host/runtime
   archive; the existing common `sc64_host` manifest declaration is not an
   Android packaging proof (D-034/D-035).
3. Compile the private generated game locally for arm64-v8a using C++20 for
   authored native code and C for generated code. Link locally;
   audit the payload for ROM/resources/generated-code disclosure. Check final
   ELF/APK 16 KiB alignment and used FPU behavior (D-012).
4. Implement verified Android ROM import into private storage, SHA-1 validation,
   save/config persistence, PCM output and lifecycle/surface recovery. Keep all
   engine graphics/render calls on the engine's owning thread.
5. Produce a private APK and prove the first on-device menu-to-mission slice
   with sound, touch input, Back, save/reload and pause/resume. Preserve desktop
   regressions before starting performance optimization.

The working output of this slice is a locally built APK plus a named-device
run receipt and acceptance checklist, not just an arm64 static archive. The
exact build commands must be documented after toolchain/header validation;
no working Android command or API is assumed in this handoff.

## Risks and inputs identified at entry

- The current host/link/picker/audio profile is macOS-specific. Android ABI,
  packaging, audio-device recovery and surface recreation need implementation.
- The accepted graphics path rasterizes software tasks and uploads through
  the former engine. Mobile CPU/upload/thermal cost must be measured before selecting GPU
  optimizations; desktop frame parity does not prove mobile performance.
- Physical multi-touch, safe areas/orientation and Android Back delivery have
  no device receipt yet. Fixture and native recipe evidence bypass OS delivery.
- Record available device models, Android versions and supported backends;
  final performance acceptance needs named Adreno and Mali profiles. Device
  availability can be resolved during Stage 5 and does not block Stage 4 closure
  or the initial toolchain audit.

## Current Stage 5 exit checks

- [x] Local arm64 archives link into the matching former Android engine/APK.
- [x] Private build/import/save boundaries and ROM SHA-1 validation pass.
- [x] Final libraries/APK alignment and verified 16 KiB ARM64 startup/Home-return
      pass on Android 16 emulator through Vulkan and OpenGL ES (ADR-054).
- [x] Established ARM64 helper FPU profile passes on desktop, phone and 16 KiB
      Android; full VR4300 exception/FP16 semantics remain deferred (D-012).
- [x] First Terran touch-only mission with diagnostic pad disabled and save/load
      after restart are USER-ACCEPTED on the phone (D-041), not a recorded walkthrough.
- [x] Actual Android Back opens/closes the mission pause menu; mission Home/return
      freezes/resumes guest/PCM/presentation in the same process on 16 KiB Vulkan.
- [x] Desktop/native/Lua/Python regressions pass (13 CTest and 149 Python/Lua).

Expanded multi-touch/cancel, orientation/safe-area, controller hotplug and
surface/audio-device recovery profiles remain unqualified. Named Adreno/Mali
visual parity and 15-minute cadence/thermal runs are explicitly deferred by user
scope; no successful result is asserted for those omitted checks.

Expanded hint/menu/camera/group/2P contexts remain D-042 and require their own
workload checks. Other desktop platforms remain D-034. The optional direct
guest-action design remains D-013; it is not required for the accepted
controller-translated input path. Stage 5 is closed only for the profile defined
in [stage5-acceptance.md](stage5-acceptance.md).

## First Android implementation (2026-10-08)

The local profile is macOS arm64 as build host, NDK **29.0.14206865**, Android
API **28** for native code/minimum OS, Build-Tools **36.0.0**, Android framework
API **36** for Java/target SDK, and the pinned SDK hash of the former engine.
Its build tool used a bundled Java runtime; `javac`, `jar` and `keytool` use the
installed Homebrew OpenJDK 21. Authored native code is C++20 and generated code remains C17.
Generated C retains `-O2 -fno-strict-aliasing -ffp-contract=off -fexceptions`.
No `-fwrapv` requirement or complete guest FCSR exception model is inferred.

`native/android/CMakeLists.txt` compiled the guarded Stage 3 runtime, private
game archive, existing software renderer and Android platform host, and linked
them with the former engine's libraries, exports and Android entry glue
**directly with CMake on this computer**, rather than in a local build
container. This explicit first-slice choice is ADR-042; the former engine's
conventional packaging remained D-035. No source or game library is submitted
to a remote build server; the former build tool's server URL was a closed
localhost endpoint and its staging project excluded the extension.

The builder staged the personal engine after resource compilation so that the
final APK library matches the local stripped engine byte-for-byte. A separate dex contains the authored
SAF/lifecycle helper; the final APK is realigned and signed with an ignored
personal debug key. Publication replaces the APK only after signature, native
identity, private-input and alignment checks pass. Desktop resources are built
in their own project/tree.

The start screen accepts click/tap or O. Android's `ACTION_OPEN_DOCUMENT`
returns a provider URI; the helper reads its content stream on a worker into a
bounded 32 MiB temporary file in app-private storage. Native `poll_rom` loads
it on the engine thread, normalizes byte order and checks the existing NTSC-U
SHA-1 before publishing the private normalized ROM. Cold starts reuse that ROM.
Saves remain private, including the 128 KiB Flash file. In the default build no
ROM, extracted asset or generated source is an APK resource or tracked file.
The opt-in personal bundled-ROM mode is described below. A normalized ROM and
compiled guest code are still personal-use inputs/output.

The former engine's SHA-1/SHA-256 helpers replaced the host's OpenSSL hashing
calls, so the Android host did not require a separate OpenSSL build. Its pinned
JNI thread-attach, class-loading and activity-result listener signatures were
checked in the SDK headers. Graphics continued exclusively through the existing
engine-thread graphics/render path. AAudio is selected by pinned miniaudio
0.11.23, as verified by the native log.

Android Activity pause/resume has an independent suspension cause, delivered
by `Application.ActivityLifecycleCallbacks`. It freezes the virtual clock and
guest workers even when the engine stops updating in the background; returning
cannot undo manual/mission-focus pause. It does not call graphics from the UI
thread. Desktop mission-only focus policy remains ADR-041. Android physical
mouse cursor hiding and audio-device/context loss recovery are still D-041 /
D-035 / D-036. The personal Android profile uses fit scaling and immersive mode.

Guest entry and guest-created threads initialize their own FPU environment;
ARM64 FZ/DN/FZ16 are cleared. Shell/audio thread FPU state is untouched. The
probe covers the pinned recompiler's four COP1 rounding modes and one gradual
underflow calculation, not all game arithmetic/exception semantics (D-012).

### Build and verify

The root Makefile wraps these same scripts: `make build-android`,
`make check-apk`, `make install-android SERIAL=DEVICE_SERIAL`, and
`make run-android SERIAL=DEVICE_SERIAL`; each is `python tools/sc64.py <same name>`
(ADR-082), which runs on macOS, Linux and Windows. `make build` / `make run` use
the desktop package of the host; `make test` runs the Python and native/Lua regressions.

The APK build finds its tools through `tools/hostenv.py`: the SDK from `ANDROID_SDK`
(`--sdk`), `ANDROID_HOME`, `ANDROID_SDK_ROOT` or the IDE location
(`~/Library/Android/sdk`, `~/Android/Sdk`, `%LOCALAPPDATA%\Android\Sdk`); a JDK 21+
from `JAVA_HOME`, Homebrew or `PATH`; the NDK LLVM prebuilt of the host
(`darwin-`, `linux-` or `windows-x86_64`) and the `.exe`/`.bat` build-tools names on
Windows. `python tools/sc64.py doctor` prints the exact `sdkmanager` commands for
missing pieces. Only macOS arm64 has been used to build an APK (D-057).
`make smoke-android SERIAL=DEVICE_SERIAL` restarts the imported game for a
bounded Home/return check. See [development.md](development.md) for storage/report/tool overrides.

```sh
python3 tools/android/build.py --configure-only
python3 tools/android/build.py
python3 tools/android/check_apk.py local/dist/StarCraft64-android.apk
adb devices -l
adb -s DEVICE_SERIAL install -r local/dist/StarCraft64-android.apk
adb -s DEVICE_SERIAL shell am start -n com.nomadic.sc64.personal/com.nomadic.sc64.Sc64Activity
```

Choose your own ROM in the system picker on the first launch. The package
requires Android 9/API 28 or newer and arm64-v8a. For a **new, private bounded
smoke** on an explicitly selected device, after importing the ROM:

```sh
python3 tools/android/run.py --serial DEVICE_SERIAL --seconds 30 --home \
  --output local/reports/android/device-smoke-01
cmake --build local/build/android --target sc64_guest_fpu_test
adb -s DEVICE_SERIAL push local/build/android/sc64_guest_fpu_test /data/local/tmp/sc64_guest_fpu_test
adb -s DEVICE_SERIAL shell /data/local/tmp/sc64_guest_fpu_test
```

The smoke script verifies installed APK identity, restarts only this package,
records device/page-size/log data and optionally sends Home/return. It does not
play a mission or certify hardware. Receipt flags deliberately retain physical
acceptance as false. Inspect the log and output JSON before expanding scope.
Home/return runs require at least 30 seconds: acceptance needs two active
telemetry samples after return, growing field/task/sample and nonzero-PCM
counters, and the same app PID at completion. Pre-Home progress alone fails.

### Evidence and first-slice acceptance

The initial APK identity below is retained in
`local/reports/android/emulator-smoke-02/run.json`. The latest build receipt and
published APK are replaced by successful local builds; the review-fix receipt
is recorded separately below. Initial APK size: 17,249,717 bytes.
Engine SHA-256: `c9f5f3e7d8369e68e445fe5127feae38c9da065285badf7096b834503b79755c`.
APK SHA-256: `126d5dbf879bd506735c5dba40e5f24a4421dee5905e6126ad0d71c2fbcb32b7`.
All packaged arm64 ELF LOAD segments and RELRO ends pass 16 KiB checks;
`zipalign -c -P 16 4` and `apksigner verify` pass. This is package validation,
not execution with 16 KiB pages.

`emulator-smoke-02/run.json` binds the installed APK to these bytes on
`sdk_gphone64_arm64`, Android 16, **4096-byte pages**, Vulkan/SwiftShader.
During its eight-second Home interval, fields/tasks/samples remain exactly
**577 / 81 / 210948**; guest work and nonzero PCM resume without a guest error
or observed crash. An earlier real SAF selection verifies normalized ROM SHA-1
inside app-private storage, then reaches the visible main/campaign menu. ADB
MotionEvent tap enters the campaign list. Opening/title skipping used Enter;
this is not a touch-only mission receipt. Screenshot and native log captures
stay ignored. Flash-file SHA-1 remains
`cb58f51f26a624a758ed770d1e9d5b4e3b5a0c5d` across the bounded cold restart;
this proves file retention, not a new gameplay save/reload.

The ARM64 emulator FPU probe passes, and desktop gates pass **11 CTest** checks
and **120 Python** tests. Desktop native freeze/resume uses the separate
`desktop-native-02` receipt; the shorter first run had insufficient samples
inside pause and is not a lifecycle acceptance receipt.

- [x] Personal arm64 APK builds locally; no cloud upload or ROM packaging.
- [x] Native library identity, signing and ELF/ZIP alignment gates pass.
- [x] Actual SAF import, SHA-1, cold ROM restart and visible menu pass in emulator.
- [x] Android-delivered taps select Wasteland, advance briefing and dismiss
  advisor messages to expose the running map. Private screenshot/hash receipt:
  `local/reports/android/android-touch-probe.json`. Enter skipped intro/title;
  this is not touch-only mission completion or physical-touch acceptance.
- [x] AAudio nonzero PCM, Home freeze/return and scoped FPU probe pass in emulator.
- [ ] Touch-only first mission, actual gameplay save/reload and full input/device recovery.
- [ ] Conventional local container build profile, Android GLES parity and surface/audio recovery.
- [ ] 16 KiB **runtime**, Adreno/Mali, sustained cadence and 15-minute thermal sessions.

The emulator reports audio underruns and does not validate audible quality or
phone performance. A physical device model/Android version remains the next
needed input; final acceptance needs both Adreno and Mali workloads. Stage 5
stays open and Stage 6 is not started. (Superseded: ADR-055 closed Stage 5 and
ADR-060 withdrew Stage 6 on 2026-10-09.)

### Review corrections (2026-10-08)

All four first-slice review findings are addressed:

1. The smoke gate binds freeze/return to ordered native events, requires two
   active post-return samples with growing guest/PCM counters, and rejects a
   missing/replaced process. A Home run shorter than 30 seconds is rejected.
2. The package auditor requires `lib/arm64-v8a/libStarCraft64.so` and binds an
   optional expected engine hash to that exact member. An auxiliary library
   cannot stand in for it.
3. `tools/desktop/game_validation.py` is shared by both builders and both engine
   CMake profiles. Missing/duplicated UI/HUD/campaign observers or the campaign
   wrap patch stop configuration; stale Stage 1 output cannot bypass this gate.
4. Portable `RomImportState` owns pending SAF results until engine consumption.
   Finalization and late completion delete unconsumed owned files. Startup
   removes only direct regular `sc64-import-*.tmp` / `.sc64-rom.tmp` files;
   symlinks, directories, saves, normalized ROMs and unrelated files survive.
   Failed deletion is reported in the native log. ADR-043 records ownership.

The updated personal APK is 17,249,717 bytes. Its SHA-256 is
`3f501851eef6dc3fc76fae5c2484a0c978cd403581c31eb1fa3aef331b964836`;
engine SHA-256 is
`69dd0af74622a8cec9583ad18bb37a11b37703dff8f66fae3206cc8358edf084`.
Private immutable build receipt: `local/reports/android/review-fixes-build.json`.
`review-fixes-smoke-01/run.json` binds the installed APK to these bytes and
records PID 3622 unchanged, three post-return samples with advancing guest/PCM,
and identical paused/resumed field/task/sample counts **568 / 93 / 208900**.

`make test` passes **129 Python tests and 12 CTest checks**. Negative tests cover
missing/stuck/paused/PCM-stalled resumes, process replacement, an absent/wrong
engine with a matching auxiliary hash, and incomplete generated game hooks.
The native import test covers pending/late cleanup, ownership transfer,
orphan cleanup, canonical directory aliases and preservation of unrelated data.
A real Android CMake probe rejects the previously accepted stale fixture.
On the updated emulator APK, seeded orphan files disappear at startup while
the unrelated marker, normalized ROM and Flash hashes remain unchanged.
An actual second SAF import passes through the updated callback state and
reaches the visible main menu: `local/reports/android/review-fixes-import.json`.
The earlier normalized copy was retained as a private backup until hash equality
was established; the backup and synthetic marker were then removed. No supplied
source ROM or saved game was replaced by test data.
Logs: `stage5-review-fixes-tests.log`, `stage5-review-fixes-build.log`,
`stage5-review-fixes-guard.log` and storage before/after hashes under
`local/reports`. These corrections do not close the physical Stage 5 gates.


## Optional bundled-ROM build (2026-10-08)

This is an option on the existing personal APK, not a new application/target.
The user explicitly authorized ROM inclusion and requested direct reads from the
installed APK. See ADR-044. The regular build continues to exclude the ROM.

```sh
make build-android BUNDLE_ROM=1 ROM="/path/to/your.z64"
make check-apk BUNDLE_ROM=1
make install-android BUNDLE_ROM=1 SERIAL=DEVICE_SERIAL
make run-android SERIAL=DEVICE_SERIAL
make smoke-android BUNDLE_ROM=1 SERIAL=DEVICE_SERIAL
```

The Python equivalent is `python3 tools/android/build.py --bundle-rom PATH`.
Omitting the option rebuilds the ordinary picker APK at the same path:
`local/dist/StarCraft64-android.apk`, package `com.nomadic.sc64.personal`.
The successful-build receipt remains `local/reports/android/build.json` and
records `rom_included`, asset SHA-1, engine hash and APK hash. Wrong-size or
wrong-hash input fails before toolchain work or publication, preserving the
previous APK. z64/v64/n64 are detected by magic, normalized, and required to match
`a36d91b5aac7bb95df0d7de5f9814f5270e25c2b`.

Only the opted-in build inserts `assets/sc64/sc64-ntsc-u.z64`, after resource
compilation/bundling. It never enters the source project, resource compilation
inputs, or a remote build request. The asset is ZIP-compressed. NDK
`AAssetManager_open` / `AAsset_getLength64` / bounded `AAsset_read` load it straight
into the ROM vector; `AAsset_close` is managed by RAII. No extraction/import
worker or temporary ROM file is involved. Session verifies SHA-1 again before
starting, transfers the vector to librecomp, and skips ROM persistence in this
mode. Flash/config files still use private storage. The asset takes priority
over an older picker import, which is preserved. Ordinary startup still reuses
that import. Missing/bad assets produce a visible startup error.

The content auditor requires `--bundled-rom` to accept this option and validates
only the single exact asset. An ordinary audit rejects the bundled APK; the
bundled audit also rejects a missing asset, wrong size/order/hash, additional ROM
members, and generated C/static libraries. Signing and 16 KiB ELF/ZIP alignment
checks are unchanged. The current bundled APK is 44,238,334 bytes, SHA-256
`740104e1acc844a88720e9c823fbc64a49a95a86e69303a7fa691fdfa60e1992`;
its engine SHA-256 is
`e39e93c00216d0bd08fd87120102624755a2de6efb58d4be72b5f3608f1bdf89`.

Acceptance evidence in ignored `local/reports/android/`:

- `bundled-ordinary-regression-01/run.json`: rebuilt ordinary APK boots using its
  imported ROM and passes the strict 35-second guest/PCM Home/return gate.
- `bundled-assets-run-01/run.json`: install/update the bundled APK on the isolated
  arm64 API-36 emulator after removing only its backed-up imported ROM. No picker
  or injected input was used. Native startup logs the APK-asset source; the game
  runs and nonzero PCM advances. Home freezes fields/tasks/samples at
  565/93/207876, and four subsequent telemetry samples prove resumed guest/PCM
  progress in the same process. No guest errors or native/Java crash were seen.
- `bundled-storage-before.json` / `bundled-storage-after.json`: private ROM and
  import temporaries are absent; Flash SHA-1 remains
  `cb58f51f26a624a758ed770d1e9d5b4e3b5a0c5d`.
- `bundled-assets-visible.png`: actual emulator screenshot shows the original
  main campaign menu after automatic startup.

Checklist for this option:

- [x] Same build command/output/package with explicit optional ROM inclusion.
- [x] Builder and runtime validate the researched image identity.
- [x] Default and opt-in APK content boundaries pass.
- [x] Direct asset startup works without picker, disk ROM or import temporary.
- [x] Ordinary import restart, guest/PCM progress and Home/return still pass.
- [x] Existing save hash is preserved in the emulator check.
- [ ] Physical Android device checks remain part of Stage 5 acceptance.

A second cold process also passes a 25-second no-input asset-start probe
(`bundled-assets-cold-02/run.json`); the native asset-source message appears
again, no ROM/import file is created, and recursive private `files/` usage is
196 KiB. Automated verification passes 133 Python tests and all 12 CTest checks
(including the real host Lua startup fixture); the shared macOS native engine
also links successfully. These checks do not close the physical-device gates.


## Smartphone audio/pan/cursor follow-up (2026-10-08)

The user reports successful physical startup, intermittent startup audio pulsing,
excessive two-finger scrolling and slow cursor travel. This is useful physical
feedback, without a device/model/build receipt or a sustained cadence measurement.
ADR-045 records the implementation: 60 ms startup/recovery PCM pre-roll, bounded
camera displacement through Z+stick and faster continuous cursor steering.

Audio diagnostics in `SC64` telemetry now include `audio_buffer_ms`,
`audio_buffering` and `audio_wait_callbacks` alongside
`audio_underruns`, discarded frames and nonzero PCM. Wait callbacks include
intentional pre-roll and must not be described as device underruns. Underruns
count actual PCM starvation. Check their deltas during
startup and settled gameplay separately. A flagship label alone does not
establish scheduling, rendering, producer throughput or audio-device latency.

Build and physical retest:

```sh
make build-android BUNDLE_ROM=1 ROM="/absolute/path/to/game.z64"
make install-android BUNDLE_ROM=1 SERIAL=YOUR_PHONE
make run-android SERIAL=YOUR_PHONE
adb -s YOUR_PHONE logcat -v threadtime > local/reports/phone-audio-input.log
```

Stop the log capture after the bounded test. Reports remain private. Acceptance:

- [ ] Listen through cold startup several times: no repeating pulses after pre-roll.
- [ ] Move both fingers by a small distance, then hold still: map stops rather
  than continuing to scroll. Lift/reapply and reverse near every map edge.
- [ ] Compare cursor travel, short precise taps and selection drags; confirm
  no overshoot-induced accidental selection/order, including around HUD hints.
- [ ] Verify Home/return preserves audio and clears held camera/controller input.

These physical criteria remain open until the user/device run; synthetic control
and PCM fixtures do not close D-035 hardware/performance gates.


Validation receipts for this follow-up:

- `local/reports/mobile-python-final.log`: 133 Python tests pass.
- `local/reports/mobile-native-tests-final.log`: all 12 native/Lua CTest gates pass,
  including delayed cursor arrivals, both camera gesture adapters, edge reversal,
  stale/future feedback, release cancellation and PCM pre-roll/recovery rates.
- `local/reports/android/mobile-camera-profile.json`: hashes and decoded original
  camera snapshots; addresses/arithmetic are independently traced in research.md.
- `local/reports/android/mobile-desktop-02/run.json`: completed 155-second native
  mission/input recipe, unchanged binary/resources during execution, clean exit.
  `gestures.json` accepts original A only after a fresh cursor arrival. A target
  requested at field 7100 receives A at 7135, with observed error (1.315,1.205).
- `local/reports/android/mobile-controls-summary.json`: a (20,12) native-image
  displacement produces (20,8), within the 4-pixel camera quantum. A stationary
  pre-release sample retains the reached position. Zero discarded source frames
  and callback mutex misses; two starvation events recover during the run.
  This runtime receipt predates the final edge-reversal rebase; the rebase itself
  passes its separate fixture. It does not change the ordinary measured pan path.
- `local/reports/android/mobile-android-01/run.json`: first rebuilt APK cold-start,
  guest/nonzero PCM progress, unchanged PID, zero guest-counter advance while Home
  suspended and four advancing/nonzero-PCM post-return samples; no errors/crash.

Desktop's first sandboxed attempt could not access Metal; `mobile-desktop-01`
is a failed launch receipt. The successful second run used normal Metal access.
Neither the background desktop recipe nor the headless Android emulator proves
physical gesture feel, audible phone quality or sustained flagship performance.

The final bundled APK also passes the same cold-start/Home/return gate in
`local/reports/android/mobile-android-final/run.json`. Its SHA-256 is
`3e11cfc32a29b8a5effbe2564c607b4850300e9bca62041b769baec9a95d8685` (44385790 bytes). The native engine is
`cbf9b4055a8b8f8df03b039155b07b0c8bae8d22b739a371bf541e80c915f7d6`; signing, ELF/ZIP 16 KiB alignment and explicit
ROM-asset identity checks pass. Physical acceptance remains open.


## Persistent menu music: scheduling and AAudio follow-up (2026-10-08)

ADR-046 supersedes a fixed 60 ms pre-roll as the complete audio fix. The user
reports persistent menu pulsing/crackle, with normal mission audio. The new host
services runnable guest priorities during owned software rasterization, removes
the producer mutex from the device callback, smooths clock correction and sizes
pre-roll to DMA cadence. Requested AAudio reserve is at least four bursts or
1536 frames, clamped to device capacity; actual size can differ.

Additional telemetry: `audio_source_hz`, `audio_dma_ms`, `audio_target_ms`,
`audio_device_xruns`, `audio_device_buffer_frames`. AAudio xruns are distinct from
host PCM starvation; -1 means unsupported, not zero.
For phone diagnosis, keep a log through at least a minute of main-menu music,
then mission start, and compare counter deltas by `ui_mode`. Avoid attributing
loading-transition silence to a steady-menu pass. Reports remain private.

`local/reports/android/menu-audio-comparison.json` binds the before/after desktop
receipts: 12 main-menu samples over 55.067 s, before cumulative starvation 4 and
after 0, discarded PCM 0. The subsequent attract transition still starves once.
The callbacks/pause paths, image-byte identity, masked guest writeback and
100000-frame concurrent PCM transport are covered by the 12 native/Lua gates in
`local/reports/menu-audio-cooperative-tests.log`; 133 Python tests pass in
`local/reports/menu-audio-python-final-02.log`. Native desktop startup/main menu
is verified; phone audible acceptance remains open.

Final evidence: `local/reports/android/menu-audio-final-summary.json` binds the
100-second Android run and 155-second desktop mission recipe. Nine Android
menu samples span 40.097 s with zero new PCM starvation, zero
AAudio device xruns and discarded frames. Requested reserve is rounded by the
emulator to 3840 frames (960-frame bursts, capacity 4360); do not assume this
80 ms device value on a phone. Home freezes guest counters exactly and four
post-return samples advance guest/nonzero PCM, with unchanged PID and no crash.
One adb fixture Enter was sent during intro; this is not physical gesture proof.
Desktop mission samples add no starvation after the separate loading gap and
the original cursor-arrival gate passes. Native route bypasses OS gestures.

Final bundled APK SHA-256: `e6b630d0b62d3519ab2a5961b505f6d8e4b1267e8326ec30a52dd2326138c2dc`, 44402174 bytes; native library
`3077854bfa8a57ae5e762af2c847b7ca7adfc20048d1b04bab5b87fa7e0b230f`. Signing, explicit ROM identity and 16 KiB ELF/ZIP
alignment pass; 16 KiB device runtime/phone audibility remain open.


## Contextual mission Back and harvesting follow-up (2026-10-08)

ADR-047 changes system Back on a fresh map to original Start and translates plain
worker/resource taps to original B using the game's cursor hit test. Other
selection taps remain A. The selected mask is latched at key-down; a held Back
does not cancel the menu it just opened. Required regenerated game output has
twenty UI observers, thirty hooks in total; both cursor anchors are checked.

The personal build command remains:

```sh
make build-android BUNDLE_ROM=1 ROM=/absolute/path/to/your.z64
```

`local/reports/back-gather-android-build-01.log` and
`local/reports/android/build.json` bind the rebuilt bundled APK at
`local/dist/StarCraft64-android.apk`: SHA-256
`48ba42aeb55ec940cfb39045fd8aadeb260c4d79cb39137dbc38b7543a485f1c`,
44402174 bytes; native engine SHA-256
`8c4637f8b4f3aa0be0a1989b4831e18a022fe8ab724ea89a991ccd938b13ea87`.
Signing, exact ROM identity and ELF/ZIP 16 KiB alignment pass. This build remains
local/personal. Runtime phone input and audible music acceptance remain open.

Regression evidence: 134 Python/Lua checks pass in
`local/reports/back-gather-python-03.log`; 12 CTest checks pass in
`local/reports/back-gather-native-tests-02.log`. Native desktop evidence and its
OS-delivery boundary are recorded in `input.md`.

- [x] Personal Android APK rebuild, signature and ROM/alignment audit.
- [ ] Physical Android Back opens the mission menu; a second Back closes it.
- [ ] Selecting an SCV, then tapping minerals starts gathering while retaining it.
- [ ] Normal unit selection, build-grid interaction and two-finger pan remain usable.


## Sliding gestures through command hints (2026-10-08)

ADR-048 permits two-finger camera gestures and mouse/touch selection rectangles
to start in or cross the original HUD hint canvas. A HUD press remains a tap
candidate until it moves more than three native pixels or another finger joins.
The original hint command/pressed feedback is cancelled before the map controller
receives the gesture. Small tap jitter still activates a hint.

`local/reports/hud-pass-through-android-build-01.log` and the current
`local/reports/android/build.json` bind the personal bundled-ROM APK at
`local/dist/StarCraft64-android.apk`: SHA-256
`441c9eef2fc76cebd5774de14a331e626df40b19715a52cd6c1dd9a13f735368`,
44402174 bytes. The native library remains
`8c4637f8b4f3aa0be0a1989b4831e18a022fe8ab724ea89a991ccd938b13ea87`:
this change affects Lua input routing/resources, with no new native/SDK APIs.
Signing, exact bundled ROM identity and ELF/ZIP 16 KiB alignment pass.

Fixture tests drive the actual GUI dispatcher with both mouse and multi-touch
actions, including HUD-first/map-first/both-HUD camera contacts, releases,
rectangle endpoints, no accidental hint/B pulse, small tap jitter and adding a
finger after a drag. Twelve CTest checks and 134 Python checks pass; logs and
native desktop scope are recorded in `input.md`.

- [x] Desktop and personal bundled Android resources rebuilt.
- [x] GUI input regressions pass; APK signature/content/alignment audit passes.
- [ ] Physical phone: pan and select through hints/padding without accidental commands.
- [ ] Physical phone: normal hint taps retain press feedback and activate once.

## Phone menu music follow-up (2026-10-08)

The user identifies an Android 16 phone and confirms that desktop menu
audio is normal, while Android music pulses both initially and after a mission.
ADR-050 records the compatibility-output and buffering implementation. The
former engine's public SDK has no streaming PCM producer API; its mixer
integration would require an exact-version internal adapter or an engine API
extension.
The current Android profile independently uses OpenSL ES, as does the pinned
engine's Android device implementation, with conservative AAudio fallback.

`sc64.audio_backend=compatibility` is the default in the former engine's project
settings. Setting it to `aaudio` before rebuilding permits comparison with the previous
low-latency float/512-frame output. Desktop output stays CoreAudio with its
previous PCM target and periods. Compatibility uses signed-16 stereo, 48 kHz,
four 1024-frame device periods, a 160 ms front-end pre-roll floor, and fixed
300 ms logical queue bounds (28802-frame physical storage per lane). Other
guest modes retain the prior DMA-sized pre-roll target; entering the front end
requests one retained-stream rebuffer. This changes host audio latency, not
original samples, guest AI deadlines or VI timing.

Evidence must retain failures: `menu-audio-compat-emulator-01` has five new menu
starvations after backend replacement alone. The first larger-reserve candidate
`menu-audio-jitter-final-01` also discards PCM because its old 200 ms storage bound
is too close to a 160 ms target. Neither is an accepted fix.
`menu-audio-profile-comparison-01.json` separately binds two diagnostic APKs with
identical native code: source gaps reach 84.45/96.58 ms, and the AAudio recording
has two partial output blocks. Both diagnostic apps intentionally exit after
80 seconds to export traces, so the ordinary smoke process-survival gate rejects
them. Their PCM observations are valid diagnostics, not lifecycle acceptance.

Optional desktop PCM capture, without a hardware loopback:

```sh
python3 tools/desktop/run.py --rom /absolute/path/to/owned-rom.z64 \
  --audio-from 2400 --audio-to 3000 --seconds 55 \
  --output local/reports/my-private-audio-probe
make build-desktop
make build-android BUNDLE_ROM=1 ROM=/absolute/path/to/owned-rom.z64
```

Capture windows are limited to 600 VI fields and 12 seconds of allocated PCM
storage per lane; stalled/overfull probes mark truncation. Traces are private
float source/pre-device PCM with per-block field/rate/time metadata. Output is
captured before final signed-16 quantization. Source-rate changes remain explicit
in metadata rather than being mislabelled as one constant-rate WAV.

- [x] Public SDK and exact engine sound source checked; no invented API.
- [x] Bounded trace, stereo conversion, retained-stream rebuffer and jitter fixtures pass.
- [ ] Physical phone: cold menu and at least two mission-to-menu returns sound continuous.
- [ ] Physical phone: compare speakers/headphones, verify mission sound and Home/return.

Physical listening remains D-028. Emulator counters, APK signing/alignment and
these fixtures cannot close it.

The final personal bundled APK is `local/dist/StarCraft64-android.apk`, 44533246
bytes, SHA-256
`0e466bf4e8fc8f36db337ef40d1caa8df1ebb002fa21038b39f89c67cace7905`;
its native library SHA-256 is
`e30853a0a62d135e9b437269b4dea6c1247c186fdcb6db0d9d6b1dbb84bed459`.
`menu-audio-android-package-final.log` and `android/build.json` bind the build,
signature, exact bundled ROM and ELF/ZIP 16 KiB audit. Twelve CTest checks pass
in `menu-audio-package-final-tests.log`; 135 Python/Lua checks pass in
`menu-audio-reserve-python-final.log`.

The final unchanged macOS engine/resource run
`android/menu-pcm-package-final-01/run.json` exits successfully after 55 seconds.
Its stable menu window at fields 2400..3000 contains 111104 source stereo frames
at 11025 Hz and 481280 pre-device output frames at 48000 Hz, with no truncation,
zero frames or partial/starved output blocks. This is a PCM pipeline check,
not a hardware loopback recording.

The final APK's 70-second Android 16 ARM64 emulator receipt
`android/menu-audio-package-final-01/run.json` passes process survival and
Home/frozen-guest/resume progress; its first two menu samples are clean.
Continuing the same process exposes a mixed result, retained in
`audio-summary.json` and `engine-extended.log`: three starvations in the first
55.82-second sampled menu episode, with a sampled software-render delay of
688.755 ms. Another starvation occurs outside the menu episode. A second
25.07-second menu episode is clean, and no PCM is discarded. The short smoke
must not be used to claim that all long-session gaps are fixed. The earlier
110-second `menu-audio-reserve-final-01` has a clean 45.08-second sampled menu
window, but is a previous build receipt, not this final APK's receipt.

The separate 110-second final-build repeat
`android/menu-audio-package-final-02/run.json` also passes process survival and
Home/resume. Its 45.07-second sampled menu window has one starvation and zero
discarded frames (`audio-summary.json`), without a simultaneous desktop game.
This confirms that the current bounded mitigation does not eliminate every
emulator menu gap. Do not dismiss the extended-run failure solely as concurrent
desktop load. The owned headless/no-audio emulator was stopped after the run;
no physical phone audio route has been recorded.

- [x] Final desktop and Android builds; APK signature/content/alignment checks.
- [x] Final desktop source/pre-device PCM window and Android Home/resume smoke.
- [ ] Long-session Android starvation elimination; retain the mixed extended run.

## Phone menu sound accepted after interpreter scheduling fix (2026-10-08)

ADR-051 supersedes ADR-050's default: `sc64.audio_backend=aaudio` restores the
previous float/512-frame output and DMA-sized pre-roll. The OpenSL candidate
is user-rejected after more obvious interruptions. The actual fix services
higher guest priorities during owned display-list interpretation and texture
decode/hash/atlas work, extending the existing rasterization checkpoints.
Live RDRAM is fully copied before any new scheduling point; no graphics API
is called from a guest thread. Interpreter wall time includes audio service.

`local/reports/android/phone-menu-audio-comparison-01.json` binds physical phone
Android 16 results on the same AAudio profile:

| Measured menu window | Before | After |
|---|---:|---:|
| Duration | 55.061 s | 55.058 s |
| Source stereo frames/s, nominal 11025 Hz | 10247.26 | 11029.00 |
| Maximum source delivery gap | 104.21 ms | 61.01 ms |
| Software starvation callbacks | 551 | 0 |
| Discarded PCM / hardware xruns | 0 / 0 | 0 / 0 |

The user confirms continuous normal menu music after installation. This is
physical audible acceptance for this route, distinct from emulator counters.
The phone's kernel uses 4 KiB pages; static ELF/ZIP 16 KiB checks still pass,
but this run does not certify a physical 16 KiB kernel.

Final bundled-ROM APK: `local/dist/StarCraft64-android.apk`, 44533246 bytes,
SHA-256 `8e6dc07c8daaffdc3bf5ca19978b2013f233cb29d16c3b6c3aadf47277379838`;
native library `5ecec5ad79a2e5a4b749a1b920630d4a9b049b6b3abb8a829acc41784fb33c54`.
`phone-audio-service-01/build.json` retains the build/signature/ROM/alignment
receipt; its `audio.json` verifies the installed APK hash. The default build
command remains `make build-android BUNDLE_ROM=1 ROM=/absolute/path/to/owned.z64`.

For another explicit phone trace, keep the running game in the desired screen:

```sh
make audio-android SERIAL='<adb-device-serial>'
# Or analyze the saved log without accessing a phone:
python3 tools/android/audio_report.py --log /path/to/engine.log \
  --output local/reports/my-audio-analysis
```

The tool records source/callback gap peaks per five-second window, callback
work, source/nonzero counts, UI/rate boundaries and hardware xruns. It does not
restart or install the app. Intentional pause/rate changes reset interval
baselines. Played-output silence ratios omit unplayed waiting/starved output;
source throughput and starvation counters must be reviewed together.

- [x] Fixed snapshot/texture/output equivalence and diagnostic concurrency fixtures.
- [x] Twelve CTest checks and 141 Python checks; desktop PCM window passes.
- [x] Personal APK build/signature/ROM/alignment checks; installed hash verified.
- [x] Physical phone bounded menu stream and user audible acceptance.
- [ ] Two manual mission-to-menu returns and headphones on the phone.
- [ ] Other devices, audio rerouting/recovery and sustained thermal acceptance.

## Texture processing optimization (2026-10-09)

ADR-052 implements the next Stage 5 performance slice. The desktop profiler
identifies historical eight-byte-per-channel content hashing as the dominant
menu interpreter cost. An exact unsigned-arithmetic reduction and earlier
semantic atlas reuse preserve all 20 full rendered RAM images, texture keys and
command accounting. Repeated offline main-menu interpretation is approximately
76% faster on macOS arm64; the original 40 dB RGB gate, 12 CTest and 144 Python/Lua
checks pass. The actual desktop application reaches the menu in a 55-second
run with zero PCM underruns/drops. See [performance.md](performance.md) for
commands, exact evidence and the limits of those timing measurements.

The personal bundled-ROM APK is installed on the selected phone. Its SHA-256 is
`3106249675609b26d319ac70a12089807999d778edeadd5ab0781068818dcc13`;
native library SHA-256 is
`18b531ffac8f6b6bfeb1fa8dfb94efefaca73415e95c0c55d12320122d4215ce`.
`local/reports/texture-processing-01/android-build.json` records local signing,
private-content and ELF/ZIP alignment checks; `phone-install-check.json` verifies
the installed APK hash. App data is retained.

The first 110-second phone collection is **excluded**: the phone is dozing and
the former engine logs `Window not ready. Waiting...`, with no advancing game
telemetry.
Raw logs remain in `phone-stream`. Installation and an Adreno Vulkan driver log
are not an executed-menu or performance acceptance result. A new foreground
collection after unlocking remains open; do not substitute offline timings or
ADR-051's previous APK measurements for that candidate's phone evidence.

- [x] Baseline/candidate private corpus outputs and original RGB gate pass.
- [x] Desktop native/PCM and regression checks pass.
- [x] APK builds, is audited, installs and matches its receipt on the phone.
- [ ] New candidate's foreground phone menu PCM/cadence is recorded.
- [ ] Stage-wide touch mission, lifecycle/backends, thermal and 16 KiB runtime
      checks remain open.

## Double-tap movement correction (2026-10-09)

The plain-map adapter now recognises two nearby one-finger taps before emitting
an original controller edge. One B follows cursor arrival; no preceding A can
change selection. Single touch selection waits up to 18 fields for the second
tap. ADR-053 and [input.md](input.md) record the contract, chosen thresholds,
desktop-native evidence and remaining physical-device checks.

The rebuilt personal bundled-ROM APK SHA-256 is
`6e4349c326fd4740fe12e7e235f5ecbddb2142f49f8ba905b9d7ab2c742d4a9e`; native library SHA-256 remains
`18b531ffac8f6b6bfeb1fa8dfb94efefaca73415e95c0c55d12320122d4215ce` because this correction changes Lua resources.
`local/reports/double-tap-android-build.json` binds the local build/audits;
`double-tap-phone-install.json` records successful replacement installation and
an identical hash read from the installed APK. App data is retained. The selected
phone was dozing, so this is installation evidence, not an Android gesture pass.
The user reports comfortable menu performance but no noticeable acceleration;
that qualitative report does not close the performance/device gates.

## 16 KiB runtime and explicit graphics profiles (2026-10-09)

ADR-054 closes the bounded 16 KiB runtime check using the official Android 16
ARM64 image. The same audited personal APK passes actual Vulkan and OpenGL ES
startup/guest/PCM/presentation progress and Home/return; both timed windows
cover the intro. The existing four-rounding-mode/subnormal executable also
passes on that kernel. Later menu rendering has a separate inspected screenshot.

`make smoke-android PAGE_SIZE=16384 ANDROID_BACKEND=vulkan` (or `opengles`)
requires the kernel profile before any install/restart and checks the former
engine's actual adapter from new `graphics_family` telemetry. It excludes suspended time
from cadence intervals and requires post-return presentation progress.
[android-runtime.md](android-runtime.md) records commands, API verification,
artifact hashes, receipts and the completed slice checklist. Physical GPU/frame
parity, broader FPU semantics, touch missions and sustained thermal acceptance
remain open. This candidate's phone gameplay/audio is not re-certified by the
emulator runs.

## Stage 5 closure (2026-10-09)

ADR-055 closes the personal Android profile after the user's phone touch-only
first Terran mission and cold save/load confirmation, expanded helper FPU probes
on both Android kernels, and actual Android Back/Home on the mission map.
149 Python/Lua checks and 13 CTest checks pass. The APK remains the ADR-054 build;
only the closing diagnostic test and documentation change in this pass.
The final validator rechecks raw mission telemetry and the installed APK hash.
[stage5-acceptance.md](stage5-acceptance.md) contains the current checklist,
commands, evidence and numerical limits. Expanded device and long Adreno/Mali
performance/thermal checks are deferred by user scope. Stage 6 entry is permitted
but no Stage 6 feature is implemented by this closure.
(Superseded: Stage 6 was withdrawn by ADR-060 on 2026-10-09.)

## Launcher APK without game code (2026-10-13, ADR-089/090)

`make build-android` still links the generated game into the APK and remains the default. A second variant
holds no game code and loads a module built on a computer:

```sh
make setup-game ROM="/path/to/your.z64" MODULE_TARGET=android-arm64   # <storage>/game-android-arm64/sc64-game.so
make build-android-launcher                                            # local/dist/StarCraft64-android-launcher.apk
make install-android ANDROID_LAUNCHER=1 SERIAL=DEVICE_SERIAL
make run-android ANDROID_LAUNCHER=1 SERIAL=DEVICE_SERIAL               # creates the app's folders, shows how to continue
make push-android-module SERIAL=DEVICE_SERIAL [DATA_DIR=...]
```

The launcher has its own application ID (`com.nomadic.sc64.launcher`), so it coexists with the personal
APK and has separate saves and ROM storage. Its start screen shows the icon, the title, what is needed next and two
buttons: "Select ROM" and "Select game module" (the system file picker; the module is checked by loading it from a
private copy and then installed over the previous one, a refused file changes nothing; ADR-091). So `adb` is optional:
copy `sc64-game.so` to the phone and pick it. Without the picker, a pushed module goes
to `/sdcard/Android/data/com.nomadic.sc64.launcher/files/sc64-game.so`; at the next start the app moves it into
its private `files/game/` (the linker does not load code from shared storage) and loads it. Run
`push-android-module` after the app has been started once: a folder created by `adb` is not readable by the app.
Use `ANDROID_LAUNCHER=1` with `smoke-android`, `audio-android` and `check-apk` as for the personal APK.
D-062 lists what has been verified (one phone, one session) and D-063 why the APK is not for distribution.
