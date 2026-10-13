# Personal builds require the private generated game under ignored local/.
# Build scripts run on a macOS arm64 host; Windows/Linux are cross-compiled.
.DEFAULT_GOAL := help

PYTHON ?= python3
# Recompilation needs the splat/spimdisasm environment.
RECOMP_PYTHON ?= .venv/bin/python
ADB ?= adb
JOBS ?= 8
ROM ?=
BUNDLE_ROM ?= 0
SERIAL ?=
ANDROID_SDK ?=
# Empty builds/uses the Vulkan APK; opengles2 selects the emulator variant (D-053).
ANDROID_RENDER_DRIVER ?=
APK ?= local/dist/StarCraft64-android$(if $(filter opengles2,$(ANDROID_RENDER_DRIVER)),-opengles2).apk
STORAGE ?= local/private/desktop/default
REPORT ?=
RUN_SECONDS ?= 0
SMOKE_SECONDS ?= 30
PAGE_SIZE ?= 0
ANDROID_BACKEND ?=
# 1 packages android:debuggable (run-as, debugger, `adb install -r -d` rollback).
ANDROID_DEBUGGABLE ?= 0
# CMake build type of the app builds (macOS, Windows, Linux, Android).
BUILD_TYPE ?= Release
BASELINE_REPLAY ?=
RENDERER_MANIFEST ?= local/inputs/renderer/manifest.json
RUN_ID := $(shell date +%Y%m%d-%H%M%S)

ifneq ($(BUNDLE_ROM),0)
ifneq ($(BUNDLE_ROM),1)
$(error BUNDLE_ROM must be 0 or 1)
endif
endif
export ROM BUNDLE_ROM SERIAL ANDROID_SDK APK STORAGE REPORT RUN_SECONDS SMOKE_SECONDS JOBS
export BASELINE_REPLAY RENDERER_MANIFEST
export PAGE_SIZE ANDROID_BACKEND ANDROID_DEBUGGABLE BUILD_TYPE

.PHONY: help bootstrap recompile build build-desktop configure-desktop run run-desktop build-windows build-linux build-appimage \
        build-android build-android-opengles icon configure-android check-apk devices install-android \
        run-android smoke-android audio-android benchmark-renderer test test-python test-native

help:
	@printf '%s\n' \
	  'make recompile ROM="/path/game.z64"  Generate local/generated/game from the ROM (needs the libultra inventory)' \
	  'make build / build-desktop   Build the personal macOS arm64 SDL3 app' \
	  'make run / run-desktop       Launch it; ROM="/path/game.z64" is optional' \
	  'make build-windows          Cross-build the Windows x86_64 zip (mingw-w64)' \
	  'make build-linux            Cross-build the Linux x86_64 tar.gz (local Docker)' \
	  'make build-appimage         Pack the Linux x86_64 AppImage from the fresh Linux build (local Docker)' \
	  '  BUNDLE_ROM=1 ROM="/path/game.z64" packs the ROM and autostarts from it (personal use only)' \
	  'make build-android          Build and audit the personal arm64 APK locally' \
	  '  BUNDLE_ROM=1 ROM="/path/game.z64" includes ROM and autostarts from APK assets' \
	  'BUILD_TYPE=Release|RelWithDebInfo|Debug   CMake build type of app builds (default Release)' \
	  '  ANDROID_DEBUGGABLE=1 packages a debuggable APK (default: release, no run-as access)' \
	  'make build-android-opengles Same with OpenGL ES for emulators (local/dist/StarCraft64-android-opengles2.apk);' \
	  '  pass ANDROID_RENDER_DRIVER=opengles2 to install/run/smoke/check-apk to use that APK' \
	  'make icon ROM="/path/game.z64"  Convert the original title art into local/icons (used by every build)' \
	  'make check-apk              Audit APK alignment and private-data boundary' \
	  '  BUNDLE_ROM=1 also required when auditing/installing/probing a bundled APK' \
	  'make devices                List adb devices' \
	  'make install-android SERIAL=<id>   Install APK on the explicit device' \
	  'make run-android SERIAL=<id>       Open the installed Android app' \
	  'make smoke-android SERIAL=<id>     Restart app and probe Home/return' \
	  '  PAGE_SIZE=16384 ANDROID_BACKEND=opengles|vulkan verifies that runtime profile' \
	  'make audio-android SERIAL=<id>     Record 60 seconds of the running app audio counters' \
	  'make benchmark-renderer BASELINE_REPLAY=<binary>   Compare private renderer timings and exact output' \
	  'make test                   Run Python and native regression tests' \
	  'make test-python            Run the synthetic Python suite only' \
	  'make test-native            Configure desktop, then run CTest checks' \
	  'make bootstrap              Fetch pinned public SDL3/stb/DejaVu/miniaudio into local/toolchains' \
	  'make configure-desktop / configure-android   Configure without packaging' \
	  'Variables: PYTHON, ADB, JOBS, ANDROID_SDK, APK, STORAGE, REPORT, RENDERER_MANIFEST,' \
	  '           RUN_SECONDS (0 = interactive), SMOKE_SECONDS (default 30),' \
	  '           PAGE_SIZE (0 = record only), ANDROID_BACKEND (empty = default)' \
	  'ROM, generated game C and saves stay private; see docs/android.md.'

bootstrap:
	$(PYTHON) tools/desktop/bootstrap.py

icon:
	@test -n "$(ROM)" || { echo 'make icon requires ROM="/path/game.z64"' >&2; exit 2; }
	$(PYTHON) tools/desktop/icon.py "$(ROM)"

recompile:
	@test -n "$(ROM)" || { echo 'make recompile requires ROM="/path/game.z64"' >&2; exit 2; }
	$(RECOMP_PYTHON) tools/recompile/recompile_all.py "$(ROM)" --jobs "$$JOBS"

build: build-desktop

build-desktop:
	$(PYTHON) tools/desktop/build.py --build-type "$$BUILD_TYPE"

configure-desktop:
	$(PYTHON) tools/desktop/build.py --build-type "$$BUILD_TYPE" --configure-only

run: run-desktop

run-desktop:
	@set --; if [ -n "$$ROM" ]; then set -- --rom "$$ROM"; fi; \
	$(PYTHON) tools/desktop/run.py "$$@" --storage "$$STORAGE" \
	  --seconds "$$RUN_SECONDS" --output "$${REPORT:-local/reports/make-desktop-$(RUN_ID)}"

build-windows:
	$(PYTHON) tools/desktop/cross_build.py windows --jobs "$$JOBS" --build-type "$$BUILD_TYPE"

build-linux:
	$(PYTHON) tools/desktop/cross_build.py linux --jobs "$$JOBS" --build-type "$$BUILD_TYPE"

build-appimage: build-linux
	@set --; if [ "$$BUNDLE_ROM" = 1 ]; then \
	  test -n "$$ROM" || { printf '%s\n' 'Set ROM to your dump when BUNDLE_ROM=1.' >&2; exit 2; }; \
	  set -- --bundle-rom "$$ROM"; fi; \
	$(PYTHON) tools/desktop/appimage.py "$$@"

build-android:
	@set --; if [ -n "$$ANDROID_SDK" ]; then set -- --sdk "$$ANDROID_SDK"; fi; \
	if [ "$$BUNDLE_ROM" = 1 ]; then \
	  test -n "$$ROM" || { printf '%s\n' 'Set ROM to your dump when BUNDLE_ROM=1.' >&2; exit 2; }; \
	  set -- "$$@" --bundle-rom "$$ROM"; fi; \
	if [ -n "$$ANDROID_RENDER_DRIVER" ]; then set -- "$$@" --render-driver "$$ANDROID_RENDER_DRIVER"; fi; \
	if [ "$$ANDROID_DEBUGGABLE" = 1 ]; then set -- "$$@" --debuggable; fi; \
	$(PYTHON) tools/android/build.py "$$@" --jobs "$$JOBS" --build-type "$$BUILD_TYPE"

build-android-opengles:
	@$(MAKE) --no-print-directory build-android ANDROID_RENDER_DRIVER=opengles2

configure-android:
	@set --; if [ -n "$$ANDROID_SDK" ]; then set -- --sdk "$$ANDROID_SDK"; fi; \
	$(PYTHON) tools/android/build.py "$$@" --jobs "$$JOBS" --build-type "$$BUILD_TYPE" --configure-only

check-apk:
	@set --; if [ "$$BUNDLE_ROM" = 1 ]; then set -- --bundled-rom; fi; \
	$(PYTHON) tools/android/check_apk.py "$$APK" "$$@"

devices:
	$(ADB) devices -l

install-android: check-apk
	@test -n "$$SERIAL" || { printf '%s\n' 'Set SERIAL to an explicit adb device ID.' >&2; exit 2; }
	@case "$$SERIAL" in emulator-*) \
	  unzip -p "$$APK" assets/sc64/config.ini | grep -q '^sc64.render_driver = opengles2$$' || \
	  { printf '%s\n' 'The Vulkan APK crashes on emulators (D-053): make build-android-opengles, then install with ANDROID_RENDER_DRIVER=opengles2.' >&2; exit 2; };; esac
	$(ADB) -s "$$SERIAL" install -r "$$APK"

run-android:
	@test -n "$$SERIAL" || { printf '%s\n' 'Set SERIAL to an explicit adb device ID.' >&2; exit 2; }
	@set --; if [ -n "$$ANDROID_RENDER_DRIVER" ]; then \
	  set -- --esa com.nomadic.sc64.ARGUMENTS "--config=sc64.render_driver=$$ANDROID_RENDER_DRIVER"; fi; \
	$(ADB) -s "$$SERIAL" shell am start -n com.nomadic.sc64.personal/com.nomadic.sc64.Sc64Activity "$$@"

smoke-android:
	@test -n "$$SERIAL" || { printf '%s\n' 'Set SERIAL to an explicit adb device ID.' >&2; exit 2; }
	@set --; if [ "$$BUNDLE_ROM" = 1 ]; then set -- --bundled-rom; fi; \
	if [ "$$PAGE_SIZE" != 0 ]; then set -- "$$@" --required-page-size "$$PAGE_SIZE"; fi; \
	if [ -n "$$ANDROID_BACKEND" ]; then set -- "$$@" --backend "$$ANDROID_BACKEND"; fi; \
	$(PYTHON) tools/android/run.py "$$@" --adb "$(ADB)" --serial "$$SERIAL" --apk "$$APK" \
	  --seconds "$$SMOKE_SECONDS" --home --output "$${REPORT:-local/reports/make-android-$(RUN_ID)}"

audio-android:
	@test -n "$$SERIAL" || { printf '%s\n' 'Set SERIAL to an explicit adb device ID.' >&2; exit 2; }
	$(PYTHON) tools/android/audio_report.py --adb "$(ADB)" --serial "$$SERIAL" \
	  --output "$${REPORT:-local/reports/make-audio-$(RUN_ID)}"

benchmark-renderer:
	@test -n "$$BASELINE_REPLAY" || { printf '%s\n' 'Set BASELINE_REPLAY to a preserved baseline replay executable.' >&2; exit 2; }
	cmake -S . -B local/build/renderer -G Ninja -DSC64_BUILD_HOST=OFF -DCMAKE_BUILD_TYPE=RelWithDebInfo
	cmake --build local/build/renderer --target sc64_gfx_replay -j "$$JOBS"
	$(PYTHON) tools/renderer/benchmark.py "$$RENDERER_MANIFEST" \
	  "$${REPORT:-local/reports/make-renderer-benchmark-$(RUN_ID)}" \
	  --baseline "$$BASELINE_REPLAY" --candidate local/build/renderer/native/gfx_core/sc64_gfx_replay

test: test-python test-native

test-python:
	$(PYTHON) -m unittest discover -s tests

test-native: configure-desktop
	cmake --build local/build/desktop --target sc64_desktop_bridge_test sc64_flash_test \
	  sc64_vi_test sc64_ui_observers_test sc64_guest_fpu_test sc64_gfx_test sc64_rom_import_test \
	  sc64_sha_test sc64_desktop_input_test sc64_touch_input_test sc64_gestures_test sc64_map_taps_test \
	  sc64_hud_input_test sc64_hud_buttons_test sc64_menu_input_test sc64_haptics_test sc64_host_telemetry_test sc64_loop_watch_test -j "$$JOBS"
	ctest --test-dir local/build/desktop --output-on-failure
