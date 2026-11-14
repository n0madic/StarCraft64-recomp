# Stage 4: mouse and touch through the original controller

## Scope, plan and risks

The user rejected the initial permanent pad: it occupied too much screen space
and obscured the original interface. That UX is not accepted. Mouse/touch gestures
are now the default; the virtual pad/editor remain an optional F3 diagnostic.
ADR-027 supersedes the initial pad-first direction. **Stage 4 is CLOSED for the
macOS arm64, 1P desktop control scope by user acceptance on 2026-10-08. Stage 5
is permitted to start (ADR-038).** The original physical-touch Wasteland gate is
transferred to Stage 5 (D-041); it is not marked as passed.

**Current implementation (ADR-074):** the adapters described below were Lua
modules and engine scripts; they are now one-to-one C++ ports in
`native/shell/input/` and `native/shell/scripts.*`, driven by SDL3 events
translated to the same action names and 960x720 design coordinates
(`native/shell/app.cpp`). Every Lua regression was ported to `tests/native/`
(`test_gestures`, `test_menu_input`, `test_hud_input`, `test_hud_buttons`,
`test_map_taps`, `test_touch_input`, `test_desktop_input`, `test_haptics`).
Android Back arrives as `SDL_SCANCODE_AC_BACK` (`android_back`); the manifest
disables `OnBackInvokedCallback` so the key event is delivered. References below
to Lua files, former engine APIs or engine logs describe the build they were
recorded on. The SDL3 recipe gate results are listed in [desktop.md](desktop.md).

The current slice maps screen gestures into the original stick and controller
buttons. It reads an established NTSC-U 1P cursor profile from owned display-task
snapshots; it never writes cursor/order/selection memory. The original game
continues to resolve hit testing, selection rectangles and context orders.

Plan: prove cursor feedback and bounded steering on desktop, validate actual
selection/order/drag behavior, investigate camera and group gestures, test modal
screens and resize/focus handling, then record the complete touch-only mission.
Risks are cursor/control latency, nonlinear stick speed, frozen modal cursors,
HiDPI coordinate units, second-finger arbitration and unproven split-screen mapping.
No user choice blocks this controller-based slice.

## Closure and revised acceptance boundary (2026-10-08)

The user explicitly requests closure after reporting completion of a mission
with the mouse alone. This is project acceptance of the desktop control design,
not a claim that Android hardware or the original touch-only criterion passed.
The accepted path translates gestures into original controller actions; direct
guest selection/order patches are optional future work (D-013).

- [x] Mouse/touch share native-coordinate input, bounded controller pulses,
  fresh cursor/UI metadata and cancellation/ownership guards.
- [x] The user reports usable selection/orders and mouse-only mission completion.
- [x] Observed menus/messages/build grids and C/B hints have controller adapters;
  command feedback, outside-build dismissal, Back and campaign navigation are implemented.
- [x] Latest campaign native recipe passes all eight gates, including one-step
  first/last wrap, entry and warning cancel/continue; engine/resources stay unchanged.
- [x] Desktop regressions pass: ten CTest checks and 111 Python tests.
- [x] User accepts desktop Stage 4 closure; Stage 5 entry is unblocked.
- [ ] Stage 5 / D-041: physical touch-only first Terran mission and Android
  multi-touch, Back, cancel/focus, safe area/orientation and gamepad/device checks.
- [ ] D-042: expanded hint/menu/camera/group workloads (2P disabled, ADR-070); D-034: other desktops.

Accepted baseline: engine SHA-256
`299d7137010dc99c62b21cabdb919e9abb7de4bc2b07a42bcb806f5dbf200f8a`,
completed `campaign-wrap-native-02/campaign.json`. Commands and hashes are below.
The prior evidence sections preserve their original scope and unchecked results.
References there to Stage 4 being open describe historical gates superseded by
ADR-038; they do not block Stage 5 entry. See [android.md](android.md)
for the handoff, local-build risks and device acceptance checklist.

## Controls

| Input | Original controller action |
|---|---|
| Move mouse over game image | In a 1P mission, place the original cursor at the mouse through its own setter (ADR-095/098); elsewhere, or with `sc64.direct_mouse=0`, steer it toward the mouse |
| Mouse at an image edge, or over the letterbox beside it, in a mission | Z+stick: scroll the camera toward that edge (ADR-097/098); leaving the window stops it |
| Left click / short one-finger tap | Reach point, then A (select/activate) |
| Double one-finger tap / double left click on the map | Coalesce touches, reach point, then one B (contextual order) |
| Right click / short two-finger tap | Reach point, then B (contextual order) |
| Left / one-finger drag | Reach starting point, hold A, move to endpoint, release A |
| Middle mouse / two-finger drag | Z+stick camera steering (drag map toward fingers) |
| Double tap / left double click on an already selected unit | Reach the point, then R, R+A, R: select that unit type on screen (other double taps stay B) |
| Tab / long stationary mouse press or touch (30 guest fields) | Open the hidden fallback panel (Continue, Back, menu arrows) |
| Sandwich button, top-left corner of a 1P mission image | Start (original pause menu) |
| Minimap button, bottom-left corner of a 1P mission image | Z+L (show/hide the original minimap) |
| Encyclopedia: ☰ top-left / ‹ › bottom-right buttons | Back to the main menu (B) / previous (D-pad Left) / next (D-pad Right) article |
| Encyclopedia: horizontal swipe (24 px, dx > 1.25 dy) / arrow keys | Drag left = next article, drag right = previous; keyboard Left/Right are the same D-pad edges |
| Encyclopedia: tap a Made by / Makes / Needs link, arrow Up/Down, Enter | Tap navigates with D-pad Up/Down then opens the link with A; keyboard Up/Down move the highlight and Enter is A (original Start leaves the screen) |
| Click/tap a visible original command hint | Its original C button or B; no cursor movement |
| Click/tap a frontend or modal button | Original selection navigation, then A after feedback |
| Click/tap an advisor message | One A after the original startup lockout; early taps are queued |
| Click/tap the title / mission location card | Start (loading screens consume clicks without queuing Start); the card also continues by itself after 360 guest fields (ADR-078) |
| Click/tap the text briefing | A: next page, then the last-page Start button starts the mission (ADR-078) |
| Horizontal swipe on the text briefing (24 px, dx > 1.25 dy) | Drag left = next page (D-pad Right), drag right = previous page (D-pad Left) |
| Click/tap a Defeat/Victory statistics tab | Original Left/Right, acknowledging each selected tab |
| Click/tap elsewhere on Defeat/Victory | A (Continue); holding does not open the command panel |
| Click/tap a build-grid cell | Original analog grid navigation, then A after feedback |
| Right click / two-finger UI tap / Panel Back | B (Back/Cancel) |
| Esc / Android system Back (button or gesture) | B (Back/Cancel) at the current original cursor; no pointer movement |
| Panel Continue | Explicit original A; fallback for unresearched custom dialogs |
| Panel menu arrows | Original D-pad navigation |
| F3 | Enable/disable diagnostic pad |
| F4 while pad visible | Edit layout; press again to save |
| F6 while using pad | Select player 1/2 for diagnostic pad |

A long mouse press or touch opens the fallback panel (two rows: Continue, menu up, Back /
menu left, down, right); release, then click/tap a command. It is shown only while open.
Build is reached through its HUD hint after selecting a worker; pause through the sandwich
button or Back/Esc; the minimap through its corner button (ADR-071). Two-finger movement cancels the
order gesture and uses the original Z+stick camera mode. The displacement from
the initial finger midpoint sets a bounded camera target; stationary fingers
stop scrolling once it is reached. Lifting either finger cancels steering. Camera world units and bounds remain owned by the game.
Keyboard bindings are listed in `native/shell/app.cpp`; Enter is Start,
X is A, C is B, Esc and Android Back are B aliases, WASD is stick, P is host pause. Mouse gameplay targets the scoped
1P profile. Split-screen and physical touch are not certified.

Researched modal screens consume pointer input before the gameplay cursor servo.
Unknown widgets retain Tab/long press → Continue, Back and menu arrows. Touch
orders use an explicit double tap or two-finger tap, so a terrain order does
not first issue A and deselect the units. Single-touch selection on the map
waits up to 18 VI fields for a second tap; cursor steering begins immediately.
The worker/resource hit-test exception is described below.

### Mouse mission report and system Back (2026-10-08)

The user reports completing a mission with the mouse alone. This is human
gameplay evidence for the desktop controls. The mission identity, full input
recording and use of physical touch were not supplied; the report does not
close the touch-only Wasteland criterion or Android acceptance (D-040).

Esc (`KEY_ESC`, action `escape_back`) and Android Back (`KEY_BACK`, action
`android_back`) now pass through the regular controller state as original B
(`0x4000`). Each source keeps independent held/release state, including the
existing C/B binding. A short press lasts at least six guest fields, so a
same-presentation-frame press/release survives a guest controller poll. Host
pause/focus handling clears the aliases as it clears other controller inputs;
controller ownership cancels pending pointer actions and the transient panel.
The original game decides whether B cancels a menu or issues a gameplay order.

The former engine shell delivered Android Back through `AKEYCODE_BACK` below
API 33 and `OnBackInvokedCallback` from API 33, the latter as a synthetic press
followed by release. The SDL3 shell disables the callback in its manifest and
receives `SDL_SCANCODE_AC_BACK` on every API level. Lua regressions cover
short presses, independent aliases, player routing, focus loss and the actual
GUI-to-host merge. On-device Back/button/gesture behavior remains a Stage 5
hardware check, separate from these source and desktop checks.

The rebuilt desktop bundle passed all nine CTest checks. Completed native run
`esc-back-02` records a real engine `escape_back` press/release, final player-1
B at field 5242 with a neutral stick, and neutral input at field 5248. The run
exited zero with unchanged binary/resources. This proves input submission; a
menu-result assertion and Android hardware check were not recorded in that run.
Private receipt: `local/reports/input/esc-back-02/escape.json`.

## Implementation and API checks

- `gestures.lua`: bounded queue (eight actions), pulse/settle steering, fresh
  cursor/arrival gates, eight-field button pulses, neutral gaps, drag arbitration
  and cancellation. An action expires after 360 guest fields. A finger re-placed
  during a two-finger gesture keeps its moved/suppressed state; a third finger
  cancels the gesture. Actions queued behind a started selection drag wait until
  the drag completes.
- `haptics.lua`: one vibration pulse per rising pointer-submitted A/Start or B
  edge (ADR-064); `rumble_motor.hpp` maps the original Rumble Pak motor.
- `input_common.lua`: shared finiteness, feedback-freshness (12 fields) and
  rectangle helpers: closed bounds for menu items, half-open bounds for
  HUD/minimap items, matching the native rectangle conventions.
- `cursor_feedback.hpp`: big-endian reads from the owned canonical render packet;
  finite/range and float/integer consistency checks. `get_info` exposes native
  cursor X/Y, source guest field and validity. Width/height must be 320x240.
- `host.script`: gesture output merges through existing `sc64.set_pad`; competing
  keyboard/gamepad input cancels pointer work. A connected but idle gamepad hides
  only the diagnostic pad; pointer gestures stay available until keyboard/gamepad
  input takes ownership. Focus loss, iconify, resize, pause and finalization clear
  pending gestures. P toggles the manual pause only while a game is running; the
  status line names P only for a manual pause (focus loss/iconify show "Paused").
- `touch.gui_script`: transient panel plus opt-in pad/editor; no permanent
  virtual buttons in the normal presentation.
- `viewport.lua`: the single aspect-preserving Fit rectangle (centered, with
  bars) shared by rendering and input; screen pixels map to native image pixels
  without inventing world coordinates.
- `ui_input.lua`: UI-owned contact state, measured 4-field controller pulses and
  4-field neutral intervals (ADR-067), a bounded retry for unanswered steps,
  fresh selection feedback before confirmation, message startup delay,
  tab/window cancellation and analog 3x3 grid navigation. No cursor warp or RAM write.
- `native/runtime/ui_feedback.cpp`: read-only font/blit observers for main menu,
  Single Player tabs, unlocked episode rows, scenario rows and saved-game rows.
  Font-local boxes are projected through the actual sprite translation/scale.
- `native/shell/ui_feedback.hpp`: bounded canonical UI-tree reader for observed
  buttons; briefing requires its overlay instruction guard and either mode 12
  or its active read-only input-loop observer. A UI sidecar
  shares the frame-mailbox slot/lifetime. `get_info` exposes UI mode/token/field,
  message ID and items alongside cursor feedback.
- `hud_input.lua`: hit testing of the actually presented 1P command hints. A
  press highlights the original icon/text area; release sends a six-field
  controller pulse and a short flash, followed by a neutral interval. Moving
  off the target, a competing finger, changed selection/label, stale feedback,
  pause or another input owner cancels it. A map click keeps its existing meaning.
- `cursor_policy.lua` / `mouse_cursor.hpp` / `file_picker.mm`: hide the macOS
  cursor only inside the current gameplay viewport with fresh guest cursor
  feedback. Restore outside it, over menus/dialogs/letterbox, during overlays,
  focus loss/iconify, pause, file picker and finalization. AppKit hide/unhide
  calls are balanced and confined to the engine thread; absolute mouse
  coordinates are preserved. Other desktop cursor implementations remain D-034.

Pointer events carry both 960x720 design coordinates and physical window
pixels (`screen_x/screen_y`, bottom-left origin); the overlay uses the matching
window-size viewport. The original controller conversion was checked in pinned
ultramodern `input.cpp`. No graphics call moves to a guest thread.

The diagnostic layout uses normalized positions/sizes/opacity and per-contact
ownership. `sys.get_save_file` selects `sc64/touch-layout-v1`; `sys.save` in the
pinned engine writes a unique temporary file and publishes by rename. Save
failure leaves the editor open with a visible error. The optional pad defaults
to off and is disabled for GPU fixture probes.

## Build, run and reproduce original observations

```sh
# Once for a freshly generated Stage 1 configuration (private files only):
cp local/recompile/symbols-v12/sc64.toml local/recompile/symbols-v12/sc64-menu-v5.toml
python3 tools/input/add_ui_hooks.py --rom local/private/recompile/save-01/sc64-ntsc-u.z64 --config local/recompile/symbols-v12/sc64-menu-v5.toml
python3 tools/recompile/run_recompiler.py local/recompile/symbols-v12/sc64-menu-v5-ui.toml --replace
python3 tools/desktop/build.py
cmake --build local/build/desktop --target sc64_ui_observers_test sc64_desktop_bridge_test
ctest --test-dir local/build/desktop --output-on-failure
python3 -m unittest discover -s tests
python3 tools/desktop/run.py --rom /absolute/path/to/your.z64 --debug-input --output local/reports/input/gestures-new
python3 tools/input/probe_cursor.py --rom local/private/recompile/save-01/sc64-ntsc-u.z64 --flash local/captures/research/save-game-02/end.flash.bin --output local/captures/input/cursor-new
python3 tools/input/probe_ui.py --rom local/private/recompile/save-01/sc64-ntsc-u.z64 --flash local/captures/research/save-game-02/end.flash.bin --route saved --output local/captures/input/ui-new
```

Use a current twenty-observer config for later regeneration; `add_ui_hooks.py` refuses
duplicate hooks or overwriting its output. The script verifies the exact ROM
SHA-1 and all instruction words. The desktop builder refuses private generated
output missing any required menu, briefing, HUD or campaign observer. Recreate
older configs from the unhooked base before regeneration; the latest
private generation receipt now uses `sc64-menu-v5-ui.toml`, containing twenty
UI observers and thirty total hooks plus the symmetric-wrap instruction patch (ADR-037). Older `sc64-ui.toml` / `sc64-ui-v2.toml`
contain only menu observers; `sc64-hud-v3-ui.toml` lacks the four campaign observers:
start from an unhooked config with a new private filename, then use the current
`add_ui_hooks.py` and regenerate locally. The builder rejects missing observers or the unpatched campaign boundary.

To rebuild the current private configuration:

```sh
python3 tools/recompile/run_recompiler.py local/recompile/symbols-v12/sc64-menu-v5-ui.toml --replace
python3 tools/desktop/build.py
```

Original probe requires the pinned research ares runner prepared in Stage 0 and
the original Flash fixture containing the expected menu state. It cold-boots,
uses ordinary controller actions, captures canonical RAM/PNG pairs, and records
hashes in `cursor-observations.json`. Savestate restoration from the later scene
hung in the tested runner; the cold route is the working experiment. Wrong ROMs,
non-z64 input, incomplete Flash and non-private/existing output paths are rejected.
All retail-derived output stays under ignored `local/`.

For native steering diagnostics, the existing private `--inputs` JSON may also
contain `{ "field": 7500, "point": [220,100], "buttons": 32768 }`; this calls
the same bounded gesture controller, bypassing only OS pointer events. Such a
recipe proves the live feedback/controller path, not physical touch delivery.
The debug log records `SC64 pad` transitions after keyboard/touch ownership and recipe merge and their cursor/field provenance.
Interface recipes use `{ "field": 8300, "ui_click": [160,120], "when_ui": "message" }`;
`right: true` requests Back. Optional `when_token` restricts a frontend screen.
These events enter the same UI contact adapter; they still bypass OS input.
`SC64 ui_click` records the requested/actual field, source field and queue status.

## Verification and acceptance checklist

- [x] Original manual controls and 1P cursor addresses/code readers recorded in `research.md`.
- [x] Original direction/neutral experiments repeated with explicit normalized stick units.
- [x] Native extractor endian, coordinate consistency, NaN and short-snapshot regressions.
- [x] Gesture queue, A/B ordering, drag hold/release, two-finger tap, long press, cancellation and delayed nonlinear steering fixtures.
- [x] Optional pad ownership/layout/save-failure and actual host merge regressions.
- [x] Real desktop build and running presentation without permanent buttons.
- [x] Native live cursor arrival precedes A/B in a completed bounded recipe.
- [x] Original Z+stick camera mode verified with horizontal and vertical forward/backward captures.
- [x] Human desktop testing confirms cursor selection and contextual orders are usable (2026-10-08).
- [ ] Full drag/camera equivalence and physical touch delivery.
- [ ] Live camera pan, groups, all relevant modal/menu/build states and split-screen transforms.
- [ ] Physical gamepad hotplug; actual multi-touch/cancel, safe area and orientation.
- [ ] Complete Wasteland with touch gestures and diagnostic pad disabled.

Current completed fixture result: 91 Python tests, eight CTest tests. The initial
`touch-desktop-01` live pad/editor experiment passed event/persistence checks but
was rejected as a product UX. `gestures-native-01` lost focus before reaching
the mission; `gestures-native-02` reached the SCV advisor with numerically valid
but frozen cursor. Neither is successful live steering evidence. `gestures-native-03` lost focus before the map. `gestures-native-04` completed
a timed background input probe with unchanged binary/resources, no errors and
zero source drops. `check_gestures.py` accepted three target arrivals before
button output (A/B/A): errors (-0.320,-0.160), (+0.350,+0.240), (+1.300,-0.270)
native pixels, source age one guest field. These are live controller/feedback
results; the chosen terrain points do not establish unit selection or order
semantics. Physical input and mission acceptance remain explicitly false.

The launcher has `--background-input-probe` for timed recipes without lifecycle
probes. It ignores deactivation only in that diagnostic mode, still honoring
iconify/user pause. Both receipt and telemetry label it; `check_live.py` refuses
it as lifecycle acceptance. Ordinary interactive launch pauses missions on focus
loss; front-end screens continue with input blocked (ADR-041).

```sh
python3 tools/input/check_gestures.py local/reports/input/gestures-native-04
```

The earlier adapted desktop bundle was opened as `gestures-interactive-final-01`;
that session has now closed cleanly. Its logo screen had no permanent pad. CUA's
Tab injection did not produce an input event, so it does not establish live panel
opening; the actual GUI adapter/command transitions have fixture coverage.

## Interface adaptation acceptance

Scope: observed main/Single Player navigation, briefing Start, single-button
advisor dismissal, pause buttons and the build grid. Generic tree walking is
bounded to 64 immediate children and the observed visible/selectable button draw
callbacks. Nested menus, settings widgets and additional scene variants are not
automatically claimed as supported hitboxes; use the transient panel and extend
the original corpus when needed.

Acceptance checks for this slice:

- [x] Original UI states, widget rectangles/flags/callbacks and briefing mode are recorded with verification methods.
- [x] Observer tests prove that font/blit callbacks do not mutate guest RAM or registers, and do not reuse prior-frame hitboxes.
- [x] Tree reader rejects incomplete pointers/cycles and disabled controls; message identity fences reused dialog roots.
- [x] Actual GUI/host fixtures exercise HiDPI message/menu dispatch, early message taps, stale-frame cancellation, tab changes and grid selection before confirmation.
- [x] Completed native recipe proves frontend navigation, briefing transition, message dismissal and pause Resume using UI clicks.
- [ ] Physical mouse/touch verification of this updated bundle and full touch-only mission.

The Mac was locked during this slice's automation; no foreground screenshot or
OS pointer injection is treated as current physical-input evidence. Background
recipes retain the existing diagnostic-only lifecycle limitation.

```sh
python3 tools/input/check_ui.py local/reports/input/ui-native-06
```

The checker requires a completed unchanged-binary/resource receipt and matching
log/recipe hashes. It distinguishes selection-before-confirmation, message
dismissal and actual Resume-to-map from mere button output. A later keyboard
Start cannot certify a briefing click. Its own negative regressions reject
unqueued/stale clicks, missing dismissal, wrong message identity and a
confirmation supplied by a later click. Build selection-before-A is recorded
separately; insufficient resources may legitimately keep the original grid open.

Completed result: `ui-native-06/ui.json` accepts all six scoped gates, with two
distinct advisor IDs dismissed and build selection-before-A accepted. The route
uses clicks for the title, main menu, Single Player tabs, Wasteland row, mission
location card and text briefing. The two Start clicks (recorded before ADR-078, when a text-briefing click was Start) occur at fields 5000 and
5500; map/messages are reached before the first raw Start at field 8000, which
opens pause solely for the Resume experiment. Advisor IDs 230 and 7 return to
the map. Resume navigates selected buttons 0→1→2→3 and emits A at field 8354;
the map is observed at 8359. Build selection moves 4→3→0 before A at 8936.
A right UI click emits B at 9700 and returns to the map at 9702.

The 170-second run exits cleanly with unchanged binary/resources and zero
discarded source audio frames. Engine SHA-256:
`e540cc162a23f99e5fc38f4ac67715a9a444d3f22dd6a439a9bd28fa4ddd48e7`.
Log SHA-256: `f9a1db6cfa6f263a23c9256cfbe1ca304c3761c14e0026f1b5e3ea5985493ecb`.
The gate proves the live UI/controller path, not OS clicks, physical touch,
successful construction, settings/custom-widget coverage or mission completion.
The original full Stage 4 gate remained open at that milestone (now superseded by ADR-038). `ui-build-11.log`, `ui-ctest-05.log` and
`ui-python-tests-final.log` retain the local build/eight CTest/91 Python results.

## Clickable original hints and pressed feedback (2026-10-08)

Original `hud-02` observations and exact command/draw anchors are recorded in
`research.md`. The hint target combines the actually drawn icon with its text;
it is not an always-active bottom-right zone. In 1P gameplay, a press tints this
area gold at 30% opacity. Release sends its controller mask for six guest fields,
flashes for twelve fields, then enforces a neutral interval. Moving away,
another finger, a changed selection/label, stale frames, overlays, pause or
competing physical input cancels it. Neither A nor stick input is introduced
while pressing a command hint.

Completed native recipe: `local/reports/input/hud-native-04/hud.json`.
Selection reaches native (100.95,64.75) before A; the original image shows a
selected marine and Hold/Move/Stop. Clicks at fields 7700 / 8000 / 8400 emit
C-Left (2) / C-Right (1) / C-Down (4), each followed by release, with zero stick.
Observed hitboxes include Hold (201,178)–(240,190), Move (252,178)–(291,190),
Stop (231,187)–(259,208). No raw C mask in the recipe can supply this result.
The native cursor-arrival checker also passes. The 155-second run exits cleanly,
with unchanged binary/resources and zero discarded source audio frames.

- Engine SHA-256: `0bd9ea454ee35c051f1c2afffe97f2917f95b6c41a7de95d120752b2b38625ba`.
- Log SHA-256: `83e2c926fefbdf0ef923a850d5541f8902f33ced2663edcb233f31836a49b5fe`.
- Recipe SHA-256: `fbfeea4d0a89ac238703bf820ab4a015de1b55c64cd68a267cffa7b3782e4b56`.
- HUD report SHA-256: `c7eb38e55963fdcdf76f62f5a45b33fa6e1b5691926f091016185337a5f461b0`.

Reproduce against a freshly built, unchanged bundle:

```sh
python3 tools/desktop/run.py --rom local/private/recompile/save-01/sc64-ntsc-u.z64 --inputs local/private/input/hud-inputs-03.json --seconds 155 --debug-input --background-input-probe --output local/reports/input/hud-new
python3 tools/input/check_hud.py local/reports/input/hud-new
python3 tools/input/check_gestures.py local/reports/input/hud-new
```

`hud-build-04.log`, `hud-ctest-05.log` and `hud-python-final.log` retain the local
build, nine CTest and 93 Python results. GUI fixtures exercise the actual GUI
dispatch/update functions and verify highlight geometry in a 1920x1440 physical /
960x720 design viewport, removal on label loss, controller masks, movement and
multi-contact cancellation. These are not rendered-pixel or physical-touch proof.
Native telemetry confirms cursor-hidden false in the frontend/briefing and true
over the map; AppKit balanced visibility calls compile against installed headers.
Automated OS clicks still delivered stale engine coordinates, so they do not
certify hint activation or actual cursor appearance. Human mouse/physical touch,
all building contexts, C-Up/B native cases, split-screen, successful construction
and touch-only mission completion remain open (D-040).

Slice acceptance checklist:

- [x] Original icon/text bounds and C/B masks have source/original evidence.
- [x] Current native single-unit C-Left/C-Right/C-Down clicks emit one matching
  controller pulse without cursor movement.
- [x] Highlight geometry and stale/context/contact cancellation pass GUI fixtures.
- [x] macOS cursor backend preserves absolute input; lifecycle/viewport guards
  and gameplay/frontend visibility state have fixture/native telemetry evidence.
- [ ] Human verification of the updated hint highlight and cursor appearance.
- [ ] All unit/building contexts and physical multi-touch behavior.
- [ ] Touch-only Wasteland completion. Deferred to Stage 5 (D-041; ADR-038).

The previous publication used `hud-build-final.log`, `hud-ctest-final.log` and
`hud-python-final-02.log` (nine CTest / 93 Python, all passing). Published engine
SHA-256: `57d25f56a7e62410faaf178dfafbee323498d1cc4e6943e51d4fb6a2e77a4f0f`.
The completed `hud-native-04` receipt above remains bound to its earlier binary
and resources; it is not relabelled as evidence for another hash. After that run,
the GUI adapter gained a targeted middle-button guard so dragging over a hint
still pans the camera; its actual dispatch fixture passes. That bundle was
opened in `hud-interactive-final-01`, without a background/lifecycle bypass.
The interactive process was interrupted before the routing-fix rebuild; its
log is not a completed acceptance receipt.

## Construction dismissal and hint routing fixes

Click/tap outside the observed build-panel rectangle now closes it through B,
waits for original game feedback and controller release, then selects at the
requested point through the existing cursor/A path. Taps on gaps or disabled
slots inside the panel stay consumed. A different root/dialog, stale feedback,
pause, timeout or a new click cancels the pending selection.

Hover and presses on the actually painted command canvas no longer steer or
click the map underneath it. This includes its transparent padding and right
clicks. Commands preserve the last map cursor target. Original dimming remains
visible but does not disable a displayed command. Font checksums are captured
after the whole canvas is painted; positive fade-in alpha below 64 is accepted.
Cancelled contacts retain ownership until release, and fast taps retain separate
controller edges with a neutral interval. The gold pressed/activation feedback
remains tied to the current label and selection.

The following HUD run is historical. For a current regeneration, use the v5
configuration in the primary build instructions above. The HUD recipe/checkers
can then be repeated in a fresh private report directory:

```sh
python3 tools/recompile/run_recompiler.py local/recompile/symbols-v12/sc64-menu-v5-ui.toml --replace
python3 tools/desktop/build.py
cmake --build local/build/desktop --target sc64_ui_observers_test sc64_desktop_bridge_test -j8
ctest --test-dir local/build/desktop --output-on-failure
python3 -m unittest discover -s tests
python3 tools/desktop/run.py --rom local/private/recompile/save-01/sc64-ntsc-u.z64 --inputs local/private/input/hud-routing-inputs-01.json --seconds 175 --debug-input --background-input-probe --output local/reports/input/hud-routing-new
python3 tools/input/check_hud.py local/reports/input/hud-routing-new
python3 tools/input/check_gestures.py local/reports/input/hud-routing-new
```

Use a fresh config/report name if these already exist; hooked configs are not
hooked a second time. Personal recipes, generated C and ROM stay ignored.

Completed `hud-routing-native-02`: the (100,64) outside build tap at field 9000
emits B at 9001, A at 9021 in game mode with fresh cursor (99.5,63.5), then release.
Earlier Hold/Move/Stop and subsequent Hold clicks all use matching zero-stick
controller pulses, with the cursor held away from the hint. Both native checkers
pass. `hud-routing-native-01` failed graphics-adapter initialization inside the
sandbox; it is not runtime acceptance. The completed run used desktop graphics
access and a bounded background-input probe, not a lifecycle test.

- Engine SHA-256: `a04b47fc291e19e24ef220222011af9b9b23bd5fcef928f626b40df715cf0d43`.
- Log SHA-256: `36a61bdff900c2b68ff2aae8dd5522a1e9df2c1bd196c79d87bfdb5ab1a4de6f`.
- Recipe SHA-256: `d1565d8aecfd23c366a22bed2b682dcab4a686815c43b1c41a2618936c734282`.
- HUD/replay report SHA-256: `86cb5a7081b6e41acee94cd2d71931ffb7e1540c68a50520689b75fd9f5f1f22`.

`hud-routing-build-01.log`, `hud-routing-ctest-final.log` and
`hud-routing-python-final.log` retain successful build / nine CTest / 95 Python
results. Fixtures cover real GUI mouse hover/press and touch build-tap dispatch,
padding, overlapping later paint, alpha 3/64/255, stale/context changes, cancelled
contact release, rapid repeated taps, replay cancellation and malformed bounds.

- [x] Outside build tap emits B, releases it, then A at the requested point using fresh native feedback.
- [x] Hold/Move/Stop clicks preserve the map cursor and emit matching native pad edges.
- [x] Actual GUI dispatch fixtures reject terrain actions beneath hints/padding.
- [x] Regression gates reject raw-input contamination, stale arrival and missing release.
- [ ] Human mouse verification on another unit/building and physical touch on all relevant hints.
- [ ] Full touch-only first mission. This historical slice did not close the then-current Stage 4 gate; ADR-038 records current closure.

That verified bundle was reopened in `hud-routing-interactive-01` with debug
input and without the background-input probe, and closed before the next rebuild.
Native run `hud-routing-native-02` remains the scoped
acceptance evidence for this engine/resources.

## HUD aiming and free mouse movement correction

Over the painted command panel, use the visible OS pointer to aim; the original
game cursor retains the last map location for orders. A hint under the mouse has
a soft gold highlight, which becomes stronger on press. The map continues to use
the original game cursor. Visibility is decided from actual macOS pointer
coordinates and projected native HUD bounds, including letterboxing/HiDPI.
Physical touch has press feedback but leaves no persistent hover after release.

Free mouse movement on the map now continuously updates the emulated stick;
it no longer uses the queued-click path's four-on/eight-off cadence. Current
original cursor divisor and latency-aware owned snapshots determine the stick
input; the desired displacement is capped at three native pixels per field
(later 8 px/field for mouse hover and 16 for touch/actions/drags, ADR-066).
Queued selection/drag arrival and all menu/HUD button adapters are preserved.

Validation: `hud-pointer-build-01.log`, `hud-pointer-ctest-01.log` (nine CTest),
`hud-pointer-python-final.log` (98 Python tests), all passing. Lua fixtures cover
150/200 divisors, zero/one/three-field snapshot lag, target changes, convergence,
step cap and absence of long neutral hover gaps. Native fixtures cover divisor
read bounds and HUD-to-content-view projection. Actual GUI dispatch checks soft
hover/strong press colors and separation from physical touch.

Completed `hud-pointer-native-01`, 185 seconds, passes `check_hover.py`,
`check_hud.py` and `check_gestures.py`. Five map target windows have 31–40 sampled
owned cursor positions each, max observed rate 3.0932 pixels/field and arrival
within 1.5 pixels. The two consecutive HUD hover targets are consumed with zero
stick and almost unchanged guest cursor in three bracketing/held snapshots.
This is sampled telemetry, not a frame-by-frame motion capture or OS-event test.

Those figures (3.09 px/field, former 3.5 cap) predate ADR-066. `check_hover.py`
schema 3 uses the ADR-066 hover servo: speed cap 8 px/field (0.5 margin), a zero
stick far (>8 px) from the target for at most 6 fields (horizon 3 + snapshot age
3; measured as the time to the next pad row because unchanged input is not
logged), and at most one 8 px step of retreat from the closest approach. The
SDL3 shell run `hud-04` (2026-10-11) accepts all six windows (max 8.15 px/field,
HUD targets intercepted); see D-055 and D-056.

Since ADR-094/095/098 the mouse in a 1P mission positions the original cursor directly
through the guarded original setter, and the stick steering above is its fallback (direct
path off, not yet acknowledged, or a selection drag at an image edge). The fallback hover cap
is 16 px/field with the ADR-066 horizon 3; horizon 1 hunted around a still target. A released
mouse drag ends where the cursor is, because the original stops a held-A cursor at x 304 and
scrolls instead. `check_hover.py` schema 4 accepts direct windows (arrival within the request
age plus snapshot age, neutral or edge-scroll stick, cursor held at the target) and stick windows
(cap 16 + 0.5, plateau ≤ 3 + 3 fields, retreat ≤ 16 px). The `hud-04` figures above predate it.

- Engine SHA-256: `784211838dd8cbe58f86e681d93f2801fb44f8682e4d0d8c004699026bb8f139`.
- Log SHA-256: `947603df7912714b9b70f37992fd6b30fa775ce7be9110765e517c382370537c`.
- Recipe SHA-256: `88bc66686c9acf249a206cea59eda4f4382ea659f2752c0c96dd767026c1fbd2`.
- Hover report SHA-256: `79b731b96c05486661339b54f10d284a6961db73cdb77032a2346d5225f0e385`.

```sh
python3 tools/desktop/build.py
python3 tools/desktop/run.py --rom local/private/recompile/save-01/sc64-ntsc-u.z64 --inputs local/private/input/hud-pointer-inputs-01.json --seconds 185 --debug-input --background-input-probe --output local/reports/input/hud-pointer-new
python3 tools/input/check_hover.py local/reports/input/hud-pointer-new
python3 tools/input/check_hud.py local/reports/input/hud-pointer-new
python3 tools/input/check_gestures.py local/reports/input/hud-pointer-new
```

- [x] Continuous native free-hover target arrival with bounded sampled movement.
- [x] HUD hover consumes input and preserves the map cursor in the native recipe.
- [x] Projected pointer exclusion and hover/press/touch feedback fixtures pass.
- [x] Hint and construction replay gates still pass on the updated bundle.
- [ ] Human verification of OS-pointer appearance and comfortable aiming at bottom-right hints.
- [ ] Physical touch/all contexts/full first mission. This historical slice did not close the then-current Stage 4 gate; ADR-038 records current closure.

The same bundle is opened for human checking in `hud-pointer-interactive-01`
without a background-input/lifecycle bypass. Its ongoing log is separate from
the completed recipe receipt above.

## Review remediation (2026-10-08)

The HUD adapter now retains up to eight pending hint actions. Each accepted
release produces a separate six-field controller press and twelve neutral
fields; completion removes only the HUD state and preserves a newer map hover.
The same freshness, label, selection-context and contact-cancellation rules
apply to the whole queue. Lua regressions cover three quick taps, the neutral
interval, stale-queue cancellation and a map move during HUD cooldown.

`SC64 pad` is the final controller submission after keyboard/touch ownership,
gestures, private recipe merges, pause handling and clamping. It carries player
and source flags. All native input checkers reject `SC64 gesture` candidate-only
logs. The historical reports above are retained as historical observations;
they do not certify the corrected submission stream. That remediation used
`sc64-hud-v3-ui.toml` with fourteen UI observers (twenty-four total hooks).
Current regeneration requires the v5 campaign observers and wrap patch described above.

Desktop presentation now observes VI black/origin/width independently of new
DLs and selects a completed matching framebuffer. A black interval or mode
change prevents reuse of a pre-transition image. This closes confirmed unsafe
scanout paths. The later investigation below finds additional coloured noise
before blanking: VI association alone did not certify the fade-out. Full VI
filtering and broader scene acceptance remain D-037. See ADR-033 and the
mission-exit investigation in research.md.

To capture a bounded personal transition diagnostic:

```sh
python3 tools/desktop/run.py --rom /absolute/path/to/your.z64 --storage local/private/input/transition-storage --inputs local/private/input/transition-inputs.json --seconds 206 --debug-input --background-input-probe --trace-from 11550 --trace-to 12000 --output local/reports/input/transition-new
python3 tools/input/check_hud.py local/reports/input/transition-new
python3 tools/input/check_scanout.py local/reports/input/transition-new
python3 tools/input/check_fade.py local/reports/input/transition-new
```

Copy the personal save into a fresh private storage directory when reproducing
the saved mission route. Keep the user's ordinary save intact. The recipe field
range is route-specific. GPU readback can lag the current VI on some backends;
inspect the bounded sequence and keep native-target, scanout-state and window
pixel evidence distinct. This diagnostic is not lifecycle or physical-touch
acceptance.

Remediation acceptance:

- [x] Three quick clicks preserve all controller edges and full neutral gaps.
- [x] HUD completion retains a newer map hover; native queued-hover steering
  resumes after the final neutral interval and approaches the requested point.
- [x] Final input logging matches actual keyboard/recipe/pause arbitration;
  candidate-only legacy evidence is rejected.
- [x] That remediation required v3 and fourteen UI observers; current instructions
  additionally require the four campaign observers and the v5 wrap patch.
- [x] Owned VI updates suppress presentation without a new DL; target selection
  rejects offscreen/future/pre-transition images and wrong extents/modes.
- [x] Completed native abort has matching target/VI association. Its late window
  samples do not certify the earlier fade-out; the additional pixel gate below
  rejects this historical run.
- [x] 102 Python tests and nine CTest gates pass; the published bundle passes
  all twenty exact software-baseline/Vulkan native-target RGB cases.
- [ ] Separate victory transition and complete VI scanout parity (D-037).
- [ ] Human recheck and full Stage 4 touch-only mission acceptance (D-040).

Completed `review-fixed-native-02` passes final submitted-input hint/FIFO,
build-dismissal/replay, cursor-arrival and scanout-association gates. The inserted
hover during the pending HUD queue intentionally waits for its controller
pulses; it is checked by the queue-resume gate. The general continuous-hover
checker is not a certificate for this pending-action interval or for a duplicate
stationary target with no new samples. Keep those scopes distinct.

The historical `review-fixed-gpu-02` bundle includes the later status GUI
correction for the no-guest fixture mode and passes twenty exact RGB cases.
The native abort run is bound to the preceding bundle; the only subsequent
product edit is the Paused-text condition when no guest runs. Their exact
engine hashes and rejected diagnostic runs are recorded in research.md.
`review-fixed-interactive-01` opens the final bundle with ordinary personal
storage, no recipe and no lifecycle bypass; it remains an interactive run,
not a completed acceptance receipt. This historical slice did not close the then-current Stage 4 gate; ADR-038 records current closure.

## RDP memory preservation and map-to-results fade correction

The user locates the remaining flash between the map and Victory/Defeat.
Native captures from the previous accepted association run show coloured noise
in the lower half before VI black. Software RDP output had been used only for
presentation: later tasks restarted from guest RAM without previous RDP stores.
Partial redraw and memory blending consequently lost the framebuffer contents.

Every live software task now completes rasterization on the submitting guest
thread and commits only its actual written byte ranges to word-swapped RDRAM.
Mailbox eviction can skip presentation while retaining all memory effects.
The presentation step uploads the owned completed result; all graphics calls
remain on the shell's main thread. Offline/GPU fixtures never commit to guest RAM.
ADR-035 records the synchronous timing/performance tradeoff.

Completed `rdp-writeback-native-01` repeats the saved Abort Mission -> Defeat
route: the map remains behind the confirmation dialog, dims normally and reaches
black without the earlier lower-half noise. The upload mosaic and final fade
gate are separate from exact VI/window parity. `check_fade.py` requires at least
three distinct terminal tasks with RGB <= 1 in the six fields before VI blank;
the original terminal screenshots establish that threshold. The old run fails
this gate; the corrected run passes with RGB maximum 0 in all six sampled
uploads. `check_hud.py`, `check_gestures.py` and `check_scanout.py` also pass.

Build and verify the changed paths locally:

```sh
python3 tools/desktop/build.py
ctest --test-dir local/build/desktop --output-on-failure
python3 -m unittest discover -s tests -p 'test_ui_tools.py'
python3 tools/input/check_fade.py local/reports/input/rdp-writeback-native-01
python3 tools/desktop/check_gpu.py local/reports/desktop/rdp-writeback-gpu-01/gpu
```

These report names identify retained completed runs; use a fresh report and a
copied private save for a new capture using the transition command above.
Both completed native/GPU runs use engine SHA-256
`77555942d0dd4f84fff63411d1c5cc6b3f5c9c13df071f16fa9d870dd9d4a8a2`.
Exact private evidence hashes are recorded in research.md.

- [x] Actual RDP stores survive a discarded presentation; unrelated CPU bytes
  and alpha-rejected pixels remain untouched, and invalid ranges fail atomically.
- [x] All nine CTest gates and eighteen targeted UI-tool Python tests pass.
- [x] Completed native abort preserves the map during fade-out; the terminal
  pixel gate detects the old defect and passes the correction.
- [x] The same published bundle passes all twenty exact RGB GPU fixture cases.
- [ ] Separate Victory transition, complete VI scanout parity and CPU-only
  scanout remain D-037.
- [ ] Android Back button/gesture delivery still needs hardware verification.
- [ ] Recorded physical-touch mission and other Stage 4 contexts remain D-040;
  the user's mouse-only mission completion is retained as human-reported evidence.

The corrected bundle is reopened in `rdp-writeback-interactive-01` using the
user's ordinary private storage, without an input recipe or background-input
bypass. Its visible running window is an interactive handoff, not a completed
acceptance receipt.

## Main-menu campaign mouse/touch controls

The bottom carousel now supports a horizontal drag/swipe: left brings the next
right-hand campaign to the centre; right returns to the previous campaign.
Both directions wrap through the edge of the six-campaign ring. A click/tap on
an inactive side campaign chooses the shortest circular route to that exact ID:
one step between first and last, at most three otherwise; ties choose Left. It waits for each animation to finish
and each selection to be acknowledged. Arrival does not enter the campaign.
Click/tap the central selected hero/platform to enter through Single Player,
even when Credits or another top button was previously selected.

When the yellow episode-order warning is visible, a stationary click/tap inside
its rendered sprite rectangle submits original Start. Outside submits original
B and keeps the current campaign. Underlying carousel/top buttons are blocked.
The warning's position/size and campaign geometry come from the presented
original blits, not host pixel constants. Viewport scaling/letterboxing therefore
apply consistently to both mouse and touch (ADR-036/ADR-037).

One swipe emits one directional pulse (four fields held, four neutral since ADR-067);
it never becomes an activation. Side selection waits up to 180 fields for each
step and at most 1080 fields overall. A new valid contact can replace navigation
after its neutral interval. Stale metadata, changed foreground, competing input
ownership and contact cancellation discard pending actions. Long carousel or
warning contacts do not open the generic radial controller fallback.

The current local config is `sc64-menu-v5-ui.toml` with eighteen UI observers
and twenty-eight total hooks plus one guarded campaign-wrap instruction patch.
The original positive-direction zero-ID boundary is replaced with NOP only in
the private generated game; the supplied ROM is unchanged. Regenerate from the unhooked base using the primary
instructions above, then build/run with the supplied personal ROM. To reproduce
the original menu research with the pinned ares runner and a personal Flash file:

```sh
python3 tools/input/probe_ui.py --rom /absolute/path/to/your.z64 --flash /absolute/path/to/your.flash.bin --route campaign --output local/captures/input/campaign-new
ctest --test-dir local/build/desktop --output-on-failure
python3 -m unittest discover -s tests -p 'test_campaign_tools.py'
python3 -m unittest discover -s tests -p 'test_ui_tools.py'
python3 tools/input/check_campaign.py local/reports/input/campaign-wrap-native-02
```

The last command checks the retained completed final native run. For a fresh
capture, copy the personal save into a new ignored storage directory and use a
new `--output` directory. The retained private recipe uses an intro click, both
side selections, both swipe directions, central entry, outside cancel, central
entry again and inside continue. It contains no raw controller event in the
campaign-action intervals:

```sh
python3 tools/desktop/run.py --rom /absolute/path/to/your.z64 --storage local/private/input/campaign-storage-new --inputs local/private/input/campaign-wrap-inputs-01.json --seconds 84 --debug-input --background-input-probe --trace-from 4200 --trace-to 4700 --output local/reports/input/campaign-native-new
python3 tools/input/check_campaign.py local/reports/input/campaign-native-new
```

Field timing depends on the route/Flash state. Recipes, snapshots, native uploads
and generated C remain under ignored `local/`; the normal interactive launcher
does not enable a recipe or background-input bypass.

- [x] Both swipe directions change original campaign selection; release never
  enters a campaign accidentally.
- [x] Both endpoint side clicks reach the requested campaign in one step; all
  30 distinct ordered pairs choose the shortest route in fixtures, with no A/Start on arrival.
- [x] Central click uses original A and the selected campaign's entry rules.
- [x] Inside warning submits Start and reaches the Episode III list; outside
  submits B and returns to the same campaign.
- [x] Frame-owned blit geometry, read-only observers, native-coordinate GUI
  routing, stale/context cancellation and bounded waits have regression coverage.
- [x] Final published bundle passes the completed eight-gate native campaign
  recipe; ten CTest gates and the targeted Python gates pass.
- [ ] Physical multi-touch/Android delivery and all later menu contexts remain
  D-041/D-042 under ADR-038; desktop closure does not certify them.

The earlier `campaign-native-03` report proves arrival and warning handling,
but its five-step endpoint traversal is historical and fails the updated
shortest-route gate. Its receipt is preserved. `campaign-interactive-02` was
closed after a rebuild changed its bundle; it is not completed acceptance
evidence. Current native evidence and interactive handoff are recorded below.

Pre-review completed receipt (preserved): `local/reports/input/campaign-wrap-native-02/campaign.json`.
The clean 84.48-second run passes eight native recipe gates, including one
Right for 0->5 and one Left for 5->0, both followed by release and ready feedback.
The checker also rejects the retained historical five-step native route.
Ten CTest checks and all 111 Python tests pass. ROM/generated C remain ignored.
Pre-review native engine SHA-256:
`299d7137010dc99c62b21cabdb919e9abb7de4bc2b07a42bcb806f5dbf200f8a`.

`campaign-wrap-interactive-01` opened that bundle with ordinary personal storage
and no recipe/background-input bypass. This running handoff is separate from
completed native recipe evidence. For a manual check: click the left campaign
from Episode I to reach the last episode in one animation, then click the right
campaign to return to Episode I in one animation. Selecting alone must not enter
it; click the centre to enter. Original warning inside/outside actions remain
Start/B. Android hardware and touch-only mission acceptance remain open.

## Review remediation (2026-10-08)

ADR-039 fixes three findings without reopening the accepted desktop scope:

- Each cached scanout image owns its cursor and UI metadata. Engine update
  publishes feedback from the VI-selected image, even when no new display list
  arrives. Future and offscreen targets retain their own feedback without
  replacing the visible image's geometry. Blank/unmatched VI clears feedback.
  Field stamps are preserved, so old images cannot bypass freshness checks.
- Middle-button camera drags bypass HUD routing for unnamed mouse-move events
  as well as button events. Hint text and transparent HUD padding no longer
  freeze the pan displacement. A foreground modal still cancels camera input.
- A released selection has a 360-field completion deadline independent of its
  consumed start action. Reaching the endpoint still releases A normally. A
  stuck endpoint cancels the selection, clears steering/queued actions and
  emits eight neutral fields. A physically held drag has no release deadline.
  The budget is host policy, not an original-game fact.

Regression coverage in `tests/native/test_desktop_bridge.cpp` verifies retained
image metadata for offscreen/future tasks and promotion when VI acknowledges
the future task. `tests/native/test_touch_input.cpp` exercises the actual GUI dispatch
for unnamed movements across a hint and HUD padding, button release and modal
cancellation. `tests/native/test_gestures.cpp` covers mouse/touch, release before A
starts, a long physically held selection, immobile but fresh cursor feedback,
deadline cancellation and a subsequent successful click. These controlled
fixtures do not claim that the original game always stalls at those points.

Build and regression commands:

```sh
python3 tools/desktop/build.py
cmake --build local/build/desktop --target sc64_desktop_bridge_test -j8
ctest --test-dir local/build/desktop --output-on-failure
python3 -m unittest discover -s tests
```

The private application is rebuilt locally in `local/dist/StarCraft64.app`.
Build/Python logs: `local/reports/input/review-remediation-build-01.log` and
`review-remediation-python-01.log`. Ten CTest gates and 111 Python tests pass.
The native campaign recipe is rerun against the rebuilt bundle, with a separate
storage directory seeded from the retained pre-warning original-menu Flash
snapshot. Do not use the mutable save left by a completed campaign run as its
starting fixture: the warning-dependent acceptance route may differ.

Completed final run: `local/reports/input/review-remediation-native-02/run.json`
and `campaign.json`. It runs for 84.74 seconds, exits zero, preserves the engine
and resource hashes and passes all eight campaign gates: both swipe directions,
side selection, central entry, warning cancel/continue and both shortest
endpoint routes. Final engine SHA-256:
`5b51e3789840d16512beb96d61de76d7077e373f12c9e0ea86c8740b876e5ed5`.

The retained starting Flash is
`local/captures/input/campaign-ui-02/main.flash.bin`, SHA-256
`c4170148c8133ad5308d7404f47fd1d60d0317752bd9f21361945e346202f66e`.
Copy it into a new ignored storage directory as `sc64.fla`, then run:

```sh
python3 tools/desktop/run.py --rom /absolute/path/to/your.z64 --storage local/private/input/review-storage-new --inputs local/private/input/campaign-wrap-inputs-01.json --seconds 84 --debug-input --background-input-probe --trace-from 4200 --trace-to 4700 --output local/reports/input/review-native-new
python3 tools/input/check_campaign.py local/reports/input/review-native-new
```

`review-remediation-native-01` is retained as a rejected acceptance attempt:
it exits zero but its mutable post-run Flash seed bypasses the episode warning,
so the warning gates fail. It is not evidence for warning handling. Ordinary
personal storage is untouched by these two runs. Native menu regression does
not substitute for the deliberately stuck cursor and future/offscreen-task
fixtures or Android hardware testing.

- [x] Visible input geometry follows the selected cached scanout image.
- [x] Middle-button movement and release work over hint/HUD padding; foreground
  modal handling still stops camera input.
- [x] Released mouse/touch selections terminate with bounded neutral recovery,
  while long held drags and normal endpoint completion remain covered.
- [x] Personal local native build, ten CTest gates, 111 Python tests and the
  completed eight-gate native campaign regression pass.
- [ ] Physical multi-touch/Android delivery remains Stage 5 work (D-041/D-042).


## Build-grid pointer correction (2026-10-08)

The reported upper-left Overlord failure is explained by the bridge treating
the build grid as an ordinary menu and excluding disabled cells.
The original grid can highlight those cells. Losing the selected intermediate
item made `ui_input.read` unable to acknowledge the prior analog pulse or choose
the next direction. ADR-040 retains all researched visible build cells, reads
the selection used by the draw callback and sets `activate=false` for disabled
cells. Clicking them displays the original requirement through selection; the
adapter sends A only after reaching a requested available cell.

Build and test commands:

```sh
python3 tools/desktop/build.py
cmake --build local/build/desktop --target sc64_desktop_bridge_test -j8
ctest --test-dir local/build/desktop --output-on-failure
python3 -m unittest discover -s tests
```

The personal macOS bundle was built locally with engine SHA-256
`159dbbd4814eebb76df1ec42afa23a2d4798e797b055f5a59a3b164552ee7459`.
Build log: `local/reports/input/build-grid-build-01.log`. All ten CTest gates
pass, including bridge disabled/hidden flags, all 81 ordered grid routes for
both mouse and touch, delayed acknowledgements, neutral intervals, repeat
clicks and actual GUI dispatch through the grey middle-left cell. All 111
Python tests pass (`build-grid-python-01.log`). No new engine API or guest write
is introduced.

Private native recipes:

```sh
python3 tools/desktop/run.py \
  --rom local/private/recompile/save-01/sc64-ntsc-u.z64 \
  --storage local/private/input/build-grid-native-storage-01 \
  --inputs local/private/input/build-grid-native-inputs-01.json \
  --seconds 160 --debug-input --background-input-probe \
  --trace-from 8050 --trace-to 8550 \
  --output local/reports/input/build-grid-native-01
```

Use a new output and fresh copied storage when repeating; the recorded seed is
`local/captures/input/hud-02/main.flash.bin`. Completed native runs 01 and 02
exit zero with unchanged engine/resources and accept three consecutive grid
clicks each (`grid.json`). Run 01 lasts 162.85 seconds. Run 02 additionally
submits a cursor action before opening the grid. Neither run observes a disabled
native cell or proves a Larva/Overlord hit or construction. Their scope is
native pulse/acknowledgement routing and repeat-click regression; the exact
disabled-cell defect is covered by original source and controlled fixtures.

Original controller-only `build-grid-01` captures confirm signs and selection
4 -> 3 -> 0 -> 1 -> 4. The copied user-save probe remains in the frontend and
the first Zerg probe stops in an advisor, so neither is native disabled-cell
acceptance. Their private observations are retained without that claim.

Acceptance and manual verification:

- [x] A visible disabled intermediate cell remains in feedback and navigation.
- [x] Mouse/touch upper-left clicks continue from centre through middle-left.
- [x] Confirmation occurs only at the requested available cell; a disabled
  target receives selection without A.
- [x] Subsequent clicks work after disabled-cell arrival in controlled tests;
  consecutive native grid requests also complete.
- [x] Local build, ten CTest gates and 111 Python tests pass.
- [ ] Exact Larva/Overlord scenario in the user's active mission is not captured
  by the native recipes; device/more-context verification remains D-042.

Restart the personal app to load the new engine. In the reported Zerg context,
open the grid, click Overlord in the upper-left, then another available cell.
Also select a grey prerequisite cell: the original requirement should appear,
and a following available-cell click should still navigate. The existing
running process is intentionally left intact; rebuilding does not hot-reload it.

## Mission-only focus pause (2026-10-08)

The former host paused on every deactivation, including menus and the intro.
ADR-041 restricts that cause to guest mode 3, using owned task snapshots rather
than live guest memory. Build grids and mission dialogs retain the same rule.
Entering a mission while inactive also pauses when its first valid task is
consumed. A missing/blank presentation image does not clear the last known
mission state. Manual pause and desktop iconify still freeze every screen.

The extension exposes `input_active` through `sc64.get_info()`. Unfocused
front-end screens run with neutral native pads; Lua rejects and clears keyboard,
pointer and virtual-pad work so refocusing cannot replay held input. The bounded
background recipe diagnostic remains separately labelled.

Validation: `python3 tools/desktop/build.py` succeeds; all ten CTest checks pass.
The bridge test exercises front-end, mission, unknown-frame retention, combined
manual/focus/iconify causes and the diagnostic exception. Lua tests exercise
neutral background output and discard/recovery without stuck buttons.

`local/reports/input/focus-menu-native-01/run.json` binds a 50-second native run
to engine SHA-256
`ac75bc8c0b106022cbe189669dc62d96ba347558b5ed4b2077efa933ed54a519`.
Its generated `focus.json` records a real deactivation event, continuing guest
mode-4 intro counters with `input_active=false`, manual pause from script seconds
25 to 35 with frozen fields/tasks/samples, and progress after resume. There is
no background diagnostic override; exit is 0 and binary/resources are unchanged.
This run covers the intro and manual pause, not native mission deactivation or
physical Android suspension. The latter remains D-041.

- [x] Focus loss outside a mission keeps guest timing/audio running, input neutral.
- [x] Mission/build/dialog focus pause and independent cause precedence pass policy tests.
- [x] Manual pause freezes/resumes in the native background intro run.
- [x] Personal bundle rebuilt and input regressions pass.
- [ ] Repeat physical mission focus/iconify and Android lifecycle checks during
  the expanded D-041 device gate; this receipt is not their certification.

Restart the personal app to load the new engine. For an interactive check,
switch to another app from the menu, then from a mission: only the latter should
freeze. Return focus to resume; a manual pause must still require manual resume.


## Mission Back and resource-tap follow-up

The system Back aliases now open the mission menu on a fresh plain map (original
Start), and go back from build/advisor/pause/front-end menus (original B). The
press keeps its chosen mask until released. The controller's original B remains
a contextual order. This replaces the earlier unconditional B mapping.

Select a worker with a normal tap/click, then tap/click a mineral container or own
resource structure. After the original cursor reaches the point, the game's
own hit-test result selects B for a worker/resource combination. A tap on another
unit still selects it with A. Explicit right-click/two-finger orders and rectangle
selection are unchanged. Minimap clicks remain A even when they bypass unit hit
testing; missing/stale pick feedback must not turn into an accidental selection.

Personal builds must regenerate from the unhooked base with the current
`tools/input/add_ui_hooks.py`: twenty-three UI observers, thirty-three total hooks
(ADR-073 adds three Encyclopedia link observers; the local config is
`sc64-menu-v6-ui.toml`). The older text below counts twenty and thirty. The local
config is `local/recompile/symbols-v12/sc64-menu-v5-ui.toml`; its two new cursor hooks
are ROM/instruction guarded. Use the documented `run_recompiler.py --replace`
command followed by `make build-desktop`, `make test`, then
`make build-android BUNDLE_ROM=1 ROM=/absolute/path/to/your.z64` for the personal
bundled Android variant.

Acceptance for this follow-up:

- [x] Fixture: Esc/Android Back opens a map menu and sends B on a subsequent modal press.
- [x] Fixture: a held Back cannot become B when the menu appears; raw B stays B.
- [x] Fixture: own worker/resource taps use B; friendly-unit/terrain/foreign-selection hits use A.
- [x] Fixture: stale/context/point mismatches defer; rectangle selection and explicit B remain intact.
- [x] Fixture: observers change neither guest RAM nor guest registers; bounds/profile gates reject unsupported state.
- [x] Native mission menu opens/closes through the contextual Back aliases.
- [x] Native worker/resource tap retains worker selection and starts original gathering.
- [ ] Physical Android Back and touch harvesting on the user's smartphone.

Native Back evidence: `local/reports/android/back-gather-probe-02/run.json`
binds a 150-second desktop run (exit 0, engine/resources unchanged). Its
`back-summary.json` records Esc through the key-down adapter sending Start at
field 7800, the displayed mission pause menu, Android Back through that adapter
sending B at field 8080, and fresh game feedback at field 8086. The private
recipe bypasses OS delivery; the rendered `frames/menu.png` verifies the menu
visually. This is not a physical Android Back/gesture acceptance result.

Native harvesting evidence: `local/reports/android/back-gather-native-06/run.json`
binds a 245-second desktop run, exit 0, engine/resources unchanged. Its
`harvest-summary.json` binds the A selection of original type 7, the automatic B
order on original mineral type 176 and retained selection. Tutorial messages are
dismissed through the existing UI adapter. Native upload frames at fields 14000
and 14600 show minerals 80 -> 88 (`gather-before.png` / `gather.png`, visually
reviewed; no OCR claim). The recipe issues no direct state/order/resource writes.
This verifies original gathering after the tap translation on Wasteland; gas,
other worker races and actual phone events remain outside this native receipt.

Final regression logs: `local/reports/back-gather-python-03.log` (134 checks)
and `local/reports/back-gather-native-tests-02.log` (12 CTest checks). The bundled
Android rebuild and outstanding physical-device acceptance are recorded in
`android.md`.


## Command-hint gesture pass-through follow-up

Command hints now capture taps while permitting sliding map gestures. A map
selection/two-finger contact continues through hints and transparent HUD padding.
A drag starting on a hint/padding transfers to the map controller after more than
three native pixels of motion; a second finger also transfers the pending press.
The original press position is retained for the selection rectangle. No hint
button or press highlight survives the transfer. A stationary two-finger HUD
press cancels without issuing a terrain order. Adding a finger to an existing
drag starts camera displacement at the current contact positions.

Build/retest:

```sh
make test
make build-desktop
make build-android BUNDLE_ROM=1 ROM=/absolute/path/to/your.z64
```

- [x] GUI-dispatch fixtures: two-finger pan with first/second/both contacts on hints or padding.
- [x] GUI-dispatch fixtures: mouse/touch selection starts on or crosses the canvas; only original A is held.
- [x] GUI-dispatch fixtures: either finger's release stops scrolling; no hint or terrain B leaks.
- [x] GUI-dispatch fixtures: small tap jitter still presses the hint; redraw cancellation can become a sliding gesture.
- [x] GUI-dispatch fixture: adding a second finger after movement creates no initial camera jump.
- [x] Native game: both pan routes move the camera and both rectangle routes submit A without hint pulses.
- [ ] Physical Android: repeat sliding across and starting within the lower-right hints.

Fixture evidence: `local/reports/hud-pass-through-tests-final.log` (12 CTest
checks), `local/reports/hud-pass-through-python-final.log` (134 Python checks).
ADR-048 records ownership and handoff decisions. Native and APK receipts are
recorded separately below after completion.

Native receipt: `local/reports/android/hud-pass-through-native-01/run.json` binds
a 165.921-second run (exit 0, engine/resources unchanged). Its
`gesture-summary.json` verifies the map-first/HUD-second and both-HUD pans, each
advancing camera X by 16 for a 20-pixel finger displacement, stopping after
release with only Z/neutral submitted. The map-to-HUD rectangle holds A from
fields 8309..8379 and releases at 8381 within 1.5 pixels of (220,185); the
HUD-to-map rectangle holds A from 8902..8961 and releases at 8962 within 1.5
pixels of (100,80). Both paths submit no command-hint button. The final native
`frames/selection.png` visibly shows a selected infantry group.

The private canonical-coordinate recipe traverses the production UI-first/map
fallback modules; it bypasses physical OS touch delivery. The actual GUI event
dispatch is exercised separately by the fixtures. Phone gesture feel therefore
remains open. The rebuilt APK/hash and that device gate are in `android.md`.

### Post-mission results (2026-10-08)

Overview, Units, Buildings and Resources now accept mouse clicks and one-finger
taps. The adapter navigates through original Left/Right and waits for the selected
tab feedback; selecting a tab never sends Continue. A stationary tap elsewhere
sends original A to leave the results. Holding on this screen does not open the
command panel, and a gesture crossing a tab edge does not become Continue.

The completed private `results-native-09/results.json` proves all four Defeat
tabs, return to Overview and exit at field 7455 on an unchanged, isolated macOS
bundle. Hit boxes follow the selected tab's expansion and the shifting tabs.
Actual mouse/touch GUI dispatch has fixture coverage. The shared Victory path
has static/profile coverage, without a separate live Victory or physical-phone
claim. See the corresponding research section for the guarded NTSC-U layout.

## Double-tap map orders (2026-10-09)

Plan: reproduce the contact-to-controller ambiguity, coalesce physical touches
before arrival, cover ownership regressions, then validate in the desktop
original game before packaging Android. Main risks are an unwanted preceding
selection, finger jitter becoming a rectangle, and late orders surviving a
selection/menu/focus change. Open device question: whether the chosen 300 ms
window and second-touch tolerance feel comfortable on the phone; this is not needed
to implement or validate the controller contract.

Two one-finger taps beginning less than 18 VI fields apart (measured from the
first release) and within 12 native pixels now become one original B order. The
second release defines the target. Cursor steering begins at the first release;
A waits for the recognition window to expire. A matched second tap tolerates up
to 10 native pixels of drift, then larger motion starts the existing selection
rectangle. Single taps still select/activate through the original arrival hit
test; resource taps keep the worker/gather exception. Mouse, hint, minimap,
menu and two-finger routes remain separate. See ADR-053.

Validation:

- [x] 13 CTest checks pass, including the new map-tap and production GUI routing
      fixtures; 144 Python checks pass.
- [x] Mutations removing deferral or translating the pair to A are detected.
- [x] Fresh desktop game recipe submits one B without A for each of two pairs:
      a travelling cursor at field 7758 and an already settled cursor at 7962.
      Each arrives within 1.5 pixels of (180,100), using fresh feedback; the
      original own-worker selection context 1225182105 is retained after release.
- [x] Desktop and bundled-ROM Android builds pass; APK installs and its installed
      hash matches the build receipt on the selected phone.
- [ ] Physical phone: select units, repeatedly double-tap destinations, then
      verify selection, resource taps, frame selection and HUD/menu gestures.

The completed native receipt is
`local/reports/android/double-tap-native-01/run.json`, with generated
`double-tap.json`. It binds a 146.364-second run, exit 0, unchanged engine/resources,
and binary SHA-256 `aba43147e0cb2fe9795fafa4aff46ef3f8b60c013119eeba8e6e1e7be20cb5f6`. The recipe traverses the production UI-first/map
routing but bypasses OS contact delivery and enables background diagnostic
input. It certifies controller translation/arrival and retained selection,
not physical multitouch, lifecycle acceptance or completed unit pathfinding.

Build and verify:

```sh
python3 tools/desktop/build.py
ctest --test-dir local/build/desktop --output-on-failure
python3 -m unittest discover -s tests
python3 tools/desktop/run.py \
  --rom local/private/recompile/save-01/sc64-ntsc-u.z64 \
  --inputs local/private/android/double-tap-native-01.json \
  --seconds 144 --debug-input --background-input-probe \
  --output local/reports/android/double-tap-native-new
make build-android BUNDLE_ROM=1 ROM=local/private/recompile/save-01/sc64-ntsc-u.z64
```

The ROM/recipe/report paths are personal ignored files; other developers provide
their own verified dump and contact recipe. The physical check above remains
open because the selected phone was dozing after installation.

## Same-type double tap and D-042 input fixes (2026-10-10)

ADR-065 (as amended) keeps one group control: a double tap on an already
selected unit plays the original same-type sequence R 4, R+A 8, R 6, then eight
neutral fields (research.md, "Group and unit-type controller sequences"). A
late poll lengthens a step but never skips one; a dialog, keyboard/gamepad
input, pause or focus loss cancels it. Panel group recall/assign and Next type
were withdrawn by the user as inconvenient; their native evidence below stays
historical research.

D-042 fixes: map gestures and mouse hover steering now require
the verified 1P 320x240 image. In other gameplay layouts (2P 512x384) taps,
drags and two-finger gestures are consumed with a notice and never press A;
long-press still opens the controller panel. A menu click whose selected item
is not exposed by the observer cancels after a newer snapshot with a notice
instead of stalling for six seconds.

The historical group runs used a since-removed `group` recipe item; reproduce
double taps with pointer items:

```sh
python3 tools/desktop/run.py --rom "/absolute/path/to/your.z64" \
  --storage local/private/input/group-storage-new \
  --inputs local/private/input/group-native-03.json --seconds 82 \
  --debug-input --background-input-probe --trace-from 4050 --trace-to 4649 \
  --output local/reports/input/group-native-new
```

Seed a fresh storage copy from the personal save. Keep the physical mouse away
from the window: desktop mouse moves are real hover input and disturbed runs
02 and 04. Evidence: `local/reports/input/group-native-03/group-summary.json`
and its `frames/assign-4141.png`, `next-type-4281.png`, `recall-4331.png`;
run 02 `frames/next-type-5186.png`. Build log `group-build-01.log`; regression
log `group-make-test-01.log` (167 Python tests, 14 CTest checks).

- [x] Fixture: same-type chord dialog/keyboard/blocking cancellation.
- [x] Fixture: double tap on a selected unit plays R/R+A/R; unselected, stale-pick and no-pick double taps keep B; two-finger holds do nothing.
- [x] Fixture: 2P taps/drags/two fingers gated, panel fallback retained (fails before fix).
- [x] Fixture: unexposed menu selection cancels with a notice (fails before fix).
- [x] Native (historical, controls withdrawn): assign, Next type and recall of group 1.
- [ ] Native: R+A selects only the pointed type among mixed selected types (D-049).
- [x] Fixture: touch.gui creates fewer nodes than `max_nodes` (with headroom).
- [ ] Device: phone double-tap same type (D-049).
- [ ] 2P cursor transform, 2P modal routing and Sound Options custom items (D-049/D-042).

## Faster original menu navigation (ADR-067, 2026-10-11)

Menu taps still navigate only through original controller pulses and fresh,
owned selection feedback; no guest write. `ui_input.lua` now names its timing:

| Constant | Value | Meaning |
|---|---|---|
| `PULSE_FIELDS` | 4 | D-pad/stick/A/Start held per press (was 6) |
| `NEUTRAL_FIELDS` | 4 | release before the next step or a new tap (was 12) |
| `RETRY_FIELDS` | 30 | re-press when a snapshot 30 guest fields after the press shows no move |
| `CANCEL_FIELDS` | 90 | after 90 fields from a step's first press, the next unanswered window cancels |
| `CAMPAIGN_CANCEL_FIELDS` | 180 | carousel steps, which also wait for `ready` |

Measured original acceptance (research.md, 2026-10-11): presses held and released
for at least 3 fields each are never lost in the main menu, Single Player rows
or pause dialog; 2-field holds or 1-field releases lose presses. Four fields
keep one field of margin and stay far below the 15-field (250 ms) frontend
auto-repeat. A step now costs 8 fields instead of 18. Retries cover dropped
presses and the Single Player tab lock (~55 fields after a change, accepted
press visible after ~20); a retry whose original press is also taken overshoots
by one and the closed-loop route reverses from fresh feedback. Before this
change, a two-step tab route stopped on the middle tab (its second press fell
into the lock and nothing retried).

| Route (native, fields from click) | Before | After |
|---|---|---|
| Single Player rows, 6 steps then A | presses 0/18/.../90, A +108 | presses 0/8/.../40, A +48 (6 runs) |
| Single Player tabs 0 -> 2 | stopped on tab 1 | tab 2 at +52/+55/+61/+104 |
| Single Player tabs 2 -> 0 | (not reached) | tab 0 at +52/+53/+56/+101 |
| Build grid diagonal then A | presses 0/18, A +36 | presses 0/8, A +16 |
| Campaign side click | one press | one press, 6/6, ready after 29-32 |

Reproduce (personal ROM/save, fresh storage copy, keep the physical mouse off
the window; a pointer moving in the letterbox clears pending menu navigation):

```sh
cp -R local/private/input/group-storage-08 local/private/input/menu-speed-storage-new
python3 tools/desktop/run.py --rom "/absolute/path/to/your.z64" \
  --storage local/private/input/menu-speed-storage-new \
  --inputs local/private/input/menu-speed-nav-01.json --seconds 75 \
  --debug-input --background-input-probe --output local/reports/input/menu-speed-nav-new
grep ERROR local/reports/input/menu-speed-nav-new/engine.log   # must be empty
```

`--debug-input` now also prints `SC64 ui_sel` (each change of the selected
item set with its snapshot field), `SC64 ui_end` (a pending menu target
arrived or was cleared) and the hit target/items in `SC64 ui_click`. Raw
train recipes `menu-speed-main-0*.json`, `menu-speed-single-02.json` and
`menu-speed-pause-01.json` (with `.meta.json` train descriptions) measured the
acceptance table. Evidence: `local/reports/input/menu-speed-summary-01/`.
Build/test logs: `menu-speed-build-0*.log`, `menu-speed-make-test-01.log`.

- [x] Original helper/dispatcher input rules recorded (level + 250 ms gate, edge, grid arm).
- [x] Native acceptance trains: main menu list, Single Player rows, pause modal, tab lock.
- [x] Fixtures: exact per-field press/neutral sequence, retry, slow acknowledgement without re-press, tab lock, overshoot reversal (list/tabs/grid), cancellation, ignored fast repeat tap (`test_menu_input.cpp`); GUI mouse/touch dispatch sequence (`test_touch_input.cpp`).
- [x] Native adapter routes: rows, tabs 0->2->0, build grid, campaign side clicks, intro/briefing/advisor with 4-field pulses.
- [ ] Results/Defeat/Victory tabs, options screens and episode-row routes with the new timing (D-050).
- [ ] Two unexplained desktop tab-route stalls (D-050); physical touch and phone feel.

## Two-Player disabled (ADR-070, 2026-10-11)

Main-menu item 1 (Two-Player) is not clickable and shows "Two-Player is not
supported". The setup screen (token 267) is left with repeated original Back
presses (one pulse every 30 fields until the token changes). Other main-menu
items are unchanged. Guest memory is not written.

- [x] Fixture: Two-Player click ignored, Credits click still navigates, setup screen repeats B and resets on leaving, stale feedback presses nothing.
- [x] Native (desktop, background-input probe, no `ERROR` in `engine.log`): a recipe click on Two-Player emits no controller edge and stays on token 256 (`local/private/input/2p-off-run-a`); a controller entry reaches token 267 and returns to 256 after the third Back, about 60 fields later, because the game ignored the first two (`2p-off-run-b`).
- [ ] Device: phone touch behaviour of the notice and controller entry.
