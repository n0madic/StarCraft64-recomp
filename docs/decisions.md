# Architecture decision records

Status terms: ACCEPTED is a project policy, PROVISIONAL awaits an experiment,
and DEFERRED has insufficient evidence for an implementation choice.

Current stage status: ADR-038 closes desktop Stage 4; ADR-055 closes Stage 5 for
the user-scoped personal Android profile. Earlier open-gate statements describe
their original acceptance boundary. Expanded device/thermal qualification is
deferred. Stage 6 was withdrawn at the user's request by ADR-060 on 2026-10-09.

## ADR-001 — Private retail-derived material (ACCEPTED)

**Context:** the user requires user-supplied ROMs and personal recompilation
builds. **Decision:** keep original/normalized images, extracted resources,
assembly, generated C/RSP output, dumps and binaries in ignored local paths;
publish only reviewed authored sources and metadata. Check normalized SHA-1
before loading. **Consequences:** no downloadable game binary or cloud upload of
derived code; build/publish audits remain necessary because ignore rules can be
bypassed. Reference-library and upstream licenses retain their own obligations.

## ADR-002 — C++20 and reproducible dependencies (ACCEPTED)

**Context:** inspected N64ModernRuntime requires C++20; the user approved that
standard. **Decision:** use C++20 for all owned native code and runtime adapters,
Python 3.10+ for tools, and emitted C for generated game code. Pin revisions and
toolchain versions. **Consequences:** engine link/NDK must support the same standard
and the pinned runtime's ABI.

## ADR-003 — Evidence gates and desktop-first execution (ACCEPTED)

**Context:** plausible metadata cannot certify gameplay or HLE coverage.
**Decision:** retain the numbered stage gates, including original-runtime
captures before native recompilation, and desktop acceptance before Android.
Separate static, external, hypothesis, fixture and runtime evidence.
**Consequences:** Stages 0/1/2 are accepted for their bounded recorded corpora.
Stage 3 has a real desktop integration but retains open live acceptance checks.
Later native/Android gates require their own runtime evidence.

## ADR-004 — CPU-only runtime renderer adapter (ACCEPTED boundary)

**Context:** ultramodern calls the renderer from its own graphics thread, while
all graphics API calls must execute on the shell's main/render thread.
**Decision:** adapt `RendererContext` to capture/interpret tasks without GPU
calls and publish owned immutable presentation packets. The shell owns GPU resource
creation, updates, draw submission and destruction. **Consequences:** no live
RDRAM pointers in the mailbox; DP/SP completion, memory writes and data lifetime
must be preserved independently of display refresh.

## ADR-005 — Triple buffering only for disposable presentation (ACCEPTED boundary)

**Context:** dropping a task can lose persistent render-target contents or guest
framebuffer writes. **Decision:** process every submitted task and required
memory side effect in order; only superseded complete presentation snapshots
may be replaced. Three slots have explicit producer/consumer ownership.
**Consequences:** a render packet must contain everything needed to present the
latest frame, or retain referenced resources/dependencies. If that cannot be
proven, preserve dependent GPU work and bound the queue with backpressure.

## ADR-006 — S2DEX2 object interpretation (ACCEPTED)

**Context:** every accepted graphics task submits the same 4 KiB text hash,
identified as S2DEX2 xbus 2.08. Lists use BG_COPY, BG_1CYC and OBJ_LOADTXTR;
libgfxd has no matching object decoder. **Decision:** implement the observed
S2DEX2 control/object commands and emitted RDP subset. BG_COPY and scalable
CI8 backgrounds are essential. Preserve segmented addressing, nested lists,
TLUT changes, texrect fixed-point derivatives, 1-cycle and COPY semantics.
**Consequences:** do not substitute F3DEX2 triangles or add unobserved command
families by default. Unsupported used commands and new microcode hashes require
an explicit evidence update, not silent omission.

## ADR-007 — CPU indexed scene plus ordered RDP presentation (ACCEPTED)

**Context:** the CPU draws terrain/units into CI8: 320×240 in 1P and 512×384 in
2P. S2DEX BG_COPY converts that surface through TLUT into RGBA16; RDP adds UI
and other layers. The indexed source aliases part of the RGBA16 allocation.
Complete sampled interpreter windows show no CPU consumption of last-RDP-owned
bytes. **Decision:** preserve the guest software renderer; snapshot its CI8/TLUT
inputs and execute the original ordered S2DEX/RDP presentation. BG_COPY itself
presents the CPU base, so do not invent an additional independent RGBA overlay.
Track memory generations and format views across aliases.
**Consequences:** a GPU-triangle-only port would lose the scene. Address overlap
alone does not justify GPU readback. Maintain guest-memory ordering and a tested
software memory model for required writeback; any newly observed feedback hazard
reopens coverage before accepting that scene. Menu VI is 640×480, 1P is
320×240, 2P is 512×384; raw emulator PNG dimensions are scanout dimensions.

## ADR-008 — Engine GPU submission mechanism (SUPERSEDED by ADR-074)

**Context:** the former engine's installed headers and matching source expose
context-registry access and render-list dispatch. Stage 2 already has accepted
integer HLE arithmetic. **Decision:** establish the first integration with that
software rasterizer, uploading its opaque native-resolution image through the
former engine's graphics API and displaying a compiled material/quad through its
render-list dispatch on the engine thread, including all GPU creation, update
and destruction. **Consequences:** 20 real Vulkan readbacks now match Stage 2
RGB exactly, including every nearest-filter replicated pixel. CPU cost and
full-image upload bandwidth are explicit tradeoffs. The fallback uses one
static buffer/texture; dynamic geometry rings and GPU tile LRU are deferred.
Android GLES/Vulkan and a GPU combiner backend remain unverified (D-034/D-035).
Native image presentation does not implement original VI filtering (D-029).

## ADR-009 — Guest time, lifecycle and synchronization (ACCEPTED policy)

**Context:** host presentation rate differs from N64 field rate. Desktop
iconify events are not a complete Android lifecycle mechanism.
**Decision:** maintain a monotonic guest timeline and independent VI scheduling;
pause guest scheduling/VI/timers at cooperative safe points, clear transient
input, quiesce tasks/audio and rebase host deadlines on resume.
**Consequences:** implementation requires runtime inspection/fork work; never
suspend an OS thread while it holds a lock or replay accumulated wall-clock
ticks after resume. Shutdown must wake waits and join producers before freeing
RDRAM or shell resources.

## ADR-010 — PCM sink independent of engine sound (ACCEPTED direction)

**Context:** N64 audio timing/output must follow emulated AI behavior.
**Decision:** bounded PCM queue to a selected miniaudio or Android AAudio sink,
with measured resampling and pause/drain semantics. **Consequences:** choose the
actual library/API after capability checks; sink callbacks allocate no memory,
touch no Lua/engine API and do not change guest time based on presentation FPS.

## ADR-011 — 8 MiB guest memory and guarded touch (ACCEPTED policy)

**Context:** the project requests a consistent Expansion Pak view and direct RTS
touch controls. **Decision:** expose 8 MiB to the guest, validate every guest
access, and apply input/memory patches only at identified guest safe points.
Version/hash guards accompany every address and patched function.
**Consequences:** this is a runtime requirement, not evidence of the original's
mode-specific requirements. Cheat addresses and drag-box support must first be
proven; no UI thread writes into executing guest state.

## ADR-012 — Local personal builds and optional web (ACCEPTED policy)

**Context:** generated game code must remain local; native threading/audio
assumptions do not automatically transfer to browsers. **Decision:** build game
archives locally, link them using an audited local relink, and exclude web from
the desktop/Android critical path. **Consequences:** test archive platform layout,
PIC/ABI, shader build pipeline and final 16 KiB ELF alignment; any future web
work gets a separate feasibility gate for threads, memory, storage and audio.

## ADR-013 — Explicit resident and overlay identities (ACCEPTED)

**Context:** menu ROM `[0xD6640,0xE38E0)` and game ROM `[0xE38E0,0x12CA10)`
load byte-identically at `0x8011FBF0`, replacing each other's code. Resident
mapping ends before those ROM ranges. **Decision:** generate separate section
identities and register the actual DMA/load transitions with the pinned runtime
mechanism after inspecting its API. Bound functions, data, BSS and jump tables
per section at Stage 1. **Consequences:** a flat VRAM map or resident libultra
scan extending through the overlay payloads produces false symbols. No executable
decompression or invented overlay TOML syntax is warranted by current evidence.

## ADR-014 — Preserve CPU audio and animated presentation (ACCEPTED)

**Context:** original execution produces AI PCM with CPU mixer stores, while all
SP tasks are graphics. Opening animation decodes an indexed surface on the CPU
and presents it through S2DEX. **Decision:** recompile those CPU routines normally,
implement AI buffering/timing and feed the actual PCM sink. Do not introduce an
unobserved RSP audio ABI or replacement FMV decoder. **Consequences:** retain
optional hash-dispatch only if a later recorded scene submits another RSP task;
new workload evidence must precede its implementation.

## ADR-015 — Reference adapter corrections and bounded provenance (ACCEPTED)

**Context:** the research fork passed byte-ordered RSP DMEM to a native-word RDP
interface and rejected a valid unaligned palette. Upstream RDP also mishandled
fragmented commands. **Decision:** use the pinned original Angrylion core, convert
XBUS words correctly, fix remaining-word accounting and retain native regression
probes. Record CPU/cache/RDP writer attribution in bounded interpreter windows.
These are emulator adapter corrections; no guest code was patched.
**Consequences:** failed diagnostic captures are excluded. Savestates help
navigation, but cartridge save acceptance comes from a fresh process with only
battery Flash imported. Screenshots and pre-task RAM need explicit field/task
pairing for Stage 2 metrics. The synthetic probe is not itself gameplay proof.

## ADR-016 — Native Stage 1 capture and private dependency patches (ACCEPTED)

**Context:** Shared-address overlays need function-map replacement on the actual
ROM DMA. The pinned recompiler only resolves jump tables inside the function's
own section; native idle/poll loops cannot receive hardware interrupts by magic.
Stock PI handling does not implement this game's lower-level Flash commands.

**Decision:** Keep resident/menu/game section identities separate. Prepare local
runtime copies with exact-source checks; register only resident code at boot and
replace complete overlay identities on measured ROM DMA. Resolve cross-section
jump tables with bounds and ambiguity checks. Preserve original ROM bytes; audit
individual coherent-memory CACHE patches and the idle scheduling hook separately.
Capture all graphics dependencies on the submitting guest thread, before enqueue,
in immutable content-addressed 4 KiB pages. The renderer adapter performs no GPU
work. Build game C and C++20 host separately with assertions enabled.

**Consequences:** Generation success and native runtime acceptance remain separate.
Full-memory captures consume private disk space but preserve complete task inputs.
The Stage 1 adapter supplies completion after capture; it does not implement RDP
memory effects or claim rendered-frame parity. Stage 2 must revisit completion and
memory effects against the measured game workloads.

## ADR-017 — Flash persistence and AI FIFO for native boot (ACCEPTED file sink)

**Context:** The game uses custom Flash PIO/DMA and actively waits for AI FIFO
full during CPU-audio initialization. Ignoring writes or returning constant AI
status changes observable behavior.

**Decision:** Implement the observed 128 KiB Flash protocol with synchronous HLE
completion and complete-image fsync/atomic publication. Match the research
emulator's Macronix profile without asserting a physical cartridge chip. Preserve
CPU audio and expose two timed DMA entries, busy/full status, rate changes and
canonical stereo PCM. Stage 1 records WAV segments and can output through the
installed SDL2 audio API; production audio selection remains Stage 3 work.

**Consequences:** Hardware Flash busy durations and analog audio are unproven.
SDL dummy/silent runs verify generated PCM and scheduling, not an audible device.
Native save UI/cold reload and the timed WAV-sink stability gate have passed.
Live output remains D-028.

## ADR-018 — Source-backed VI and explicit worker lifetime (ACCEPTED implementation)

**Context:** Native execution exposed empty default VI initialization, unsupported
Y scaling, and a shutdown race against context cleanup. Review also found broken
non-head queue removal and a detached timer that could outlive guest RAM.

**Decision:** Initialize the SDK default black NTSC context, alternate fields,
and implement X/Y scaling and mode reset from the pinned libultra sources. Add
event scheduling at measured wait heads; preserve guest-owned counters. Join
guest contexts before closing graphics input, drain captured tasks, join VI and
timers, then release RAM. Count actual pending context joins before sending a
cross-producer cleanup sentinel. Lock shared context transitions.

**Consequences:** Actual patched-source fixtures exercise scheduler contention,
context cleanup, queue removal, VI copies and idle/future/periodic timer shutdown.
These tests do not establish physical timing or full campaign behavior. The
present workload contains only graphics RSP tasks; another RSP workload reopens
its task-queue lifetime audit (D-020).

## ADR-019 — File PCM sink for the headless Stage 1 gate (ACCEPTED scope)

**Context:** The development environment exposes no default audio device. SDL2
is a compatibility layer over SDL3, and its dummy device consumes sustained menu
PCM slowly enough to reach the one-second queue guard. This is separate from the
native CPU mixer and the timed two-DMA AI model.

**Decision:** Run the headless ten-minute gate with the WAV sink and timed AI
enabled. Keep original PCM bytes, rate changes and FIFO status; `--silent` only
disables opening a live device. Retain failed SDL dummy runs and D-028. Do not
clear the output queue or discard PCM to hide drift.

**Consequences:** Native file audio and game stability can be reviewed without
an unavailable physical device. Audible live playback and clock adaptation
remain required for Stage 3, whose planned audio backend is miniaudio/AAudio.
Stage 1 acceptance explicitly identifies its audio sink and cannot certify
the failed sustained SDL dummy path.

## ADR-020 — Owned fixed-point rectangles and physical texture atlases (ACCEPTED)

**Context:** S2DEX2 generates rectangles from CPU indexed images, TMEM loads and
TLUT changes. Float host quads lose fixed-point border/derivative semantics;
mask values can exceed the physical TMEM dimensions.

**Decision:** Use the C++20 DrawList v1 in `gfx.hpp`: owned initial canonical RAM,
ordered fixed-point rectangles, complete target/state, physical TMEM/TLUT and
decoded RGBA8 atlases. CPU backgrounds own their full source colors. Content keys
are hash candidates with full-payload collision checks. Batch only adjacent
equal texture/target/state, preserving every rectangle's sampling basis.

**Consequences:** The core and fixtures require no engine/GPU API. A GPU
backend derives host geometry and implements RDP sampling rather than relying
on ordinary hardware bilinear filtering. Frame identity/VI are provided by the
runtime adapter. Atlas ownership permits source RAM reuse after submit. This
contract is in-memory; raw host struct serialization is prohibited.

## ADR-021 — Fenced original-target RGB gate and completion ordering (ACCEPTED)

**Context:** Earlier pre-task captures and later VI screenshots have different
time/scanout domains. Native game frames alias CPU CI8 sources with RGBA16 targets.

**Decision:** Capture original post-task RAM in the reference Angrylion SyncFull
callback after workers join, before the guest interrupt. Freeze full native-target
RGB PSNR >= 40 dB per frame; compare at least ten frames across menu, briefing,
1P and 2P without crop/rescale shortcuts. Use the raw-RDP oracle only as a separate
diagnostic. Twenty frames now pass the metric and visual review.

Interpretation owns all sources and the software backend executes target stores
in draw order. A later source load overlapping a previous target write fails
with D-019 instead of sampling stale input. The runtime must finish required guest-memory
effects before signalling task completion. In the bounded no-feedback corpus,
the desktop bridge acknowledges each parsed task and executes presentation
stores on its immutable snapshot; it does not claim live guest RDP writeback.
New feedback reopens D-019. A mailbox may discard only completed
presentation packets; it cannot drop uncommitted writes or replace queued SP/DP
completion. Required guest writeback needs separate observed proof.

**Consequences:** Stage 2 is accepted for native-target RGB, not exact hidden
coverage, VI filtering or an unplayed campaign. D-029 preserves precision debt
and requires the shell GPU/scanout path to match this baseline or use the software
presentation fallback. Existing guest-thread/shell-thread separation remains
mandatory. No direct GL/Vulkan path or RT64 dependency is introduced.

## ADR-022 — Separate GBI image pointers from physical RDP addresses (ACCEPTED)

**Context:** The two parser entry points share RDP operation handling, but their
image operands have different address domains. Discarding the GBI segment byte
silently directs framebuffer stores to the wrong guest RAM region.

**Decision:** Select an explicit image address domain when creating the parser.
GBI SETCIMG and SETTIMG use the segment resolver; raw RDP image commands retain
physical addresses. Descriptor and display-list pointers remain GBI operands.

**Consequences:** A synthetic segment-1 target at base `0x20000` plus offset
`0x4000` writes only `0x24000`, matching the equivalent raw RDP stream. This
establishes parser behavior, not the use of segmented targets by the game.

## ADR-023 — Pinned local engine relink (SUPERSEDED by ADR-074)

**Context:** generated game code cannot be uploaded. The former engine's installed
SDK contains its static libraries, exported-symbol list and macOS link profile;
its standard build path would send native extensions to a remote build service.
**Decision:** compile the authored registration/bridge and private game/runtime
locally with CMake; resource compilation skips the extension and points the
build service at a closed localhost endpoint. Fetch public
dependencies only from pinned official URLs with SHA-256 guards.
**Consequences:** a real macOS arm64 app is produced without cloud game-code
transfer. Conventional archive packaging, other desktops and Android builds
need their own proof.
The current bundle depends on this machine's OpenSSL dylib. Stage and publish
whole bundles; preserve the previous one and never truncate a running binary.

## ADR-024 — Virtual time and bounded live PCM (ACCEPTED for the desktop prototype)

**Context:** upstream guest scheduling has no ready-made whole-session pause.
Nominal VI time and hardware audio device time can drift independently.
**Decision:** prepare a separate guarded runtime copy with cooperative
scheduler/VI/timer pause gates; exclude paused wall time from guest deadlines.
Explicitly convert unsigned VI deadline arithmetic to the signed clock type.
Use miniaudio 0.11.23 with a preallocated 200 ms stereo queue, nonblocking callback
access and bounded host-only resampling correction. **Consequences:** pause/stop
must wake all waiting workers; guest AI busy/full remains a timed two-DMA model.
Starvation/contention and discarded frames have separate counters. Audible
hardware output is user-confirmed, but device-loss recovery and all platform
pause paths are separate tests. No SDL dummy/file sink is treated as that proof.

## ADR-025 — Complete desktop snapshots and explicit resume evidence (ACCEPTED)

**Context:** the three-file runtime marker could silently retain stale OS code;
native-binary integrity did not cover mutable compiled resources; a freeze-only
run could satisfy a check used to certify resume. **Decision:** hash the complete
source/prepared trees, publish verified replacement runtime snapshots and retain
previous trees. Verify the complete resource file set before/after execution and
require that result in both acceptance checkers. Require a measured running
interval after an observed resume when resume is requested. **Consequences:**
unrelated desktop-tree edits are refused, failed publication rolls back, identical
trees are reused, and old receipts cannot be retrospectively promoted to the
new integrity gate. Freeze-only paused shutdown remains separately testable.

## ADR-026 — Desktop virtual controller before guest touch patches (SUPERSEDED by ADR-027)

**Context:** Stage 3's revised desktop lifecycle gates now pass. Original cursor,
camera, selection and order structures are still unproven. **Decision:** build
the first Stage 4 slice as a pure Lua contact/layout state machine plus an engine
GUI adapter, merged through the verified controller API. Keep per-contact
ownership, guest-field short pulses, focus/pause clearing, editable normalized
layout and explicit save errors. Use the pinned engine's atomic `sys.save`.
Share the rendered viewport formula with the image-coordinate inverse.
**Consequences:** the controller can be tested without guest RAM patches;
GUI fixture evidence, live mouse input and physical multi-touch remain separate.
No direct RTS gesture or screen-to-world assumption is inferred from controller
support. D-039/D-040 and the touch-only mission gate keep Stage 4 open.


## ADR-027 — Gestures through original controller input (ACCEPTED; implementation partial)

**Context:** Human testing rejected the permanent pad because it obscured the
original interface and was awkward to use. Original manual controls and cursor
RAM/code observations now establish a controller-based approach. **Decision:**
make mouse/touch gestures the default, hiding the diagnostic pad unless F3 is
requested. Read cursor coordinates only from owned canonical display-task RAM
snapshots. A bounded pulse/settle servo steers the original analog stick; it
waits for a fresh stationary cursor before issuing A/B. Dragging holds A for
the original selection box. Right-click/two-finger tap issues B without a prior
A; a transient command panel uses original C buttons, Start, Z+R and Z+L.
**Consequences:** no guest-memory writes, custom selection/order subsystem or
screen-to-world approximation. ADR-028 adds context-aware interface clicks for
screens with frozen cursors. Original Z+stick camera scrolling is now verified in a bounded 1P probe and
mapped to two-finger/middle-button relative steering. Its bounds/modal and
split-screen equivalence require further original probes (D-040); physical multi-touch and full mission acceptance remain open.
Field-based input duration is independent of host presentation frequency.

## ADR-028 — Read-only interface feedback and controller navigation (ACCEPTED; desktop validation bounded)

**Context:** Human testing found gameplay selection/orders usable but menus and
advisor messages still required keyboard input. The original game freezes its
cursor in modal screens. Frontend text rectangles are local to temporary sprites;
they cannot be treated as screen hitboxes. Advisor dispatch also ignores keys
until its original 750 ms deadline expires.

**Decision:** classify researched UI states from the owned canonical RAM snapshot.
Use profile-checked N64Recomp hooks to observe text rectangles and their later
sprite blits, without changing registers or RAM. Publish this owned metadata in
the same triple-buffer slot as its display task. Mouse/touch selects an observed
item by original D-pad pulses, waiting for selection feedback before A. The build
grid uses analog pulses because its original navigation differs. A single-button
advisor accepts a tap anywhere in the game image; an early tap waits until the
message has been observed for 60 guest fields, then produces one A edge. Message
identity and root pointer fence pending actions. Right-click/two-finger UI tap
sends B; the transient panel retains Back/Continue/navigation fallbacks.

**Consequences:** guest logic still owns selection, activation and dismissal.
Loading and guarded briefing mode are distinct; input-loop/return observers
identify the text briefing after the mode-12 location card restores mode 4. Stale frames, changed windows,
changed list tabs, pause/focus/resize and competing physical input cancel work.
No graphics API runs in guest threads and gfx_core remains shell-independent.
The exact NTSC-U ROM and eight instruction anchors are required; private generated
C must be regenerated locally with observers before the desktop build. Unknown
or nested/custom widgets need further observation; the fallback panel remains
available. Desktop recipes bypass OS events and cannot certify physical touch or
complete Stage 4 mission acceptance (D-040).

## ADR-029 — Original HUD hints as pointer targets and independent cursor hiding

**Context:** Selection-dependent C/B hints already occupy the original HUD. The
user wants keyboard-free activation and visible press feedback without a
permanent controller overlay. The former engine's mouse lock hides and recentres
the mouse; its public SDK lacks independent cursor visibility.

**Decision:** add five ROM/instruction-guarded read-only observers: clearing
the command canvas, the C-text return, the B-text call/return, and presenting its sprite.
Combine actual font bounds with the actual controller icon bounds, then translate
through the observed sprite presentation. Preserve metadata across sprite reuse
only while selected-list/command context and painted pixels match. Publish it
with the owned display task. Lua consumes clicks on visible hints and sends
the original C/B mask without a cursor action. GUI draws a translucent gold
press/activation highlight over the same bounds. Mouse and touch long presses
both expose the existing temporary Build/Pause panel.

On the verified macOS profile, use installed AppKit `NSCursor.hide/unhide`
with balanced ownership, active window and content-view hit testing. Native and
Lua guards require fresh gameplay cursor feedback and restore on UI/lifecycle
transitions. No mouse lock or pointer warp is used.

**Consequences:** original game logic owns command meaning and availability.
No assets, generated game code or ROM enter the repository; no guest memory is
written. Split-screen hint layouts and other desktop cursor APIs need separate
proof (D-034/D-040). Synthetic, original-emulator, native-recipe and physical
input results remain separate acceptance evidence.

## ADR-030 — Close construction on map taps and retain HUD contact ownership

**Context:** The original build interface captures controller input until B.
An outside pointer tap could not select another object. Hint interception started
only on button down, allowing prior hover to move the guest cursor underneath
the hint; incomplete/faded metadata could then leak a press to the map.

**Decision:** export validated build-root bounds from owned canonical RAM. A
stationary tap outside that root queues B, releases it after the interface closes,
waits for a fresh game/cursor snapshot and at least 18 neutral/pulse fields from
the initial edge, then queues the original cursor/A selection at the tap. Panel
gaps remain consumed. Stale feedback, another root/dialog, pause, competing input
and timeout cancel replay. Original guest hit testing decides the selected object.

Seal HUD checksums after the complete original canvas draw. Accept all positive
presentation alpha, export the observed painted-canvas bounds, and intercept its
hover, buttons and padding before terrain routing. Preserve the last map cursor
position while a hint is pressed. A recent region can consume a missing-present
frame for at most 12 fields with the same selection context; it cannot authorize
a stale command. Cancelled hint contacts keep ownership through release. Fast
repeated taps queue separate controller edges after the neutral interval. The
existing pressed highlight follows only a currently matching actionable label.

**Consequences:** no game RAM writes, synthetic selection or framebuffer layout
guesses are needed. One additional guarded observer requires local recompilation.
Original opacity and contextual command meaning are preserved. GUI fixtures,
native controller/cursor receipts and human input remain separate; all contexts,
physical touch and full mission acceptance remain D-040.

## ADR-031 — Visible HUD aiming and continuous free mouse tracking

**Context:** Suppressing guest hover under the command canvas preserved order
targets but left no visible pointer there while the OS cursor was hidden. The
user reported skipped areas and could not aim at hints. Free mouse tracking also
shared a four-field movement/eight-field settle loop intended for queued clicks.

**Decision:** retain the guest cursor's last map target when entering HUD, but
make the OS pointer visible inside the actual presented HUD bounds. The macOS
bridge tests real AppKit pointer coordinates against the projected region; it
does not rely on a remembered Lua mouse event to control visibility. Add a soft
GUI hover highlight, with stronger pressed feedback; mouse and touch ownership
remain distinct.

Give free mouse hover a continuous controller path with snapshot velocity
prediction and a three-native-pixel desired step cap. Read the current original
cursor divisor from owned RAM; invert the verified original square/deadzone and
pinned ultramodern octagonal normalization. Keep queued clicks and selection
drag on their existing arrival path. Stale feedback or invalid divisor yields
neutral hover input. No guest writes or pointer warp are introduced.

**Consequences:** hints can be aimed at without moving the order target beneath
them. The map's original cursor remains the sole pointer there. Native snapshot
probes establish sampled tracking/click behavior; projection/highlight fixtures
do not certify actual OS pointer pixels or physical touch. Desktop human recheck
and full Stage 4 acceptance remain D-040; other cursor platforms remain D-034.

## ADR-032 — Preserve HUD actions and log submitted input

**Context:** Clearing every gesture at HUD completion discarded a newer map
hover. Replacing HUD state at each tap lost the third quick tap and shortened
controller release. Logging candidate gestures before keyboard/recipe arbitration
could claim an action that was never submitted.

**Decision:** finish only HUD ownership; preserve the later map hover. Keep a
bounded FIFO of eight validated hint actions with six pressed fields followed
by twelve neutral fields. Cancel the queue on stale/changed labels, context,
competing contacts or explicit ownership transfer. Log `SC64 pad` immediately
before `sc64.set_pad`, after all merges, clamping and pause handling, including
player and provider provenance. All native input checkers reject the historical
`SC64 gesture` candidate stream.

**Consequences:** successive actions retain separate original controller edges.
Older native input reports remain historical; rerun them for submitted-input
proof. Human hint/drag testing and touch-only mission acceptance remain D-040.

## ADR-033 — Owned VI state controls desktop presentation

**Context:** the desktop renderer ignored `RendererContext::update_screen()`
and always presented the last RDP target. Original and native abort traces show
VI black fields and a 320-to-640 mode change while display-list work stops.
Reused memory and offscreen targets must not become visible scanout images.

**Decision:** copy the fourteen pinned runtime VI registers in its graphics
consumer callback into a mutex-protected owned mailbox. Associate that update
with the last display-list task completed by the same consumer. Engine update
selects a bounded cache of eight rendered RGBA images using VI origin, width,
pixel size and task completion. Accept only recorded row-zero/one/two origin
offsets and valid RAM extents. Blacking or mode changes invalidate earlier tasks;
that invalidation persists even when the engine misses the black interval. No
new task is required to suppress the quad: the render script clears to black.
All engine graphics/render access stays on the engine thread. Static GPU fixtures
retain their explicit native-target path without a live guest VI dependency.

**Consequences:** mode transitions cannot reinterpret an old image as the new
framebuffer or expose the last offscreen target. The fallback retains up to
32 MiB of RGBA images. Diagnostic traces are opt-in, bounded to 600 VI fields
and forty GPU readbacks, and stay under ignored `local/`. Callback field stamps
are host VI-counter observations, not original emulator field numbers. VI
filtering, exact interlaced scanout, CPU-only scanout without a DL, victory and
other scene/mode coverage remain D-037; no framebuffer writes go to guest RAM.

## ADR-034 — Route host Back keys through original B

**Context:** Mouse-only mission play is user-reported, but Esc and Android
system Back have no binding to the original game's Back action. Sharing one
action id with C/B could let one key release clear another held key.

**Decision:** bind the pinned engine's `KEY_ESC` and `KEY_BACK` to separate action ids,
`escape_back` and `android_back`, each mapped to original controller B (`0x4000`)
in `pad_state.lua`. Use the existing short-press retention, controller ownership
and focus/pause clearing. Preserve Android's engine-provided Back callback path;
do not introduce a native Android lifecycle handler.

**Consequences:** menus receive the same Back/Cancel as the original controller,
and simultaneous aliases retain independent releases. In gameplay B retains
its original contextual-order semantics. Lua and desktop checks do not certify
Android button/gesture delivery; that remains an on-device Stage 5 check.

## ADR-035 — Complete software RDP memory effects before presentation eviction

**Context:** Native abort uploads contain coloured lower-half noise during
fade-out, before VI blanking. Every software task previously restarted from
guest RAM lacking prior RDP stores. Dropping a mailbox packet also dropped its
memory effects, breaking partial redraw and memory blending.

**Decision:** the current desktop software fallback rasterizes every task on
the submitting guest thread, where the capture bridge already owns RAM, before
enqueueing the runtime graphics action. Record actual written byte ranges,
including equal-value stores and excluding alpha-rejected pixels. Validate all
ranges before committing only those bytes to word-swapped guest RAM. Publish the
immutable completed result with its DrawList/UI metadata; engine update uploads
the selected target without rerasterizing. SP/DP completion still follows the
graphics consumer's `send_dl`. Static offline/GPU fixtures remain independent
and never write guest RAM.

**Consequences:** presentation may discard obsolete packets while all RDP
stores survive into the next task or CPU read. No engine graphics/render call is
made on a guest/runtime thread. Software work is synchronous on the guest
submit path, so its performance must be measured; each retained mailbox result
adds an owned 8 MiB RAM image. Observable early RAM completion is an HLE timing
approximation before the asynchronous completion messages. Same-task feedback
and exact hidden coverage remain D-019/D-029; this is not the future GPU
backend's general synchronization contract.

## ADR-036 — Observe the campaign carousel and keep its original input path

Campaign routing and the asymmetric boundary described here are superseded by
ADR-037. The read-only observations, entry and warning rules remain applicable.

**Context:** The main-menu heroes/platforms and episode-order warning are not
part of the existing generic widget tree. A geometric one-step side click can
choose the wrong campaign: the original blocks Right at campaign zero, while
campaign five remains visible on the left. Activating on drag release can also
open a campaign unintentionally.

**Decision:** add four SHA-1/instruction-guarded read-only observers to the
researched NTSC-U main-menu function. Copy hero/platform blit extents, selected
campaign, animation readiness and foreground warning ID/bounds into the owned
frame metadata. Account for the exact call delay-slot Y additions. Combine
hero/platform rectangles per campaign; suppress all underlying menu targets
while a warning is visible.

Map a horizontal swipe to one original controller step matching screen motion:
Left (`0x0200`) brings the right-hand campaign into the centre; Right (`0x0100`)
returns the previous campaign. A side click queues the clicked campaign ID and
uses the researched numeric episode sequence, waiting for each original
animation and selection acknowledgement before another pulse. Arrival only
selects. A central click navigates to the original Single Player button and
presses A. Inside the rendered episode-order rectangle, a stationary release
presses Start; outside, it presses B. Each pulse retains its neutral gap. Stale
metadata, changed foreground, cancellation, controller ownership and bounded
navigation timeouts discard unfinished input.

**Consequences:** mouse and touch share native-coordinate routing, including
letterboxing. No menu/selection memory writes or direct guest action calls are
introduced. The original game retains campaign availability and entry rules.
Metadata requires local recompilation with eighteen UI observers (twenty-eight
total hooks); older generated outputs are rejected. Completed native recipe
evidence and actual GUI-dispatch fixtures have separate scopes. Physical
multi-touch/Android delivery and full Stage 4 acceptance remain D-040.

## ADR-037 — Choose the shortest campaign route and enable symmetric wrap

**Context:** Numeric ID comparison made side clicks traverse five animations
between IDs 0 and 5. Main-menu initialization at `0x80128EE0..0x80128F1C`
overwrites the seed table with `(selected + 6 - index) % 6`; Left advances this
ring and Right reverses it. The original handler separately blocks Right at
ID zero, preventing the short reverse route even with corrected host input.

**Decision:** For a side-click target, compute `(target - current) % 6`.
Choose Left for distances 1..3 and Right for 4..5; the three-step tie chooses
Left. Retain original animation/readiness acknowledgement and neutral pulses;
arrival only selects. Replace the single `beqz` at `0x801296C8` with NOP in the
private recompilation configuration, allowing the existing Right rotation to
wrap 0->5. Require the exact normalized ROM SHA-1 and original instruction
`0x1040000B` at ROM offset `0xE0118`. Use the pinned N64Recomp parser's
`[[patches.instruction]]` fields `func`, `vram`, `value`; do not edit the ROM.
Reject generated game code missing the patched instruction before bundling.

**Consequences:** Both endpoint side clicks take one original controller step;
swipes and keyboard/gamepad input also gain symmetric wrap. Original campaign
entry, availability, warning handling and animations remain in control. This
is an explicit port behavior change at one original instruction, not evidence
that the original supported reverse wrap at zero. UI observers remain read-only;
no direct selection-memory writes or action calls are introduced. Verify all
30 distinct campaign pairs in fixtures, both endpoints in a completed native
recipe, and retain physical-touch/Android acceptance under D-040.

## ADR-038 — Close desktop Stage 4 and move hardware acceptance to Stage 5 (ACCEPTED)

**Context:** The user reports completing a mission with the mouse alone and now
explicitly requests Stage 4 closure so Android work can proceed. Controller
translation, observed UI/HUD routing and campaign navigation are implemented.
The latest desktop bundle passes the completed eight-gate campaign recipe,
ten CTest checks and 111 Python tests. The initial touch-only Wasteland criterion
has no recorded physical-touch walkthrough and cannot be marked as passed.

**Decision:** Close Stage 4 for the implemented macOS arm64, 1P desktop control
scope by the user's acceptance on 2026-10-08. Replace its hardware entry blocker
with Stage 5 exit checks: physical multi-touch/cancel, safe areas/orientation,
Android Back, gamepad ownership, and a recorded first-Terran-mission walkthrough
using touch only with the diagnostic pad disabled (D-041). Carry expanded
hint/menu/camera/group/2P contexts into D-042, with regressions required when
those workloads are exercised. Retain controller translation as the accepted
design; direct guest selection/order patches are a deferred optional feature
(D-013), not a Stage 4 prerequisite. Record the revised gate separately from
original/runtime evidence and preserve historical unchecked items.

**Consequences:** Stage 5 may start now. No Android build, hardware test or
physical-touch mission is falsely certified by desktop closure. Device gates
remain necessary before Stage 5 acceptance; other desktop profiles remain
D-034. ROM/private-code boundaries, C++20, local build requirements and render
thread ownership remain unchanged. Stage 4 regressions are part of Stage 5
changes; closing the stage does not authorize ignoring later input defects.

## ADR-039 — Keep input feedback with VI presentation and bound released drags (ACCEPTED)

**Context:** Review found three desktop control defects. The engine published UI
and cursor metadata from the newest display-list packet even when VI selected
an older cached image. Unnamed mouse-move events during a middle-button pan were
intercepted by the command HUD. A selection that had started holding A lost its
action timeout because the start action had already been consumed.

**Decision:** Store owned `CursorFeedback` and `UiFeedback` on each
`ScanoutImage`. Publish feedback only after selecting the image for the current
VI state, on every engine update, including updates with no new display list.
Clear feedback when no valid scanout image exists. Preserve the existing field
stamps and freshness checks; selecting an older image must not make its input
feedback appear fresh. Classify unnamed movement with an active middle-button
pan as pan input before HUD routing, retaining modal-mode cancellation.

Record the physical release field for a selection drag. Allow the endpoint
servo to finish normally, but cancel all pending selection input and emit a
neutral controller interval after 360 fields from release, independently of
whether the start action still exists. This is a host control policy matching
the existing bounded-action budget, not a discovered original-game timeout.
Do not impose that deadline on a drag while the mouse button/finger is held.

**Consequences:** Future/offscreen targets cannot replace displayed input
geometry. The cache adds only bounded owned metadata; no guest-memory writes,
graphics calls from game threads or new engine APIs are introduced. Mouse and
touch release paths share the deadline. Regression fixtures cover retained and
future VI tasks, hint/padding pan movement and release, modal cancellation,
release-before-start, long held drags and recovery after a stuck endpoint.
Native campaign regression remains distinct from fixture proof of deliberately
immobile cursor feedback. Physical-touch/Android gates remain D-041/D-042.


## ADR-040: Retain disabled build cells in pointer navigation (ACCEPTED)

**Context:** The ordinary widget predicate excludes disabled buttons. Applying
it to the game's separate analog build-grid navigation removes the selected
intermediate cell from feedback. A centre-to-upper-left click then stops at
middle-left when that cell requires an unavailable prerequisite.

**Decision:** Export visible build children for the researched draw callback and
IDs 1..9, including disabled cells, and read selection from the value used by
the original cell draw callback (`0x800AFE9C`). Keep their row/column geometry
and the existing controller pulse/acknowledgement sequence. Set `activate=false`
for disabled cells so a pointer click can display their requirement through
selection without submitting A. Retain the stricter ordinary menu predicate.

**Consequences:** Navigation can acknowledge and traverse unavailable cells;
subsequent clicks can steer from them. Requirements and construction logic stay
in the original game. No new engine API, guest write, resource or instruction
patch is needed. Bridge flag regressions and exhaustive mouse/touch grid route
fixtures cover the failure; device delivery remains D-041/D-042.

## ADR-041: Pause on focus loss only inside a mission (ACCEPTED)

**Context:** The initial host combined every deactivation with manual pause and
iconify. This unnecessarily froze the original front end and intro. The user
requests focus auto-pause only on the game level.

**Decision:** Use the researched guest mode 3 from owned graphics task snapshots
to classify mission activity, including build menus and dialogs. Reevaluate on
mode changes and lifecycle events; retain the last known mode across unavailable
scanouts. Focus loss pauses only this activity. Explicit user pause and desktop
iconify always pause. Block native and Lua controller input while inactive even
when the guest is running. Keep the bounded background recipe exception.

**Consequences:** Front-end animation/audio continue on focus loss, while
missions freeze cooperatively. A mission entered in the background pauses as
soon as its task snapshot is consumed. Focus recovery cannot undo user/iconify
pause or replay pending pointer actions. No new engine call or guest write is
introduced. The pinned extension header marks iconify events as desktop-only;
physical Android suspension/surface behavior still requires D-041 validation.
Policy/Lua tests and a native intro deactivation/manual-pause receipt establish
separate evidence; native mission focus is not claimed by the intro receipt.

## ADR-042: First personal Android APK from the pinned local SDK (SUPERSEDED by ADR-074)

**Context:** Stage 4 desktop is accepted. NDK r29 is installed; the former
engine's matching SDK contains arm64 Android archives, an engine export/link
profile and activity-result/JNI headers. The host previously used AppKit and a
desktop link. Private game-derived code must stay out of remote build payloads.

**Decision:** Add a separate local arm64-v8a CMake profile from that exact SDK's
engine link profile, retaining the guarded runtime and software renderer. Use
native API 28, static libc++, C++20 authored code and C17 private game C. Use an
authored Java SAF/activity lifecycle helper and verified engine Android APIs.
Replace host OpenSSL hashes with the engine's pinned hash helpers; keep graphics
on the engine thread and initialize FPU state only on guest workers.

Package the personal engine locally with no remote upload, requiring exact APK
engine identity. Include the helper dex locally, realign/sign the personal APK
and verify ELF/ZIP alignment, signature and private-input boundaries before
publishing.

**Consequences:** A working personal APK is reviewable and emulator-testable.
This first slice uses **direct local SDK relinking**, an explicit deviation
from the engine's conventional local packaging; that requested profile remains
D-035 before full Stage 5 closure. Package alignment is not 16 KiB runtime
proof. Activity suspension is an independent pause cause so Home freezes
workers when the engine loop stops without cancelling manual pause on return.
Vulkan/SwiftShader emulator import/menu/PCM/lifecycle and scoped FPU probes are
bounded evidence, not physical touch, audible quality, Adreno/Mali or thermal
acceptance. Stage 5 stays open under D-012/D-035/D-036/D-041/D-042.

## ADR-043: Explicit ownership of asynchronous ROM import files (ACCEPTED)

**Context:** SAF copies finish on a Java worker and are consumed later on the
engine thread. Clearing a completed result during finalization leaked
its temporary file. Process termination during copying also leaves an orphan.

**Decision:** Keep callback state in a portable `RomImportState`, serialized by
the existing Android mutex. A completed result stays busy until `take()` moves
file ownership to the engine; the engine retains its existing load-and-delete
path. Finalization and late results delete unconsumed imports. Startup removes
only direct regular files with the authored import temporary names in the
canonical app-private directory; preserve symlinks, directories and other data.
Log cleanup failures. Keep graphics calls on the engine thread.

**Consequences:** Pending/late/orphan files have defined cleanup paths and
duplicate picker requests cannot replace a pending result. The state and file
ownership rules are tested without JNI; Android startup/import checks exercise
the actual bridge. ROM/save contents and user-supplied source files are preserved.


## ADR-044 — Optional personal ROM asset in the existing Android APK

**Context:** The user explicitly requested a personal build including their ROM,
then refined the request to an option on the existing build and direct APK reads
without a second ROM file. The normal SAF importer and its persistent private
copy already work. The pinned N64ModernRuntime `load_stored_rom` normally reads
`config_path / stored_filename()` when starting a game, even when the host has
already read and validated the image.

**Decision:** Add `--bundle-rom PATH` / `BUNDLE_ROM=1 ROM=PATH` to the existing
Android builder. Keep its output name, signing key and application ID unchanged.
Normalize z64/v64/n64 inputs and require the researched 32 MiB NTSC-U SHA-1 before
building. Insert exactly `assets/sc64/sc64-ntsc-u.z64` after local resource
packaging; never place it in the tracked project or send it to a build service.
The default content audit rejects ROMs. An explicit bundled audit allows and requires only
that named asset with the exact size, byte order and SHA-1; other private inputs
remain forbidden.

The staged `sc64.bundled_rom=1` setting selects the asset before any old imported
ROM. Read it with the installed NDK's AAsset APIs into an owned vector. The native
Session normalizes/verifies the bytes again, moves them into the existing
`recomp::set_rom_contents` API, then starts the game. A source-drift guarded patch
lets `load_stored_rom` return early only for this host's verified, preloaded game
ID; otherwise the upstream file path remains. The same handoff avoids rereading
the validated picker import. Bundled startup never creates a private ROM file.

**Consequences:** This explicitly authorized option changes the earlier blanket
APK exclusion rule for personal outputs only. ROMs/generated sources remain
ignored and absent from version control; default builds still exclude the ROM.
A bundled APK replaces an installed ordinary build rather than installing a
second application. Saves and any previously imported ROM are preserved. RAM
still needs a 32 MiB ROM buffer; this is direct asset loading without a disk copy,
not zero-copy DMA from the ZIP. The current implementation applies to Android;
desktop retains its existing picker/import path. Stage 5 hardware gates remain
open.


## ADR-045 — Buffered host audio and displacement-based pointer controls

**Context:** The user reports successful smartphone startup, intermittent pulsing
startup audio, excessive two-finger map scrolling and slow cursor travel. The
host drained the first PCM DMA immediately. Camera input was a persistent analog
velocity proportional to displacement from the initial midpoint; stationary
fingers therefore kept scrolling. Click/selection steering still used a four-field
pulse followed by eight neutral fields.

**Decision:** Pre-roll 60 ms of source PCM before host playback and after an
actual starvation. Keep the 200 ms storage cap and the guest AI virtual-clock,
two-DMA FIFO deadlines unchanged. Fade in over 240 host frames. Separate intentional
buffer waits, real starvation and callback mutex misses in telemetry. This adds
bounded output latency rather than changing guest simulation speed.

Read the researched 1P camera offsets/bounds from the same owned canonical
render snapshots as the cursor. Translate fingertip/middle-button displacement
to a clamped camera target in native-image units and steer with original Z+stick,
two-field pulses and fresh feedback after neutral settling. Stop within one
4-pixel original camera quantum; lifting either finger cancels pending steering.
Keep split-screen/invalid/future/stale feedback disabled. No guest RAM is written.

Use the continuous, divisor-aware predictive cursor servo for both free hover and
queued touch/selection arrivals. Increase its native-image speed cap from 3 to
4.5 pixels per field and reduce the proportional horizon from 5 to 3 plus observed
snapshot age. Retain fresh observed arrival before submitting A/B. Incomplete
diagnostic feedback retains the prior bounded fallback.

**Consequences:** The map follows a bounded displacement instead of behaving as
an infinite joystick drag. Original integer camera steps and input/render latency
remain visible. Faster cursor steering does not raise guest VI frequency or game
speed. Buffering can absorb brief startup stalls; sustained producer slowness or
audio-device loss still needs separate measurement/recovery (D-028/D-035/D-036).
Actual audible quality and physical gesture feel require another smartphone run.


## ADR-046 — Service guest audio during software rasterization

**Context:** The user reports menu music still pulsing/crackling after ADR-045,
while mission audio sounds normal. A new 100-second desktop main-menu/attract run
reproduces PCM starvation despite the larger buffer and removal of the device
callback's producer mutex. Guest graphics submission rasterized a complete frame
synchronously without servicing external events/runnable guest priorities.

**Decision:** Add an optional offline-independent checkpoint to software rendering
at rectangle/32-row boundaries. The host throttles servicing to roughly 1 ms
intervals and invokes the pinned runtime's existing
`wait_for_external_message_timed(rdram,0)` / `check_running_queue(rdram)` APIs.
The latter only switches to a higher-priority runnable guest thread. Rasterization
uses exclusively owned snapshots/results during these switches. Resume guest
ownership before committing only the actual RDP stores. Keep SP/DP completion
behind writeback, preserve the single-submission guard, and never hold the
publication mutex across a scheduler switch. No engine graphics API moves threads.

The device callback now consumes a fixed SPSC inbox; guest producers remain
serialized by the AI mutex. Only the device owns interpolation/playback state.
Frame rate generations discard obsolete PCM at frequency changes. The inbox and
playback storage are fixed; each has a 200 ms logical cap at the source rate, and
the drained playback queue is capped at 200 ms. Full inboxes drop incoming frames;
playback overflow drops oldest frames, both counted. Guest AI FIFO/deadlines remain
independent of host pre-roll. Pre-roll is sized to two DMAs with a 60 ms floor and
160 ms ceiling; interpolation lookahead is included. Smooth the bounded 0.5%
clock correction over two seconds to avoid DMA-cadence playback-rate jumps.

Use checked NDK AAudio APIs on the pinned miniaudio stream before starting it:
request at least four bursts or 1536 frames, limited by capacity. Request four
periods at device creation. Device callbacks report actual AAudio xrun/buffer
counts; unsupported backends report xrun=-1. These APIs are linked only on Android,
and audio stays independent of engine graphics. Stream/device reroute recovery
remains D-036; this change does not certify it.

**Consequences:** The same desktop main-menu route changes from four cumulative
starvations by the last menu sample to zero, with no discarded PCM. This is
native scheduling/producer evidence, not proof of phone audibility. An attract
loading transition still causes a starvation and is reported separately.
`software_ms` remains wall time and now includes time spent servicing other guest
threads; it is not a pure raster CPU measurement. Output latency can rise for
large DMA blocks/device burst sizes. Hardware sound quality requires retest.


## ADR-047 — Contextual system Back and worker resource taps

**Context:** Esc/Android Back was mapped unconditionally to original B, which does
not open the mission menu. A plain pointer tap always selected with A, including
mineral containers when a worker was selected. The original controls distinguish
selection (A), contextual order (B), and mission menu (Start).

**Decision:** Resolve system Back at key-down: fresh plain gameplay sends Start;
build, advisor, pause/front-end menus and unknown/stale feedback send B. Retain
that chosen mask through release/minimum pulse so opening a menu cannot convert
the same held key into B. Physical B and the existing C keyboard binding stay B.

For a plain mouse/touch tap, steer and settle the original cursor first. Observe
its original hit-test return value at `0x8001AD78`; record the sampled cursor point,
camera and selected-list context in owned render metadata. The original cursor
query entry `0x8001AE94` initializes an A result, including minimap/command paths
which bypass the unit hit test. If the current own selection includes a worker
(unit-table bit 3), a hit on an own/neutral resource container (bit 13) sends B.
Other hits send A. Missing, mismatched or stale metadata waits and eventually
cancels rather than guessing an order. Explicit B orders, HUD targets and A-held
selection rectangles keep their existing routes.

**Consequences:** One tap can order a selected worker to gather without first
selecting the mineral patch. Selecting another friendly unit remains possible.
The original game still decides order legality, pathfinding and resource behavior;
the adapter neither writes selection/order memory nor duplicates sprite hit tests.
The observed profile is NTSC-U, single player. Two new guarded observers are
required in personal regenerated C; desktop/Android build validation rejects older
output. This supersedes the unconditional system-Back rule in ADR-034.


## ADR-048 — Sliding map gestures pass through command hints

**Context:** The original command-hint canvas captured a press until release,
even after movement cancelled its button. A second finger starting on the HUD
could also clear a first map contact. This left the lower-right area unusable
for selection rectangles and two-finger camera gestures.

**Decision:** Keep the HUD as a tap candidate while no map gesture owns input.
Once a map contact/rectangle/pan exists, every subsequent contact and movement
bypasses HUD hit testing, including a second finger pressed on a hint. A fresh
game-mode HUD press transfers to the existing map controller when it moves more
than three native pixels (the existing selection-drag threshold), or when a
second finger arrives. Replay the uncommitted original press and current event,
then discard its hint queue/feedback. The command canvas and game code stay intact.

A stationary two-finger press originating on the HUD cancels the hint without
a terrain B order; ordinary two-finger map taps retain B. Right-click cancellation,
stale/modal ownership, ordinary hint taps and their visual press feedback remain
in their existing routes. Start a joined two-finger pan at the contacts' current
positions, preventing displacement from the first finger's earlier selection
motion from becoming a camera jump.

**Consequences:** Rectangles can start in or cross the hint canvas; two fingers
can start on either side or both inside it. Sliding gestures do not submit a
hint button. Tap jitter within three native pixels still activates a valid hint.
No guest selection/camera writes or engine graphics calls are introduced. The
input boundary uses owned fresh UI feedback, and physical phone feel remains a
separate acceptance check.

## ADR-049 — Direct navigation through the visible minimap

**Context:** The user requests desktop-style minimap camera navigation without
waiting for the controller-driven game cursor to reach the clicked point.

**Decision:** Read the original 1P visibility flag, inclusive hit rectangle and
map conversion parameters from owned render snapshots. Intercept mouse clicks
and held mouse movement inside this rectangle before HUD/terrain input. Touch
commits a single-finger tap on release; sliding or competing contacts cancel.
Keep the OS pointer visible over the minimap and suppress the terrain cursor
servo while the minimap owns the pointer.

Send a latest-request mailbox to the existing guest cursor-query entry hook
`0x8001AE94`. On that guest thread, recheck live visibility, 1P mode, modal/build
locks and the map geometry stamp, then apply the original `0x800365CC` viewport
centring and `0x80034DB8` pixel conversion. Call original camera setter
`0x8000B988`, preserving the interrupted query's complete register context.
The setter owns camera clamping, tile-cache updates and redraw notification.
Host/render threads never access live guest RAM. Pause/focus loss clears requests;
future or older-than-12-field requests are rejected.

**Consequences:** Minimap navigation sends no A/B/stick input and does not write
cursor/selection/order state. This is a scoped exception to the controller-only
camera adapters of ADR-045/048: camera changes occur through original guest code,
not the cursor servo. Right-click orders and existing map/controller gestures
retain their routes. Split-screen remains outside this profile.

## ADR-050 — Android compatibility output and front-end PCM reserve

**Context:** The user reports persistent menu music pulsing on a phone,
Android 16, including after returning from a mission. Desktop menu and mission
audio are reported normal. Previous zero-starvation emulator snapshots did not
establish audible phone quality. A bounded Android PCM trace now observes source
DMA arrival gaps of 84.45 ms (OpenSL) and 96.58 ms (AAudio), with two partial/silent
AAudio output blocks in the latter ten-second window. Average source delivery
remains near 11025 Hz. These measurements establish emulator delivery jitter,
not a vendor driver defect or the phone's complete root cause.

**Decision:** Default Android to pinned miniaudio's OpenSL ES backend, signed
16-bit stereo, 48 kHz, four 1024-frame periods and the media stream category.
If device initialization fails, try conservative AAudio with the same format.
Keep `sc64.audio_backend=aaudio` as an explicit comparison profile using the
previous float/512-frame low-latency route. Actual backend and buffer size appear
in telemetry; OpenSL xrun=-1 means unavailable, not zero.

Use a 160 ms pre-roll floor while the observed original guest mode is 4
(front end/intro/campaign menus). On a game-to-front-end transition, rebuffer once
without deleting queued source samples. Other modes keep the previous DMA-sized
60..160 ms target. Compatibility PCM queues have a 300 ms logical bound and fixed
28802-frame storage per lane, providing room for late DMA bursts above pre-roll.
The first 160 ms candidate with the old 200 ms cap discarded source samples and
is rejected. Desktop and explicit AAudio retain the previous 200 ms logical cap;
desktop output format/periods and latency policy are unchanged.

**Engine API check:** The former engine's public sound API offered no PCM
producer callback; its streaming and OpenSL device code was internal only, so
the change uses the same platform API family independently of its mixer. WAV
chunk playback is not selected as a live-stream substitute. OpenSL is a scoped
compatibility route, not a general replacement for modern AAudio/Oboe
recommendations.

**Diagnostics:** Optional private PCM traces capture source and pre-device output
in separate single-writer preallocated lanes for at most 600 VI fields. File I/O
occurs only after both guest and device threads stop. Output trace is float PCM
before final signed-16 quantization, not a hardware loopback. A 12-second storage
limit reports truncation; recordings and derived APKs remain ignored. Ordinary
smoke gates reject the intentional timed exit of diagnostic APKs; their PCM
reports must not be labelled process-survival/lifecycle acceptance.

**Consequences:** Menu buffering absorbs measured near-two-DMA delivery gaps at
the cost of more presentation latency. Game AI busy/full/deadline semantics, VI
cadence, source music and graphics-thread ownership stay intact. Engine
internals are not linked by this adapter. Physical phone listening before/after
a mission remains required; software counters cannot certify audible quality.
The final APK's extended emulator run also retains three menu starvations near
a sampled 688.755 ms software-render delay. The buffer is a bounded mitigation
for measured delivery jitter, not a guarantee against arbitrary stalls or a
closed physical-device audio acceptance gate.

## ADR-051 — Service guest audio throughout display-list interpretation

**Context:** The user reports that ADR-050's OpenSL compatibility profile makes
interruptions worse on an Android 16 phone. Physical AAudio instrumentation
then shows a healthy device callback (zero hardware xruns), but only 10247.26
source stereo frames/s against the configured 11025 Hz, maximum source gaps of
104.21 ms and 551 software starvation callbacks in a 55.06-second menu window.
The interpreter runs synchronously on the submitting guest thread. ADR-046 only
services external messages during rasterization, leaving display-list parsing,
texture decoding, content hashing and atlas creation uninterrupted. Enlarging
a reserve cannot compensate for a sustained source production deficit.

**Decision:** Restore the AAudio float/512-frame profile as the Android default,
including the previous DMA-sized pre-roll and 200 ms logical storage cap. Keep
the rejected OpenSL profile available only through explicit `compatibility`.
Supply an optional host-independent interpreter checkpoint at commands and
bounded background/decode/hash/atlas loops. Both interpretation and software
rasterization use one host callback, throttled to approximately 1 ms, which
calls the pinned runtime's `wait_for_external_message_timed(rdram,0)` and
`check_running_queue(rdram)`. No scheduling is added during the live RDRAM copy:
complete the coherent snapshot first. Subsequent checkpoints operate exclusively
on owned bytes. Retain guest priorities, SP/DP completion after actual RDP stores,
AI FIFO/deadline semantics, original samples and engine graphics ownership.

Expose windowed maximum source/callback gaps, callback wall time and cumulative
source/nonzero frame counts. The callback performs no allocation or log/file I/O
and adds no producer mutex. Pause/rate-change epochs invalidate lane baselines,
so intentional suspend intervals do not become audio-gap peaks. Collection is
explicit through `take_audio_timing`, once per existing five-second log window;
ordinary `get_info` calls do not clear diagnostics. `tools/android/audio_report.py`
collects only the selected app PID and binds an installed APK hash without
restarting the game. Played-output silence excludes unplayed waiting/starved
frames and must not be used as an overall device silence ratio.

**Consequences / validation:** The same physical device with the same AAudio
profile now supplies 11029.00 source frames/s; its 55.06-second sampled menu
window has zero starvation/discards/hardware xruns and a 61.01 ms maximum source
gap. The user confirms normal menu sound from the installed APK. Interpreter
wall time still reaches roughly 60..71 ms and includes guest audio service;
this change does not claim faster rendering. Desktop PCM and snapshot/output
equivalence fixtures pass. This accepts the reported menu defect for the tested
phone route; two manual mission returns, headphone routing, audio-device loss,
other phones and thermal sessions remain separate acceptance work.

## ADR-052 — Preserve texture keys while reducing interpreter hashing work

**Context:** Stage 5 remains open after the physical phone menu audio repair.
The audio-safe interpreter still has 60–71 ms sampled wall times on that phone.
A repeated fenced main-menu replay on macOS arm64 identifies content hashing as
the dominant CPU work: 650 of 831 sampled stacks are on its channel hash loop.
The existing FNV encoding feeds each channel/TMEM byte as a little-endian
64-bit value, including seven zero bytes. Atlas construction also precedes the
semantic check for an already-owned identical texture.

**Decision:** Retain the existing key format exactly. For values known to be
bytes, replace the eight FNV steps with `(hash XOR value) * prime^8`, using
unsigned 64-bit arithmetic modulo 2^64. Keep the full eight-byte path for tile
parameters and dimensions. Compare the complete existing semantic payload
before building the atlas; a matching texture reuses its owned atlas, while a
hash collision with unequal semantics still fails. Add no cross-task cache or
mutable guest pointer. Keep all decode/hash/command checkpoints and the owned
RDRAM snapshot, ordered RDP writeback and completion protocol from ADR-051.

**Consequences / validation:** An independent slow hash fixture covers all 256
component values and CI8/TLUT changes; reuse fixtures preserve atlas bytes and
confirm that repeated rectangles skip atlas work. A preserved baseline binary
and the optimized binary produce exactly identical full rendered RAM, texture
keys and command accounting on all 20 fenced menu/briefing/1P/2P cases. Twenty-five
measured interpretations per case, after one warm-up, show a median 75.88%
main-menu time reduction (roughly 31 ms to 7.5 ms) on this macOS arm64 host.
Every original RGB comparison still meets 40 dB; minimum is 41.61048 dB.
These offline times exclude rasterization, guest/audio service, presentation
and device thermal behavior. Android measurements and stage-wide acceptance
remain separately recorded in `performance.md` and `android.md`.

## ADR-053 — Recognise double touch taps before cursor arrival

**Context:** The user reports that double-tapping a movement destination often
selects that area instead. The adapter queued every one-finger release as A;
the time between original controller edges depended on cursor travel, settling
and neutral gaps. It had no explicit physical double-tap recogniser. Original
selection is A, whereas B invokes the game's contextual order resolver (the
manual/source evidence is recorded in `research.md`).

**Decision:** On the plain game map, retain a single touch tap as a candidate
for 18 VI fields (approximately 300 ms). Cursor steering starts immediately;
its button edge waits. A second touch beginning within that interval and 12
native-image pixels consumes the candidate. On the second release, submit one
explicit B at that release point after fresh cursor arrival; no A precedes it.
Allow up to 10 pixels of second-contact drift before treating it as a deliberate
selection drag. A larger slide discards the pending order and uses the existing
A-held rectangle route. This is a presentation/input choice, not a claim that
the original game or Android supplies these thresholds.

Candidates are stamped with the owned UI token and selection context. Changed
contexts cancel them; stale snapshots wait, and existing focus/controller/menu
ownership clears them. Mouse selection remains immediate. HUD, minimap and
modal hit tests still run first; their taps never enter this recogniser. Single
touch taps retain the original arrival hit-test policy, including automatic B
for a selected worker tapping resources. No guest memory or game handlers change.

**Consequences:** Touch selection can wait up to 300 ms when the cursor is
already at its target. Cursor travel can hide that wait; the second tap resolves
the order immediately on release, subject to fresh cursor feedback. Explicit
right-click/two-finger B and keyboard controls keep their existing paths.
`tests/native/test_map_taps.cpp` covers delayed travel, poll cadence, contact IDs, jitter,
single/resource taps, late/distant taps, slides and cancellation. The production
GUI fixture verifies MULTI_TOUCH routing and duplicate mouse suppression;
in-memory mutations removing the delay or changing B to A both fail the test.
Native and physical-device evidence is recorded separately in `input.md`.

## ADR-054 — Verify Android page size and graphics adapter at runtime (adapter part SUPERSEDED by ADR-074)

**Context:** The personal APK's ELF/ZIP 16 KiB alignment already passes, but
the phone and previous emulator use 4 KiB kernels. Stage 5 still needs actual
16 KiB execution. The smoke tool checked guest/PCM progress without requiring
presentation recovery or confirming which adapter a requested profile used.

**Decision:** Extend the existing smoke tool with optional required kernel page
size and explicit `opengles`/`vulkan` requests. Verify ARM64/page size before
installation or restart. Use the pinned GLFW string-array Intent extra for
`--graphics-adapter`, then compare the engine thread's installed adapter-family
telemetry against that request.
Require advancing presentation, including after Home/return, alongside guest
and PCM counters. Fence log collection to the new launch, preserve installed
APK/hash checks and reject candidate file changes during the run. Exclude
suspension-crossing intervals from cadence estimates. Emit schema 2 receipts
with explicit bounded acceptance or the failure reason; physical-device
acceptance remains separate.

Use a separate, ignored official Android 16 ARM64 16 KiB AVD. No phone kernel
or existing AVD is changed. Keep the current APK, default adapter selection,
gameplay/input, audio scheduling, software renderer and graphics ownership.

**Consequences / validation:** Five additional regression tests and the existing
suites pass (149 Python/Lua and 13 CTest). The actual desktop application runs
35.86 seconds with unchanged artifacts and zero PCM underruns. The same locally
audited APK passes 55-second 16 KiB Vulkan and OpenGL ES intro/Home-return
windows, with actual families 4 and 3, advancing post-return guest/PCM/images,
and zero software underruns/discards/hardware xruns. The existing four COP1
rounding/subnormal probe also passes on that kernel. Later menu rendering is
visually inspected separately. This closes bounded 16 KiB startup/recovery,
not physical GPU parity, mission completion, broad VR4300 numerics or sustained
performance. Commands, hashes and receipts are in `android-runtime.md`.

## ADR-055 — Close the personal Android profile with explicit exclusions (ACCEPTED)

**Context:** The user requests Stage 5 completion while omitting expanded device
validation and long Adreno/Mali performance/thermal sessions. The local personal
APK already has private-input/package audits, ROM import/asset loading, Flash
persistence, phone menu-audio acceptance and actual 16 KiB Vulkan/OpenGL ES
startup/Home-return evidence. The user now confirms the first Terran mission
with touch only and save/load after a full restart on the phone.

**Decision:** Close Stage 5 for the profile in `stage5-acceptance.md`, keeping
human acceptance separate from recorded native recipes and emulator checks.
Accept the successful pinned local SDK/CMake engine link (ADR-042) as the
personal delivery path; conventional Docker-hosted engine packaging is an
optional unimplemented alternative, not another delivery gate. Add the completed
supported-helper FPU probes on desktop/phone/16 KiB Android and the actual Android
Back/Home mission receipt. Preserve raw attempts, APK/test hashes and numerical
limits. Defer expanded device/input/backend/surface/audio-route qualification
and long Adreno/Mali cadence/thermal sessions rather than marking them passed.

**Consequences / validation:** 149 Python/Lua checks and 13 CTest checks pass.
The unchanged APK passes its alignment/ROM audit again. The Android mission
receipt shows `game -> modal -> game` after system Back, matching screenshots,
the same PID, zero guest-counter suspension delta and advancing post-return
guest/PCM/presentation. No guest error or native crash is observed. The final
FPU executable passes on both Android kernels; its exact helper scope and the
earlier artificial FZ16 probe limitation are documented. Human phone acceptance
is not a newly recorded walkthrough or an exact mission-session APK identity.
No broad device compatibility, midrange frame rate or thermal result is claimed.
Stage 6 entry is permitted, with implementation awaiting a separate user request.
(Superseded: Stage 6 was withdrawn by ADR-060 on 2026-10-09.)

## ADR-056 — Opt-in image sampling (withdrawn by ADR-060)

**Context:** The user requests the next stage after scoped Stage 5 closure.
The accepted renderer produces an owned native RGBA image; original text is
already painted into a CPU indexed canvas. Camera, viewport offsets and image
strides also feed cursor/minimap calculations. Replacing textures or changing
the output aspect ratio alone cannot establish HD text or a wider game view.

**Decision:** Start Stage 6 with Original/Sharp/Smooth image sampling and saved
Integer/Fit scaling. Default to the previously accepted Original/Integer path.
Use the pinned engine's render constant buffer and material shader, entirely on
the engine/render thread. Sharp implements nearest integer prescale plus bilinear
resizing without an intermediate target; Smooth uses bilinear, both with four
clamped texel reads. Keep the existing native image, guest/RDP/audio paths and
one viewport calculation for rendering and pointer inversion.

Keep preference validation, persistence and feedback in a shared Lua module.
Add Image/Scale to the existing temporary command panel, with matching contact
ownership and slide cancellation. F5/F2 provide desktop shortcuts. Failed saves
report failure while retaining the working choice. Explicit launch overrides
are transient; static GPU fixtures ignore saved choices, never persist them,
exclude GUI and suppress guest autostart so one fixture owns the mailbox.

Require the Original pixel gate to remain exact. Check optional modes against
an independent virtual two-pass image oracle, with a maximum 1/255 channel
rounding difference. Build desktop first, then verify actual Android adapters
and requested/observed filter modes. Record original-font/widescreen anchors
as static evidence; D-043/D-044 require layout/visibility experiments before any
guest patch. No extracted assets or generated game code enter Git.

**Consequences / validation:** Desktop/Android builds, 154 Python/Lua checks and
14 CTest checks pass. Twenty macOS Vulkan frames per mode preserve native RGB;
Original output is exact, both optional fractional-fit modes pass within one
channel level and keep black letterboxes. The fresh native original gate remains
41.61048 dB minimum. Sharp/Vulkan and Smooth/OpenGL ES pass bounded 35-second
16 KiB emulator Home/return windows with the same audited APK. Actual keyboard
changes and cold settings restoration are observed; GUI contact behavior is
fixture-tested, while automated macOS pointer injection delivers unrelated
coordinates and does not prove a native settings click. Phone settings taps,
HD fonts, widescreen, long GPU costs and expanded device qualification remain
open. Commands, hashes and exact acceptance scope are in `presentation.md`.


## ADR-057 — Independent glyph experiment (withdrawn by ADR-059)

**Context:** CPU-painted CI8 text cannot gain detail from image sampling alone.
Original glyph calls expose resolved placement and source metrics; guest canvases
also contain non-text graphics. The user reported that the initial bright-pixel
HD bounds changed letter size and shape during menu animation.

**Decision:** Observe bounded original glyph source metrics and before/after cells
without changing guest state. Keep the observers active for retained canvases,
copy metadata coherently before checkpoints, and reject stale/unproven cells.
Preserve original raster writes before preparing an optional owned HD raster.
Draw independently licensed Noto Sans ASCII through an input-free engine GUI
using the existing viewport. Restore only proven glyph changes/shadows. Unsupported
compositions stay Original. Default to Original; add saved Text choice/F7.

Derive geometry from bounded original encoded shade/skip data rather than animated
RGB. Palette animation controls color only. Guard source/canvas bounds and fall
back for malformed or unproven streams. Do not ship reconstructed original fonts.

**Consequences:** HD adds a second software pass and GUI glyph draws. Baked text,
unsupported glyphs and untested 2P HD coverage remain original. Test read-only
observation, retained redraw, shade-independent geometry, clipping/overlap and
settings ownership. Native builds, original-frame gates and actual desktop/phone
layout evidence are recorded in `presentation.md`; no broad HD parity claim is
made from a fixture or a successful build alone.

## ADR-058 — Wider geometry research (work withdrawn by ADR-060)

**Context:** Original allocation, cache traversal and camera clamps depend on
viewport extents also used by cursor/minimap conversion. Eight guarded original
snapshots and nineteen ROM instructions establish those dependencies. A wider
counterfactual changes allocation requests and the reachable camera bounds.

**Decision:** Publish the reproducible original-geometry audit without enabling a
wider guest patch. Preserve Original aspect and shared pointer mapping. Require
paired original/wider selection, fog/visibility, script and save experiments
before deciding gameplay changes. Keep D-044 open; no unanswered scope question
is treated as authorization to defer a required Stage 6 gate.

**Consequences:** Fit/Sharp/HD text can be tested independently; they do not create
a wider map. The cache-request arithmetic is evidence of dependencies, not a
widescreen execution receipt or proof of a fog/culling change. Full Stage 6 closure
requires the wider experiment or explicit user acceptance of a narrower scope.


## ADR-059 — Remove rejected HD font support and preserve original typography

**Context:** The user reports that animated menu glyphs change size/shape and
that the replacement font still looks foreign and awkward after the attempted
metric correction. The user explicitly asks to roll back all HD font support.

**Decision:** Remove the independent atlas, separate GUI, settings/bindings/APIs,
text observers and optional presentation raster. Regenerate private game code
from the existing UI-only config with no text callbacks. Ignore obsolete `text`
fields in saved presentation records, retaining image/scale preferences. Keep
original font artwork/metrics for all scenes and supported sampling modes.

**Consequences:** D-043 is cancelled by user choice rather than marked visually
passed. Historical private evidence/builds remain preserved; new desktop and
Android builds need their own tests, original pixel gate and phone install.
Original/Sharp/Smooth and Integer/Fit continue unchanged. Widescreen research and
D-044 remain separately gated; font removal does not authorize wider visibility.


ADR-059 verification: 154 Python/Lua checks and 14 CTest checks pass. The final
macOS Vulkan 20-frame gate is exact. Final Android OpenGL ES/16 KiB and phone
Vulkan Home/return windows pass after installed-APK hash audits. Both bundles
exclude all removed HD resources; generated input has zero text callbacks.
See `presentation.md` for artifact hashes, preserved attempts and evidence limits.


## ADR-060 — Withdraw Stage 6 and fix original presentation to Fit

**Context:** After rejecting HD fonts, the user also rejects image-filter choices
and scale settings, requesting complete Stage 6 rollback and always-Fit behavior.

**Decision:** Restore the original nearest shader/material, typography and
controller panel. Remove optional filter code, settings persistence, selectors,
Lua setters, F2/F5/F7 and filter/scale launch options. Fix the native mode to Fit
and remove integer rounding from the common viewport so old config or preferences
cannot affect rendering or pointer inversion. Preserve the fixture-only guest/GUI
isolation needed for correct original-frame validation. Withdraw widescreen work.

**Consequences:** D-043/D-044 are cancelled by explicit scope change. Historical
research, reports and authored withdrawn source remain preserved privately; they
are not current features or acceptance for new artifacts. Rebuild desktop then
Android, check original pixels and Fit mapping, audit the APK and replace the
personal phone installation without clearing saves or changing signing identity.
No guest code, ROM, audio path, camera or gameplay state is changed.

ADR-060 verification: 151 Python checks and 13 CTest checks pass. The final
desktop 20-frame gate preserves exact native RGB and valid nearest Fit sampling
(16 exact GPU frames; four fractional split-screen frames differ only at exact
texel boundaries). GPU menu/split-screen readbacks were visually inspected.
The final local APK passes manifest/content/alignment audit and an installed-hash
bound 45-second phone Vulkan Home/return probe. Historical evidence is preserved;
Stage 6 is withdrawn, with no remaining HD/widescreen acceptance requirement.
Artifact hashes, commands and scope limits are in `presentation.md`.

## ADR-061 — Gamepad connection alone does not disable pointer gestures

**Context:** The host and touch Lua scripts disabled all mouse/touch
gestures while any gamepad was connected, even when idle. No ADR or input
document required this; ADR-027 and `input.md` only state that competing
keyboard/gamepad *input* cancels pointer work. Android devices may report
non-pad HID devices as gamepads.

**Decision:** Gate gestures only on the visible diagnostic pad. Ownership stays
per event: actual keyboard/gamepad input still clears pending gestures. A
connected pad continues to hide the diagnostic touch pad.

**Consequences:** Mouse/touch remain usable with an idle pad attached. A
synthetic host regression covers connect → pointer B → pad input cancellation.
Physical hotplug and multi-touch device proof remain open under D-041.

## ADR-062 — Guest-thread failure reporting keeps the root cause

**Context:** `ultramodern::thread_terminated` derives from `std::exception`, so
the graphics-task handler swallowed shutdown unwinding, reported
"std::exception" and returned into guest code after quit. `Session::fail`
overwrote earlier errors, hiding the root cause. `~Session` ran `stop()` during
static destruction, which can deadlock when `std::exit()` runs on a guest thread.

**Decision:** Host work on guest threads rethrows `thread_terminated`
(`native/runtime/guest_errors.hpp`); the worker treats it as a clean exit. The
session keeps the first error. `~Session` only detaches; `stop()` belongs to the
finalize callbacks. A failed ROM launch validates the save before starting audio
and shuts audio down on later failures.

**Consequences:** Shutdown is no longer reported as a failure and the real cause
stays visible. Abnormal process exit no longer joins guest threads. The launch
rollback and destructor path have no unit test because they need the runtime link.

## ADR-063 — Parallel software rasterization by RDRAM address range

**Context:** phone telemetry shows `software_ms` medians of 6–13 ms with peaks
near 33 ms in menus, competing with audio service on the guest thread. The
rasterizer is pure over owned `DrawList` data, but each pixel's blend reads the
store of earlier rectangles and targets may alias in RDRAM.

**Decision:** `gfx_core` owns a persistent `RasterPool`. A task is split into
`4 * (helpers + 1)` even-aligned RDRAM address ranges taken by an atomic
counter; each range replays all rectangles in command order and only touches
its own bytes. Guest checkpoints run only on the calling guest thread (between
its ranges and while waiting); a checkpoint exception cancels unstarted ranges
and waits for running ones. The first failure in serial order (rectangle,
pre-pixel phase, range) is rethrown. `SoftwareResult::writes` becomes sorted,
merged byte ranges. Small tasks, odd target addresses and zero-helper hosts use
the same code as one unbounded range. The session creates the pool before
guest startup and destroys it in `stop()` after guest threads join.

**Consequences:** Rendered RAM, stored bytes and pixel counts are byte-identical
to the serial path (synthetic aliasing/blend/failure fixtures, 20 corpus cases).
Offline menu rasterization is 64.5% faster with three helpers. Helpers compete
with the engine render and audio threads on phones; phone cadence, audio and
thermal effects remain D-047, and `default_helpers()` is the tuning point.

## ADR-064 — Touch haptics and Rumble Pak output through the phone vibrator

**Context:** Touch orders and selections give no tactile confirmation. The
original game supports a Rumble Pak (research ledger: detected at startup;
motor actuation in combat untested), and ultramodern exposes
`input_callbacks.set_rumble` for `osMotorStart/Stop`.

**Decision:** Two independent layers, both gated by `sc64.haptics` (default 1)
through `sc64.set_haptics`:
- `haptics.lua` emits a short pulse on rising A/Start (12 ms) or B (22 ms,
  stronger) edges submitted by pointer gestures or the touch panel, at most one
  per 60 ms. Keyboard/gamepad presses never vibrate.
- Player 1 `set_rumble` sets a session flag; the engine thread maps it to
  re-armed 5-second one-shot vibrations (`rumble_motor.hpp`) and stops them when
  the guest stops the motor, the game pauses, the app is inactive/iconified or
  the extension finalizes. Touch pulses never interrupt an active motor.

Android uses `com.nomadic.sc64.Haptics` (VibrationEffect) and a project
manifest that is the pinned engine template plus `VIBRATE`. macOS is a no-op.
Haptic failures are logged and never fail input or the session.

**Consequences:** No guest code or controller semantics change. Telemetry adds
`rumble_starts`. Physical feel, amplitude and whether the game actuates the
motor during play remain device checks (D-048).

## ADR-065 — Group controls as staged original controller sequences; 1P layout gate

**Context:** The original assigns/recalls four groups with R+C / Z+C, cycles
on-screen unit types with a lone R tap and selects every unit of a type with
R+A at the cursor (research.md, 2026-10-10). Touch users have no modifier
buttons, and the timing is strict: C must be released while the modifier is
still held. D-042 also found map gestures hard-coded to 320x240, which dropped
2P (512x384) taps at x>=320 and cleared selection on drags crossing x=320.

**Decision:**
- `gestures.lua` gains a step player (`M.sequence`): each step lasts its field
  count from its first submission, so a late poll lengthens but never skips a
  step; completion leaves eight neutral fields. Assign = R 4, R+C 8, R 6;
  recall = Z 4, Z+C 8, Z 6; Next type = R 8; same type = steer the cursor with
  the existing arrival servo, then R 4, R+A 8, R 6. No guest writes.
- Group commands start only on a fresh, unpaused 1P mission map; the steered
  same-type action carries the selection stamp/token and is discarded when it
  changes. A dialog during a chord, keyboard/gamepad input, blocking and focus
  loss cancel it through the existing clear paths.
- Touch: a stationary two-finger hold of at least 36 fields selects the type
  under the first finger (shorter stays B, moved stays camera). Transient panel:
  Next type and Group 1-4 (tap recalls, 30-field press assigns).
- Map gestures, group commands and hover steering require the verified
  320x240 layout (`input_common.map_layout`). Elsewhere contacts only open the
  long-press controller panel and show a notice; panel button pulses still work.
- A menu click whose selection or target is not exposed in two successive
  fresh snapshots cancels with a notice instead of waiting 360 fields.

**Consequences:** All controls remain original controller events. Desktop
recipes prove assign/recall/next-type decoding for C-Up; R+A type filtering,
the other C slots, phone timing feel and 2P support are D-049/D-042 work.

**Amendment (2026-10-10, phone feedback):** The added panel entries exceeded
`touch.gui` `max_nodes: 64`, so GUI init failed on device ("Out of nodes"):
HUD press highlights, the long-press panel and the group entries disappeared.
`max_nodes` is now 128 and a Lua regression keeps node creation below it with
headroom. The two-finger hold was not discoverable; same-type selection now
follows PC StarCraft: a double tap whose arrival pick reports the hit unit as
already selected (`cursor_pick.target_selected`, read-only check of selection
list `0x800C6C20` / count `0x800C6C14`) plays R, R+A, R instead of B; other
double taps remain B and two-finger holds do nothing. The open panel shows
"Group: tap to select, hold to assign" and a held group entry turns green as
"Assign group N".

**Amendment 2 (2026-10-10, user decision):** The panel "Next unit type" and
"Group 1-4" entries are removed as inconvenient, together with
`gestures.group` and the private `group` recipe item. Same-type selection by
double tap on an already selected unit remains the only group control. The
research facts about assign/recall/next-type decoding stay in research.md.

## ADR-066 — Faster cursor steering for touch, actions and selection drags

**Context:** Touch selection boxes waited for the original cursor to reach the
first corner and then the second at 4.5 native px/field with a gentle
proportional controller: a box across most of the map took ~103-178 guest
fields depending on snapshot lag. The user asked for faster boxes and cursor
travel. Arrival was decided from a lagged snapshot, so a faster controller
could press A while in-flight stick motion still carried the cursor past the
point (a lag-6 fixture overshot).

**Decision:** Split controller parameters. Mouse hover: 8 px/field cap
(was 4.5), horizon 3 + snapshot age. Touch targets, queued actions and drags:
16 px/field cap (original maximum ~16.8 at divisor 200), horizon
1 + 1.5 * age. Arrival additionally requires a cursor snapshot newer than the
last non-zero steering input plus `ARRIVAL_MARGIN` (2) fields. No guest write
or cursor warp; all motion stays original stick input.

**Consequences:** In the original-like simulation a 200x120 touch box
completes in 56/75/108/161 fields at lag 0/1/3/6 (was 103/115/142/178); both
corners stay within 1.5 px. Android logs show snapshot age mostly 2 (lag 1).
Teleporting test fixtures now wait the margin. Further speed would need a
researched, guarded cursor warp (not done).

## ADR-067 — Measured menu pulse timing with bounded retry

**Context:** Menu taps reach original selections through one D-pad/stick pulse
per step: 6 fields held, 12 neutral, then a wait for fresh feedback; 18 fields
(~0.3 s) per item, and a step without a response waited 90 fields before
cancelling. Source reading shows the frontend list/tab helpers are
level-triggered with a 250 ms repeat gate that a sampled release resets, the
in-mission dispatcher is edge-triggered, and the build grid re-arms on stick
release. Native trains (research.md, 2026-10-11) lost no press at >=3 fields
held and >=3 neutral in the main menu (136/136), Single Player rows (4/4:
16/16) and pause (40/40); 2-field holds or 1-field releases lost presses.
Single Player tabs ignore input for ~55 fields after a change, so a two-step
tab route stopped on the middle tab under the old adapter.

**Decision:** `PULSE_FIELDS=4`, `NEUTRAL_FIELDS=4` (one field above the measured
minimum, far below the 15-field auto-repeat). Navigation stays closed-loop:
the next step needs a fresh snapshot showing the move. If a snapshot 30 guest
fields after a press (`RETRY_FIELDS`, above the ~21-field tab acknowledgement)
still shows no move, the same press is repeated. After 90 fields from a step's
first press the next unanswered window cancels (`CANCEL_FIELDS`; at most ~120
fields; campaign 180). A double move from a late acknowledgement is corrected
by the normal route from fresh feedback; the six-entry ring keeps its
shortest-direction rule. Advisor confirm delay, HUD hint pulses and map
gestures are unchanged.

**Consequences:** Rows/lists/grid: 8 fields per step instead of 18 (six rows
then A: +48 vs +108; grid diagonal then A: +16 vs +36). Two-step tab routes
complete in 52-104 fields instead of stopping. Activation A/Start also uses
4 fields; intro, briefing, advisor and main-menu A were accepted natively.
Results/Victory tabs, options screens and episode rows were not rerun, and two
of nine desktop tab runs stalled for reasons not yet explained (D-050).

## ADR-068 — CPU framebuffer scanout and VI-scaled presentation

**Context:** On Android the boot logos and the title appeared only for about 12
fields and the opening animation was a strip in the upper half. Original
ares/angrylion timelines (research.md, 2026-10-11) show the accessory screen,
Nintendo, Blizzard, Mass Media and title held for 219/130/130/128/295 fields.
During those holds no RSP task runs: the CPU decodes each image into the two
640x480 RGBA16 framebuffers and swaps them. The port only presented completed
RDP task targets, so each screen was visible only during its RDP fade-out.
The opening animation's 320x240 target is scanned with VI Y scale 716, so the
VI raster covers 168 rows; the port showed all 240 rows at 4:3.

**Decision:** The desktop/Android runtime copy calls `sc64_vi_swap` at the
start of `osViSwapBuffer` on the guest thread. If no RDP task targeted that
buffer since its previous swap, the RGBA16 bytes (VI width, rows covered by
the Y scale) are copied into an owned `CpuScanout`. The engine presents it
when the VI origin matches and it is newer than the matching task image.
A newer drawing task retires it only after that task completes; VI
blank/mode/width transitions retire older CPU images through the existing
task floor. Presentation then resamples the native image's VI-covered region
(640 output pixels and 240 lines per field through the X/Y scale registers)
over the native image size with nearest sampling. All recorded menu, 1P, 2P
and CPU-logo modes cover their whole target, so they are unchanged. Guest
memory is only read; there is no guest patch and no new input.

**Consequences:** The boot screens are held for their original durations and
the opening animation has the original letterbox. Validated scenes keep
native coordinates; the fenced RGB gate and the GPU presentation probe are
unchanged. A CPU frame costs one 600 KiB copy per swap while no task draws
the buffer; repeated identical swaps are not converted again. Remaining limits
are in D-051. The boot screens accept no A, B or Start input in the original,
so no skip was added.

## ADR-069 — Left mouse clicks join the double-tap recogniser

**Context:** The user reports that on desktop a double click neither issues a
move order nor selects units of the same type. ADR-053 and ADR-065 made the
recogniser touch-only (`is_touch` gates in `map_taps`), so two left clicks were
two plain A selections.

**Decision:** Left mouse clicks on the plain game map use the same candidate
window (18 VI fields, 12 native pixels) as touch taps: a double click is B at
the second release, or R, R+A, R when the arrival pick reports an already
selected unit. Right click stays an immediate B. HUD, minimap and modal hit
tests still run first. No guest memory is written.

**Consequences:** A single left click on the map now waits up to 300 ms for a
second click (hidden by cursor travel when the pointer is far). The ADR-053 note
"Mouse selection remains immediate" no longer holds for the map. Desktop feel is
not yet human-verified (D-049).

## ADR-070 — Two-Player is unsupported

**Context:** The user reports that the Two-Player setup screen cannot be
changed or started with mouse/touch. The observer exposes no items there, and
split-screen play needs a second controller: only a second physical gamepad
drives player 2, and mouse/touch gestures are gated to the verified 1P layout.
Playing both halves alternately would need a second cursor, camera, pick
context, command panel and minimap model (the 2P cursor words near
`0x800AFDC0`/`0x800AFE1C` move with player 2's stick in `feedback-2p`, but
their layout is unresearched), and the setup offers no computer opponent.

**Decision:** The mode is disabled in the input adapter only. A click on main
menu (token 256) item 1 shows "Two-Player is not supported" and presses
nothing. If the setup screen (menu 11, token 267, reached natively by
clicking that item) is entered by another route, the adapter presses the
original Back button once per 30 fields until the screen changes. Guest
memory is not written, so the menu entry itself remains visible.

**Consequences:** Removing the entry would need a guarded guest patch with
original evidence, which was not requested. A controller user can still select
the item and is returned by the repeated Back after a brief flash of the setup
screen. Existing 2P layout gating stays for a split-screen session already
running (for example from a saved state).

## ADR-071 — Corner buttons for pause and minimap; six-entry fallback panel

**Context:** The long-press panel offered 14 controller commands, of which only
Pause/menu and Minimap had no direct equivalent: Build is reached through its
original HUD hint after selecting a worker, orders/Back/Continue/C buttons/menu
arrows have gestures or hints, and Back/Esc already opens pause. The panel was
hard to discover, covered the screen centre, and hid Minimap behind a 0.5 s
press plus a second tap. A permanent joystick overlay was rejected by ADR-027
and would only bring back slow stick-driven cursor travel (ADR-066).

**Decision:** `hud_buttons.lua` adds two small buttons in the left image corners
of the 1P mission image: a sandwich (Start) at native (2,2)-(14,14) and a minimap
glyph (Z+L, dimmed while the original minimap is hidden; its dot sits bottom-left
like the player marker on the real map) at (2,226)-(14,238).
Selection text and the minimap start at x=17, so the buttons overlap no original
HUD. A tap needs press and release on the same button; sliding off, a second
finger, a modal screen, pause, stale UI feedback or a non-320x240 layout cancel
it. Hit areas extend into the letterbox (via the unclipped
`viewport.to_native_raw`) and along the free strip x<16, never into the HUD.
Both buttons are drawn in the HUD palette (navy plate, blue frame, lavender
glyph, gold outline on hover/press) and take pointer events before UI/map
routing. The fallback panel keeps only Continue (A), Back (B) and the four menu
arrows for unresearched dialogs; Order, Pause, Build, Minimap and C entries are
removed. No guest memory is written.

**Consequences:** Minimap and pause are one tap away without covering the map.
On macOS the system pointer stays hidden over the image corners (only hover
highlighting indicates the button); showing it would need extra visible regions
in `set_cursor_hidden` (D-052). Physical look and touch feel are unverified.

## ADR-072 — Encyclopedia buttons and article swipe

**Context:** The main-menu Encyclopedia (token 264) exposes no UI items, so
mouse/touch could only open the fallback panel and press D-pad edges one by one
(research.md, 2026-10-11: D-pad Right/Left change article, B returns; ADR-073
adds the link rows).

**Decision:** `hud_buttons.lua` gets a second button set for token 264 on the
640x480 target, drawn with the ADR-071 plates: a sandwich at the top-left (B,
back to the main menu) and chevrons at the bottom-right (previous = D-pad Left,
next = D-pad Right). Geometry is kept in 320x240 units and scaled by one half
for menu images. A horizontal swipe of at least 24 px (and 1.25 times the
vertical travel) pulses Right for a left drag and Left for a right drag, like
the main-menu carousel. Desktop arrow keys already map to the same D-pad edges
(`game.input_binding`), and a regression asserts it. A finished menu pulse no
longer blocks the buttons: only a live contact or HUD candidate owns the pointer.

**Consequences:** Reading the entire Encyclopedia works with mouse, touch or
keyboard without the panel. List wrap-around, other categories and the layout
of articles beyond the Terran start were not inspected (D-052).

## ADR-073 — Encyclopedia article links through three read-only draw observers

**Context:** Articles list "Made by", "Makes" and "Needs" links that Up/Down
highlight and A opens. A host-side decode of the article ring was built first
and works on emulator snapshots, but the highlighted index only exists in the
menu thread's stack frame, whose address differs between the emulator and the
port, and the shown record/target globals cannot tell apart a target that is
both made-by and needs (Marine: Barracks twice). The article is also drawn only
when it changes, so per-frame observer data is empty most of the time.

**Decision:** `add_ui_hooks.py` gets three `jal 0x8003F070` observers in
`sc64_menu_801232C8` (made-by, makes, needs rows; ROM anchors checked) and
`sc64_ui_font` kinds 5-7 export group-7 items: id from the loop index, rectangle
from the call arguments cut to the 16-pixel row pitch, highlight from the colour
pointer, `context` = article record. `sc64_ui_blit` converts them to screen
coordinates and stores them in a list that survives `take_frontend_feedback`;
the made-by draw that opens each pass clears it, and `read_ui` drops group 7
outside menu 8. Lua treats them as a vertical list: a click navigates with
D-pad Down/Up and then presses A; a changed article cancels the click. Keyboard
Enter maps to A inside the Encyclopedia (original Start leaves it), and the
arrow keys are already D-pad. Games must be regenerated with the 33-hook
config (`sc64-menu-v6-ui.toml`); `game_validation.py` now expects eight
`sc64_ui_font` observers.

**Consequences:** Links are tappable and keyboard-operable on every platform,
without stack scraping or guest writes. Rows beyond the nine that fit the panel
are not tested natively; the Zerg and Protoss lists do not appear in this ROM's
Encyclopedia. Existing private generations lacking the three observers fail the
builder checks and must be regenerated.

## ADR-074 — SDL3 shell replaces the former engine shell

**Context:** The former engine served only as a shell: window, one RGBA
texture quad, input events, GUI overlay and packaging. Rendering, audio
(miniaudio/AAudio), lifecycle and ROM import were already authored. That
engine's cross-platform value is its cloud build service, which cannot receive
game-derived code, so every target needed a hand-maintained SDK relink
(ADR-023/042) and only macOS arm64 and Android existed (D-034). The bridge
depended on internal graphics/render context-registry use pinned to one engine
revision. The user asked to remove that engine entirely, to port the Lua input
adapters to C++, and to cover Windows and Linux as well.

**Decision:** Replace the engine with an authored C++20 shell on SDL 3.4.18,
built statically from the pinned source release:

- `native/shell/app.cpp` uses `SDL_MAIN_USE_CALLBACKS`; only that main thread
  calls SDL video/render. Each iteration runs, in order: presentation selection
  (`Native::tick`, the former extension update), host merge, overlay update,
  draw, optional readback, present. Guest, raster, VI/timer and audio threads
  are unchanged.
- `SDL_Renderer` draws the original image through one streaming RGBA texture
  with nearest sampling in the unchanged Fit viewport, then the overlay with
  `SDL_RenderGeometry` and a DejaVu Sans Mono Bold atlas (stb_truetype).
- The Lua modules and engine scripts are ported one to one to
  `native/shell/input/` and `native/shell/scripts.*`; every Lua regression was
  ported to a C++ CTest. Design-space (960x720) pointer coordinates and the
  former action names are kept, so routing and recipes are unchanged.
- Telemetry/debug lines keep their formats; logs use `LEVEL:SC64:` prefixes.
  Configuration remains `--config=sc64.key=value` (desktop), plus
  `assets/sc64/config.ini` and an intent string-array on Android.
- Platform services: SDL file dialog with an owned import copy on every
  platform (Android content URIs included), `SDL_GetPrefPath`/Android internal
  storage (the same paths as before), cursor hiding with `SDL_HideCursor`,
  Android vibration through the existing `Haptics` class over SDL's JNI.
  `RomPicker.java` is removed. Activity suspension arrives through an SDL event
  watch before SDL blocks the main loop.
- SHA-1/SHA-256 are authored (`native/runtime/sha.*`); OpenSSL is not linked.
- Android packaging uses aapt2/javac/d8/zipalign/apksigner without Gradle or
  the former engine's build tool, keeps the package name and signing key and
  raises versionCode to 2. OpenGL ES is the default renderer (D-053; changed by
  ADR-075).
- Windows x86_64 (mingw-w64) and Linux x86_64 (local Docker image) are
  cross-built on the macOS host (`tools/desktop/cross_build.py`).

**Consequences:** Neither the former engine, its build tool, LuaJIT nor OpenSSL
remains in any build. The runtime copy is now
`local/deps/N64ModernRuntime-shell` with marker `sc64-shell-patch.json`.
Receipts change schema: desktop build 2, desktop run 3, GPU acceptance 4,
Android build 2/run 3; earlier receipts stay historical. The overlay text no
longer stretches non-uniformly on non-4:3 windows. The diagnostic touch-pad
layout from the engine's binary save cannot be read and restarts from defaults
(`touch-layout-v2.json`). Verified: macOS Metal nearest-Fit readback of all 20
cases (same result as the former engine baseline), desktop resume/paused-stop
gates, recipe UI/HUD/gesture gates, Android 16 KiB emulator Home/return with
save preservation over the old install, and a Windows run under Wine.
Windows/Linux user acceptance (D-054), the SDL Vulkan Android surface-loss crash
(D-053) and the stale hover gate (D-055) remain open at this point (D-055 was
closed later on 2026-10-11; see debt.md).

## ADR-075 — Vulkan is the packaged Android renderer; OpenGL ES variant for emulators

**Context:** ADR-074 packaged OpenGL ES because SDL 3.4.18's Vulkan renderer
dereferences null in `VULKAN_HandleDeviceLost` when the Android *emulator* returns
from Home (D-053). The user then ran a Vulkan APK on the physical phone and
reported that it works well (user report 2026-10-11; no recorded receipt).
Vulkan is optional on API 28 devices, so a Vulkan-only default could abort at
startup where OpenGL ES always works.

**Decision:** `make build-android` packages `render_driver = vulkan,opengles2` in
`assets/sc64/config.ini`; SDL tries the list in order, and the shell logs a
warning when the first choice was unavailable. `make build-android-opengles`
writes a separate `StarCraft64-android-opengles2.apk` and `build-opengles2.json`
(`render_driver = opengles2`), same package, key and versionCode, for emulator
runs. Explicit requests (`run.py --backend`, intent `--config=` arguments,
`ANDROID_RENDER_DRIVER` for `run-android`) name one driver and stay strict.
`install-android` refuses an `emulator-*` serial unless the APK carries the
OpenGL ES configuration; `ANDROID_RENDER_DRIVER=opengles2` selects that APK for
install, check, smoke and run.

**Consequences:** Phones use Vulkan by default with an OpenGL ES fallback;
emulators need the variant or an explicit intent argument. The Vulkan emulator
crash stays open as D-053 (retest a newer SDL release). Receipts record
`render_driver` and `render_driver_chain`. Physical Vulkan Home/return and
frame-pacing evidence on the phone are not yet recorded.

## ADR-076 — Application icon converted from the original title art

**Context:** Only the Android APK had a launcher icon (authored navy plate with
"SC64"); the macOS app, Windows executable and Linux package had none. The user
asked for an icon made from the original game's convertible resources.

**Decision:** `tools/desktop/icon.py` (`make icon ROM=...`) validates the NTSC-U
ROM, reads the title image and palette leaves from BOLT (research.md, "Title
artwork resource"), crops the Terran marine to a square (no text) and writes
`icon-1024.png`, a macOS `.icns` (rounded plate with margin, built by
`iconutil`), a Windows `.ico` with a `.rc`, an Android 192 px PNG and a Linux
256 px PNG under ignored `local/icons/` with a receipt `icon.json`. Builds use
whatever exists there: the macOS bundle copies the `.icns` and sets
`CFBundleIconFile`, CMake embeds the `.rc` into `StarCraft64.exe` (`SC64_ICON_RC`,
passed by `cross_build.py`), the Linux tarball carries `StarCraft64.png`
(an ELF cannot embed one), and the APK replaces `res/mipmap-xxxhdpi/icon.png`.
Without `local/icons/` the build is unchanged (authored Android icon, no icon
elsewhere).

**Consequences:** The icon is game-derived art (one cropped frame) inside
personal builds, so those packages are no longer free of original assets even
without `BUNDLE_ROM`; ROM, saves and generated code remain excluded and the
package audits still pass. The tracked repository contains only the converter.
Generated icons are private: do not commit or publish them. Re-run `make icon`
when the crop changes; builds do not regenerate it (they have no ROM input). No
Linux window/taskbar icon is set at runtime.

## ADR-077 — gfx_core and its tools build with -O3 and link-time optimization

**Context:** All targets used the CMake `RelWithDebInfo` default (`-O2 -g -DNDEBUG`),
without LTO. The software rasterizer dominates `software_ms`, so the optimization
level was measured with `tools/renderer/benchmark.py` (macOS arm64, Homebrew LLVM
clang, 20 original corpus cases, 25 repetitions, one warm-up, baseline `-O2`).
Offline timings, not phone cadence or thermals.

| Variant | Interpretation | Raster, 1 helper-less thread | Raster, 3 helpers |
|---|---|---|---|
| control (`-O2` vs itself) | 1.00x | 1.00x | – |
| `-O2` + full LTO | 1.13x | 1.09x | – |
| `-O3` | 1.04x | 1.37x | 1.25x |
| `-O3` + full LTO | 1.17x | 1.46x | 1.28x |
| shipped: `-O3` + ThinLTO (CMake default) | 1.04x | 1.37x | 1.31x |

Every variant produced byte-identical RDRAM, texture keys and command statistics
for all 20 cases. Receipts: `local/reports/opt-flags-01/`.

**Decision:** `cmake/gfx_optimize.cmake` (`sc64_optimize_gfx_targets`) adds `-O3` and
`INTERPROCEDURAL_OPTIMIZATION` to `sc64_gfx_core`, `sc64_gfx_replay` and
`sc64_gfx_test` in `RelWithDebInfo`/`Release` only; the Android profile, which
defines its own `sc64_gfx_core`, uses the same function. LTO is skipped when
`check_ipo_supported` fails. The rest of the application and the generated game
code keep `-O2` without LTO. `-ffast-math` and `-march` are not used.

**Consequences:** Desktop (macOS `-flto=thin`), Android arm64 NDK (`-flto=thin`),
Windows mingw-w64 and Linux Docker (GCC `-flto=auto`) all build and link; the
native CTest suites pass. Full LTO would add about 5-9 percentage points over
ThinLTO on interpretation/1-thread raster but is not selectable through the
CMake property; revisit only with a phone measurement. Phone `software_ms`,
audio and thermal effects are unmeasured (D-047). Debug builds are unchanged.

## ADR-078 — Text briefing taps turn pages; the location card continues by itself

**Context:** A click anywhere in the mission briefing sent Start (ADR-028, ADR-067).
The original text briefing has five pages, an "End of Briefing" page with a
Start button, and different keys: A pages forward and, on the final page,
leaves; Start leaves at once; D-pad Left/Right turn pages. Every tap therefore
skipped the pages and started the mission. Separately, the mission location
card (guest mode 12) stayed on screen until a click. Its script declares a 5 s
display time, but the guest only checks that timer on an event type that is
posted in mode 4 (see research.md); the card waits for a button in the original.

**Decision:** The adapter tells the two screens apart by guest mode (12 card,
4 text briefing; both keep UI token 263). On the text briefing a tap is one A
pulse and a horizontal swipe is D-pad Right (drag left) or Left (drag right),
as in the Encyclopedia. On the card a tap stays Start, and once the card has
been observed for 360 fields (the 5 s display time plus a fade margin) the
adapter issues one Start pulse, repeated if it is dropped. The text briefing
never advances without input. Keyboard Start still skips straight to the map.

**Consequences:** A phone user reads five pages and starts with a sixth tap,
as in the original; the guest owns paging, the final Start button and mission
start, and no guest memory is written. The automatic Start is an input-layer
convenience, not guest behavior, and is the only unattended frontend input.
`tools/input/check_ui.py` accepts Start (card) or A (text page) for the briefing
click gate; older receipts that clicked Start twice stay valid for what they
recorded. Evidence: `tests/native/test_menu_input.cpp` and the native runs
`local/reports/input/exp-brief-06`, `-08`, `-10` (Wasteland, one save, macOS
desktop). Android touch, other missions and the last-page Start on a 2P/other
campaign were not exercised.

## ADR-079 — DrawList textures no longer own a decoded RGBA8 atlas (ACCEPTED)

**Context:** ADR-020 gave every texture a decoded RGBA8 atlas for a future GPU
backend. The shipped path is the software rasterizer, which samples TMEM and
background pixels directly; only fixtures read the atlas. On the private
20-case original corpus the atlas decode was about a fifth of GBI
interpretation time (median 5.42 ms with, 4.19 ms without).

**Decision:** Remove `Texture::rgba8`, `atlas_width` and `atlas_height` and the
decode loop. Content keys, collision checks and the 64 MiB payload budget keep
their meaning (the budget now counts decoded background pixels only). Unsupported
texture formats are therefore rejected when the rectangle is rasterized, not
when it is interpreted. SETTILESIZE limits are still stored unvalidated;
rasterization rejects a tile whose limits are reversed (`sh < sl`, `th < tl`),
which would otherwise give the addressing clamp an inverted range.

**Consequences:** All 20 corpus cases render byte-identical RAM. A future GPU
backend derives its own atlas from `Texture::memory`. ADR-020 otherwise stands.

## ADR-080 — App builds default to CMake Release on every platform (ACCEPTED)

**Context:** The macOS, Windows, Linux and Android builders configured
`RelWithDebInfo` (`-O2 -g -DNDEBUG`); Android already stripped debug info and
shipped an APK without debuggable packaging by default.

**Decision:** `tools/desktop/build.py`, `tools/desktop/cross_build.py` and
`tools/android/build.py` default to `Release` and take `--build-type
Release|RelWithDebInfo|Debug`; the Makefile passes `BUILD_TYPE` (default
`Release`). The build type is recorded in each receipt. Test and runtime code
that needs assertions keeps its explicit `-UNDEBUG`. The offline renderer
benchmark target stays `RelWithDebInfo` so it remains comparable with preserved
baseline binaries.

**Consequences:** Smaller binaries without debug info (macOS app 9.4 MB →
8.6 MB sc64_app); symbols need `BUILD_TYPE=RelWithDebInfo`. A build-type change
reconfigures and rebuilds the existing tree. Release was compiled and the 18
desktop CTest cases passed on macOS; Windows, Linux and Android Release builds
were not rebuilt here.

## ADR-081 — Linux x86_64 AppImage with an optional packaged ROM (ACCEPTED)

**Context:** The Linux tar.gz needs manual launcher setup. Flatpak needs a
Flatpak runtime and `flatpak-builder` (bwrap) that do not cross-build cleanly on
the arm64 macOS host and mainly serve Flathub, which cannot host a port built from
the user's ROM. Snap needs LXD/Multipass on Linux; `snapcraft remote-build` and the
Store build farm are remote builds and are forbidden for game-derived code. The
shell already links SDL statically and opens windowing/audio libraries at run time,
so only glibc is required from the host.

**Decision:** `tools/desktop/appimage.py` (`make build-appimage`) wraps the staged
Linux package, after verifying it against `local/reports/linux/build.json` and the
x86_64 ELF header, into `local/dist/StarCraft64-x86_64.AppImage`. AppDir holds
`AppRun`, a `.desktop` entry, the icon and `usr/bin/{StarCraft64, font, license}`.
The squashfs (zstd) is built with `mksquashfs` in the existing local cross image
and joined with the type2 runtime pinned by tag and SHA-256 in `bootstrap.py`, as
`appimagetool` does; nothing runs remotely. The finished file is unpacked and
audited again. `BUNDLE_ROM=1 ROM=...` adds the normalized, SHA-1-verified ROM as
`usr/bin/sc64-ntsc-u.z64` and writes a differently named
`StarCraft64-x86_64-bundled-rom.AppImage`. `AppRun` adds
`--config=sc64.bundled_rom=1` only when that file exists; the shell reads it via the
shared `sc64_read_packaged_rom` (also used by Android) into owned memory without a
second ROM copy. All members are normalized to 0644/0755 because the image is
root-owned.

**Consequences:** The binary is built on Debian trixie, whose newest required glibc
symbol is `GLIBC_2.38`: Debian 13, Ubuntu 24.04+, Fedora and Arch run it; Ubuntu 22.04
(glibc 2.35) does not. A lower floor needs an older base image and a re-check of
SDL and the toolchain. Hosts need FUSE or `--appimage-extract-and-run`. Verified here: the
audited unit tests, the packed file system listing, and a run of the unpacked bundled
AppDir under linux/amd64 emulation (dummy SDL video/audio, root): it loaded the ROM
from the package, reached the main menu and wrote only the save to storage.
The AppImage file itself cannot run under QEMU binfmt (its ELF header carries the
`AI` marker), and no real Linux desktop run exists (D-054). Flatpak and Snap are
not provided.

## ADR-082 — One cross-platform build pipeline driven by tools/sc64.py (ACCEPTED)

**Context:** The build scripts assumed a macOS arm64 host: a hard `Darwin/arm64`
check, `/opt/homebrew` LLVM/JDK paths, the macOS NDK prebuilt, `.app`-only launch
and a Docker image fixed to `linux/arm64`. The Makefile is POSIX shell, and
Windows has no `make`. A user on Linux or Windows could not follow the README.

**Decision:** `python tools/sc64.py <command>` is the single entry point and runs
on macOS, Linux and Windows; the Makefile only forwards to it. `tools/hostenv.py`
detects the host and finds tools (environment override, then `PATH`, then known
install locations) so no tool hard-codes a machine layout. `sc64.py build` makes
the package of the machine it runs on: the `.app` on macOS (Homebrew LLVM or
`LLVM_BIN`), the host-compiler package on Linux x86_64
(`cmake/toolchains/linux-native-x86_64.cmake`, tree `local/build/linux-native`) and the
mingw-w64 package on Windows from an MSYS2 MINGW64 shell. Explicit
`build-windows` (mingw-w64 from macOS, Linux or Windows) and `build-linux` (Docker
everywhere except Linux x86_64, or with `--docker`) work from any host. The Docker
platform follows the host architecture, a Windows host mounts the repository at
`/work`, and AppImage packing uses local squashfs-tools on Linux when present.
Packaged builds define `SC64_PACKAGED_BUILD`, which keeps the private root relative
(`local`) independently of whether CMake calls the build a cross compile. `run.py`
launches the host's own package under the same receipt binding as the macOS bundle.
`sc64.py doctor` reports missing tools with an install command per OS. Receipts
gain `host` and `build_mode`. An app bundle is still built on macOS only.

The same driver covers the Android APK (`hostenv` finds the SDK, a JDK 21+ and the
host's NDK prebuilt; Windows uses the `.exe`/`.bat` build-tools names; pins live in
`tools/android/pins.py`), the device commands (the emulator guard of
`install-android` is Python, not shell), `venv`/`recompile` (project `.venv`
per host layout, LLVM optional) and `benchmark-renderer`/`test-native`.

**Consequences:** A host-compiler Linux package
inherits the build machine's glibc floor, unlike the pinned Debian image. Verified
here from macOS arm64: the macOS, Windows (mingw) and Linux (Docker) builds and the
AppImage through the new driver, a macOS run with and without ROM, the Android
APK build and audit (no device), the N64Recomp build step of `recompile`
(the inventory step is covered by ADR-083), the linux/amd64
Dockerfile, and synthetic tests of host detection, tool discovery, dispatch and
path mapping. A full host-compiler Linux x86_64 build and a `run` with dummy SDL
drivers succeeded inside a linux/amd64 Debian container (emulated, not real
hardware; 600 guest fields, no errors). The Windows-host (MSYS2) and
Windows-host Docker paths are not exercised on real Windows (D-057).

## ADR-083 — `make recompile` generates the resident libultra inventory (ACCEPTED)

**Context:** `recompile_all.py` treated `local/reports/libultra-resident/inventory.json` as
a prepared input: when it was missing the pipeline stopped, although every ingredient is
reproducible. The inventory comes from `n64sym` candidates, a cold-boot RDRAM capture of the
research emulator and the pinned reference objects. The RAM dump is required evidence
(`prepare.py` rejects a reference match whose body is not verified equal to loaded RAM), so
the equality cannot be assumed.

**Decision:** `tools/research_libultra/ensure_inventory.py` (`sc64.py inventory`,
`make inventory`) runs the chain for the resident section and `recompile_all.py` calls it when
the default inventory is missing. Steps are skipped when their result exists, nothing existing
is overwritten, captures and the inventory are staged beside their target and published only
when complete, and the inventory must match `accepted.py` (346/121/51, all RAM-equal) or it is
rejected and kept for diagnosis. `n64sym` is built when absent and run in Docker on its own
platform when the host cannot execute it (`tools/dockerenv.py`, shared with the Linux package
build and stdlib-only so the recompile `.venv` can import it). The research emulator and the
ultralib reference objects stay prepared inputs that are diagnosed, not built. `pyelftools` joins
the pinned `.venv` packages. `inventory.py` now reads the replacement set from the pinned
N64Recomp commit instead of the working tree: the Stage 1 patch adds seven names, which made a
regeneration report 57 instead of 51 names.

**Consequences:** Verified on macOS arm64: the chain from an empty inventory took 28 s; the
capture and inventory were identical across two runs; a full `recompile` with the regenerated
inventory produced a byte-identical `local/generated/game` (86 files). The chain needs the
research emulator, which has only been built on macOS (D-057), so Linux and Windows hosts
need a prepared inventory or a macOS-built one. Menu and game sections remain manual.

## ADR-084 — The NTSC-U recompilation recipe is committed metadata (ACCEPTED)

**Context:** Everything the analysis derives before N64Recomp runs depends only on the one
supported ROM (SHA-1 checked): function names, addresses and sizes, section intervals, the
eleven CACHE-nop sites and the authored hook text. Regenerating it needs Python, spimdisasm,
the libultra inventory (n64sym, Docker and a research-emulator RDRAM capture), which is too much
for a one-time task on a user's machine and blocks a launcher that takes only the ROM. The
previous header and AGENTS.md called this metadata private.

**Decision:** `recipe/ntsc-u/` is tracked: `sc64.syms.toml` (names, `vram`, sizes, section
`rom`/`vram`/`size`), `sc64.toml` (config template with `@ROM@`/`@OUTPUT@`, instruction patches
with `value = 0`, hooks calling host callbacks) and `generated.json` (SHA-256 of the 86 generated
C files and of the two inputs). It holds no game bytes and no generated code; `recipe.py check`
(also a unit test over the committed files) accepts only whitelisted line forms, so an instruction
word, a byte blob or a stray comment fails. `recipe.py generate` runs the patched N64Recomp with the
recipe and rejects any generated file whose hash differs; `recipe.py export` refreshes the recipe
only from a `make recompile` analysis whose recorded generation and generated directory match.
`make recipe-verify ROM=...` and `make recipe-export ANALYSIS=...` wrap them. The full analysis
(`make recompile`, inventory, spimdisasm) stays the maintainers' generator and the evidence
trail; the audit files (`instruction-patches.json` with original words, `poll-hooks.json`) stay
private. AGENTS.md now names this directory as the one tracked ROM-derived exception.

**Consequences:** Verified on macOS arm64: N64Recomp with only the recipe produced all 86 files
identical to the existing `local/generated/game` in 0.3 s, and a corrupted hash is rejected. The
user-side path no longer needs Python, spimdisasm, the inventory, Docker or the emulator, only the
ROM, the patched N64Recomp and a C compiler. The hashes assume the pinned N64Recomp plus
`patch_recompiler.py`; other builds of it (Linux/Windows, other compilers) must be shown to emit the
same bytes (D-058). The launcher stages remain: prebuilt N64Recomp, game module separate from the
shell, `sc64_setup`, packaging, Android.

## ADR-085 — Release N64Recomp binaries are built per target and checked against the recipe (ACCEPTED)

**Context:** The launcher (ADR-084) runs a prebuilt, patched N64Recomp, so the binary must exist for
macOS arm64, Linux x86_64 and Windows x86_64, and each build must produce the bytes the recipe hashes
describe. `make recompile` built it only for the host with whatever compiler was found. The pinned
N64Recomp opens the generated C with text-mode `std::ofstream`, which writes CRLF on Windows and would
fail every hash there.

**Decision:** `tools/recompile/build_recompiler.py` (`make build-recompiler`, `make verify-recompiler`)
builds only the `N64RecompCLI` target in Release with the Stage 1 patches and publishes
`local/dist/recompiler/<target>/` with a `receipt.json` (source commit, hash of the patch tool and of the
three patched files, submodule commits, compiler, binary SHA-256, build mode). It reuses the existing
cross-build plans: host compiler on Linux x86_64, the pinned local Docker image elsewhere, mingw-w64 for
Windows (cross or MSYS2). Linux is linked fully static (independent of the user's glibc), Windows static
with only system DLLs, macOS with deployment target 13.0. `patch_recompiler.py` additionally opens the four
generated-output files in binary mode. `make recompile` builds the same way for its own host (a generic
host tree remains for hosts without a release target). `verify` runs the published binary on the recipe
with `recipe.generate` and records the result in the receipt; foreign binaries run in a local amd64
container (the Linux one in the pinned Debian, the Windows one under Wine from
`tools/recompile/wine.Dockerfile`, prefix in ignored `local/recompile/`).

**Consequences:** On macOS arm64 all three binaries were built from the pinned source and each generated
the 86 files with the recorded hashes: macOS natively, Linux in amd64 Docker, Windows under Wine. The
binary-mode patch left the macOS hashes unchanged. The Linux and Windows checks run under emulation, not
on real machines or with their native toolchains (D-058). The binaries themselves are not bit-reproducible
and need not be; the contract is the hash of the generated C. Packaging them into a release and the
launcher stages remain.

## ADR-086 — The game is a loadable module, not part of the shell executable (ACCEPTED)

**Context:** A launcher (ADR-084/085) ships a prebuilt shell and builds only the game on the
user's machine. The shell touched the generated code in three places (the overlay section table,
`recomp_entrypoint` and the minimap setter) and the game called 73 host functions (`os*_recomp`,
`get_function`, `do_break`, `pause_self`, `switch_error`, `sc64_*` observers/polls). Linking the
final executable per user would need the SDL/Cocoa frameworks and a linker with the macOS SDK
(zig has none); a module of pure C needs only libc.

**Decision:** `native/module/` defines the ABI. The module exports `sc64_module_init(const Sc64HostApi*)`
and returns `Sc64ModuleInfo` (entrypoint, section tables, minimap setter, ROM SHA-1, struct
sizes). The host passes one table of function pointers; `module_main.c` defines every name in
`sc64_imports.def` as a trampoline through that table, so the module needs no symbols from the
executable and no import library on Windows. Both sides expand the same X-macro list; adding an
entry bumps `SC64_MODULE_ABI`. A shim `librecomp/sections.h` lets the module build without the
runtime tree; the host rejects a module whose struct sizes differ from the real header.
`game_module.cpp` loads it with `SDL_LoadObject` (never unloaded), validates ABI, layout and ROM
SHA-1 and runs before the session's first side effect; `game_static.cpp` provides the same
`Sc64ModuleInfo` for Android, which stays statically linked, as does the capture host.
`cmake/game_module.cmake` builds `sc64_game_module` (`sc64-game.dylib/.so/.dll`); packages carry
it beside the executable and the receipts bind its hash.

**Consequences:** Host exceptions (`ultramodern::thread_terminated`) must cross module frames. A
spike and the CTest `game_module` (synthetic modules, shim headers, exceptions thrown on the main
and a worker thread) show they do on macOS (module built by a different compiler, with and
without `-fexceptions`), Linux x86_64 (amd64 container) and Windows (mingw under Wine). The real
game runs from the module on macOS (60.03 fields/s, no `ERROR:`) and in the Linux package; 31
traced frames (fields 300-330, intro) from three module and three static runs are byte-identical.
A trampoline costs 0.77 ns against 0.78 ns for a table call; the intro crosses the boundary about
15 million times a second (guest wait loops) and process CPU time is the same (25.9-26.1 s against
25.9-26.0 s in 25 s). Mission gameplay was not measured (D-059). The module is game-derived code
and stays private like the old executable. The user-side build of the module, the setup tool
and release packaging are later stages.
