# Project status

Historical and current status of the staged plan. The stage gates, accepted
scopes and evidence are in [the implementation plan](implementation-plan.md); this page
keeps the narrative summary that used to open the README.

**Stages 0–5 closed for their documented scopes.** Stages 0–4 use the
accepted macOS desktop baseline; Stage 5 closes the personal Android profile
on 2026-10-09 (ADR-055). See [Stage 5 acceptance](stage5-acceptance.md).
Original-ROM captures establish S2DEX2 2.08, CPU CI8 rendering, CPU audio,
FlashRAM save/reload, two uncompressed overlays and the libultra replacement
inventory. The C++20 native capture host reaches title/menu, saves and reloads a
game, and completes a real ten-minute run: 36,000 VI fields and 33,004 tasks.
Stage 1 uses file PCM output; Stage 3 adds verified live miniaudio/Core Audio
output (D-028), confirmed audible by the user.
The standalone C++20 HLE renderer passes 20 original post-task RGB frames at
PSNR >= 40 dB (minimum 41.61 dB). Stage 3 built a personal
macOS arm64 application: real Vulkan presentation matches all 20 accepted native
RGB frames exactly, and live sound is user-confirmed. Revised live resume and
paused-stop receipts pass. Stage 4 now prioritizes mouse/touch gestures translated into original controller
input with read-only cursor feedback. The large virtual pad is opt-in (F3).
The user accepts Stage 4 closure on 2026-10-08 after reporting a mouse-only
mission completion. Stage 5 separately adds user acceptance of a touch-only first
Terran mission and save/load after restart on the phone. Researched menus, advisor messages,
pause buttons and the build grid now have mouse/touch adapters using original
controller events and read-only UI feedback; see the Stage 4 acceptance scope.
Visible original C/B command hints are clickable and show a pressed highlight.
The OS pointer remains visible over their painted panel and hints highlight on
hover. The map cursor follows free mouse movement continuously. Panel hover
preserves the last map cursor target; hint
clicks and panel padding consume input. A tap outside construction closes the
panel through Back, then selects at the tapped point through original input.
Long mouse press/touch opens the temporary Build/Pause panel. The macOS system
cursor is hidden over the map while the original game cursor is active.
Clicking the visible minimap moves the camera immediately through the original
camera setter, without moving the game cursor or sending a selection/order.
The OS pointer remains visible over the minimap; mouse dragging also navigates,
and a single-finger tap navigates after release. Hidden minimaps, menus,
split-screen and stale frames do not use this direct navigation path.
On Defeat/Victory, click or tap a statistics tab to switch it; tap elsewhere
to continue. Tab selection uses the original D-pad input without moving the cursor.
Stage 5 now produces a personal arm64 Android APK locally. Android emulator
checks cover system-picker ROM import, private ROM reuse, live guest/PCM progress,
Vulkan presentation, campaign/briefing taps through the Wasteland map and
Home/return suspension. ADR-054 also verifies Vulkan/OpenGL ES startup and
Home/return on a 16 KiB Android 16 ARM64 kernel; see
[runtime profiles](android-runtime.md). Expanded helper FPU probes pass on
phone and 16 KiB Android; actual Android Back and mission Home/return also pass.
The user confirms the first Terran mission with touch only and save/load after
restart on the phone. Expanded device/GPU qualification and long Adreno/Mali
performance/thermal runs are deferred by the requested scope.
Those Stage 3–5 results were recorded on the former engine build. The SDL3
shell (ADR-074) reproduces the desktop pixel, lifecycle and recipe gates, the
16 KiB emulator Android profile (OpenGL ES variant; Vulkan is the packaged default and crashes on the emulator, D-053), and cross-builds
Windows and Linux x86_64 packages (Linux also as an AppImage, ADR-081) whose user acceptance is pending (D-054); see
[desktop](desktop.md) and [Android](android.md).

**Stage 6 was withdrawn by the user on 2026-10-09 (ADR-060).** Original fonts
and nearest image sampling are restored. Presentation always uses aspect-preserving
Fit with black letterboxes and the same transform for mouse/touch. There are no
HD, image-filter or scale choices, persisted presentation settings, F2/F5/F7
shortcuts or widescreen guest patches. Historical research remains documented in
[the withdrawal record](presentation.md).

The phone menu audio defect is user-accepted (ADR-051). ADR-052 optimizes texture
hashing and repeated atlas work: all 20 original-corpus
outputs remain exact relative to the baseline, and offline main-menu
interpretation on macOS arm64 takes approximately 76% less time. This is not a
phone frame-rate/thermal claim. Reproduction and current receipts are in
[the performance workflow](performance.md); `make benchmark-renderer`
compares a preserved baseline executable against the rebuilt renderer.
