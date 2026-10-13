# Personal builds require the private generated game under ignored local/.
# Desktop targets run through tools/sc64.py (macOS, Linux, Windows); on a host
# without make, call `python tools/sc64.py <command>` directly. See `make doctor`.
.DEFAULT_GOAL := help

PYTHON ?= python3
# Recompilation needs spimdisasm: empty uses the project .venv (`make venv` creates it).
RECOMP_PYTHON ?=
ADB ?= adb
JOBS ?= 8
ROM ?=
BUNDLE_ROM ?= 0
ALLOW_DIRTY ?= 0
FORCE ?= 0
USE_ZIG ?= 0
# setup-game: android-arm64 cross-builds the module for a phone; NDK overrides the pinned one.
MODULE_TARGET ?=
NDK ?=
# 1 builds the Linux package in the pinned Docker image even on a Linux x86_64 host.
DOCKER ?= 0
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

ifneq ($(BUNDLE_ROM),0)
ifneq ($(BUNDLE_ROM),1)
$(error BUNDLE_ROM must be 0 or 1)
endif
endif
SC64 = $(PYTHON) tools/sc64.py
DOCKER_FLAG = $(if $(filter 1,$(DOCKER)),--docker)
export ALLOW_DIRTY ANALYSIS DATA_DIR FORCE USE_ZIG MODULE_TARGET NDK RECOMPILER_TARGET ROM BUNDLE_ROM RECOMP_PYTHON SERIAL ADB ANDROID_SDK APK STORAGE REPORT RUN_SECONDS SMOKE_SECONDS JOBS
export BASELINE_REPLAY RENDERER_MANIFEST
export PAGE_SIZE ANDROID_BACKEND ANDROID_DEBUGGABLE ANDROID_RENDER_DRIVER BUILD_TYPE

# `make build-desktop`/`run-desktop`/`configure-desktop` are the older names of build/run/configure.
.PHONY: help doctor venv bootstrap inventory recompile recipe-verify recipe-export setup-game package package-macos package-linux package-windows build-recompiler verify-recompiler build build-macos build-desktop configure-desktop run run-desktop build-windows build-linux build-appimage \
        build-android build-android-opengles icon configure-android check-apk devices install-android \
        run-android smoke-android audio-android benchmark-renderer test test-python test-native

help:
	@printf '%s\n' \
	  'make venv                   Create .venv with the pinned recompilation packages (spimdisasm)' \
	  'make inventory ROM="/path/game.z64"  Generate the libultra inventory (n64sym scan + emulator RDRAM capture)' \
	  'make recompile ROM="/path/game.z64"  Generate local/generated/game from the ROM (runs inventory first if it is missing)' \
	  'make recipe-verify ROM=...  Regenerate the game C from the committed recipe (recipe/ntsc-u) and compare hashes' \
	  'make package / package-macos / package-linux / package-windows  Release launcher (no game code) in local/dist/launcher' \
	  'make setup-game ROM=... [USE_ZIG=1] [DATA_DIR=...] [MODULE_TARGET=android-arm64]  Build the game module from the ROM like a user would (sc64_setup)' \
	  'make build-recompiler [RECOMPILER_TARGET=linux-x86_64]  Patched N64Recomp release binary in local/dist/recompiler' \
	  'make verify-recompiler ROM=...  Run it on the recipe and compare the generated hashes' \
	  'make recipe-export ANALYSIS=local/recompile/symbols-<stamp>  Refresh the recipe after a recompile (maintainers)' \
	  'make doctor                 Check which build tools this machine has for each target' \
	  'make build / build-desktop   Build the package of the OS you run on (macOS app, Linux or Windows package)' \
	  'make build-macos            Build the macOS arm64 app bundle (macOS hosts only)' \
	  'make run / run-desktop       Launch it; ROM="/path/game.z64" is optional' \
	  'make build-windows          Windows x86_64 zip (mingw-w64: cross on macOS/Linux, native in MSYS2 MINGW64)' \
	  'make build-linux            Linux x86_64 tar.gz (host compiler on Linux x86_64, local Docker elsewhere)' \
	  '  DOCKER=1 forces the pinned Docker image (reproducible glibc baseline) on a Linux x86_64 host' \
	  'make build-appimage         Pack the Linux x86_64 AppImage from the fresh Linux build (local squashfs-tools or Docker)' \
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
	  'Variables: PYTHON, RECOMP_PYTHON, ADB, JOBS, ANDROID_SDK, APK, STORAGE, REPORT, RENDERER_MANIFEST, DOCKER,' \
	  '           RUN_SECONDS (0 = interactive), SMOKE_SECONDS (default 30),' \
	  '           PAGE_SIZE (0 = record only), ANDROID_BACKEND (empty = default)' \
	  'ROM, generated game C and saves stay private; see docs/android.md.'

doctor:
	$(SC64) doctor

bootstrap:
	$(SC64) bootstrap

icon:
	$(SC64) icon

venv:
	$(SC64) venv

inventory:
	$(SC64) inventory

recompile:
	$(SC64) recompile

recipe-verify:
	$(SC64) recipe-verify

recipe-export:
	$(SC64) recipe-export

setup-game:
	$(SC64) setup-game

package:
	$(SC64) package $(DOCKER_FLAG)

package-macos:
	$(SC64) package-macos

package-linux:
	$(SC64) package-linux $(DOCKER_FLAG)

package-windows:
	$(SC64) package-windows

build-recompiler:
	$(SC64) build-recompiler $(DOCKER_FLAG)

verify-recompiler:
	$(SC64) verify-recompiler

# `build` makes the package of the OS it runs on; build-macos/-windows/-linux name a target.
build: build-desktop

build-desktop:
	$(SC64) build

build-macos:
	$(SC64) build-macos

configure-desktop:
	$(SC64) configure

run: run-desktop

run-desktop:
	$(SC64) run

build-windows:
	$(SC64) build-windows

build-linux:
	$(SC64) build-linux $(DOCKER_FLAG)

build-appimage:
	$(SC64) build-appimage $(DOCKER_FLAG)

build-android:
	$(SC64) build-android

build-android-opengles:
	$(SC64) build-android-opengles

configure-android:
	$(SC64) configure-android

check-apk:
	$(SC64) check-apk

devices:
	$(SC64) devices

install-android:
	$(SC64) install-android

run-android:
	$(SC64) run-android

smoke-android:
	$(SC64) smoke-android

audio-android:
	$(SC64) audio-android

benchmark-renderer:
	$(SC64) benchmark-renderer

test: test-python test-native

test-python:
	$(SC64) test-python

test-native:
	$(SC64) test-native
