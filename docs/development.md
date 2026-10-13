# Development guide

Build, test and device workflow for contributors, plus the private-data rules
the tools enforce. The user-facing overview is in the [README](../README.md);
project status is in [project-status.md](project-status.md).

## Build, run and device workflow

The personal build scripts run on **macOS, Linux or Windows** and need the
private generated game/runtime prepared by [Stage 1](native-runtime.md) and
[Stage 4](input.md). `python tools/sc64.py <command>` is the single entry point on
every system (ADR-082); the Makefile only forwards to it, so a machine without
`make` runs the same commands. `tools/hostenv.py` finds the host's tools (LLVM,
mingw-w64, Docker); `sc64.py doctor` reports what is missing and how to install it.
Which package a host builds is in the [desktop matrix](desktop.md). See also
[Android toolchain pins](android.md). They never upload game code.

Build and run scripts live in `tools/desktop` and `tools/android`. Their working
directories are `local/build/desktop` and `local/build/android`, with build
receipts in `local/reports/desktop` and `local/reports/android`. Desktop saves
default to `local/private/desktop/default`; the Android signing key lives in
`local/private/android`. See [repository layout](repository-layout.md)
for the research, recompilation, renderer and input tools.

```sh
make help
make doctor                      # = python tools/sc64.py doctor
make build                       # package for this host
make run                         # click / O to choose your own ROM
make run ROM="/path/to/your.z64"   # or provide it explicitly
make test

make build-windows               # local/dist/StarCraft64-windows.zip
make build-linux                 # local/dist/StarCraft64-linux.tar.gz
make build-appimage              # local/dist/StarCraft64-x86_64.AppImage

make venv                        # once: .venv for `make recompile`
make recompile ROM="/path/to/your.z64"

make build-android
make check-apk
make devices
make install-android SERIAL=DEVICE_SERIAL
make run-android SERIAL=DEVICE_SERIAL
```

Desktop output is `local/dist/StarCraft64.app` on macOS, `StarCraft64-linux/` on
Linux x86_64 and `StarCraft64-windows/` on Windows; Android output is
`local/dist/StarCraft64-android.apk`. On Android, tap the start screen to import
your own ROM through the system picker; later launches reuse its private copy.
Android requires arm64-v8a and API 28 or newer. Build before the first launch;
the run targets launch an existing build without rebuilding it.

For a personal Android build that starts automatically from its own APK assets:

```sh
make build-android BUNDLE_ROM=1 ROM="/path/to/your.z64"
make install-android BUNDLE_ROM=1 SERIAL=DEVICE_SERIAL
make run-android SERIAL=DEVICE_SERIAL
```

This uses the same APK output and application ID. The builder normalizes and
verifies the supplied ROM before packaging; the runtime reads it directly from
APK assets, verifies SHA-1 again, and passes the owned memory buffer to the
runtime. It creates no additional ROM file in private storage. Saves still use
private storage. An older ROM imported by the picker is preserved, but the
bundled asset takes priority. Without `BUNDLE_ROM=1`, the build contains no ROM
and retains the picker workflow. Pass `BUNDLE_ROM=1` to `check-apk` and
`smoke-android` when checking an APK built with this option.

`make smoke-android SERIAL=DEVICE_SERIAL` restarts the installed app and runs a
30-second Home/return probe after ROM import. It records private evidence and
does not certify a physical touch mission. `REPORT=local/reports/new-name`
overrides the timestamped report directory; it must not already exist.
`RUN_SECONDS=30` bounds a desktop run (default 0 means interactive), and
`STORAGE=local/private/my-session` selects desktop ROM/save storage. `ANDROID_SDK`
overrides the Android build SDK location; `ADB` selects the adb executable.
Rendering always uses Fit; launch overrides and saved Stage 6 presentation
preferences cannot change it.

## Private data boundary

Supply your own cartridge dump. ROMs, extracted assets, RAM/display-list dumps,
generated recompilation C/RSP code, native game libraries, and personal builds
stay local and are ignored. No game-derived output is uploaded to build services.
The runtime validates normalized SHA-1 before starting the game.
Builds containing recompiled game code are for personal use only.
ROM inclusion is an explicit personal-build option requested by the user;
the default APK excludes it, and all such outputs remain under ignored `local/`.

The initial directory contained a ROM and `tools/bolt_extract.py` /
`tools/sc64_probe.py`; those inputs were preserved. These older exploratory
tools remain exploratory. `bolt_extract.py` normalizes and verifies the NTSC-U
ROM; both write only under ignored `local/` (default `local/extracted/sc64-usa`):

```sh
python3 tools/bolt_extract.py "StarCraft 64 (USA).z64" local/extracted/sc64-usa
python3 tools/sc64_probe.py local/extracted/sc64-usa --png local/reports/sc64-probe
```

A separate bounded BOLT audit agrees on all 2,111 leaf payloads, but does not
certify their heuristic resource labels or a production importer. Runtime claims come from the original capture corpus.

Before publishing, inspect ignored files and stage only reviewed authored
source/documentation paths. `.gitignore` cannot prevent `git add -f` or remove
files that were already tracked. Never stage the supplied ROM or `local/`.

## Run the analyzer

From the project root:

```sh
python3 tools/rom_info.py "StarCraft 64 (USA).z64"
python3 -m unittest discover -s tests -v
```

To save a private report, create its parent directory first and use a new output
filename. The analyzer refuses to overwrite existing files, including the input.

```sh
mkdir -p local/reports
python3 tools/rom_info.py "StarCraft 64 (USA).z64" \
  --expect-sha1 a36d91b5aac7bb95df0d7de5f9814f5270e25c2b \
  --output local/reports/rom-info-new.json
```

`--normalized-out local/roms/sc64.ntsc-u.z64` optionally writes a **new** z64 copy;
format detection uses magic bytes, not the extension. Create `local/roms` first.
All report offsets describe the normalized image. The two SHA-1 fields identify
the input representation and normalized image separately. Unknown save hardware
and active microcode remain null; identifier strings are only candidates.

## Recheck Stage 0 locally

With the existing private corpus and reference probe built:

```sh
python3 -m unittest discover -s tests -v
python3 tools/research/check_acceptance.py "StarCraft 64 (USA).z64" \
  --output local/reports/research/acceptance-new.json
```

The checker redecodes task/RDP data and verifies overlay equality, save identity,
reference regressions and evidence hashes. It does not replace the documented
original-screen review or certify native/runtime/Android behavior. Rebuilding
and replaying a fresh corpus is documented in `docs/reference-capture.md`.

## Documents

- [Repository layout](repository-layout.md): present files and proposed modules.
- [Research ledger](research.md): verified metadata, external evidence, hypotheses.
- [Stage 0 workflow](research-workflow.md): ROM analysis, splat, n64sym, emulator captures.
- [Capture build and replay](reference-capture.md): executed desktop research harness and acceptance checks.
- [libultra inventory](libultra-inventory.md): 51 replacement body candidates and runtime gaps.
- [Stage 1 native workflow](native-runtime.md): private generation, host build and acceptance evidence.
- [Stage 2 offline renderer](renderer.md): build, native capture previews and original-frame acceptance.
- [Desktop shell](desktop.md): SDL3 build/run on macOS, Windows and Linux, controls and acceptance.
- [Stage 4 touch controls](input.md): gesture input, cursor feedback, optional diagnostic pad and acceptance checks.
- [Stage 5 acceptance](stage5-acceptance.md): closed personal Android profile, evidence, commands and explicit exclusions.
- [Stage 6 presentation](presentation.md): withdrawn experiment, fixed Fit baseline and fresh rollback checks.
- [Android implementation](android.md): working local APK and implementation history.
- [Implementation plan](implementation-plan.md): stages, experiments, build/verification gates.
- [Decisions](decisions.md): architecture and consequences.
- [Debt](debt.md): unresolved work and prerequisites.
- [Toolchain sources](toolchain-sources.md): inspected upstream revisions and SDK limitations.
