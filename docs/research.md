# Stage 0 research ledger

Inspected and executed on 2026-10-07. **Stage 0 is accepted for the supplied
NTSC-U revision and the recorded scene corpus.** This is original-ROM research
in a desktop emulator, not a native port, engine integration or Android proof.
All game claims below refer to normalized ROM SHA-1
`a36d91b5aac7bb95df0d7de5f9814f5270e25c2b`.

## Evidence classes and reproducibility

- `VERIFIED-STATIC`: observed bytes or inspected source, with reproducible anchors.
- `VERIFIED-RUNTIME`: original-ROM execution with recorded input, task/VI/device trace.
- `EXTERNAL-EVIDENCE`: upstream database or first-hand external report.
- `INFERENCE`: interpretation supported by stated evidence but not recovered provenance.
- `HYPOTHESIS`: an untested explanation or proposed mechanism.
- `UNKNOWN`: evidence is insufficient.

The private captures live under `local/captures/research/`; their `input.json`,
`events.jsonl`, RAM/microcode/Flash snapshots and PNG/WAV files stay ignored.
Commands, exact source pins and instrumented adapter corrections are in
[the capture workflow](reference-capture.md). Metadata is regenerated with
`tools/research/summarize_capture.py`; source-level reference regressions and
synthetic malformed-input tests are separate from game-runtime evidence.
A cold replay with a formatted Controller Pak and saved cartridge Flash image
is `cold-saved-mission-02`: it reaches and reloads Wasteland **without loading
an emulator savestate**. `reference-2p-fields` records individual interlaced
fields and full VI register words. Failed diagnostic runs are excluded from
acceptance. There are no guest-code patches or native recompiled functions.

## Supplied artifact identity

Command: `python3 tools/rom_info.py "StarCraft 64 (USA).z64"`.
Private output: `local/reports/rom-info.json`; schema version 1.

| Property | Observed value | Verification | Status |
|---|---|---|---|
| Size | 33,554,432 bytes = 32 MiB | File length | VERIFIED-STATIC |
| Byte order | z64, big-endian | Magic `80371240` | VERIFIED-STATIC |
| Normalized SHA-1 | `a36d91b5aac7bb95df0d7de5f9814f5270e25c2b` | Full normalized image | VERIFIED-STATIC |
| SHA-256 | `e7d21880c13953b45b8f8db6b2b6cfa02daa29d35b23799a967e96f21001b020` | Full normalized image | VERIFIED-STATIC |
| Header title | STARCRAFT 64 | 20-byte field at `0x20` | VERIFIED-STATIC |
| Country | `0x45`, E, USA/NTSC hint | Byte at `0x3E`; code mapping in tool sources | VERIFIED-STATIC |
| Revision | 0 | Byte at `0x3F`; no release-history inference | VERIFIED-STATIC |
| Cartridge ID | SQ | Bytes `0x3C..0x3D` | VERIFIED-STATIC |
| Manufacturer field | `0x0000004E` | Big-endian word at `0x38` | VERIFIED-STATIC |
| Header entrypoint | `0x80000400` | Big-endian word at `0x08`; startup and DMA mapping verified below | VERIFIED-STATIC |
| Header CRC1 / CRC2 | `0684FBFB` / `5D3EA8A5` | Words at `0x10` / `0x14`; not recalculated | VERIFIED-STATIC |
| IPL3 CRC-32 | `90BB6CB5` | CRC-32/ISO-HDLC over `[0x40,0x1000)` | VERIFIED-STATIC |
| IPL3 family | CIC-6102 / PAL alias 7101 | Fingerprint plus NTSC header hint; no physical chip observation | VERIFIED-STATIC |
| IPL3 SHA-1 | `b2afae246e1dab746bfb28cb346e2911965eefa1` | Same bootcode range | VERIFIED-STATIC |

The analyzer's hash is a local identity anchor, not a claim that an external
preservation database certified the dump. Filename timestamps are not release
dates. The ROM was read, not changed.

## Executable layout, overlays and compression

| Region | ROM range, end exclusive | Guest virtual range | Proof | Status |
|---|---|---|---|---|
| IPL3 transfer | `[0x1000,0x101000)` | physical `[0x400,0x100400)` | `reference-cold`: first PI DMA | VERIFIED-RUNTIME |
| Resident image before overlay material | `[0x1000,0xD6640)` | `[0x80000400,0x800D5A40)` | Startup BSS boundary plus per-body ROM/RAM equality; this is a mixed code/data image, not all `.text` | VERIFIED-STATIC/RUNTIME |
| Resident BSS | no ROM payload | `[0x800D5A40,0x80112760)` | Startup stores clear this interval | VERIFIED-STATIC |
| Menu overlay | `[0xD6640,0xE38E0)`, `0xD2A0` bytes | `[0x8011FBF0,0x8012CE90)` | `reference-cold`, field 35: cart `0x100D6640` → physical `0x11FBF0`; executed pages match ROM | VERIFIED-RUNTIME |
| Game overlay | `[0xE38E0,0x12CA10)`, `0x49130` bytes | `[0x8011FBF0,0x80168D20)` | `mission-02`, field 540, and `game-2p-01`, field 48: cart `0x100E38E0` → physical `0x11FBF0`; executed pages match ROM | VERIFIED-RUNTIME |
| BOLT container | begins `0x12CA10` | asset destinations vary | Directory audit and asset DMA traces | VERIFIED-STATIC/RUNTIME |

Startup enters at `0x80000400`, establishes SP `0x803FFFF0`, clears resident BSS
and calls `0x80004770`. Resident displacement is `0x7FFFF400`. Menu and game
section displacements are respectively `0x800495B0` and `0x8003C310`.
The same virtual address means different code after an overlay DMA; a flat
address-to-function map would be wrong. The copying PI helper executes at
`0x8009AC50`; the guest loader must notify the runtime's overlay mechanism at
the actual section-load boundary, not guess from later function execution.

Both observed overlays are **uncompressed**: full loaded intervals, not just
banners, are byte-identical to their ROM payloads after loading. Recorded
execution pages fit the resident/menu/game mappings; no additional executable
asset or compressed-code load appeared in this corpus. This is a bounded
observation, not a proof about every unplayed campaign path. Overlay `.text`,
`.data`, BSS, jump-table and function-size subdivision belongs to Stage 1.
The initial splat auto-config stopped code near `0x10F0` and warned about a
nontraditional entrypoint. Its boundary guess is unusable; it did not prove a
packed bootloader. The 1 MiB IPL3 copy also does not mean all copied bytes are
resident code: it initially copies the later overlay material as ordinary data.

## libultra and replacement boundary

The OS **GCC libultra 2.0J family** is strongly identified by complete,
relocation-aware `initialize.o` and `exceptasm.o` matches, unique to J among the
built I/J/K/L references. Exact historical archive provenance remains an
INFERENCE. See [the complete replacement table](libultra-inventory.md) for
names, addresses, function-size evidence and wrapper files.

The corrected resident scan accepts 346 versioned reference objects / 90
unique body ranges and 121 masked-CRC body hits; all accepted resident bodies
agree with original captured RAM. It identifies 51 exact-name replacement body
candidates with runtime definitions. The menu scan adds a generic 32-byte CRC
candidate named `n_alResampleParam` at ROM `0xD7238`, **actual overlay address
`0x801207E8`**. It has no accepted full-reference match; do not adopt that name.
Its formerly assumed resident address `0x800D6638` lies in BSS and was wrong.
The separate game-overlay scan found no accepted reference-library body.

The current runtime is insufficient unchanged: `osEPiWriteIo` has no wrapper
and is ignored by default, `osEPiReadIo` asserts for save PIO, raw PI DMA paths
quick-exit, `__osSpSetPc` asserts, and Pak wrappers return `PFS_ERR_NOPACK`.
The game issues a custom Flash protocol through lower PI calls; accepting a
standard `osFlash*` inventory would not cover it. These are explicit Stage 1
implementation debts, not excuses to add silent stubs.

## Active microcode and command census

| Item | Evidence | Result/status |
|---|---|---|
| Graphics text | Every graphics task in the accepted corpus; physical `0x8EDC0`, 4096 bytes; exact ROM match at `0x8F9C0` | SHA-1 `7d0f686719afde0703d377ccc7a492318c01eec0`, VERIFIED-RUNTIME |
| Graphics identity | Submitted data includes the 2.08 xbus banner; lists use `DE`/`DF`/`DB` and object `09`/`0A`/`05`; pinned `gs2dex.h` under `F3DEX_GBI_2` matches | **S2DEX2, xbus 2.08**, VERIFIED-STATIC/RUNTIME |
| Boot microcode | OSTask boot pointer `0x8008ECF0`, boot length 208 | Recorded task descriptor; not the graphics-text hash |
| Separate 1.07 banner | ROM `0xCF9F9`, inside adjacent submitted data span | String verified; no separate active 1.07 text observed. HYPOTHESIS: compatibility/adjacent data |
| Audio RSP workload | All recorded SP starts have task type 1 and the graphics-text hash; AI continues producing sound in all scenes | No audio RSP microcode is active in this corpus; CPU audio is established below |

Observed GBI control/object work: `G_DL`, `G_ENDDL`,
segment `G_MOVEWORD`, `G_BG_1CYC`, `G_BG_COPY`, `G_OBJ_LOADTXTR`,
`G_SETOTHERMODE_H/L`, `G_TEXRECT` with `RDPHALF_1/2`, scissor, image/tile state,
combiner, primitive/blend color and synchronization. `BG_COPY` and scalable
background objects are mandatory; an ordinary F3DEX2 triangle parser is not an
adequate substitute. libgfxd does not supply this S2DEX object decoder.

The emitted RDP census contains texture rectangles, `LOADTILE`, `LOADTLUT`,
`SETTILE`, `SETTILESIZE`, image/scissor/othermode/combiner/color changes and
sync commands. **No triangles, `FILLRECT`, `LOADBLOCK`, `TEXRECTFLIP`, 2-cycle
or FILL cycle appeared in the accepted corpus.** Used cycles are 1-cycle and
COPY. Implement observed scope first; a new used command must fail explicitly
and expand the corpus before being supported. Palette loads include 256-entry
RGBA16 TLUTs and single-entry updates. A palette payload can begin six bytes
past its header: the captured title load uses source `0x2A6FC6`; do not require
8-byte alignment. The 518-byte palette files have a six-byte header plus
**256 × 2** payload bytes; a header value 255 is not a 255-entry payload count.

## CPU-produced pixels, RDP composition and feedback

The critical observed path is a **hybrid CPU indexed surface → S2DEX/RDP
composition**, not a triangle-based map renderer.

- In Wasteland, CPU produces a 320×240 CI8 image. A task's `BG_COPY` references
  physical `0x189140`, format CI (`2`), size 8-bit (`1`), and outputs into RGBA16
  color target `0x176540`. The source begins `320×240` bytes into the color
  allocation: CI8 source and RGBA16 output share physical storage. Another
  buffer repeats this arrangement. Source byte writes include guest PCs
  `0x80048300` and the `0x80004Bxx..0x80004Exx` software-pixel loops.
- `save-loaded/loaded-map-task-386.rdram.bin`, interpreted with the submitted
  palette at `0xA5F30`, yields the terrain/unit scene **before RSP execution**.
  `tools/research/preview_cpu_surface.py` reproduces the private CI8 PNG. S2DEX
  adds portraits, minimap/interface images, text rectangles and other overlays.
- In two-player gameplay the CPU CI8 composition is 512×384, e.g. source
  `0x23AD70`, copied into RGBA16 target `0x1766C0`. Byte-copy PCs `0x800067F8`
  (`lbu`) / `0x80006804` (`sb`) manipulate indexed surfaces; they are not
  evidence of RGBA GPU readback.
- Menu and briefing layers are also S2DEX backgrounds/text rectangles. CPU
  startup clears/copies RGBA allocations, so a future host still preserves
  guest memory initialization and blanking semantics.

Attribution hooks count actual software-RDP memory stores and every logical CPU
request during interpreter windows, including cache hits. A separate provenance
experiment tags bytes written by RDP, clears the tag on subsequent CPU writes,
and compares returned CPU read values with physical RAM. **No read of bytes
last produced by RDP was observed** in the 24-field cold-loaded 1P window,
24+24-field 2P/cursor-motion windows, or additional 24-field 2P reference window.
This rules out that feedback in those experiments only. CPU reads of indexed
surfaces and texture/color-allocation aliasing are real and must not be confused
with that test result. Hardware/cache timing and unplayed effects remain bounded
limitations; new hazards cannot be silently treated as presentation-only work.

The initial DrawList must own the CPU image/TLUT bytes needed by each load,
with format, source ranges, scissor and VI state. Preserve command order and
in-place CI8→RGBA16 expansion semantics in the software reference. No extra
independent RGBA CPU layer should be blindly composited on top: `BG_COPY`
already describes how the CPU scene is presented. Triple buffering may replace
only complete presentation packets, never pending guest-memory effects.
See ADR-005/007/015 in [decisions](decisions.md).

## VI modes and coordinates

| Scene | Native RDP image | VI width | Serrate | X/Y scale | Reference PNG | Evidence |
|---|---|---:|---:|---|---|---|
| Title, main/mission menu, briefing | 640×480 RGBA16 | 640 | 1 | 1024 / 2048 | 640×480 | `reference-cold`, `mission-01/02` |
| One-player map | 320×240 RGBA16 from CI8 | 320 | 0 | 512 / 1024 | 640×240 | `game-1p-01/02`, `cold-saved-mission-02` |
| Two-player map | 512×384 RGBA16 from CI8 | 512 | 1 | 819 / 1638 | 640×480 | `reference-2p-fields`, `feedback-2p` |
| Opening animated presentation | 320×240 target; captured CI8 animation rectangle 160×100 | 320 | 0 | 512 / 716 observed | 640×240 | `reference-cold` intro, `intro-decode` |

Horizontal start/end are normally 108/748; visible vertical start/end alternate
35/509 and 37/511 in interlaced modes. Progressive mode uses 37/511. H_SYNC
is 3093; V_SYNC is 524 interlaced / 525 progressive. The final captures include
all 14 reconstructed VI register words and field parity. Stable 1P control is
`0x00003206`; stable 2P is `0x00003246`. Raw PNG size is a scanout artifact,
not the native cursor-coordinate system or an automatic square-pixel aspect
choice. Use native scene coordinates and VI-aware presentation.

The instrumented ares clock measures about 59.83 progressive fields/s and 59.94
interlaced fields/s (93.75 MHz CPU-count scale). This is **emulator timing**, not
a physical-console frequency certification: synchronization sampling and this
fork's progressive half-line schedule limit precision. Preserve the recorded
register modes, keep VI separate from presentation FPS, and close D-016 with an
independent timing reference before locking the production timer.

## Audio path and cinematics

AI receives 2048-byte buffers cycling through physical `0x16D840`, `0x16E040`,
`0x16E840`. The submitting call is `osAiSetNextBuffer`, with observed CPU PC
`0x80090FBC`. DAC rate 4415 gives approximately 11025 Hz under the recorded NTSC
video clock. CPU producer PC `0x8009C870` writes signed 16-bit samples; static
instructions clamp to -32768..32767 immediately before that store. Interpreter
traces show these stores inside the exact subsequently submitted AI ranges.
The accepted scenes have PCM/WAV output and no type-2 SP tasks. Thus retain
this CPU sound code and provide a real AI/PCM sink; **do not add an imagined
RSP audio ABI or require audio microcode recompilation at Stage 1**. A future
new SP workload reopens that scope.

The opening starship animation is locally observed. It is a separate presentation
path from the recorded text briefing, and must be preserved. The sampled animated CI8 image is generated by CPU routine `0x8009D8FC`:
its traced stores at `0x8009D934`, `0x8009D990`, `0x8009D9B4` implement literal,
skip/repeated-byte style runs; the body reads row-offset/run data and row stride
from `0x80111C50`. S2DEX `BG_1CYC` displays that CPU image; the active RSP text
remains the same graphics microcode. Asset DMA reaches BOLT type-byte 20
material while audio streams separately. **INFERENCE:** this is a CPU-decoded
indexed animation/RLE family, not RSP video decoding. The entire stream format
and meaning of every control code are not claimed recovered. Static recompilation
preserves the decoder, so a replacement media codec is unnecessary initially.
Wasteland's location card and text/portrait briefing are separately captured;
opening animation and text briefing must not be conflated.

## Save device, peripherals and identity beyond the header

| Claim | Verification | Status |
|---|---|---|
| Cartridge persistence is 128 KiB FlashRAM | PI identification/status/erase/page program/read commands; `save-game-02` saves through the original interface | VERIFIED-RUNTIME |
| Save survives process restart and reload | Original slot Terr01 saved; exported Flash SHA-256 `c4170148c8133ad5308d7404f47fd1d60d0317752bd9f21361945e346202f66e`; cold import, original Load Saved list and mission reload; same final bytes | VERIFIED-RUNTIME |
| Flash commands used | `E1`, `F0`, `D2`, `4B`, `78`, `B4`, `A5`; save trace has 388 program-page commands and 5 sector erases | VERIFIED-RUNTIME; emulated chip identity is not a physical cartridge-chip observation |
| Rumble support | Original startup text says support in one-player mode; linked/matched motor code; attached Rumble Pak triggers `0x8000` bank probes | VERIFIED-STATIC/RUNTIME detection; motor actuation during combat not tested |
| Controller Pak behavior | Cold run with formatted Controller Pak reaches and loads the cartridge save; trace contains only status and `0x8000` motor-detection probes, no note-directory/file I/O | VERIFIED-RUNTIME for this corpus; do not claim universal non-use |
| Two-player Expansion Pak requirement | 4 MiB cold run rejects Two-Player with the original Expansion Pak/two-controller message; 8 MiB run reaches split-screen | VERIFIED-RUNTIME |
| Brood War-specific Expansion Pak gating | User context; no separate episode-IV gate experiment here | UNKNOWN; retained as an optional later mode-coverage check, 8 MiB policy satisfies the present runtime requirement |
| Developer/copyright | Original title screenshot `reference-cold/step-2.png`: Mass Media and ©2000 Nintendo/Blizzard | VERIFIED-RUNTIME text; copyright year is not an independently researched release date |

The exact-ROM MD5 is `559f71b861f639b6376d891e3023414b`.
The pinned [Mupen64Plus database](https://github.com/mupen64plus/mupen64plus-core/blob/ba95bab92a76744753bfe61470823a4937850ab0/data/mupen64plus.ini#L15036)
agrees on FlashRAM/Rumble/two players; the local traces above supply the runtime
proof. No EEPROM/SRAM workload was observed. Direct touch cursor/camera/selection
addresses are Stage 4 research, not guessed here from cheat addresses.

## BOLT audit

`tools/research/bolt_audit.py` walks all bounded directory tables, checks input and
output limits for the [Heinermann N64 codec](https://github.com/heinermann/BOLTextract), preserves overlapping-copy semantics,
and compares every decoded leaf with the pre-existing extractor's private
output. Result: **23 directories, 2111 leaves, 61,833,578 decoded bytes**, all
2111 comparisons equal. All offsets remain inside the 32 MiB supplied image and
compressed streams do not cross the next leaf's start. This establishes codec
and tree coverage for this image; heuristic file extensions do not establish
semantics or prove the absence of executable/video content.

The header's last word is `0x9C036DD0`, larger than the ROM. Its meaning is
UNKNOWN; the older extractor labels it `end_offset`, which is not a validated
physical archive boundary. The archive ends are bounded using actual ROM length
and entry offsets instead. Asset labels, timestamp decoding and the two older
exploratory scripts remain subject to D-011 before any production importer use.
Observed executable overlays precede BOLT and are uncompressed; the archive's
asset compression must not be mistaken for code compression.

## Title artwork resource

| Fact | Evidence | Scope |
|---|---|---|
| The title screen is BOLT directory `002` leaf `2770A221` (640×480, 16-byte header, 8-bit indices) with palette leaf `6F527E67` (six-byte header plus 256 RGBA16 entries) | Decoding that pair with `tools/desktop/icon.py` reproduces the RGBA16 title framebuffer at physical `0x1767C0` of `snap-01/title.cpu-visible.bin` with zero differing pixels (5-bit RGB compare; alpha bit ignored). A search over all 33 640×480 images × 142 518-byte palettes found no other match | VERIFIED-RUNTIME/STATIC for this NTSC-U image; no claim about the other 640×480 leaves |
| Image leaves in directory `000` pair with 402-byte leaves of directory `001` by index, not with 518-byte TLUT files | Equal counts (91/91) and sizes only | **HYPOTHESIS**: `001` is sprite metadata. Discriminating experiment: decode a `000` leaf with the `001` bytes as a palette and compare against a rendered portrait |

## Accepted private reference corpus

| Evidence directory / PNG | Scene and proof |
|---|---|
| `reference-cold/step-2.png` | Original title/developer text |
| `reference-cold/step-5.png` | Opening indexed animation |
| `reference-cold/step-8.png` | Main menu, full task/CPU/RDP window |
| `mission-01/step-0.png` | Mission selection |
| `mission-01/step-7.png` | Wasteland location card |
| `mission-02/step-3.png` | Wasteland text/portrait briefing |
| `mission-02/step-5.png` | Mission-loading presentation |
| `game-1p-01/step-3.png` | 1P map and original advisor overlay |
| `game-1p-02/step-2.png` | 1P map, CPU CI8 base plus RDP layers |
| `game-1p-02/step-4.png` | Paused 1P map |
| `menu-2p-01/step-4.png` | Two-player setup |
| `game-2p-01/step-2.png` | Actual split-screen map |
| `reference-2p-fields/step-0-field-0..23.png` | Per-field 2P PNGs, VI registers, task RAM/DMEM |
| `save-restart/step-10.png` | Saved slot visible after cold restart |
| `cold-saved-mission-02/step-13-field-0..23.png` | One cold execution from boot to saved-mission reload, with Controller Pak |

Every directory carries the input sequence and event stream. Task RAM captures
are pre-execution snapshots; screenshots are later VI scanouts. They must be
paired by event/task/field when preparing Stage 2 metrics, not equated merely
because filenames are adjacent. RDP streams span flushes; the analyzer carries
partial commands and reports leftover words. Synthetic regression proves the
reference parser's fragment fix; this corpus did not require fabricated game
commands. Stage 2 still needs a replayable image/VI state contract and a frozen
PSNR threshold; these reference captures do not themselves prove renderer parity.

## Stage 0 acceptance

Executed local gate: `tools/research/check_acceptance.py`; final ignored report
`local/reports/research/acceptance-final.json`: **PASS, 14 original-ROM captures,
139 evidence files**. The 21 synthetic tests and three native RDP adapter probes
pass separately. The pinned emulator rebuild succeeds; a fresh `reference-cold`
recipe produces byte-identical title, opening-animation and main-menu PNGs.
ROM SHA-1 remains unchanged. Original screenshots were reviewed; this is not a
claim based on file existence alone.

- [x] Supplied revision, byte order, SHA-1, header, entry and IPL3 family recorded.
- [x] Active S2DEX2 2.08 graphics text identified from submitted tasks across scenes.
- [x] Audio execution characterized: CPU PCM, no audio RSP task in the corpus.
- [x] FlashRAM verified through original save UI, cold process restart and reload.
- [x] Rumble/Controller Pak observations and their evidence limits recorded.
- [x] libultra family and 51-name replacement inventory reviewed, including API gaps.
- [x] Resident and two uncompressed, overlapping-VRAM executable sections mapped.
- [x] Ten-plus reference images cover menus, briefing, 1P, 2P and animated presentation.
- [x] CPU indexed-surface rendering, RDP composition, aliasing/feedback and VI modes measured.
- [x] Hybrid/S2DEX scope recorded in ADRs; remaining production proofs assigned to later stages.

**Stage 1 has passed its separate native file-audio gate from these reviewed inputs.**
The acceptance is a bounded research result for the named scenes/revision, not
complete-campaign certification, a native-runtime stability test or an Android
performance result. Later unknown used code, microcode, command or device behavior
requires new evidence and an explicit debt/gate update.

## Stage 1 native findings (accepted corpus)

| Finding | Verification method | Scope/status |
|---|---|---|
| Initial worker indirect entries at `0x800049AC` and `0x800958D8` need explicit function starts | Native missing-function diagnostics, exact original call sites and recovered section boundaries; private extra-functions manifest | VERIFIED-NATIVE startup; no whole-game indirect closure claim |
| AI status leaf at `0x8009C690` reads MMIO `0xA450000C` | Exact 16-byte ROM body, comparison with `ultralib/src/io/aigetstat.c` and `rcp.h`; native SIGBUS at the raw load before interception | VERIFIED-STATIC / VERIFIED-NATIVE; real timed AI FIFO wrapper required |
| Current VI line leaf at `0x80099100` reads MMIO `0xA4400010` | Exact 16-byte ROM body and `ultralib/src/io/vigetline.c`; waiting callers mask the parity bit | VERIFIED-STATIC; nominal native line phase remains D-016 |
| Game-owned VI counter is `0x80111670` | Original worker `0x800957CC` receives its VI queue, increments/stores at `0x8009584C`; private `vi-counter-evidence.json` | VERIFIED-STATIC; host preserves the writer and delivers actual queued events |
| Standard `__osViIntrCount` is `0x80112100`, a different object | Two independent relocation-aware reference matches (`vimgr.o` and `timerintr.o`) resolve the same address | VERIFIED-STATIC; game counter must not be mislabeled or overwritten |
| A default black VI context precedes the first explicit game video mode | SDK `io/vi.c` initializes NTSC LAN1; pinned runtime's `osCreateViManager` was empty, leaving mode null and suppressing the registered VI queue | VERIFIED-SOURCE / VERIFIED-NATIVE: fixing context initialization allowed graphics tasks to begin |
| VI Y scale is used during opening presentation | Native boot-07 assert in upstream `osViSetYScale`; source-backed patch removes the unsupported-function path; boot-08 records Y scale 716 at 320-pixel width | VERIFIED-NATIVE register observation; rendered-frame parity is not established |
| Native graphics uses the same submitted S2DEX2 text | All captured native task descriptors are checked against the exact 4096-byte SHA-1 before enqueue | VERIFIED-NATIVE observed startup; new workload remains an explicit failure |

Private failed captures (`boot-01` through `boot-07`) are retained as diagnostics.
They do not pass any title/menu/stability gate. Successful compilation, source
comparison and synthetic tests are separate from completed native execution.

`boot-08` also failed at shutdown; the recorded native stack exposed a scheduler
race, fixed and tested before the final workload. `stability-01/02` failed the
SDL dummy audio queue guard, retained as D-028. They are excluded from acceptance.

The completed native corpus includes reviewed CPU title bytes, main/mission menu
GBI/source-image matches, a 130.040-second original-UI save and 90.033-second cold
reload. Battery contents remain byte-identical and loaded-map census/static
source images match the original. `stability-03` completes 600.039 seconds,
36,000 nominal VI fields, 33,004 tasks and 13,211,652 stereo sample values, using
the timed AI model and file sink. All tasks complete and workers join normally.
Peak native resident memory is 101,122,048 bytes. Private acceptance report:
`local/reports/recompile/acceptance-final-bounded.json`. Exact commands, tests and
scope are in `native-runtime.md`; this proves neither live audio hardware nor
rendered-frame, full campaign, Windows/Linux or Android parity.

## Stage 2 offline graphics findings

| Finding | Verification method / private evidence | Confidence and boundary |
|---|---|---|
| A pre-task RAM + later VI PNG pair is not an exact task pixel golden | Inspected SP/DP/VI ordering; intervening uncaptured tasks exist in earlier captures | VERIFIED-CAPTURE; older PNGs remain scene context |
| Original post-task framebuffer can be captured at a fenced boundary | `patch_reference.py`: pinned source hashes; SyncFull interrupt occurs after Angrylion `cmd_flush` joins workers, before guest DP interrupt. `stage2_post_task` records task identity and 8 MiB post RAM; hidden coverage is also retained | VERIFIED-ORIGINAL, 20 accepted paired tasks |
| Native-target RGB HLE output clears the frozen per-frame threshold | `prepare_corpus.py --fenced-captures`, `compare.py --mode gbi`, `check_acceptance.py`; `local/reports/renderer/acceptance-review-fixed.json` | VERIFIED-RENDERED: 20 frames, minimum 41.610483871 dB; 4 exact RGB matches |
| BG_COPY is an owned CPU CI8 source expansion followed by S2DEX layers | Original pre/post inputs and synthetic overlapping CI8/RGBA16 allocation fixture | VERIFIED-RENDERED for the corpus; a later load of prior target writes fails explicitly |
| Scalable background frame border differs from naïve whole-image quad bounds | Pinned `ultralib/src/gu/us2dex_emu.c`: maximum frame size is `((image dimension << 10)/scale - 1) & ~3`; native scene differences exposed the extra lower row | VERIFIED-SOURCE/RENDERED; residual scaled strip-phase differences remain D-029 |
| TMEM texture fetch and framebuffer blend reads expand 5-bit RGB differently | Pinned Angrylion `rdp/tmem.c` replicates bits; `rdp/fbuffer.c` zero-extends framebuffer RGB. Separate arithmetic reduced 1P/2P regression error | VERIFIED-SOURCE/RENDERED; no claim of exact coverage/noise state |
| COPY writes alpha-zero palette pixels when alpha compare is disabled | Pinned COPY write-mask logic and authored alias/alpha-zero store fixture | VERIFIED-SOURCE/FIXTURE; palette alpha is preserved in target stores, not ordinary host transparency |
| Original task streams can be rendered without any platform GPU | `sc64_gfx_core`, separate offline CLI and verified native capture adapter | VERIFIED-BUILD/RENDERED on macOS arm64; engine integration and Android remain separate |
| GBI image operands and raw RDP image operands require distinct address handling | `image_address_fixtures`: segment-1 base `0x20000` plus offset `0x4000` writes `0x24000`, matches the equivalent physical RDP stream and leaves `0x4000` untouched; ADR-022 | VERIFIED-FIXTURE; does not assert segmented framebuffer use in the game |
| Negative odd 4-bit S must round down before TMEM byte wrapping | Pinned Angrylion `rdp/tmem.c` uses unsigned texel-address shifting; hand-expected I4/IA4/CI4 fixtures include `S=-1`, row swap and lower 2 KiB CI bank | VERIFIED-SOURCE/FIXTURE; does not expand the game's observed texture inventory |
| Malformed full-byte S2DEX format/size fields are rejected before source-size shifts | BG_COPY/BG_1CYC fixtures reject sizes 4/31/32/255 and unsupported format pairs; strict ASan/UBSan passes | VERIFIED-FIXTURE; unsupported inputs fail instead of invoking undefined behavior |

Accepted scene set: four main-menu, four mission-list, four portrait/text briefing,
four cold-loaded 1P and four split-screen tasks. Full native targets are compared
at 640×480, 320×240 and 512×384. No resizing/masking of failing regions or
VI-filtered/unfiltered mixing is used. Individual scores are generated, not edited.

All 20 paired images and amplified differences were reviewed. No missing objects,
wrong palettes or visible tile seams were found; small amplified scaled-sprite
and quantization/noise differences remain. Original hidden coverage, random seed
inheritance and VI resolve/filtering are not asserted equivalent. D-029 records
that precision boundary. The failed restore of the old 1P checkpoint produced
no VI/task before timeout; it was excluded and replaced by an original cold boot
with the UI Flash save. One checkpoint initially called “main-menu” actually
showed the intro; the final main-menu capture uses `reference-cold/end.state`.

The raw-RDP oracle adapter initially split arbitrary 4 KiB command batches and
lost draws at command fragments. The corrected adapter submits complete commands;
acceptance uses fenced original post-task pixels, independent of oracle output.
The interpreter/software core is authored source; game pixels and all captures
stay ignored. No Angrylion/ares implementation is linked into the port.

## Stage 3 desktop integration evidence

These are host capability/runtime observations, not new inferred game formats.

| Finding | Verification method / private evidence | Confidence and boundary |
|---|---|---|
| The former engine's installed version matches its pinned engine revision | Read installed editor config; downloaded matching official SDK and build tool; build receipt hashes exact SDK headers | VERIFIED-LOCAL; re-audit on upgrade |
| Authored extension can submit a compiled quad through the former engine's SDK | Checked extension/graphics/render headers and matching engine RenderListBegin/message/End ordering; local CMake SDK relink; real Vulkan/MoltenVK launch on M4 Max | VERIFIED-BUILD/RUNTIME for macOS arm64 only |
| POST_RENDER callback registration is restricted to AppInitialize | Late registration emitted a runtime error; moved registration to AppInitialize and observed all 20 captures | VERIFIED-RUNTIME for this SDK |
| GPU nearest presentation preserves Stage 2 RGB | `gpu-review-fixed-01/run.json`, native snapshot SHA-256, `gpu-review-fixed-01/gpu/acceptance.json`: 20 accepted frames, every replicated pixel exact, maximum channel error 0; executable and complete compiled-resource set unchanged after exit | VERIFIED-GPU native targets; exact VI scanout is not claimed |
| Game PCM reaches a real independent audio sink | `final-live-01/engine.log`: miniaudio Core Audio, MacBook Pro speakers, 48 kHz host/device stereo; played/nonzero counters; user reported sound on 2026-10-07 | VERIFIED-RUNTIME/USER; queue starvation and device recovery remain separately tracked |
| VI cadence is independent of presentation | Live telemetry logs show nominal approximately 60 fields/s while task/presentation counts vary; source has a separate VI worker and virtual-time clock | VERIFIED-BOUNDED-RUNTIME; final acceptance report remains separate |
| Focus/iconify can enter and leave a cooperative pause | `first-mission-01/engine.log`: activation/deactivation/iconify/deiconify; paused telemetry; restored rendering/audio; clean normal exit | VERIFIED-BOUNDED-RUNTIME; final paused-stop test remains separate |
| Current executable freezes guest/audio counters and exits while paused | `keyboard-02/run.json` and `lifecycle.json`: unchanged executable, exit 0; fields/tasks/samples/played/nonzero counters remain exactly constant across paused samples | VERIFIED-BOUNDED-RUNTIME; resume is a separate check |
| Current executable resumes without accumulating paused VI fields | `lifecycle-02/lifecycle.json`: accepted cadence/freeze/resume check; paused at 80 and resumed at 95 script seconds, then nominal 59–61 fields/s outside recorded transitions | VERIFIED-BOUNDED-RUNTIME; keyboard and mission identity are not timing-check claims |
| FMV written-rectangle extent is insufficient for presentation height | Prior live telemetry reported 320x134 by using maximum written row; Stage 0 recorded a 320x240 animation target. Changed native extent to recorded scissor height; final live telemetry retains 320x240 | VERIFIED-EXTENT; FMV pixel/VI parity remains D-037 |
| Automated case labels are not mission identity proof | Recipe-driven menus/map displayed, but a run named `wasteland` does not itself prove the selected original mission; screenshot/title review is required | EVIDENCE POLICY; no campaign-completion claim |
| Current native campaign route selects Wasteland | CUA screenshot in `campaign-title-01` launch on 2026-10-07 shows "WASTELAND", "Office of the Colonial Magistrate", "Mar Sara Colony"; same initial five recipe entries as `lifecycle-02`, whose map/advisor visually matches original `game-1p-01/step-3.png` | VERIFIED-VISUAL entry through guest-pad recipe; physical keyboard and mission completion remain unproven |

The user-visible application has shown menu and map screens through the former
engine shell, not just offline PNGs. Earlier short automated keyboard calls produced no Lua
events, including after removing a stale blank copy. Later
`interactive-final-01/engine.log` records A/Start/stick and pause press/release
events with no input recipe, and guest map telemetry. Live Lua event delivery is
therefore observed. Physical gamepad hotplug/device recovery remains D-036.
The earlier short schema 2 live replays stayed paused by window deactivation;
their resume acceptance was correctly rejected. The later 125-second
`resume-stage4-gate-01` and 50-second `paused-stop-stage4-gate-01` passed both
revised gates with unchanged binary/resources, zero source drops and exit 0.
The resume run's stable intervals were 59.90–60.10 fields/s; paused intervals
froze all five counters exactly. CUA clicking the actual game surface produced
ACTIVATEAPP; raising the window alone had not. Historical receipts remain
historical and are not retrospectively modified.

## Stage 4 controller baseline evidence

| Finding | Verification method / private evidence | Confidence and boundary |
|---|---|---|
| Mouse/multi-touch can share the existing controller bridge without guest RAM writes | `tests/native/test_touch_input.cpp` executes the actual GUI adapter and host merge with fixtures; separate ownership/pulse/deadzone/layout/letterbox checks | VERIFIED-FIXTURE; not direct RTS semantics |
| Dynamic virtual-controller GUI renders with the installed engine | `touch-desktop-01`, local resource build and CUA screenshot show all labeled controls over the original boot/FMV image | VERIFIED-BUILD/VISUAL on macOS arm64 |
| Design input coordinates differ from physical screen pixels | Pinned `engine.cpp` transforms pointer/contact coordinates with inverse physical width/height; matching GUI script source returns scene dimensions | VERIFIED-SOURCE at the pinned engine revision; no inferred world transform |
| `sys.save` publishes through a temporary file | Matching `script_sys.cpp` checks write/close then renames a unique temporary file; GUI regression tests keep editing after save failure | VERIFIED-SOURCE/FIXTURE; live persistence check is separate |
| Live virtual button/stick events and editor save reach the desktop adapter | Completed `touch-desktop-01`: 38 contact presses, 34 releases, A/B/Z/C/L/R/D-down masks, analog X -0.492..0.788 and Y -0.692..0.585; two F4 presses enclose `Touch layout saved`; platform save file exists; clean exit and unchanged binary/resources | VERIFIED-BOUNDED-RUNTIME; cold restore equivalence, physical multi-touch and original action semantics remain separate |

The initial virtual-pad slice established no guest addresses. The subsequent
read-only cursor investigation below establishes a scoped cursor profile; camera,
selection/order structures and world mapping remain unproven. No guest-memory
write is required for controller-based gestures.


## Stage 4: original controls and read-only cursor profile (2026-10-08)

Profile: exact NTSC-U SHA-1 `a36d91b5aac7bb95df0d7de5f9814f5270e25c2b`,
1P Wasteland, 8 MiB RDRAM. Canonical addresses below are guest virtual addresses;
physical snapshot offsets subtract `0x80000000`. Retail snapshots/generated C
remain under ignored `local/`.

| Fact | Verification method / evidence | Status / limit |
|---|---|---|
| Stick moves the cursor; A selects and A-held cursor dragging makes a selection box; B resolves a contextual order | Original Nintendo instruction booklet, Basic Controls and Advanced Controls, printed pp. 4–7; local scanned pages `local/research/input/manual-page-04.png` and `manual-page-05.png` | VERIFIED-MANUAL; gameplay equivalence still needs runtime selection/order observations |
| Z+R opens the build menu; Z+L toggles the minimap; C buttons depend on the command HUD | Same original manual, printed pp. 6–9 | VERIFIED-MANUAL; no invented fixed meaning for each C slot |
| Cursor floats X/Y at `0x800AFDBC` / `0x800AFDC4`, rounded signed 16-bit X/Y at `0x800AFDCC` / `0x800AFDCE` | Original ares/angrylion runs `cursor-cold-02` and `cursor-cold-03`, eight directional/neutral canonical CPU-visible snapshots paired with PNG cursor locations; generated resident code `0x80037FE0`, `0x80038680`–`0x80038710` reads/writes the same floats and integer representation | VERIFIED-BOUNDED-RUNTIME + SOURCE; 1P profile, not a selection or world-position structure |
| `0x80037FE0` sets cursor coordinates from signed 16-bit X/Y arguments | Inspected NTSC-U generated code from the verified recipe: it writes both floating coordinates, rounded halfwords and sampled halfwords at `0x800AFE18`/`0x800AFE1A`, indexed by player byte `0x800A5B0E`. Existing guarded observer `0x8001AE94` reads the sampled pair | VERIFIED-SOURCE; bounded native mouse-target and drag recipes are recorded in ADR-095/097; full map bounds and modal behavior remain unverified |
| Direct hover alone did not scroll the top edge; Z+positive host stick Y did | Windows background-input Wasteland recipe, 320x240: cursor target (160,0) without Z kept camera Y at 800; the updated path held Z with Y=+0.5 and camera Y reached -16 | VERIFIED-BOUNDED-NATIVE; recipe bypasses physical mouse events and does not prove all maps or edge speeds |
| Guest cursor coordinates map 640x480 to the native 320x240 target by dividing both axes by two | Neutral floats (320,240) coincide with native cursor (160,120); right/up/down snapshots independently match PNG tip positions. ares VI images are 640x240, so their horizontal image coordinate alone is not a native pixel coordinate | VERIFIED-BOUNDED-RUNTIME; split-screen/other VI modes not certified |
| Motion is nonlinear and input observations have latency | `0x800386A0`–`0x800386B8` squares signed X input and divides by a denominator; Y subtracts the analogous displacement at `0x80038710`. `stationary` still moves after six neutral fields, then `rest` and 18-field `settled` agree | VERIFIED-SOURCE/RUNTIME; do not treat normalized stick as a linear per-field pixel delta |
| Native normalized positive Y becomes positive guest stick Y, which moves cursor upward | Pinned ultramodern `input.cpp:142`–`162` preserves Y sign; guest update subtracts Y displacement. The ares test runner uses the opposite host Y convention | VERIFIED-SOURCE; runtime servo direction must be tested separately |
| ares test-runner stick units are normalized [-1,1] | Pinned `local/toolchains/ares-64/test-runner/js-host.cpp:573` clamps and scales to 32767. Early probes using 50 saturated; revised probe explicitly uses 1 | VERIFIED-SOURCE; early results establish saturated motion only |
| D-pad right did not visibly scroll this map in the tested neutral-cursor state | `cursor-cold-03/settled.png` versus 24-field `dpad_right.png`, identical map landmarks and cursor; left likewise retains cursor coordinates | VERIFIED-NEGATIVE-BOUNDED-RUNTIME; not proof D-pad never controls another state |
| Cursor feedback can remain numerically valid while a modal dialog freezes it | `gestures-native-02` shows SCV advisor text and stable (160,120) despite attempted target moves | VERIFIED-BOUNDED-RUNTIME; coordinate validity is not modal eligibility |

Manual source: [Nintendo StarCraft 64 USA instruction booklet (scan)](https://nintendo64.pl/wp-content/uploads/StarCraft-64-USA.pdf).
The booklet is original material; the site is a scan mirror. No copyrighted page
or extracted retail resource is tracked.

Directional snapshot values in guest cursor units (original `cursor-cold-03`):

| Snapshot | X | Y | Rounded X/Y |
|---|---:|---:|---|
| neutral | 320.00 | 240.00 | 320 / 240 |
| right6 | 390.56 | 240.00 | 391 / 240 |
| right12 | 496.40 | 240.00 | 496 / 240 |
| stationary | 531.68 | 240.00 | 532 / 240 |
| left12 | 355.28 | 240.00 | 355 / 240 |
| positive_y6 (ares) | 320.00 | 310.56 | 320 / 311 |
| negative_y12 (ares) | 320.00 | 169.44 | 320 / 169 |
| rest / settled | 320.00 | 134.16 | 320 / 134 |

A read-only native extractor verifies finite values, observed coordinate bounds
and agreement of float/rounded forms, and publishes task field provenance.
The gesture servo additionally requires recent snapshots and arrival before A/B.
These guards do not identify modal states; explicit Continue A bypasses pointing
by producing an ordinary controller button. Further original experiments must
cover camera, groups, menu/build/dialog arbitration and split-screen (D-040).


Original camera experiment `local/captures/input/camera-z-01`: after the neutral
1P cursor is at guest (320,134.16), holding Z with normalized ares X=+0.5 for
24 fields moves map landmarks out of view and shifts the minimap viewport right.
The cursor floats/integers remain unchanged in `z_right`, `z_rest` and `z_left`.
Holding Z with X=-0.5 for 24 fields returns the same visible terrain landmarks.
This establishes **Z+stick camera scrolling in the tested 1P state**. Guest code
`0x80039290` tests held Z and sets the branch flag consumed at `0x800383AC`;
that branch computes scroll displacement instead of the ordinary cursor update.
The follow-up `camera-xy-01` adds 12-field positive/negative ares Y pulses:
positive ares Y shifts the minimap viewport downward and map landmarks upward;
negative Y reverses the movement. Cursor coordinates remain unchanged. This
confirms both axes and the host/guest Y sign mapping. Camera world units, limits
and other modal/2P states remain unproven.
The gesture adapter uses the same buttons/stick rather than writing camera RAM.

Native steering evidence `gestures-native-04/gestures.json` accepts three target
arrivals before A/B/A output. Source snapshots are one field old; maximum
coordinate error is 1.300 native pixels. The timed diagnostic ignores window
deactivation, is explicitly tagged, and is not a lifecycle gate. These target
points are terrain; this observation is not proof of selecting/ordering units.

## Stage 4: original interface state and pointer adaptation (2026-10-08)

The user now reports that cursor selection and contextual orders are convenient,
but menus and message dismissal still required keyboard input. This is human
desktop feedback, not a full mission or physical multi-touch certification.
The following profile is restricted to the exact NTSC-U SHA-1 recorded above.
No guest writes or guessed world coordinates are used.

Original evidence: `local/captures/input/ui-01` is a cold original ares/angrylion
saved-game route with canonical RAM and paired PNGs. Its older capture did not
record the script hash; do not attribute the later script hash to that run.
`ui-mission-01/ui-observations.json` records the newer mission-route provenance:
runner SHA-256 `3d76096e31516b173f62ba4f9736c7a89ecd5708dfd31370f090a9980d1fe591`,
script SHA-256 `c4412eac16d297c3caf0a6649dce9ec53f56ce118deffc0f9f2dcf430b968c6b`.
Checkpoint names describe intended actions. For example, its `advisor` checkpoint
is still mode 4 loading, not evidence that an advisor is on screen. The current
`probe_ui.py` records ROM/Flash/runner/script/log/snapshot hashes on new runs.

| Fact / scoped interpretation | Method and exact anchors | Status / limit |
|---|---|---|
| Signed halfword `0x800A5AB4` is 4 in the sampled frontend/text briefing, 3 in the sampled 1P map/modals and 12 in the mission location card | Original canonical snapshots and resident state branches; `ui-mission-01/briefing.cpu-visible.bin` SHA-256 `fa4e0c2ab62afcf79ed8d241e18da5d065ca301bd650cf435027be50f5aa0249`; paired PNG shows the planet, Wasteland title and location text | VERIFIED-ORIGINAL/SOURCE; checkpoint name is not the portrait/text briefing |
| Frontend dispatch word `0x800D1170` is 25 for intro, 0 main, 6 Single Player and 7 briefing/loading | Original main/Single snapshots and menu-overlay dispatch; briefing additionally matches entry word `0x27BDF4B0` at `0x80127DB4` | VERIFIED-SOURCE/SNAPSHOTS; mode 4/menu 7 alone is ambiguous |
| Main selected index is byte `0x800D13B8`; episode selected row is word `0x800D138C` | Menu overlay `0x80128C7C` and `0x80125E7C`, original main Down/Up and episode-row captures | VERIFIED-SOURCE/BOUNDED-ORIGINAL |
| Episode byte `0x800D1182` indexes the unlocked row count at `0x800D13C4 + episode`; boundary rows are drawn grey | Overlay row comparison/style branch and captured row images | VERIFIED-SOURCE; the observer excludes IDs at or beyond that boundary |
| Cursor lock halfword `0x800F0C20` is nonzero for sampled advisor/pause dialogs | Original `ui-01/advisor` and `pause`; resident `0x80042970` reads it and `0x80038100` branches around normal cursor update | VERIFIED-SOURCE/ORIGINAL; a finite cursor alone does not make pointing valid |
| Root word `0x800A4AB0` points to the current generic UI; root +0x40 selects a child and +0x44 links its first child | Original tree snapshots; `0x80018388`, `0x80018028`, `0x800193F0` traversal/selection; child +0 is next sibling, +0x34 parent | VERIFIED-SOURCE/SNAPSHOTS; image fields +0x10/+0x3C are not child links |
| Node rectangle is signed halfwords at +4/+6/+8/+0xA relative to its parent; flags +0x18, signed ID +0x20, event callback +0x2C, draw callback +0x30 | Original advisor/pause/build tree dumps matched against PNG geometry and resident widget code | VERIFIED-SOURCE/SNAPSHOTS; only immediate observed button children are exposed |
| Ordinary selectable visible nodes require flags `(flags & 0x1A) == 0x18`; the build grid has a separate navigation path | Original predicate at `0x8001843C`: reject bit 2, require bit 8 and bit 0x10. Build row/column updates and child lookup bypass this predicate (see the build-grid correction below) | VERIFIED-SOURCE; disabled ordinary menu buttons are excluded, but visible build cells must remain in navigation feedback |
| Advisor has one sampled button and root callback `0x8003FAAC`; current message word is `0x800B03D8` | `ui-01/advisor` has message 7; `0x8003FB48` assigns the ID, `0x8003FA60` clears it to 0xFFFF; original six-field A then neutral changes snapshot to 0xFFFF and unlocks cursor | VERIFIED-ORIGINAL/SOURCE; tap-anywhere applies only to this observed single-button family |
| Advisor intentionally suppresses initial input until its deadline | `0x8003FC10` adds 0x2EE to clock result and writes `0x800AFDA8`; `0x80037E10`–`0x80037E30` forwards events only after that deadline. Clock helper `0x800638B0` divides OS ticks by 0xB71B (46875) | VERIFIED-SOURCE; adapter conservatively retains an early tap for 60 VI fields; it does not alter the original deadline |
| Pause has four sampled buttons; Resume is at native rectangle (114,122)–(205,141) | `ui-01/pause` root `0x8036FEA0`, callback `0x8003FE04`, parent rectangle (102,20)–(217,155); selected child changes after Down and B returns to map | VERIFIED-ORIGINAL/SOURCE; rectangles are read from each owned frame, not hardcoded root allocations |
| Build grid is controlled by analog stick rather than the sampled D-pad Down | `ui-01/build` versus `build-down` retains selection 4; resident `0x80038238`/`0x80038350` updates row/column and index, `0x8007334C` resolves selected child | VERIFIED-NEGATIVE-BOUNDED + SOURCE; other contexts are separate |
| Build active byte/root are `0x800C6A64` / `0x800C6A68`; selection word `0x800AFE0C` is row*3+column | Original panel root `0x8035D2F0`, rectangle (232,143)–(319,223), nine children IDs 1–9, event `0x80072AF0`, draw `0x800723BC`; current index 4 | VERIFIED-SOURCE/SNAPSHOT for player 0; 2P layout unproven |
| Frontend font boxes are sprite-local; screen rectangles require the later blit | `0x80122DEC` installs canvas pointer `0x800AFD60`; text calls `0x8003F0B0`/`0x8003F070`; image +0xC matches canvas pixel buffer. `0x800661DC` receives screen X/Y in a2/a3 and percent scale at old sp+0x10 | VERIFIED-SOURCE + NATIVE hook geometry; directly using the main local box (30,13)–(179,33) as screen geometry is incorrect |
| Briefing accepts Start to skip/start and A to advance text | Overlay input branches at `0x80128274` inspect original pressed masks 0x1000 and 0x8000; original mode 12 capture independently identifies the screen | VERIFIED-SOURCE/ORIGINAL; loading consumes pointer clicks without issuing either mask |
| The location card uses mode 12 and returns to mode 4 before the text briefing input loop | `0x80120DCC` saves the old mode, writes 12, calls the location-card routine and restores the old mode at `0x80120E2C`; `0x80127F74` calls it before the text briefing loop. Observers at `0x80128274`/`0x80128900` fence the latter | VERIFIED-SOURCE/NATIVE; native `ui-native-05` confirms one Start skips the location card and a second is needed for the text briefing; mode 4 is classified as briefing only with the active observer, menu state and overlay guard |
| Text briefing input loop: A pages forward and leaves from the final Start page, Start leaves at once, B returns to the menu, D-pad Left/Right (or stick X) turn pages back/forward | Disassembly of `0x80127DB4` after `0x80128274`: pressed-mask halfword `0x800AFE24+0xA` tested for `0x8000`/`0x1000`/`0x4000`; A calls the page builder `0x80127B44` and a result of zero marks the last page, after which A writes mode 1; `0x80122DC4` -> `0x80122BF4` returns -1/+1 for D-pad Left `0x200` / Right `0x100` | VERIFIED-SOURCE; native `exp-brief-06/08/10`: tap A moves page counter 1/5 -> 2/5 -> 3/5 -> 4/5, swipe left/right gives 4/5 then 3/5, a fifth tap shows "End of Briefing" with a Start button and a sixth tap reaches the map. Scope: Wasteland, one save |
| The location card never continues by itself in the original code | The card script (RDRAM `0x802B3EB0`) ends with `</DISPLAYTIME 5000>`; its timer is checked only by callback event type 13 (`0x80120B04` -> `0x801208BC`, deadline `0x8011276C`), and the only poster of type 13, `0x8001FA84`, requires `0x800A5AB4 == 4` while the card runs in mode 12. Native diagnostic (temporary log, removed): script state identical to the reference snapshot (deadline 65174 ms, delay 5000 ms, `0x8011277C` unset) and unchanged for 30 s after the deadline had passed; reference recipe `mission-02` also needs A after 360 idle fields | VERIFIED-SOURCE/NATIVE; the same guest code runs in the reference emulator, so real hardware is expected to wait for input too (**HYPOTHESIS**, not observed on hardware). The shell therefore adds a Start pulse after 360 fields (ADR-078) |

Selected original snapshot SHA-256 values:

| Snapshot under `local/captures/input/ui-01` | SHA-256 |
|---|---|
| `advisor.cpu-visible.bin` | `2aa87bd29123b9f5b90908dd441f43feb999d6ba94df5ef86f814fe6a7e3fba9` |
| `advisor-0.cpu-visible.bin` | `7709fdf34d73d50ab0cb820a872d3eeae853c070988a73e60df474e4ecf1513d` |
| `pause.cpu-visible.bin` | `05dce50a63719365c874d8c91b1b7c49825b043fd659f391a30d819311eed4d3` |
| `build.cpu-visible.bin` | `a0ee4bd85c0088b3e2647cc3f06a020c6d7021c9544ba337194cef26560ccd43` |

N64Recomp hook syntax was checked in the pinned `src/config.cpp:204`–`247`:
`[[patches.hook]]`, `func`, `before_vram`, `text`. The profile guard in
`tools/input/add_ui_hooks.py` checks the full ROM hash and eight instruction words
at main font call `0x80129ADC`, tab font call `0x8012793C`, row font calls
`0x80126A40`/`0x80127010`/`0x801277F4`, blit entry `0x800661DC`, and briefing
input/return `0x80128274`/`0x80128900`.
Callbacks only read guest RAM/registers. Synthetic observer regressions compare
both against copies and verify frame-local ownership/translation and invalid
pointer handling. They are fixture evidence, not another original capture.

Native `ui-native-01`–`03` proved portions of menu navigation but are not message
dismissal gates: clicks encountered another pre-mission screen or the initial
advisor lockout. The complete `ui-native-06/ui.json` accepts observed menu/tab/row
navigation, both pre-mission Start clicks, dismissal of advisor IDs 230 and 7,
Resume-to-map, and build selection-before-A. The map is reached before any raw
keyboard-style Start in that recipe. Log SHA-256:
`f9a1db6cfa6f263a23c9256cfbe1ca304c3761c14e0026f1b5e3ea5985493ecb`.
`input.md` records the exact fields and completed receipt. This is native
feedback/controller evidence; OS events are bypassed and physical touch/mission
acceptance remain false.
Unknown nested/custom widgets, later routes and 2P remain D-040; do not promote
this scoped classification into a general game-state decoder.

## Selection-dependent HUD hints and desktop cursor (2026-10-08)

| Fact / scope | Verification method and anchors | Status |
|---|---|---|
| The 1P command canvas is 136x60 bytes, descriptor `0x800DE1E0`, pixels at descriptor+4 | Resident `0x80007B50` allocates width*height; `0x80007C7C` clears/paints it; canonical `hud-02/group-selected` has dimensions 136x60 and pixel pointer `0x803610C0` | VERIFIED-SOURCE + SNAPSHOT; 2P is separately sized |
| Four C hints use icon assets `0x917+i` and 18-byte layout records at `0x800A2424+i*18` | Draw loop `0x80007E0C`–`0x80007EE0`; layout+14/+16 are icon coordinates, font bounds after `0x800211D8` are signed halves at `0x800A5A20` (+0,+2,+4,+6), clipped by `0x800A5A18` | VERIFIED-SOURCE; observe only successful draw returns |
| Hint index 0/1/2/3 sends C-Up/C-Left/C-Right/C-Down (masks 8/2/1/4) | Text query arguments 1/3/5/7 at `0x80007E10`; original controller dispatch `0x800394F4`–`0x80039550` calls `0x80072290` with those same arguments. `hud-02` additionally presses C-Left and C-Right | VERIFIED-SOURCE + BOUNDED ORIGINAL-RUNTIME |
| The separate hint uses B (`0x4000`) and asset `0x91B` | Original draw `0x80007F24`–`0x80007FC0`, icon (34,49), text (46,49), conditional label table at `0x800A2B1C`; paired group PNG shows B Attack | VERIFIED-SOURCE + IMAGE; label meaning depends on context |
| Controller icons use inclusive source bounds (0,0)–(11,10) | Stack rectangle set at `0x80007D88`–`0x80007DB8`, passed to both `0x800073C0` icon draws | VERIFIED-SOURCE |
| 1P canvas is presented at native image (176,160); positive visibility alpha can be 1–255 | Resident `0x80008048`, presentation call `0x80008288` to `0x80066594`; fade-in `0x800081B8`–`0x800081C4` uses 3*elapsed, fade-out clamps to 64 at `0x8000821C`–`0x80008228`; no presentation in its cleared/hidden branches | VERIFIED-SOURCE; supersedes the earlier 64–255-only observer guard |
| Selected-list identity and command context can fence targets | C hint query reads `0x800C6C18` and `0x800FD0B0`; list/count uses `0x800C6C20` / `0x800C6C14`. Group capture has count 10, pointer `0x802E31D0`, command selector 242 | VERIFIED-BOUNDED; no general unit structure or selection semantics inferred |
| The former engine's mouse lock cannot independently hide the cursor while retaining absolute input | Exact pinned engine window-script mouse-lock implementation hides and recentres; its public HID header has no independent visibility API | VERIFIED-PINNED-SOURCE; do not invent an SDK function |
| macOS cursor hiding is available without pointer warp | Installed Xcode SDK AppKit `NSCursor.h` declares `hide/unhide`; `NSEvent.h`, `NSWindow.h`, `NSView.h` declare global mouse location, content view and coordinate conversion used by the bridge | VERIFIED-HEADERS + LOCAL COMPILE; runtime validation is recorded separately |

Original probe: `tools/input/probe_ui.py --route hud`, ares/angrylion,
`local/captures/input/hud-02/ui-observations.json` SHA-256
`167c8c846ea19f05ab960659d231c3cb253b2fed5cca0696d5fc1873a21563ab`.
The `unit-selected`/`single-selected` filenames name intended actions, not
verified unit selections. The group rectangle does select ten units; paired
PNG displays Hold, Move and B Attack. Group canonical RAM SHA-256:
`5850564c1d26b40b241db4d76bb40cc7460196ea1badeb3190f6aa48b30351ff`;
PNG SHA-256: `e217ec5f748f8e729ab9f19e9aefde591f7776ab27753a8f612c72d7d351803d`.
C-Left/C-Right snapshots are retained separately; later PNGs show the selected
group moving toward the cursor after C-Right. This does not establish construction,
all context commands or campaign completion.

Five extra instruction anchors (thirteen observers total): canvas clear completion
`0x80007DC4` / ROM `0x89C4` / word `0x3C03800A`, C font return
`0x80007EC8` / ROM `0x8AC8` / word `0xAEF059F4`, B font call/return
`0x80007FBC` / ROM `0x8BBC` / word `0x0C008476` and `0x80007FC4` / ROM `0x8BC4` /
word `0x00002021`, and HUD presentation `0x80008288` / ROM `0x8E88` / word
`0x0C019965`. The B return requires a preceding call observer, avoiding its
skip branch. The temporary
canvas metadata persists across frames only with the same selected-list/context,
pixel pointer and painted-region hash. Current 1P code fails closed for split-screen.
Synthetic fixtures verify sign-extended MIPS pointers, unchanged guest RAM/registers,
sprite reuse, changed/cleared pixels, context changes and skipped B text draws.
They are separate from the original and native runs.

An initial font-return observer tried to classify text by guest `$ra`.
`hud-native-03` selected a unit and rendered the hints but exported no targets.
Inspection of private generated `sc64_resident_80007C7C` confirmed its direct
call to `sc64_resident_800211D8` does not assign guest `ctx->r31`.
Therefore exact caller-PC observers replace the register-based classifier;
this is a verified property of the current generated call sequence, not an
assumption about original MIPS `jal` semantics. `hud-native-01` was interrupted;
`02` and `03` are retained as bounded diagnostics, not HUD acceptance.

The corrected `hud-native-04` completes its 155-second recipe with unchanged
binary/resources. Owned UI metadata includes the visible Hold/Move/Stop bounds;
clicks emit C-Left/C-Right/C-Down with zero stick and a later release. Both
`check_hud.py` and `check_gestures.py` accept their scoped gates. Engine SHA-256
`0bd9ea454ee35c051f1c2afffe97f2917f95b6c41a7de95d120752b2b38625ba`;
log SHA-256 `83e2c926fefbdf0ef923a850d5541f8902f33ced2663edcb233f31836a49b5fe`.
Native telemetry has hidden-cursor false on frontend/briefing, true over the
gameplay viewport. The GUI highlight geometry/visibility is separately tested
through its actual dispatch/update functions with synthetic HiDPI input.
Neither evidence stream certifies physical touch, actual OS cursor appearance,
construction or mission completion; see the Stage 4 checklist.

## Build-panel dismissal and HUD pointer ownership (2026-10-08)

| Finding / scope | Verification method | Status |
|---|---|---|
| The observed original build root has signed rectangle halves at +4,+6,+8,+10 | Canonical `hud-02/build.cpu-visible.bin`: `0x800C6A68` points to `0x8035D2F0`, rectangle (232,143)–(319,223); existing child coordinates are relative to the root origin. Snapshot SHA-256 `0dcb5900e4504d1d979c465fba9db71360f5809890bb330174835fc916eaa108` | VERIFIED-SNAPSHOT for this root; validate every owned root before exporting bounds |
| HUD dimming is not itself a command-availability predicate | `0x80008048` calculates presentation alpha after the canvas draw/query; C query `0x800712DC` and its skip sentinel `0x38F` are handled separately in `0x80007C7C`. Alpha 1–63 is possible during fade-in, although the previous port observer rejected it | VERIFIED-SOURCE; original command logic still decides what each button does |
| A per-label checksum taken during painting need not equal the completed canvas | C text returns at `0x80007EC8`; later C icons/text and the B hint are drawn before canvas restore `0x80007FF8`. Synthetic observer regression writes into an earlier box after its font return and proves the old intermediate checksum would reject it | VERIFIED-SOURCE + FIXTURE; not a claim that every original label layout overlaps |
| Hovering over a hint previously moved the guest cursor under the panel | Lua UI adapter only intercepted a label's down event; preceding mouse move reached `gestures.pointer` and updated its terrain hover. Actual GUI dispatch regression now verifies interception before the cursor servo and no terrain queue on hint/padding clicks | VERIFIED-PORT-CODE + GUI-FIXTURE |

A fourteenth read-only observer at `0x80007FF8` (ROM offset `0x8BF8`, word
`0xAD2AFD60`) seals the finished 1P HUD canvas before the original restores
`0x800AFD60`. Metadata requires this completed draw, matching pixel pointer,
selection/command context and final checksum. A positive-alpha painted canvas
also exports its full presentation bounds independently of actionable labels.
Owned-frame canonical RAM fences the region by the current selection context.
Its padding consumes pointer input; no guessed permanent screen zone is used.

Native completed run: `local/reports/input/hud-routing-native-02`, 175 seconds,
exit 0, unchanged engine/resources. Engine SHA-256
`a04b47fc291e19e24ef220222011af9b9b23bd5fcef928f626b40df715cf0d43`;
log SHA-256 `36a61bdff900c2b68ff2aae8dd5522a1e9df2c1bd196c79d87bfdb5ab1a4de6f`.
Hint clicks at fields 7700/8000/8400 emit 2/1/4 with zero stick; the guest cursor
remains (99.39,63.60), away from the clicked HUD. After Z+R opens the build panel,
the outside click (100,64) at field 9000 emits B at 9001, returns to game mode,
and emits A at 9021 using fresh cursor (99.5,63.5). A later Hold click at 9600
still works. `check_hud.py` accepts both hint and close/replay gates;
`check_gestures.py` accepts initial cursor arrival. This proves original
controller sequencing and cursor placement, not every unit/building hit test,
successful construction, physical touch or mission completion.

## Aiming at HUD hints and free mouse tracking (2026-10-08)

| Finding / scope | Verification method | Status |
|---|---|---|
| The previous pointer adapter created an invisible aiming area over the command panel | `hud_input.lua` deliberately stopped guest hover in the observed HUD region, while the macOS bridge hid the OS pointer over the whole gameplay image. User reported skipped areas most noticeably at bottom right | VERIFIED-PORT-CODE + USER REPORT; no claim of measured OS-event coordinates |
| Previous free mouse steering used four active fields followed by eight neutral fields | `gestures.lua` shared its queued-action `drive`/`settle_until` path with free hover. Interactive log `hud-routing-interactive-01` includes fields 22423/22427/22435 with this cadence and large cursor displacements | VERIFIED-PORT-CODE + NATIVE TELEMETRY; no frame-by-frame smoothness claim from that log |
| Original raw-stick deadzone is one unit per axis | Resident `0x80037F34`–`0x80037FC8` subtracts/adds constants at `0x80001B54/58/5C/60`; canonical `hud-02/group-selected` has 1.0 at all four addresses | VERIFIED-SOURCE + SNAPSHOT |
| Current cursor displacement divisor is the float at `0x800AFDF8` | `0x80038634`–`0x800386B8` assigns/reads it and divides signed squared X; Y uses the same divisor at `0x8003870C`. Canonical group snapshot has 200.0; constant at `0x80001BA0` is 150.0 for the alternate branch. The other branch computes 200.0 minus float at `0x800D13D8` | VERIFIED-SOURCE + SNAPSHOT; semantics of that configuration float are not inferred |
| Native normalized input becomes an octagonal raw stick with base radius 82 and angle constant 1.39414574 | Pinned former-shell runtime copy, `ultramodern/src/input.cpp`, `convert_to_n64_range(float,float,int8_t&,int8_t&)` | VERIFIED-PINNED-SOURCE; no invented linear stick conversion |

Free mouse tracking now has its own continuous path. Owned feedback supplies
the divisor; invalid/nonpositive values disable hover steering. Observed
velocity predicts in-flight motion before each proportional update. Desired
native velocity is capped at three pixels per field, transformed through the
original square/deadzone and inverse pinned octagonal conversion. The queued
click/selection-drag arrival path retains its existing pulse/settle behavior.
No cursor positions or game memory are written.

The OS-pointer bridge projects the actual painted HUD bounds into the same
normalized content-view rectangle as the game image. AppKit mouse position
keeps the pointer visible over that region, independent of Lua event freshness;
the map still hides it. The GUI separately uses actual mouse events for a 12%
gold hover highlight and 30% pressed highlight. Touch release does not create
a persistent mouse-hover highlight. Projection and GUI dispatch are fixture
proof; actual OS-pointer appearance still needs human verification.

Completed `hud-pointer-native-01`: 185 seconds, exit 0, unchanged engine/resources.
`check_hover.py` accepts five map targets, each sampled at 31–40 owned snapshots,
with maximum observed displacement rate 3.0932 native pixels/field, arrival
within 1.5 pixels, and no recorded neutral burst far from the target. Two
consecutive HUD hover targets at fields 9850/9900 are intercepted; the three
bracketing/held cursor snapshots have maximum rate 0.001286 pixels/field.
Input telemetry records only changed controller values, so the checker carries
the last zero state across the stationary interval; this is not continuous
pixel capture. Earlier hint presses, construction close/replay and click arrival
also pass their separate checkers on this same bundle.

Engine SHA-256 `784211838dd8cbe58f86e681d93f2801fb44f8682e4d0d8c004699026bb8f139`;
log SHA-256 `947603df7912714b9b70f37992fd6b30fa775ce7be9110765e517c382370537c`.
This remains scripted controller/snapshot evidence, not physical pointer/touch,
rendered highlight pixels or mission acceptance.

## Mission-exit scanout investigation (2026-10-08)

The user reports a brief coloured-noise frame when leaving a mission or after
victory. This initial experiment covers **Abort Mission -> Defeat** only.
At this point neither victory nor the actual native-port noise frame had been
captured; the later RDP investigation below captures the native defect.

| Finding / scope | Verification method | Status |
|---|---|---|
| The original abort transition blanks VI for six consecutive fields while changing its width from 320 to 640 | Exact NTSC-U cold boot with the existing Flash fixture, ordinary controller actions, pinned ares/angrylion. `local/captures/input/transition-exit-02/events.jsonl`: fields 3072-3077 have `hstart == hend == 0`; width becomes 640 at field 3074. Six consecutive screenshots `exit-030.png` through `exit-035.png` contain only black RGB pixels | VERIFIED-ORIGINAL, bounded exit route; screenshot and register observations retain their own sampling boundaries |
| The prepared runtime implements `osViBlack` through the next VI context and zeroes H_START at context update | Current former-shell runtime copy, `ultramodern/src/events.cpp`, `osViBlack` and `update_vi`; the graphics-action consumer provides its owned register update to `RendererContext::update_screen` | VERIFIED-LOCAL-SOURCE; do not read the returned VI-register pointer from the engine thread |
| The former engine bridge discards VI updates and presents the last RDP rectangle's target | `native/shell/session.cpp`, empty `Renderer::update_screen`; the former extension's `sc64_update` selects `frame->rectangles.back().target` without VI blank/origin checks | VERIFIED-PORT-CODE; a scanout defect, not a captured native noise frame |
| Ignoring scanout blanking or framebuffer/mode changes could expose a transitional image that the original hides | Compare the original blank interval with the bridge's unconditional target presentation | HYPOTHESIS for the reported coloured noise. Confirm with native frame/VI/target tracing across the same abort route, then a separate victory route; D-037 remains open |

The completed second original probe returned zero. Its private analysis report
is `transition-exit-02/transition-analysis.json`, SHA-256
`db6813bc4d8f992d98b50fadaabd098d82359adb3c7454c5ce37349c00ad3400`.
Event log SHA-256:
`04410272e2a0719d1317cdbf6c7e6d88ced88f30a090657a16d677aa11f34d43`.
Private controller script SHA-256:
`93796bbc15353d113566d9c6aa61ac6be80e91cf04fcb9215a683fa0b28d030c`.
The first probe selected No and stayed in pause; it is not exit evidence.
The third probe also reaches Defeat and samples task submission near the blank
interval. None of these original captures certifies a fix in the native port.

### Desktop VI bridge remediation and native abort trace

Verified against the pinned prepared N64ModernRuntime source:
`ultramodern/include/ultramodern/renderer_context.hpp` defines the fourteen
`ViRegs` members. `ultramodern/src/events.cpp` assigns the queued
`ScreenUpdateAction` registers and calls `RendererContext::update_screen()` on
the graphics consumer. `get_vi_regs()` returns shared storage, so the bridge
copies its fields inside that callback and retains no runtime register pointer.
The task completion stamp is taken on that same consumer. Engine graphics APIs
were checked in the installed SDK graphics/render headers; guest callbacks
only copy metadata. Native memory/target extent and no-DL blanking tests are
synthetic regressions, not original-game evidence.

| Observation | Method and scope |
| --- | --- |
| Native abort also enters VI black and switches width 320 to 640 without a new completed graphics task during the blank interval | `transition-before-01`, controller/UI recipe; bounded VI callback and software-upload trace. The previous presenter did not consult these registers. The reported coloured flash was not separately reproduced in this baseline. |
| Corrected presentation suppresses the game quad during all four sampled black fields; resumed visible targets match VI origin/width/pixel size and completed task generation | Completed `review-fixed-native-02`, 206.56 seconds, exit 0, unchanged binary/resources. `check_scanout.py` passes with four black presents, 858 visible presents and no association violations. This is callback/render association, not exact VI filtering or original-field alignment. |
| Reused framebuffer allocations cannot display a cached pre-transition task | Persistent task floor is verified by native fixtures, including an engine consumer that misses the entire black interval, reused width/address, a future task, offscreen final CIMG and invalid RAM extent. |
| The late window samples show black followed by a fade-in to the Defeat overview | Twenty-four bounded engine-graphics/Vulkan readbacks triggered at visibility/width changes. Six of eight hidden samples are all-zero RGB; two contain only neutral RGB values 0..8. **HYPOTHESIS:** those two samples are older swapchain/fade images returned by asynchronous readback. These samples did not cover the earlier fade-out and cannot establish absence of the reported coloured noise; the follow-up below finds it in uploaded images before VI black. |
| Three quick hint clicks remain distinct final controller submissions and a map hover received during their cooldown resumes | `review-fixed-native-02`: click fields 9600/9607/9614, C-Left submissions 9600/9618/9636, each with zero stick and release. The map hover at 9620 resumes steering at 9654 toward its requested point before another mouse recipe. `check_hud.py` passes FIFO cadence, resumed approach and construction close/replay. A separate arrival recipe passes `check_gestures.py`. These bypass OS input and are not physical-touch acceptance. |

Private evidence SHA-256:

- `review-fixed-native-02/run.json`: `38a1add6ff123ee78f4659cfdb4aa29a2a99183bcd7c87f22e2c7daa06238a39`.
- `frames/frames.jsonl`: `ac7446d904a9141673cb187164ed689927466efe52d832994c928ac5bbeaae50`.
- `hud.json`: `287ece42bed3f92a8aa4ba95baa751c40486fc6999e37e3af53130e780c51c1c`.
- `scanout.json`: `a1ab75e6acba4ece766476077bda1896fe1af4bbf94faaa84592e310d6b6c08c` (includes hashes of the private trace/readbacks).

The native input/abort run used engine SHA-256
`dbef77c34e0ceb530a2ad0dc1f8736953b97a0549052dd4f1c85d65e8b83654f`.
A later one-line status GUI correction shows the Paused text only while a guest
is running. It prevents focus changes from overlaying static no-guest GPU
fixtures. The resulting published bundle, engine SHA-256
`aa5396e96bd1e74d7bbba0ce5b432182a0d8eae43c9f93b81a00bd62aafacc48`,
passes all twenty native-target RGB cases in `review-fixed-gpu-02`; both software
baseline and every replicated Vulkan pixel match exactly. The earlier
`review-fixed-gpu-01` fails its last frame because of the Paused text; it is not
accepted. `review-fixed-native-01` and `transition-before-01` contain a Remotery
port-conflict error from an older running build; all native acceptance checkers
reject them. Logs/receipts were not rewritten to remove errors.

Victory was not exercised. This VI-only investigation did not establish the
cause of the specific coloured flash. The later RDP investigation below finds
noise before blanking and confirms an additional port defect. Broader
VI/transition parity remains D-037; physical input and recorded touch-only
mission completion remain D-040.

## System Back input and mouse-only mission report (2026-10-08)

| Observation | Verification method | Status / limits |
| --- | --- | --- |
| The user completed a mission with the mouse alone | Direct user report in this task on 2026-10-08 | HUMAN-REPORTED; mission identity and input recording are unspecified. This supports desktop usability but does not establish physical-touch Wasteland acceptance. |
| The installed input schema exposes `KEY_ESC = 68` and `KEY_BACK = 124` | Pinned SDK `include/input/input_ddf.h`, lines 112 and 168; matching engine `engine/input/proto/input/input_ddf.proto` at the pinned revision, lines 77 and 133 | VERIFIED-PINNED-SOURCE; these are input-binding enums, not guest controller bits. |
| Below Android API 33, Back key events emit `GLFW_KEY_BACK` with their original press/release action | Same pinned engine `external/glfw/lib/android/android_init.c`, lines 776-785 | VERIFIED-PINNED-SOURCE; no Android hardware run yet. |
| At Android API 33 and above, the Back callback emits a synthetic `GLFW_KEY_BACK` press, followed by release after the engine's ten-poll special-key countdown | Pinned engine Android activity Java source, lines 273-285; `external/glfw/lib/android/android_window.c`, lines 77-81 and 370-378 | VERIFIED-PINNED-SOURCE; button/gesture delivery still needs on-device testing. The ten polls are engine event polls, not N64 VI fields. |
| Esc/Android Back aliases submit original B independently of the existing C/B source | `tests/native/test_desktop_input.cpp`: same-frame press/release, held aliases, player isolation and clear; `tests/native/test_touch_input.cpp`: actual GUI/host arbitration and `sc64.set_pad` submission | VERIFIED-FIXTURE; pending pointer input is discarded by existing controller ownership. Original B semantics remain contextual. |

The fetched pinned input schema and Android sources are retained privately under
the ignored local toolchain directory. No Android API was inferred from a
different engine version. ADR-034 records the host binding decision; D-040 and
the Stage 5 hardware gate remain open.

## Remaining mission-exit noise: RDP memory effects (2026-10-08)

The user reconfirms visible coloured noise **from the map to Victory/Defeat**
after the VI-only fix. The follow-up confirms missing RDP memory effects and
corrects the native abort route. The prior scanout register gate was insufficient
to certify all transition pixels. Victory remains a separate untested route.

| Finding | Verification method | Status / scope |
| --- | --- | --- |
| Coloured noise is already present before VI black in the earlier native run | Inspect `review-fixed-native-02/frames/task-9415.rgba` through `task-9421.rgba`, uploads at fields 11638-11644; the lower half contains coloured high-frequency pixels while the upper half is black. VI remains enabled through field 11645 and blanks at 11646. `exit-contact.png` is a generated private inspection mosaic, not a new run. | VERIFIED-NATIVE-UPLOAD; the user independently reports this map-to-results route. Earlier window samples started too late to see these frames. |
| The original abort fade preserves the map while dimming it | Existing pinned ares/angrylion `transition-exit-02/exit-012.png` through `exit-028.png`; VI black follows in `exit-030.png` through `exit-035.png`. Original screenshots are 640x240. | VERIFIED-ORIGINAL, bounded abort route; no victory capture yet. |
| The old native software path discarded RDP RAM stores between tasks | `render_software` started each task from `DrawList::initial_ram`; engine update used the result only for texture upload. `sc64_capture_submit` captured guest RAM but never committed those RDP stores, and `FrameMailbox` could discard unpresented tasks. | VERIFIED-PORT-CODE; memory-dependent blending and partial redraw cannot retain the original framebuffer. |
| The prepared runtime's SP and DP completion messages follow `Renderer::send_dl` | Pinned prepared `ultramodern/src/events.cpp`, graphics action consumer at lines 392-410; the Stage 1 bridge postpones both events. `sc64_capture_submit` runs on the submitting guest thread before enqueueing that action. | VERIFIED-LOCAL-SOURCE; the new synchronous software path commits actual stores on that submitting thread before completion is observable. No engine graphics/render calls move to the guest thread. |
| Discarding a presentation packet no longer discards RDP memory effects | Native regression renders a copy then a memory-blended fade, commits their exact store ranges to word-swapped RAM, and consumes only the second mailbox packet. Red/green framebuffer values persist and fade to the expected RGBA16 values; unrelated CPU bytes, alpha-rejected pixels and failed-range validation remain untouched. | VERIFIED-FIXTURE; original/natively rendered transition pixels have a separate gate. |
| The corrected native abort preserves the map behind the confirmation dialog and during fade-out | Completed `rdp-writeback-native-01`, 207.25 seconds, exit 0, unchanged engine/resources. Inspect the bounded native-upload mosaic `frames/exit-contact.png`: map -> confirmation -> dimming -> black -> Defeat. Hint FIFO/build replay, cursor arrival and VI/target association checkers pass on this same run. | VERIFIED-NATIVE-UPLOAD/TELEMETRY; no separate victory capture or exact window/VI pixel parity claim. |
| Terminal native abort frames no longer contain the coloured residuals | `check_fade.py` examines 320x240 uploads in the six fields preceding the first 320-wide VI blank. Threshold RGB <= 1 comes from original terminal screenshots. Old `review-fixed-native-02` fails with RGB maxima 49/33/24/8/0/8. New run blanks at field 11645; six uploads from five distinct tasks at fields 11639-11643 all have RGB maximum 0. | VERIFIED-NATIVE-UPLOAD; this gate certifies the terminal fade, not all earlier pixels or other transitions. |
| GPU presentation preserves the offline baseline after the writeback correction | Completed `rdp-writeback-gpu-01`, unchanged engine/resources; `check_gpu.py` passes all twenty software-baseline and replicated Vulkan native-target RGB cases with maximum error 0. Nine CTest gates and eighteen targeted UI-tool Python tests also pass. | VERIFIED-GPU/FIXTURE; static GPU fixtures do not execute the guest or write guest RAM. |

ADR-035 records the memory-effect correction. Same-task texture/framebuffer
dependencies still fail explicitly under D-019, and hidden coverage remains
D-029. Esc's native submission receipt is `esc-back-02/escape.json`: fields
5242-5248, engine SHA-256
`a7d6dc8f05fca30d0cbfaca7b9cc3bd2099d87e82d025888b2b58cf273df6839`.

The corrected abort and GPU runs both use engine SHA-256
`77555942d0dd4f84fff63411d1c5cc6b3f5c9c13df071f16fa9d870dd9d4a8a2`.
Private evidence SHA-256:

- `rdp-writeback-native-01/run.json`: `dbca45c131b794ac7f8f6e7dc07d57066e51c7c4d100280cb6d119142a1fae4a`.
- `rdp-writeback-native-01/frames/frames.jsonl`: `46690c66d989e4df013330ebe1711c6666c09a8e378a994b9c63579ce621a3cc`.
- `rdp-writeback-native-01/fade.json`: `136bde9da4ed37bbe85ea3f0c5085ad24cdd9fa391908fb90609ed9c85d46050`.
- `rdp-writeback-gpu-01/run.json`: `37bf0b8eb199d15146c349e4c2ee5065ad92187ddd5919c670e4117f7ca132c7`.
- `rdp-writeback-gpu-01/gpu/acceptance.json`: `0c9cd130a95911f159e3d14e3d3d02d420d7899193b54ea411d75b9a8a05ab73`.

## Main-menu campaign carousel and episode-order warning (2026-10-08)

All addresses below are for normalized NTSC-U SHA-1
`a36d91b5aac7bb95df0d7de5f9814f5270e25c2b`. Original code/canonical snapshots
remain private; the committed observer script retains only instruction anchors
and the input adapter does not modify original menu/selection memory.

| Finding | Verification method | Status / limits |
| --- | --- | --- |
| The carousel/warning belong to main function `0x80128C7C`, mode halfword `0x800A5AB4 = 4`, menu dispatch word `0x800D1170 = 0` | Stage 1 menu disassembly and original `campaign-ui-03` snapshots/PNGs; episode-order warning keeps the same dispatch and has no generic widget root | VERIFIED-ORIGINAL/SOURCE; generic widget-root detection cannot distinguish this foreground warning. |
| Selected campaign is the first word of the six-entry stack list at `sp+0x88`; active initialization is `(selected + 6 - index) % 6` | Original main snapshot and initialization at `0x80128EE0..0x80128F1C`; rotations at `0x8012965C..0x801296B4` / `0x801294F0..0x80129544` | VERIFIED-ORIGINAL/SOURCE. The copied resident seed `[0,3,4,5,2,1]` at `0x800D0E60` is overwritten before input. Left advances the six-campaign ring; numeric IDs are not screen positions. |
| Left (`0x0200`) brings the right-hand campaign into the centre; Right (`0x0100`) returns the previous campaign and is ignored at ID zero | Horizontal helper `0x80122BF4` / wrapper `0x80122DC4`; main positive branch checks `sp+0x88 != 0` at `0x801296C4..0x801296CC` | VERIFIED-SOURCE plus native traversal. Earlier original `campaign-ui-01/02` Right probes at ID zero do not demonstrate failed D-pad delivery: they exercise this boundary. |
| The horizontal helper also accepts raw stick X below -65 or above +65, with a 250 ms repeat gate | Helper branches and pinned float at `0x800D0F80` (`0x42820000`); original `campaign-ui-03` uses X=-0.95 twice and then X=+0.95 after returning | VERIFIED-ORIGINAL/SOURCE. Two negative steps show Zerg Episode II then Protoss Episode III; the positive return selects Episode II. |
| Animation readiness requires direction `sp+0x2B8 == 0`, leaving flag `sp+0x2AC == 0`, and completed main animation `sp+0x2C8 != 0` | Main animation/input branches in disassembly; frame-owned native feedback switches readiness during each original rotation | VERIFIED-SOURCE/NATIVE. Progress `sp+0x2BC` moves by four per menu iteration; host VI fields are not menu-loop iterations. |
| Hero/platform hit areas can be obtained from actual blits instead of screenshot constants | Platform call `0x80129BEC` and hero call `0x80129D64` to `0x800661DC`: image descriptor width/height at +8/+10, X/Y in a2/a3, scale percent at `sp+0x288`, campaign ID in s4. Platform list position is s3-1; hero position is s3. Hero Y adds v0 in the call delay slot | VERIFIED-SOURCE; observers copy the exact effective coordinates, clip to 640x480 and union the two rectangles per ID. Native fixtures prove guest RAM/registers remain unchanged. |
| The episode-order warning ID is `0x217` in `sp+0x27C`; no warning is -1 | Main assignment `0x801298FC..0x80129904`; original warning/back/continue screenshots match the user-supplied dialog | VERIFIED-ORIGINAL/SOURCE. Other warning IDs are explicitly blocked from this adapter's Start/inside rule. |
| Warning sprite bounds are available at call `0x8012A2A0`, then final main state at `0x8012A2A8` | Exact instruction words/ROM offsets guarded by `add_ui_hooks.py`. Warning X is a2, effective Y is a3+88 from its delay slot, scale 100 percent; the native blit rectangle is `(160,220)..(480,386)` | VERIFIED-SOURCE/NATIVE. Rectangle includes the sprite's padding; no fixed host-pixel rectangle is used. |
| Inside warning -> original Start; outside -> original B restores the same campaign | Original `campaign-ui-03`: A opens warning, B cancels, A opens again, Start continues to Episode III. Main handler uses mask `0x9000` for A/Start and `0x80128C18` to clear the warning on B | VERIFIED-ORIGINAL/SOURCE. Native mapping deliberately uses Start (`0x1000`) for the user's inside action, not a direct transition call. |
| The earlier linear adapter reached its target but took the long route across the ring edge | Completed historical `campaign-native-03`: click `(100,330)` at field 2050 targets ID 5 from ID 0; five Left submissions at 2050/2132/2214/2299/2383. Click `(530,330)` at 2850 targets ID 0 from ID 5; five Right submissions at 2850/2931/3013/3093/3174. Each waits for ready feedback and has zero stick/release | VERIFIED-NATIVE-SUBMISSION/STATE, private recipe bypasses OS events. No A/Start appears in either side-selection interval. |
| Both swipe directions, central entry, outside cancel and inside continue worked in the earlier bundle | Same completed 104.18-second run: swipe states 0->1->0->1->2; central A at 4701 opens warning, outside B at 5000 returns to ID 2, central A at 5301 reopens it, inside Start at 5600 enters dispatch 6. `check_campaign.py` passes all six gates, verifies final submitted providers, freshness, releases and resulting state | VERIFIED-NATIVE-RECIPE; native uploads `task-1834.rgba` (warning) / `task-1921.rgba` (Episode III list) also inspected. Not physical OS touch or Android acceptance. |
| The GUI dispatch maps both mouse and touch to the same native rectangles | `test_touch_input.cpp`: actual GUI dispatch at 1920x1440 physical / 640x480 native, both side clicks, swipe, central click and warning inside/outside. `test_menu_input.cpp` now covers shortest traversal of all 30 distinct pairs, no A at arrival, animation timeout, changed foreground, stale/cancelled contacts and long-drag suppression | VERIFIED-FIXTURE; all ten CTest gates, eighteen UI-tool, six campaign-tool and twenty-one Stage 3-tool Python tests pass. |

Historical campaign-native-03 engine SHA-256:
`cb23122f54f6f3ae9b8bcf8452fcf8e13ff2ad5aa75cd312f9816502b4e7df49`.
Private evidence SHA-256:

- `campaign-ui-03/ui-observations.json`: `140ed0a384a88debbe9b1d2e6b1c98577654b59f947f66a72c83a355422cc951`.
- Original `campaign-warning.png`: `2b8628b70f8b4b7b3120de619fd447d75ea32d4cb99e77ec3cac8858294df6ac`.
- Original `campaign-continued.png`: `b3cbb0bd365d97ed66b4306590903932322329eefffb25bf3d8fdbb2372cf538`.
- `campaign-native-03/run.json`: `e77b2047e9918423eb605349582d95a80bb258f8f2e6025d705a4f5b67eac70a`.
- `campaign-native-03/campaign.json`: `1f66b6aaa4c79c75a7165e12ed7323768e4a67fbea609a69d70aa377c7ff0ba5`.
- `campaign-native-03/frames/frames.jsonl`: `1e7faa91a72bb85246bbe6cfcbfe5fbd9155b74417218e1cfa9454862bedb734`.
- Warning native upload: `8c2f32dda18be71db3bc1769c73dbdff3dcb0a5daaa500982bc1aa8d2cf3fd3a`.
- Episode III native upload: `a89a0128b4f82875b9fff340e4662e70fd4a06a8444623716dfba950a51caf92`.

The predecessor `campaign-native-01` exposed reversed swipe geometry and is not
accepted as the final adapter. `campaign-native-02` verifies the corrected swipe
and warning route before multi-step side selection was added; it is retained as
bounded predecessor evidence, not the current campaign certificate. `campaign-native-03` also remains historical: its five-step endpoint traversal fails the new shortest-route gate.
The final native recipe uses an independent copy of the original private Flash
fixture (`c4170148c8133ad5308d7404f47fd1d60d0317752bd9f21361945e346202f66e`).
ADR-036 records the architecture. Full Stage 4 remains D-040.

## Shortest campaign navigation and symmetric wrap (2026-10-08)

| Finding / adaptation | Verification method | Status / limits |
| --- | --- | --- |
| The menu initializes a circular six-entry list, rather than using the resident seed as its active order | Disassemble the original `0x80128EE0..0x80128F1C`: selected byte at `0x800D1182`, subtract `index-6`, signed remainder by six, store into the six words at `sp+0x88`. This overwrites the earlier copied seed. | VERIFIED-ORIGINAL-SOURCE; corrects the earlier seed-order description. |
| Original Right at campaign zero is explicitly blocked, although the existing positive rotation can wrap the list | `beqz v0` at `0x801296C8`, ROM offset `0xE0118`, instruction `0x1040000B`; preceding `blez` at `0x801296C0` rejects non-positive horizontal input. Rotation completion at `0x801294F0..0x80129544` moves first entry to last. | VERIFIED-ORIGINAL-SOURCE; previous native Right-at-zero probe stayed at zero. No claim that the unmodified ROM has symmetric keyboard wrap. |
| A single guarded instruction patch enables the original reverse rotation from zero | `add_ui_hooks.py` checks normalized ROM SHA-1 and the instruction anchor before emitting NOP. Inspect generated private `funcs_56.c`: `0x801296C8: nop`; the old conditional/goto is absent. | VERIFIED-RECOMPILATION; supplied ROM is unchanged, observers remain read-only. Explicit port behavior adaptation, ADR-037. |
| The configuration uses an existing N64Recomp instruction-patch schema | Pinned `third_party/N64Recomp/src/config.cpp`, `get_instruction_patches`: parses `patches.instruction` array, required `func`, aligned `vram`, unsigned `value`. Actual private regeneration succeeds with 86 emitted files. | VERIFIED-PINNED-SOURCE/GENERATION; no guessed API or configuration syntax. |
| Host side-click routing chooses the shortest ring distance, with Left for three-step ties | `test_menu_input.cpp` exercises all 30 distinct ordered pairs with intervening non-ready frames and arrival checks; `test_touch_input.cpp` exercises real GUI dispatch for endpoint mouse/touch clicks. No A/Start at arrival. | VERIFIED-FIXTURE; native evidence below has its own scope. |
| An altered patch anchor is rejected even if a synthetic fixture's SHA-1 is accepted | `test_campaign_tools.py` mutates the original instruction to NOP after building a synthetic, non-retail fixture and recalculates its mocked hash; generation raises the anchor error. | VERIFIED-FIXTURE; no retail code or resource fixture is committed. |

Completed `campaign-wrap-native-02` runs for 84.48 seconds, exits zero and
retains unchanged engine/resources. `check_campaign.py` passes all eight gates:

- First-to-last side click at field 2050 submits one Right (`0x0100`), releases
  at 2056, and the next ready observation selects ID 5 at field 2300.
- Last-to-first side click at field 2300 submits one Left (`0x0200`), releases
  at 2306, and a ready observation selects ID 0 at field 2401.
- Subsequent swipe states are 0->1->0->1->2. Central A at 3600 opens the
  episode-order warning; outside B at 3900 restores ID 2. Central A at 4201
  opens it again; inside Start at 4500 reaches Episode III dispatch 6/262 by
  field 4803. All submissions use the shared gesture provider, neutral stick,
  fresh rendered metadata and bounded releases. No side-selection A/Start.

This is VERIFIED-NATIVE-RECIPE: OS events are bypassed. Actual GUI mouse/touch
routing has fixture coverage; physical Android touch is not certified. The
current checker rejects both historical five-step endpoint actions from
`campaign-native-03` without modifying that report. `campaign-wrap-native-01`
contains a Remotery port-conflict error from a separate empty host instance;
its zero exit does not make it accepted evidence. The clean second run uses a
fresh independent copy of the original private Flash fixture. All ten CTest
checks and all 111 Python tests pass; a ResourceWarning in the
Python run is retained in the private log.

Engine SHA-256:
`299d7137010dc99c62b21cabdb919e9abb7de4bc2b07a42bcb806f5dbf200f8a`.
Private evidence SHA-256:

- `campaign-wrap-native-02/run.json`: `39ec702c18ce2b7c6d181d4b164342a32fa3d82756ae427b101ddab5bb518297`.
- `campaign-wrap-native-02/campaign.json`: `35fd2b34855eb20466c5ce132fa12e6a0c7c9936ca21b51201239f6a4a06796f`.
- `campaign-wrap-native-02/frames/frames.jsonl`: `55a6491cec0c9d719df84eefbc87d3fb4dbba0cb4bdde817f80d529b6b1dac17`.

The current configuration is private `sc64-menu-v5-ui.toml`, regenerated from
the unhooked base. Build log: `campaign-wrap-build-01.log`. Python log:
`campaign-wrap-python-01.log`. ROM, generated C and native captures are ignored.
ADR-037 records this bounded adaptation. ADR-038 subsequently closes desktop
Stage 4; physical-touch/device checks remain D-041 and expanded contexts D-042.

## Stage 4 closure decision (2026-10-08)

The user explicitly requests Stage 4 closure to permit the next stage. This is
USER-ACCEPTED-DESKTOP-SCOPE, recorded in ADR-038, not new original-ROM evidence.
The earlier mouse-only mission report remains HUMAN-REPORTED; mission identity
and physical-touch recording are unspecified. Desktop fixture/native evidence
retains the hashes and scopes above. At closure, the completed campaign run's
log hash matches its receipt, all eight gates are accepted, and its engine hash
matches both the current build receipt and personal bundle. The retained Python
log reports 111 tests passed; ten CTest checks passed in the preceding fix.

Stage 4/D-040 is closed for the implemented macOS arm64, 1P desktop controls.
Stage 5 entry is allowed. Physical touch-only first-Terran-mission completion,
Android delivery/lifecycle and device checks move to D-041; expanded input
contexts move to D-042. No unchecked hardware or whole-game result is converted
to a passing result. See `android.md` for the remaining acceptance gates.


## Build-grid disabled-cell navigation correction (2026-10-08)

| Finding / scope | Verification method | Status |
|---|---|---|
| The original build grid computes `column + 3 * row`, clamping each axis to 0..2, without testing a child availability flag | Resident `0x800381F8..0x80038384`, exact researched NTSC-U disassembly. `0x8007334C` looks up child ID `index + 1` through `0x8001809C`, whose ID traversal does not use `0x8001843C` | VERIFIED-ORIGINAL-SOURCE; not a generic widget rule |
| The cell draw callback compares child ID minus one against the word at `0x800AFE9C` | Resident `0x80072518..0x8007254C`. `0x800AFE0C` is a separate previous/current navigation cache used by `0x80038354..0x80038384`; it is not the draw callback's selection input | VERIFIED-ORIGINAL-SOURCE |
| Disabled cells can still receive the original grid highlight; flag 2 changes presentation and command dispatch | Resident `0x80072578..0x80072598` selects the disabled colour, after selection comparison; `0x80072B8C..0x80072BA4` handles flag 2 in command dispatch | VERIFIED-ORIGINAL-SOURCE; no guest command patch introduced |
| The pointer adapter previously lost its current cell after passing through a disabled cell | `read_ui` applied the ordinary `(flags & 0x1A) == 0x18` filter to build cells. `ui_input.read` requires an exported selected item before advancing or acknowledging a pulse. The reported centre-to-upper-left route first traverses middle-left | VERIFIED-PORT-CODE; source explains the report, exact user mouse delivery is not recorded |

The bridge now exports visible (`flags & 8`) build children with the researched
cell draw callback and IDs 1..9, preserving unavailable intermediate cells.
Selection follows `0x800AFE9C`. Availability is exposed using the existing
`activate` field: a pointer click may select a disabled cell to show the original
requirement hint, but does not emit A for it. Ordinary menu filtering and all
row/column controller signs are unchanged. No guest-memory writes are added.

The synthetic bridge regression distinguishes painted selection from the old
cache and covers disabled/visible/hidden cells. The Lua regression traverses all
81 ordered grid pairs for both mouse and touch, delays acknowledgement, checks
neutral intervals, confirms only at the requested available cell and checks a
second click after arrival. These are controlled fixtures, not retail captures
or physical Android acceptance.

## First Android host/package evidence (2026-10-08)

These findings concern the authored host and its pinned dependencies; they do
not introduce new facts about the original game's structures or rendering.

| Finding / scope | Verification method | Status |
|---|---|---|
| The former engine's pinned SDK exposes Android thread attachment, activity class loading and activity-result listeners used by the SAF bridge | Inspected its pinned Android SDK header, compiled and invoked on Android; no remembered API substituted | VERIFIED-PINNED-SDK + EMULATOR |
| Pinned SHA-1/SHA-256 helpers accept byte buffers and explicit lengths | Inspected the former engine's pinned crypt header (SHA-1/SHA-256 hash functions), linked desktop/Android hosts and validated imported ROM identity | VERIFIED-PINNED-SDK + NATIVE |
| A local SDK/CMake Android engine can be packaged without a remote build-service submission | Exact SDK library/export link profile; local resource build before staging the engine for bundle; final APK engine byte identity and signatures checked in `local/reports/android/build.json` | VERIFIED-LOCAL-BUILD; conventional engine packaging remains D-035 |
| Current APK has 16 KiB-compatible ELF segment/RELRO and native ZIP placement | `tools/android/check_apk.py`, Build-Tools zipalign/apksigner; receipt `local/reports/android/apk-audit.json` | VERIFIED-PACKAGE; emulator page size is 4096, so no 16 KiB runtime claim |
| Actual SAF content-stream import yields the accepted normalized NTSC-U ROM and removes its temporary input | Android system-picker selection, native SHA-1 gate and `adb run-as` identity check: `a36d91b5aac7bb95df0d7de5f9814f5270e25c2b`; private storage reused after restart | VERIFIED-EMULATOR; no packaged ROM |
| AAudio was selected and nonzero PCM was produced | Native backend log and compact telemetry in `local/reports/android/emulator-smoke-02/engine.log`; no host-audible quality test | VERIFIED-EMULATOR; audio underruns observed |
| Activity Home/return freezes guest work independently of manual/focus pause | Initial APK smoke logs pause/resume at identical field/task/sample counts `577/81/210948`; `emulator-smoke-02/run.json` records zero deltas | VERIFIED-EMULATOR; no full surface/audio-device recovery claim |
| Private 128 KiB Flash file is retained across a cold restart | `adb run-as` size/hash check, SHA-1 `cb58f51f26a624a758ed770d1e9d5b4e3b5a0c5d` before/after restart | VERIFIED-FILE-RETENTION; new gameplay save/reload remains open |
| Scoped guest FPU initialization passes four pinned COP1 rounding cases and a nonflushed float subnormal | `tests/native/test_guest_fpu.cpp`, desktop CTest and ARM64 Android executable receipt `local/reports/android-fpu-run.log`; FPCR FZ/DN/FZ16 definitions checked against [Arm Cortex-A55 FPCR documentation](https://documentation-service.arm.com/static/5e7e16d8b2608e4d7f0a2f45) | VERIFIED-NARROW-PROBE; complete arithmetic/FCSR semantics remain D-012 |
| Original campaign menu, briefing and advisor messages accept Android-delivered taps through the running Wasteland map | Fresh screenshot hashes in `local/reports/android/android-touch-probe.json`, including `stage5-emulator-wasteland-map.png`; explicit `adb input tap` events on isolated `emulator-5580` | VERIFIED-EMULATOR-SCREEN; Enter skipped intro/title, no touch-only mission completion |

The initial APK for these native checks has SHA-256
`126d5dbf879bd506735c5dba40e5f24a4421dee5905e6126ad0d71c2fbcb32b7`;
its game engine has SHA-256
`c9f5f3e7d8369e68e445fe5127feae38c9da065285badf7096b834503b79755c`.
Device: Android 16 `sdk_gphone64_arm64`, SwiftShader Vulkan, 4096-byte pages.
This is isolated emulator evidence, not Adreno/Mali or physical-touch acceptance.

The root Makefile reuses the existing guarded builders and launchers. Its
`make test` run passes 120 Python tests and 11 CTest checks; `make check-apk` and
`make configure-android` pass, and `make run-android SERIAL=emulator-5580`
opens the installed app. Omitting SERIAL fails before sending an adb command.
Private command logs: `stage5-make-tests.log`, `stage5-make-apk.log`,
`stage5-make-configure.log`. The actual Makefile smoke target also passes:
`local/reports/android/make-smoke-01/run.json` records live guest/nonzero PCM,
no guest/native crash and identical Home pause/resume counts `558/73/203780`.
The isolated emulator is stopped after verification. Stage 5 stays IN PROGRESS;
see its exit checklist.

## Stage 5 review corrections (2026-10-08)

| Finding / scope | Verification method | Status |
|---|---|---|
| Pre-Home progress no longer accepts a stuck/paused return; guest/PCM must advance after ordered resume events, and the process must survive | Negative Python fixtures reject absent/single/paused/stalled post-resume samples and changed PID. Updated `review-fixes-smoke-01/run.json` has unchanged PID 3622, three post-return samples, advancing field/task/sample/nonzero-PCM counters, and zero paused deltas at `568/93/208900` | VERIFIED-FIXTURE + EMULATOR; physical acceptance remains false |
| The APK auditor binds engine identity to the actual game library member | Synthetic APKs with no `libStarCraft64.so` or a wrong game library and matching auxiliary hash now fail; rebuilt APK passes signature, ELF/ZIP and exact named-member checks | VERIFIED-FIXTURE + PACKAGE |
| UI/HUD/campaign observers and symmetric wrap are required in both builders and engine CMake profiles | Shared `tools/desktop/game_validation.py`, negative fixtures for missing/duplicated anchors and both builders. Actual Android CMake rejects the stale synthetic generated tree formerly accepted; `stage5-review-fixes-guard.log` | VERIFIED-FIXTURE + CONFIGURATION; not a stale-game runtime test |
| Pending, late and crashed import files have explicit ownership and cleanup | Same portable `RomImportState` compiled into Android and `rom_import` CTest: pending/finalize, late completion, transferred result, orphan cleanup and preservation of symlinks/directories/unrelated files. On updated emulator APK, seeded own temporary files disappear and the unrelated marker survives startup | VERIFIED-NATIVE-FIXTURE + EMULATOR-STARTUP; no injected JNI race claimed |
| Updated Android SAF bridge still imports the exact user-provided ROM, removes the successful temporary input and reaches the menu | Actual system-picker selection, inspected screenshot `stage5-review-fixes-import-live.png`, private `review-fixes-import.json` / `stage5-review-fixes-import.log`. Normalized ROM SHA-1 matches retained backup and Flash-file SHA-1 remains unchanged | VERIFIED-EMULATOR; file retention, not a new gameplay save/reload |

Current personal APK SHA-256:
`3f501851eef6dc3fc76fae5c2484a0c978cd403581c31eb1fa3aef331b964836`.
Engine SHA-256:
`69dd0af74622a8cec9583ad18bb37a11b37703dff8f66fae3206cc8358edf084`.
The immutable build receipt is `local/reports/android/review-fixes-build.json`;
full verification log `stage5-review-fixes-tests.log` reports 129 Python tests
and 12 CTest checks passed. The updated APK was built through `make build-android`
and verified through `make smoke-android` on the isolated Android 16 arm64
SwiftShader emulator with 4096-byte pages. No hardware/16 KiB runtime/thermal
gate is closed by these corrections. ADR-043 records temporary-file ownership.


## Optional direct APK ROM loading — verified 2026-10-08

This is a user-authorized personal build option (ADR-044), not a new fact about
the original game. The normal Android build still excludes the ROM. Authored
source/documentation contain only image identity and loader logic; input and
outputs remain ignored.

| Observation | Verification method and evidence | Scope |
|---|---|---|
| Existing `recomp::set_rom_contents(std::vector<uint8_t>&&)`, `is_rom_loaded()` and `get_rom()` support an owned in-memory ROM | Inspected the pinned local `librecomp/include/librecomp/game.hpp` and `librecomp/src/pi.cpp`; Android and macOS native engines link successfully | API/source verification |
| Upstream `load_stored_rom` would reopen a disk file at game start | Inspected pinned `librecomp/src/recomp.cpp`; guarded Stage 3 transform adds an early return only for the host's verified preloaded game; source-drift fixture fails on missing/duplicate anchor | API/source plus fixture |
| APK asset can be read without filesystem extraction | Installed NDK 29.0.14206865 `android/asset_manager.h` and the former engine's pinned Android SDK header expose the implemented calls/NativeActivity asset manager; actual engine logs the asset source and boots without a disk ROM | Header/compiler plus emulator runtime |
| Bundled bytes match researched NTSC-U revision | Builder normalization, APK audit and Session SHA-1 validation require `a36d91b5aac7bb95df0d7de5f9814f5270e25c2b`; wrong size/order/hash and additional-ROM fixture cases are rejected | Package/runtime verification |
| Opt-in uses the ordinary APK path/package | `local/reports/android/build.json`: 44,238,334 bytes, APK SHA-256 `740104e1acc844a88720e9c823fbc64a49a95a86e69303a7fa691fdfa60e1992`, engine SHA-256 `e39e93c00216d0bd08fd87120102624755a2de6efb58d4be72b5f3608f1bdf89`; installed APK identity matches the build | Package plus emulator |
| No new private ROM or import temporary exists after startup/restart | `bundled-storage-before.json` / `bundled-storage-after.json`, actual app-private listing and recursive disk-usage listing (196 KiB for files/); backed-up previous imported ROM was removed from this isolated emulator before the test | Emulator filesystem verification |
| Flash is preserved | Before/after Flash SHA-1 `cb58f51f26a624a758ed770d1e9d5b4e3b5a0c5d`; no save data cleared | Emulator filesystem verification |
| Bundled startup and cold restart work with no picker/input injection | `bundled-assets-run-01/run.json`, `bundled-assets-cold-02/run.json`, native source log and `bundled-assets-visible.png` (original main campaign menu) | Android arm64 API-36, SwiftShader Vulkan, 4 KiB emulator |
| Home/return freezes and resumes the preloaded guest | 35-second bundled run freezes fields/tasks/samples at 565/93/207876; four post-return samples advance guest and nonzero PCM in the same process; no guest error/crash | Emulator lifecycle verification |
| Default import path still works | `bundled-ordinary-regression-01/run.json`: ordinary rebuild, imported ROM startup, nonzero PCM and strict Home/return pass | Emulator regression |
| Targeted regression checks pass | `local/reports/bundled-python-final.log`: 133 Python tests; `bundled-native-01.log`: all 12 CTest/native/Lua checks; `bundled-desktop-link.log`: shared macOS native engine links | Automated/compile verification |

The ordinary APK audit rejects the bundled APK unless explicitly passed
`--bundled-rom`. A synthetic bad-ROM builder probe preserves an existing APK and
never starts toolchain work. Bundle startup prefers its asset over any previous
picker import and reports asset/verification errors to the existing startup UI.
The old private import is preserved if present; this mode never creates one.
Physical-device mission, GPU/lifecycle and performance acceptance remain open.


## Smartphone feedback and relative camera profile (2026-10-08)

| Finding / scope | Verification method | Status |
|---|---|---|
| Game starts on the user's flagship smartphone; startup audio sometimes pulses, pan feels excessive and cursor slow | Direct user report; device model, refresh rate, build identity and callback counters were not supplied | USER-REPORTED physical startup; not sustained performance/thermal acceptance |
| Existing camera gesture kept outputting Z+stick while displaced fingers were stationary | Inspect the previous `gestures.lua` origin-relative `camera(dx,dy)` | VERIFIED-PORT-CODE; explains indefinite scrolling |
| Camera X/Y offsets are signed words `0x800A2EE8/0x800A2EEC` | Original resident helpers `0x8000B758/7E4/870/8FC` read, modify and clamp these words; `0x8000ACE4` reads them for map presentation | VERIFIED-ORIGINAL-SOURCE; scoped 1P profile |
| Camera X bounds are `0x800A2EB4/0x800A2EBC`; Y bounds are `0x800A2EB8/0x800A2EC0` | Same four original helpers' signed clamp branches | VERIFIED-ORIGINAL-SOURCE; no inferred unit-order semantics |
| Z scrolling divides deadzone-adjusted raw input by `trunc(6 - (setting+120)/80)`, then rounds to even integer units and shifts left two | `0x800383C8..0x800384FC`; original snapshot floats at `0x80001B84/88/8C` are 120/80/6, setting at `0x800D13D8` is 0 | VERIFIED-ORIGINAL-SOURCE + SNAPSHOT; setting semantics remain unknown |
| Existing original camera snapshots independently change these offsets | `local/captures/input/camera-xy-01/*.cpu-visible.bin`: settled (0,0), z_right (480,0), z_rest (520,0), z_left (-32,0), z_positive_y (-32,288), z_negative_y (-32,96) | VERIFIED-ORIGINAL-SNAPSHOT; residual motion after release is explicit |
| New adapter exposes half-size camera offsets/bounds alongside cursor snapshots | Native header read tests; source map/320x240 presentation transform and previous camera screenshots | VERIFIED-PORT-TRANSFORM; full world/camera/2P model is not claimed |
| Startup host sink previously consumed sub-target PCM immediately | `native/shell/audio.cpp` before this change had no start/recovery pre-roll gate | VERIFIED-PORT-CODE; pulsing on the smartphone remains a scheduling/buffering HYPOTHESIS until measured |

The new playback fixture tests pre-roll, non-consuming waits, starvation,
recovery, stereo order, finite output and reset at 1/22.05/32/48/96 kHz.
Gesture fixtures use the original-like octagonal/deadzone/nonlinear cursor and
integer camera arithmetic with delayed feedback. These are control invariants,
not audible device or emulator parity proof. No original ROM, snapshots or
recompiled sources are added to version control.


## Main-menu PCM starvation follow-up (2026-10-08)

| Finding / scope | Verification method | Status |
|---|---|---|
| Smartphone music still pulses/crackles in the main menu; mission audio is normal | Direct user report after the previous APK; model/OS/log not supplied | USER-REPORTED; previous buffering is not accepted as an audible fix |
| Host reports a constant 11025 Hz and 512-frame (46.44 ms) DMA in the compared menu route | Native `SC64` telemetry in `menu-audio-desktop-01/02`; do not infer actual console DAC rate from the nominal host callback | VERIFIED-NATIVE-HOST |
| Device callback could previously insert a whole silent block when the AI mutex was busy | Previous `audio.cpp::callback` try-lock failure path | VERIFIED-PORT-CODE; not independently proven as this phone's root cause |
| Raw occupancy correction had callback-sized rate jumps | Previous `pcm_playback.hpp`; smoothing fixture now bounds the correction delta | VERIFIED-PORT-CODE + FIXTURE; audible contribution remains HYPOTHESIS |
| Software rendering delayed servicing of higher guest priorities | `sc64_capture_submit` previously held the publication mutex while synchronously rasterizing and did not call runtime scheduling; pinned `ultramodern/src/scheduling.cpp` compares runnable thread priorities and switches native guest threads | VERIFIED-PINNED-SOURCE; no invented game thread-priority inventory |
| Optional rendering checkpoints preserve image bytes and unrelated guest writes | `test_desktop_bridge.cpp`: interrupted/uninterrupted rendering equality, masked writeback preserving a changed non-framebuffer byte; existing gfx corpus fixtures | VERIFIED-FIXTURE; full campaign parity remains separate |
| Servicing during rasterization eliminates menu starvation in this repeated desktop route | `local/reports/android/menu-audio-comparison.json`: 12 menu samples span 55.067 s in each 100 s run; last menu cumulative starvations 4 before vs 0 after, discarded PCM 0 in both | VERIFIED-BOUNDED-NATIVE; first run already included inbox/pre-roll/smoothing, isolating the subsequent scheduling change except two lookahead frames |
| Attract/demo loading still has a starvation outside the menu sample window | Last native game sample after the menu timeout: first run cumulative 5, second 1 | VERIFIED-NATIVE; not silently included in a zero-underrun menu claim |
| AAudio reserve/query APIs and miniaudio stream access are checked locally | NDK 29 `sysroot/usr/include/aaudio/AAudio.h`: setBufferSizeInFrames/getBufferSizeInFrames/getBufferCapacityInFrames/getFramesPerBurst/getXRunCount, API 26; pinned miniaudio 0.11.23 `ma_device.aaudio.pStreamPlayback` and stream lifecycle | VERIFIED-INSTALLED-HEADERS/SOURCE; Android profile minimum API is 28 |

The producer-parked-mutex fixture proves the inbox consumer still drains published
PCM. A 100000-frame concurrent transfer verifies order/stereo/generation coherence.
No callback-time allocation or guest AI mutex remains in host PCM consumption.
Exact physical waveform/audibility and Bluetooth/wired-device paths remain open.

The final Android receipt `menu-audio-final-summary.json` independently records
40.097 s of menu samples with no new PCM starvation, no AAudio xruns and no
discarded frames. The backend rounds reserve to 3840 frames, with 960-frame
bursts and capacity 4360. Home/return passes its strict guest freeze/progress
gate. This is headless-emulator timing evidence, not audible physical sound.
Desktop `menu-audio-mission-01` completes to gameplay and passes cursor arrival;
no additional gameplay starvation after the observed loading gap.


## Contextual mission Back and resource taps (2026-10-08)

| Fact | Verification method | Scope |
|---|---|---|
| Start opens the mission menu; B does not perform that action on a plain map | Original input branch `0x80038DF4..0x80038E84` tests pressed mask `0x1000` and dispatches the mode-3 menu; existing original `hud-02/pause.png` / `pause-back.png` pair | VERIFIED-ORIGINAL/SOURCE. System Back now chooses the original mask at key-down. |
| Cursor hover uses the original sprite/unit hit-test result | `0x8001AD70` calls `0x8008A688`; at `0x8001AD78`, v0 is the result before the subsequent call; caller `0x8001AE94` handles minimap/alternate cursor paths | VERIFIED-ORIGINAL-SOURCE. No host sprite/bounding-box approximation. |
| Sampled cursor coordinates for that query are signed halfwords `0x800AFE18` / `0x800AFE1A`; world point adds camera words and subtracts twice viewport offsets `0x800A5B0A/C` | Original `0x8001AD20..0x8001AD74`; observer publishes half-size native-image coordinates and fences both camera words | VERIFIED-ORIGINAL-SOURCE. Distinct from the continuously moving float cursor. |
| Unit type and owner fields are +0x64 (unsigned halfword) / +0x4C (byte); per-type flags start at `0x800C99DC` | Hit-test reads +0x64 at `0x8008A6F0`; owner comparison at `0x8001AE30..0x8001AE44`; original resource paths below read the same type table | VERIFIED-ORIGINAL-SOURCE. Not a complete unit structure. |
| Type-table bit 3 is set for IDs 7, 41, 64 in the loaded game fixture; bit 13 is the original resource-container predicate | Canonical `local/captures/input/hud-02/advisor-5.cpu-visible.bin`: bounded scan of 228 table entries; original `0x80132470..0x8013248C` and `0x80139760..0x80139798` test bit 13 and resource ownership. `0x80133374..0x801333DC` distinguishes three mineral types 176..178 and gas structures 110/149/157 | VERIFIED-ORIGINAL-SOURCE/SNAPSHOT. The adapter uses the table predicates, not PC-game addresses. Unused resource types and all gas states are not runtime certified. |
| Current player owner is read from `0x800B00FC`; the selected primary/list/count are `0x800C6C18` / `0x800C6C20` / byte `0x800C6C14`, up to 18 pointers | Original contextual-order handler `0x800472BC..0x800474AC`, selection owner check and 18-slot loop; prior selected-list capture in this document | VERIFIED-ORIGINAL-SOURCE/SNAPSHOT. Only own selected workers and own/neutral targets enable the automatic B rule. |
| The observers leave canonical guest state and registers unchanged; incomplete pointers, build/modal/split-screen and mismatched render context are rejected | `test_ui_observers.cpp`, `test_desktop_bridge.cpp`; Lua tests cover mouse/touch, deferred fresh picks, explicit B, A-held drag and Back held across menu changes | VERIFIED-FIXTURE. Native execution evidence is recorded separately in `input.md`. |

The observer generator checks exact ROM SHA-1 and both original instruction words:
`0x8001AD78` / ROM `0x1B978` = `0x0C01F789`, and `0x8001AE94` / ROM `0x1BA94` =
`0x27BDFFD8`. Generated game C, canonical snapshots and personal recipes remain
ignored. A resource tap is deliberately translated into an original B order;
it is not a direct harvest-function patch or a forced change of selection.

Native Back is also observed in a bounded desktop recipe:
`back-gather-probe-02/back-summary.json` binds fresh game -> Start at field 7800
-> displayed mission pause menu -> B at field 8080 -> game at field 8086. The
recipe uses the production key-down adapter but bypasses OS event delivery.

`back-gather-native-05/resource-order-summary.json` separately binds original
hit type 7 selected with A at field 7141, hit type 176 with a selected own worker
translated to B at field 8132, and the same selection context afterwards. This
run is native translation/selection evidence only: original tutorial advisors
interrupt the worker's route before the final trace. Test-point calibration
`native-03` hit terrain beside the patch and correctly remained A. `native-04`
failed before loading the game in the engine's Remotery profiler (SampleTree_Pop);
the unchanged-binary retry completes. Neither is a harvesting pass.

The completed `back-gather-native-06/harvest-summary.json` binds the same
production path to an unchanged 245-second native run (exit 0). After closing
original tutorials, native rendered fields 14000 and 14600 show minerals
80 -> 88 while the selected-worker context remains unchanged. The values are
visually reviewed from native RGBA upload derivatives, not inferred from input
submission or an OCR fixture. This verifies gathering on the Wasteland mineral
patch, not every worker race/gas structure or physical Android event delivery.


## Hint-canvas sliding input policy (2026-10-08)

This is host input adaptation, not a new fact about original guest structures.
`test_touch_input.cpp` drives the actual GUI mouse/multi-touch dispatch and
verifies HUD-first/map-first/both-HUD two-finger input, selection starts/endpoints,
no hint/B leakage, tap jitter and current-position camera origins. Existing
hint stale/ownership/FIFO tests remain green (12 CTest checks, 134 Python checks).

The unchanged-binary native 165.921-second run
`local/reports/android/hud-pass-through-native-01/run.json` verifies the two camera
routes and both rectangle routes. `gesture-summary.json` binds submitted Z/A,
release/arrival and camera movement to that recipe; the final uploaded frame
visibly shows the original selected infantry group. This is production-module
controller/guest evidence with canonical-coordinate replay. Physical phone touch
delivery/feel is not certified by the recipe. ADR-048 defines the new tap/slide
ownership policy; no guest RAM write, new graphics API or ROM-specific hook was
added for this adaptation.

## Direct minimap camera navigation (2026-10-08)

| Finding / scope | Evidence | Status |
|---|---|---|
| 1P minimap visibility is byte `0x800AFD7C`, with inclusive screen bounds at `0x800AFD84..8A` in 320x240 units | Original `0x8001AE94` halves sampled cursor coordinates, checks visibility and calls rectangle predicate `0x8001B32C`; `0x80034D38` toggles visibility | VERIFIED-ORIGINAL-SOURCE |
| Minimap conversion uses scale/shift/divisor `0x800EF656/654/650`, extent `0x800EF670/672`, and viewport dimensions `0x800A5B04/08` | Original `0x800365CC` subtracts integer half-viewport size before `0x80034DB8` subtracts rectangle origin, clamps and multiplies; `0x80036700..44` rounds down to 16 world pixels | VERIFIED-ORIGINAL-SOURCE; preserve integer centring/rounding |
| Camera setter clamps, aligns to eight pixels, updates camera words/tile cache, and requests redraw | Original `0x8000B988..BADC`, including `0x80024BD8`; `0x80068D50` checks byte `0x800FD038` | VERIFIED-ORIGINAL-SOURCE; calling this setter is a scoped guest-code action, not a read-only observer |
| Native camera jumps do not require the game cursor to travel | Completed `local/reports/android/minimap-native-04/run.json`, bound `minimap.json`, neutral submitted controller input and owned fresh snapshots | VERIFIED-NATIVE-RECIPE: three mouse clicks and one synthetic single-finger tap; all four consumed in their request field |

The native run moves the camera to (528,544), (128,144), the clamped far corner
(720,800), and (368,384), in half-size image units. The cursor remains (160,120)
through all four transitions. Turning off the minimap through original Z+L leaves
the navigation count at four despite a later click in its old rectangle. Uploaded
frames before/after the first jump show the viewport moving on the minimap and
the cursor remaining in place. The first diagnostic result can share a VI field
with an older snapshot; acceptance therefore uses strictly later owned snapshots,
and the current diagnostic waits for a strictly later cursor frame.

Twelve native/Lua checks cover conversion, canonical/word-swapped equivalence,
query-entry dispatch, changed geometry, hidden/modal/build/2P locks, queue clearing,
mouse hover/drag, stationary-pointer deduplication and touch cancellation. The
135 Python checks also pass. macOS and the existing personal bundled-ROM Android
profile both build. Native replay is not physical mouse/touch delivery or phone
acceptance. ADR-049 records the original-setter exception to controller-only pan.

## Android-only menu music follow-up (2026-10-08)

| Finding / scope | Verification | Status |
|---|---|---|
| Menu music pulses on an Android 16 phone, including after returning from gameplay; desktop menu and gameplay audio are normal | Direct user report in this conversation | USER-REPORTED; headphone route not specified |
| The former engine's public SDK has no live PCM producer API | Its installed sound header contains group mute operations; its sound-data resource header only identifies the resource handle | VERIFIED-INSTALLED-HEADERS |
| The exact engine provides internal resource streaming and an Android OpenSL device | Fetched the exact pinned revision: `engine/sound/src/sound.h::NewSoundDataStreaming`, `devices/device_opensl.cpp`; private copies under ignored `local/research/` | VERIFIED-UPSTREAM-SOURCE; internal API is not a public extension PCM contract |
| Android source delivery has longer gaps than a fixed two-DMA startup reserve safely covers at all queue occupancies | `local/reports/android/menu-audio-profile-comparison-01.json`: same native library in two private diagnostic APKs; source max gaps 84.45175 / 96.577084 ms, average 11039.92 / 10996.82 stereo frames/s | VERIFIED-EMULATOR; complete phone cause remains a HYPOTHESIS |
| One AAudio trace has two partial output blocks and 713 zero stereo frames versus one zero frame in the source stream | Same report hashes original source/output float recordings; compatibility trace has no partial output/zero frames | VERIFIED-BOUNDED-EMULATOR; sequential runs have different scheduling jitter and do not isolate a vendor driver bug |
| Backend replacement alone is insufficient | `menu-audio-compat-emulator-01/audio-summary.json`: five new menu starvations over 20.05 s; subsequent diagnostic OpenSL trace is clean | VERIFIED-MIXED-EMULATOR-OUTCOME; retain failed run |
| Increasing pre-roll without increasing storage also loses PCM | Rejected `menu-audio-jitter-final-01` uses 160 ms target with the old 200 ms cap; discarded-frame counter rises | VERIFIED-REJECTED-CANDIDATE; ADR-050 adds 300 ms compatibility storage |

The current mitigation targets delivery jitter and platform output compatibility without
changing guest audio samples, AI deadlines or VI cadence. The deterministic
15-second jitter regression alternates 1024-frame deliveries around their nominal
11025 Hz deadlines; the old 1022-frame reserve starves, the 1764-frame menu reserve
does not. Separate tests cover retained-stream rebuffering, signed stereo
conversion/clipping, callback sizes beyond scratch capacity, bounded trace
storage and invalid trace ranges. Hardware audibility and transition acceptance
remain separate from these fixtures and emulator recordings.

Final-build evidence is in `android.md`. The initial 70-second final APK
smoke passes Home/resume with zero software starvation counters. Extended
same-process telemetry subsequently records three starvations in the first
menu episode near a sampled 688.755 ms software-render delay; the next sampled
menu episode is clean and no PCM is discarded. This is a mixed bounded emulator
result, not proof that all Android menu gaps have been eliminated or that a
vendor hardware output route is defective.
An isolated 110-second final-build repeat also records one starvation and no
discarded PCM in a 45.07-second menu window, while passing Home/resume. Its
receipt is `menu-audio-package-final-02/run.json`; therefore eliminating
simultaneous desktop gameplay alone does not establish a clean menu stream.

## Defeat/Victory pointer input (2026-10-08)

The shared post-mission handler `8012CA48` constructs the results root at
`80112800` with callback `8012C90C`. The selected statistics tab is the word at
`80112804`; `8012B688` clamps it to 0..3. Original Left/Right keys (`0x25/0x27`)
are emitted from D-pad `0x0200/0x0100` by `80038F74..80038FD8`. The host now
publishes a distinct `results` UI with four non-activating horizontal items.
An ordinary tap outside those items emits A through the original Continue path.
No guest cursor movement or direct tab/window state writes are used.

The four assets at `80112860` are full 384x45 strips painted at (218,30) by
`8012B4A4`, rather than individual tab sprites. Both nonzero-pixel and pixel-diff
bounds overlap because of the shared frame/artwork and palette differences.
The final NTSC-U profile uses the original visible tab edges for each selected
tab: 232/373/445/521/592 (Overview), 232/284/445/521/592 (Units),
232/284/361/521/592 (Buildings), and 232/284/361/443/592 (Resources).
The selected tab expands and shifts the collapsed tabs to its left; keeping
fixed Overview bounds would select the wrong tab on the return trip. Y is
44..72, guarded by menu IDs 13..18, the active root and
callback identity, original callback/draw prologues, valid asset pointers and
all four strip dimensions. Invalid tab state or strip data publishes no result
hit boxes. The coordinates are
native 640x480 pixels; existing viewport conversion handles mouse and touch.

Completed private `local/reports/android/results-native-09/results.json` verifies
Defeat tab selection 0→1→2→3→2→1→0 through six original D-pad pulses. The outside
tap at field 7450 emits A and returns to menu at 7455. Engine and complete
resource hashes remain unchanged for the 140-second run on an isolated bundle.
Supplementary native screenshots `frames/overview.png`, `units.png`,
`buildings.png` and `resources.png` retain the rendered tabs from run 07 using
the same unchanged build; `frames/source-run.json` records their provenance.
Run 07 only supplies visual evidence: frame recording disrupted short pulses.
Earlier diagnostic runs are not complete navigation acceptance evidence.

Twelve native/Lua checks and 135 Python checks pass. Fixtures cover every tab
pair for both mouse/touch, actual HiDPI GUI dispatch, selected-tab no-confirm,
outside Continue, hold behavior, movement/cancel/stale/transition rejection and
asset/profile guards. macOS and the existing personal bundled-ROM arm64 Android
APK rebuild successfully. Victory shares the original constructor/callback and
layout; a separate live Victory run and physical Android touch remain untested.

## phone audio starvation isolated and corrected (2026-10-08)

| Finding | Verification | Status |
|---|---|---|
| The OpenSL compatibility candidate worsens audible interruptions | Direct user report after the previously delivered APK; app-scoped phone log observes OpenSL, 160 ms target and rising software starvation counters | USER-REPORTED + VERIFIED-PHYSICAL-TELEMETRY; reject ADR-050 as the default |
| AAudio hardware output is not starving in the measured failing menu window, while guest PCM is undersupplied | `local/reports/android/phone-aaudio-before-menu-01/audio.json`, derived from captured PID 8670 prefix; installed APK identity comes from `phone-aaudio-diagnostics-01/audio.json`: zero xruns/discards, 551 software starvation callbacks over 55.06094 s, source 10247.26 frames/s at nominal 11025 Hz, maximum gap 104.21 ms | VERIFIED-PHYSICAL-PHONE; does not generalize to every Android hardware route |
| Display-list interpretation had no scheduling checkpoints, unlike rasterization | Current `interpreter.cpp` loops and pre-change `sc64_capture_submit`; pinned `scheduling.cpp::check_running_queue` only resumes higher-priority runnable guest threads | VERIFIED-PORT/RUNTIME-SOURCE; no invented inventory of game priorities |
| Checkpoints through interpretation restore source throughput on the same phone/output profile | `phone-menu-audio-comparison-01.json` binds the before/after reports and APK identities: source 11029.00 frames/s, maximum source gap 61.01 ms, zero starvation/discards/hardware xruns over 55.05774 s of menu | VERIFIED-PHYSICAL-BOUNDED-INTERVENTION; supports the scheduling defect mechanism |
| Current menu playback sounds normal | User's explicit confirmation after installation during this turn | USER-ACCEPTED-PHYSICAL-MENU; headphone route unspecified |
| Scheduling does not change parsed texture keys or framebuffer stores in fixtures | Interpreter test services a 64x64 background while mutating the caller's live input; the owned result exactly matches the unserviced reference. Cancellation propagates; interval epochs and concurrent peak collection are tested | VERIFIED-FIXTURE |

The device is an Android 16 phone, arm64-v8a, 4096-byte pages.
APK `39015aab432b4c676e8feb95cb3476d8bc273dcca9b743918b9b1ee0de43c9b2`
is the instrumented failing AAudio baseline; final APK
`8e6dc07c8daaffdc3bf5ca19978b2013f233cb29d16c3b6c3aadf47277379838`
is verified installed. The latter is also user-accepted for menu listening.
The earlier capture attempted while Android Settings was foreground has frozen
counters and is not an advancing-window acceptance run. The 100-second baseline
collection later captures mostly attract/intro; its **prefix log**, retained in
`engine-before.log`, provides the explicitly separated failing menu window.
Neither trace is hardware loopback. Source/played-output silence ratios count
original/played samples, excluding the unplayed zeros during rebuffering.

ADR-051 records the complete-snapshot-before-yield rule and scope. The corrected
macOS run `phone-audio-service-desktop-01/run.json` binds unchanged engine and
resources, successful 55-second exit and a 2400..3000-field menu PCM window:
110592 source frames at 11025 Hz, 480768 output frames at 48000 Hz, no truncation,
zero frames, partial output or starved output blocks. Physical phone listening
and this pre-device PCM check are distinct evidence.

## Texture interpreter CPU profile (2026-10-09)

These are host implementation measurements, not newly inferred original-game
semantics. Private evidence is under `local/reports/texture-processing-01/`.

| Fact | Method and evidence | Scope |
|---|---|---|
| Texture key hashing dominates the profiled offline menu interpretation | macOS `sample`, 1 second/1 ms sampling, owned baseline replay process; `baseline-sample.txt` has 650 of 831 stacks on the channel hash loop and 774 in `Interpreter::store` overall | One fenced main-menu task repeated 100 times; no guest or GPU execution |
| Optimized texture keys and RDP stores preserve the previous renderer result | `comparison/benchmark.json`, hashed baseline/candidate executables and canonical inputs; all 20 full rendered RAM hashes and semantic reports match | Menu, briefing, 1P and 2P accepted corpus; not all campaigns |
| Offline main-menu interpretation median falls by 75.88% | 25 iterations per case after one warm-up; alternating baseline/candidate order; per-case medians recorded by `tools/renderer/benchmark.py` | macOS arm64 only; excludes rasterization, audio service and phone/thermal behavior |
| Original-target RGB gate is unchanged | `original-rgb/comparison.json`, all 20 fenced post-task original frames meet frozen 40 dB; minimum 41.61048387106946 dB | Existing bounded native-target RGB metric, not bit-exact VI/coverage |

ADR-052 records the unsigned-arithmetic/key and ownership decisions.

## Physical double-tap translation defect (2026-10-09)

| Fact | Verification method | Scope |
|---|---|---|
| The previous map adapter did not explicitly recognise double touch taps | Inspected `gestures.pointer`: every non-drag one-finger release enqueued automatic A; cursor-arrival polling independently emitted each action. The user reports unwanted area selection | VERIFIED-HOST-SOURCE and USER-REPORTED symptom; not a measured original double-A timing rule |
| The revised input adapter coalesces two nearby touch taps into one B without a preceding A | `tests/native/test_map_taps.cpp`: 0/4/12/17-field intervals, 1/2/5-field polling, 0/5/9-pixel second-touch drift, cursor travel, context changes and cancellation; production MULTI_TOUCH dispatch in `test_touch_input.cpp` | VERIFIED-FIXTURE; selected unit movement and physical Android delivery need separate evidence |
| The regression detects both loss of the deferral and a wrong A translation | In-memory module mutations, `local/reports/double-tap-mutations.json`, both exit 1 at the no-A invariant | Mutation-test evidence only; repository sources were not altered for the probe |

ADR-053 documents the chosen gesture thresholds. They are host UX parameters,
not newly inferred ROM facts or platform API defaults.

The completed desktop native contact recipe
`local/reports/android/double-tap-native-01/double-tap.json` verifies two B edges
at fields 7758 and 7962, no A in either pair's window, arrival within 1.5 pixels
of (180,100), and retained own-worker selection context 1225182105. The hashed
run exits 0 with unchanged engine/resources. This is VERIFIED-NATIVE-RECIPE
translation/selection evidence; it bypasses physical OS contact delivery and
does not certify completed unit movement or physical Android gestures.

## Android 16 KiB startup and graphics profiles (2026-10-09)

These are host/platform observations, not new ROM addresses or game semantics.
Private evidence is in `local/reports/android-runtime-profile-01/`.

| Fact | Verification method | Scope |
|---|---|---|
| The official Android 16 ARM64 16 KiB image boots with 16384-byte pages | Google repository archive SHA-1 matches `dda632745023567113d9954101709c6526a3c250`; explicit emulator serial reports `getconf PAGE_SIZE=16384`; both runtime receipts record ABI/page size | Separate revision-7 AVD; the physical phone remains a 4 KiB device |
| The personal APK executes through both requested graphics adapters | Audited candidate and installed APK hashes match; current-launch `graphics_family` telemetry is exclusively 4 for Vulkan and 3 for OpenGL ES; guest/PCM/presentation counters advance | VERIFIED-ANDROID-EMULATOR; two 55-second intro windows, no mission or exact frame parity |
| Home/return preserves that guest and restores presentation/PCM | Same PID in both receipts; suspension delta is `[0,0,0]` fields/tasks/samples for Vulkan and `[1,0,0]` for OpenGL ES; all post-return lanes advance, without guest error/crash | Bounded emulator lifecycle; not lock/unlock, process death or physical audio-device recovery |
| Existing COP1 mode/subnormal probes execute on that 16 KiB kernel | Hashed Android `sc64_guest_fpu_test`, `fpu.json`, exit 0 with four rounding modes and one non-flushed subnormal | Existing helper contract only; broader VR4300 arithmetic and exceptions remain D-012 |
| The main menu is visible after the timed OpenGL ES smoke | Inspected `opengles/screen-after.png`, captured after dismissing Android's immersive guidance | Separate post-window visual check, not measured menu cadence or pixel parity |

ADR-054 and `android-runtime.md` document the APIs, commands and acceptance
boundaries. The renderer/core and original guest gameplay are unchanged.

## Scoped Stage 5 closure evidence (2026-10-09)

These are host/platform tests and user acceptance, not new original ROM facts.
Private evidence is in `local/reports/stage5-close-01/`; ADR-055 records scope.

| Fact | Verification method | Status / limits |
|---|---|---|
| First Terran mission can be completed with touch only on the phone; save/load after restart works | User replies to the explicit phone touch-only/no-keyboard/no-diagnostic-pad mission and cold save/load question: "Mission completed, save/load checked" | USER-ACCEPTED-PHYSICAL; no new recording or exact walkthrough APK hash |
| The established helper numeric profile passes on both Android kernels | Same hashed standalone `sc64_guest_fpu_test` exits 0 on the phone pages4096 and Android 16 ARM64 emulator pages16384; `phone-fpu-supported.json`, `emulator-fpu-supported.json` | VERIFIED-HELPER-PROBE: four rounding modes, arithmetic/conversions, subnormal operands/results, signed zero, host NaN and guest-thread isolation; not full VR4300 exceptions |
| Actual Android Back opens/closes the original mission pause menu | Android `KEYCODE_BACK` events, current-launch `game -> modal -> game` telemetry, inspected `back-open.png` and resumed `screen.png`, successful runtime validator | VERIFIED-ANDROID-EMULATOR; native recipe enters the map, so this is not a touch-only walkthrough |
| Mission Home/return freezes guest work and resumes the same process | `back-home-final/run.json`: identical PID, suspension fields/tasks/samples 14324/12605/5213188 both sides; delta `[0,0,0]`, three advancing post-return samples, no guest error/native crash | Bounded 16 KiB Vulkan lifecycle; software audio underrun count stays 1 across Back/Home, no zero-underrun full-session claim |
| Current production APK identity and legal/alignment audit remain intact | Repeated `make check-apk BUNDLE_ROM=1`: APK SHA-256 `b1189d2375af40c60ad8ff1cdee3702c577ac8aeed2867cf09cd71ff77d87978`, final ELF/ZIP 16 KiB alignment and ROM SHA-1 verified | Existing ADR-054 build, no production rebuild or gameplay change in this closing diagnostic pass |
| Desktop/native/Lua/Python regressions pass | Completed `native-final.log` (13 CTest) and `python.log` (149 Python/Lua) | VERIFIED-REGRESSION; separate from physical mission acceptance |

The final Android FPU executable SHA-256 is
`274bdc797a8595519bf7f8a8884ee68bc16fc831b450fc73a6f5fdf171c0c59a`.
An earlier artificial FZ16 parent-state probe fails only on the emulator: FPCR
changes from `0x03880000` to `0x03800000` across thread creation/join. Its cause
remains unverified; it does not establish a game defect or FP16 certification.
The final probe seeds modes affecting guest 32/64-bit arithmetic and still checks
that guest initialization clears FZ16. Failed attempts are preserved.

Expanded device validation and long Adreno/Mali performance/thermal tests are
explicitly omitted from Stage 5 acceptance at the user's request. They remain
deferred without successful-result claims. The complete current checklist,
build commands and limits are in `stage5-acceptance.md`.

## Stage 6 original text and viewport audit (2026-10-09)

This is a static source/instruction audit of the normalized NTSC-U ROM SHA-1
`a36d91b5aac7bb95df0d7de5f9814f5270e25c2b`, not a new HD/widescreen runtime trace.
Private `local/reports/stage6-presentation-01/source-audit.json` records original
instruction words and inspected generated/runtime source hashes. Resident PC
mapping uses the previously verified `ROM = PC - 0x80000000 + 0xC00` section.

| Observation | Verification method / anchor | Boundary |
|---|---|---|
| The resident text routine preserves its incoming `a2` stream pointer in `s3` | Original word at `0x800211E4 = 0x00C09821`; inspect generated `sc64_resident_800211D8`, `funcs_10.c` | VERIFIED-STATIC data flow; character encoding/control codes and complete call coverage remain unverified |
| Text wrapper `0x8003F070` stores clip coordinates and forwards a stack text argument as `a2` | Generated `funcs_22.c`: `a0..a3` writes to the clip block at `0x800A5A18`; call at `0x8003F09C = 0x0C008476` and delay slot `0x8003F0A0 = 0x8FA60028`. New SP has moved by 0x18, so +0x28 is caller SP+0x10 | VERIFIED-STATIC wrapper; layouts/erase order still need original execution traces |
| Text uses the current CPU canvas pointer and dimensions | `0x80021244 = 0x8C45FD60` reads the pointer at `0x800AFD60`; `0x8002124C = 0x84A40000` reads its signed width, followed by height at +2. Existing text loop/clip and indexed-store research are consistent with this path | VERIFIED-STATIC corroboration of the observed CPU text path; not a separate GPU glyph draw list |
| Camera rendering depends on viewport offsets and canvas geometry | Generated `sc64_resident_8000ACE4`, `funcs_2.c`: camera coordinates `0x800A2EE8/EC`, halfword viewport offsets `0x800A5B0A/0C`, canvas geometry at `0x800A5AF0/FC/0x800A5B00` feed stride and tile-cache calculations; original words `0x8000AD24 = 0x97C25B0C`, `0x8000AD2C = 0x96E45B0A` | VERIFIED-STATIC dependencies, not proof that changing an offset alone is safe |
| Input/minimap reuse viewport geometry | Existing guarded cursor/minimap helpers and original `0x8001AD20` path: camera addition and twice the viewport offset relate screen and map coordinates; minimap helpers use `0x800A5B04/08` | Existing researched transform, re-audited for Stage 6; widescreen allocation/culling/fog/scripts/2P remain unqualified |

**HYPOTHESIS:** increasing rendered canvas/view bounds can affect culling and
visible-area rules. No Stage 6 runtime experiment has established whether it
changes fog or gameplay visibility. Trace allocation, tile traversal and original
selection/minimap/fog state in paired widths before promoting this interpretation
or enabling wider world rendering (D-044).

This was the planned HD experiment before the user rejected replacement fonts.
The glyph observations below remain research; D-043 is cancelled by ADR-059.

## Stage 6 optional image-mode evidence (2026-10-09)

These are host/platform observations, not new game behavior. Private outputs
are in `local/reports/stage6-presentation-01/`; ADR-056 and `presentation.md`
record scope and reproducible commands.

| Fact | Verification method | Scope / status |
|---|---|---|
| The former engine supports the constant-buffer draw route used here | Pinned engine render-script source: `render.constant_buffer` creation and `render.draw` options parser; local desktop/Android material/shader builds succeed | VERIFIED-PINNED-SOURCE and BUILD; no invented engine render entry point |
| New presentation preserves the native image and the Original GPU output | Fresh native comparison and acceptance recomputation: minimum 41.61048 dB; all 60 comparison PNGs identical to the previous reviewed corpus. `original-final/gpu/acceptance.json` and `presentation.json`: all 20 outputs exact, black bars | VERIFIED-OFFLINE/NATIVE-GPU, existing menu/briefing/1P/2P corpus on macOS Vulkan |
| Sharp/Smooth match an independent pixel reference at fractional fit scales | `sharp-final/presentation.json`, `smooth-final/presentation.json`: 20 whole-viewport comparisons each, maximum channel error 1/255. Oracle virtually repeats source pixels before bilinear resizing, rather than copying the shader's coordinate formula | VERIFIED-NATIVE-GPU at 2000×1400 physical window; not reconstructed font detail or Android pixel parity |
| Preferences save and restore on desktop | Actual F5/F2 input changes modes/feedback; cold launch shows Sharp/Fit. Final key logs restore Original/Integer. Real GUI handler fixtures cover matching touch/mouse ownership, slide cancellation, no guest pulse and save errors | VERIFIED-NATIVE-KEYBOARD/PERSISTENCE and FIXTURE; native settings clicks are not certified because automation delivered unrelated coordinates |
| New shader modes execute through Android Vulkan and OpenGL ES | Same APK hash verified against installed APK; two 35-second 16 KiB emulator receipts report Sharp/Fit family 4 and Smooth/Fit family 3, same PID, advancing post-return guest/PCM/presentation and matching requested/actual modes | VERIFIED-ANDROID-EMULATOR bounded lifecycle; no phone reinstall, audible emulator audio, all-mode/backend combinations or phone settings-tap acceptance |

Earlier static-fixture attempts included a loading GUI overlay or sandbox GPU
unavailability; they remain preserved as failed/diagnostic attempts. Accepted
pixel runs isolate fixture mailbox ownership and omit GUI only in `gpu_probe`.
Normal application GUI/autostart remain active. Stage 6 remains in progress.
(Superseded: Stage 6 was withdrawn by ADR-060 on 2026-10-09.)


## Stage 6 glyph observation and wider geometry dependencies (2026-10-09)

Revision: normalized NTSC-U SHA-1 `a36d91b5aac7bb95df0d7de5f9814f5270e25c2b`.
Verification combines original resident instructions, generated data flow and
owned native traces under `local/reports/stage6-text-01/`. These facts do not
certify every font, scene or wider gameplay profile.

| Observation | Verification / anchors | Evidence scope |
|---|---|---|
| The resolved glyph call uses the current stream character and actual destination | `80020D98 = 0C007CAC`, return `80020DA0 = 3C08800A`; s2 is the stream; delay slot `80020D9C` adds v0 to a0. Width/height at `800A5A64/68`; canvas pointer `800AFD60`, dimensions +0/+2 and pixel pointer +4 | VERIFIED-STATIC; read-only native before/after observer; printable ASCII coverage only |
| Measurement mode skips this paint call | `80020D44` reads byte `800A59FC`; `80020D48` branches around painting | VERIFIED-STATIC; fixture verifies no exposed metadata or guest writes |
| Original glyph source separates shape from shades | `80020D88` passes s0+4 as a1; `8001F31C` reads a byte, `8001F324` shifts by 3 for skip, `8001F32C` masks 7 for shade; `8001F330/334` maps it through the table at `800A5A60`; `8001F33C` stores the resulting index. `8001F308/310/350` carries a skip across rows | VERIFIED-STATIC; bounded synthetic RLE and animation regressions. Highest-two-level bounds are an authored presentation choice |
| CPU canvas height and S2DEX source height can differ by one padding row | `80066594` path reads source header height at +2 (`800665F4`), adds one at `80066600`, and emits the background. Advisor native trace has 100-row canvas / 101-row background | VERIFIED-STATIC + NATIVE advisor composition; not permission to ignore arbitrary bounds |
| Observed frontend/HUD compositions include pure texel and tinted shadow | Native background traces: combiners `FCFFFFFFFFFCF279` and `FC119623FF2FFFFF`; blender high words `00400000`/`00500000` (source alpha) and `0C080000` (pixel/zero/pixel/one). Original software combiner supplies presentation color | VERIFIED-NATIVE bounded scenes; other compositions fall back |
| Palette-dependent bright-pixel bounds can change HD letter shape during a flash | User observation on the phone; previous HD path derives bounds from evaluated RGB; fixture varies backing brightness | VERIFIED defect; corrected bounds derive from encoded source levels, with real animation acceptance recorded separately |
| Original 1P/2P geometry shares allocation and camera dependencies | Guarded tool `audit_widescreen.py`, 19 words and 8 original snapshot hashes. Setter `8002443C` stores viewport extents and doubles them into `800A5B04/08`; `80033F24` reads them for cache request; `8000A010` subtracts rounded world width from camera X maximum; centered camera `80028B60` reads extents | VERIFIED-STATIC + original snapshot measurements in `widescreen-audit.json`; no wider gameplay execution |

The measured cache-request formula is `((world_width+31)>>5)+4` times
`((world_height+31)>>5)+4`. The original 640×480 request is 456 bytes; a derived
864×480 extent requests 589. A wider 432×240 canvas is a counterfactual, not an
implemented 16:9 mode. Heap rounding, fog, culling, scripts, save compatibility
and full split-screen changes remain unmeasured. Their gameplay effects are
**HYPOTHESIS**, and require paired original/wider experiments under D-044.


The HD glyph experiment above was rejected by the user on 2026-10-09 and all
runtime support was removed (ADR-059). Historical measurements remain research
facts, not evidence that replacement fonts are shipped or visually accepted.
The final runtime uses only original glyphs and contains no glyph observer hook.


## Stage 6 withdrawal (2026-10-09)

The user cancels the complete optional presentation experiment (ADR-060).
The observations above remain historical research only. The authored widescreen
audit tool is preserved privately under
`local/reports/presentation-removal-01/withdrawn-source/tools/research/` and removed
from the active repository. No HD/filter preference or wider guest patch remains.
Original fonts/nearest sampling and fixed Fit define the current presentation.

## RDP COPY alpha compare, LOADTLUT span and combiner inputs (2026-10-10)

Method: source analysis of the pinned angrylion oracle (`rdp/rasterizer.c`
`render_spans_copy`/`fetch_qword_copy`, `rdp/tex.c`
`tile_tlut_common_cs_decoder`/`loading_pipeline`, `rdp/combiner.c`), plus the
20-frame fenced original-frame gate, which stayed byte-identical (minimum
41.61 dB). Scope: renderer semantics; no new game scene was captured.

- COPY into a 16-bit target with alpha compare tests bit 0 of the raw texel or
  TLUT entry; blend alpha and alpha dither are ignored. Other COPY alpha-compare
  combinations now fail explicitly. Raw COPY stores remain D-045.
- LOADTLUT entry count is `(sh>>2)-(sl>>2)+1`; reversed limits, multi-row loads
  and more than 256 entries fail explicitly.
- Combiner: sub-B 6/7 are key centre and K4; mul-C 6/7 are key scale and combined
  alpha; C 13-15 are LOD/K5; sub-A 7 is noise. All now throw instead of silently
  returning zero.

## Group and unit-type controller sequences (2026-10-10)

Method: source analysis of generated `sc64_resident_80038D1C` (controller
dispatch) and sampler `0x80037E60`, the scanned manual (pages 4, 5 and 7), and
native desktop recipes that drive the production `gestures.group`/`same_type`
player (`local/reports/input/group-native-01`..`04`). Scope: 1P NTSC-U mission
map, Terran Command Center plus four SCVs from the user's save. `ui_context` is
the read-only selection stamp hashed from `0x800C6C14`, `0x800C6C18`,
`0x800FD0B0` and the 18-entry list at `0x800C6C20`.

| Fact | Evidence | Level |
|---|---|---|
| Z, R and L modifier flags are copied from the held state each poll into `0x800CD1D4`, `0x800CD1D8`, `0x800CD1DA` | Sampler/dispatch source | VERIFIED-SOURCE |
| Assign group 1-4: hold R, press and release C-Up/C-Left/C-Right/C-Down while R is still held; ignored while the build menu flag `0x800C6A64` is set | `0x80039648`–`0x80039688`, manual p.7; native run 03 shows the original "Group U Set" message after R 4 / R+C-Up 8 / R 6 fields with selection unchanged | VERIFIED-SOURCE + NATIVE (C-Up only) |
| Recall group 1-4: hold Z, press and release C inside the Z hold | `0x800395C8`–`0x8003960C`, manual p.7; native run 03: after Next type selected four SCVs (stamp 1239175821), Z 4 / Z+C-Up 8 / Z 6 restored the Command Center with the exact pre-assign stamp 243401338 and "Group U Selected" | VERIFIED-SOURCE + NATIVE (C-Up only) |
| Next unit type on screen: lone R tap; the cycle runs on R release when the arm byte `0x800AFDB8` is set, and every R release sets it again. In the build menu R release cycles build pages instead | `0x800397F8`–`0x800399BC`; native run 03 (CC -> four SCVs) and run 02 (CC -> four SCVs) | VERIFIED-SOURCE + NATIVE |
| Select all of one type on screen (max 18): cursor on the unit, hold R, press A | `0x8008AEF0`, manual pp.4 and 7. Native run 03 emitted the steered sequence at the Command Center (already selected, so not discriminating) | VERIFIED-SOURCE; native type filtering among mixed types **not observed** |
| **HYPOTHESIS:** R+A (and R+C) clear the arm byte so the next R release only re-arms; R+A on empty terrain leaves it set | Inferred from the arm-byte writers; run 02 cycled on the first R tap after an R+A at empty terrain. Experiment: native R+A on a unit followed by one R tap, compare stamps | HYPOTHESIS |
| A chord that releases R/Z and C together generates no group code (for R the release then triggers a type cycle); C pressed before the modifier is an ordinary C hint | Dispatch decodes C on release inside the modifier | VERIFIED-SOURCE; not exercised natively |

Run 01/04 limits: the desktop window received the user's physical mouse
movement during run 04 (and partly 02), which steered the cursor and camera
before the scripted steps; those runs are not evidence for selection results.
2P (512x384) group/cursor behaviour is not researched; the port gates it (ADR-065).

## Menu input acceptance and pulse timing (2026-10-11)

Method: source reading of generated `sc64_menu_*`/`sc64_resident_*` functions
(MIPS comments in the private generated C) plus native desktop recipes with
raw D-pad trains of fixed hold/neutral lengths, counted from the debug
`SC64 ui_sel` selection trace (`local/reports/input/menu-speed-*`, summary
`menu-speed-summary-01/acceptance.json` and `navigation.json`). Scope: NTSC-U
1P, macOS arm64 desktop, recipe input bypassing OS events; fields are host
`info.fields` (guest VI count observed per presentation frame), not guest poll times.

| Fact | Evidence | Level |
|---|---|---|
| Sampler `0x80037E60` keeps 16-byte pad records at `0x800AFE24`: +0/+4 stick X/Y floats, +8 held, +0xA pressed (`held & ~previous`), +0xC released, +0xE previous | Sampler stores at `0x80037EEC..0x80037F30` | VERIFIED-SOURCE |
| Frontend vertical helper `0x80122B00` and horizontal helper `0x80122BF4` (via `0x80122DC4`) are level-triggered on held D-pad/stick with a 250 ms (`0xFA`) repeat gate; timestamps `0x800D1244` / `0x800D1248+4*port`; a poll with nothing held clears the timestamp, so the next press is accepted at once | Helper code; callers `0x80123890`, `0x8012404C`, `0x80125E7C`, `0x80127DB4`, `0x80128C7C` | VERIFIED-SOURCE |
| Main menu A/Start test reads held state (`0x801297AC`, mask `0x9000`), not the pressed edge | Main menu loop | VERIFIED-SOURCE |
| In-mission dispatcher `0x80038D1C` turns D-pad *pressed* edges into keys immediately; stick keys use a 200 ms (`0xC8`) gate at `0x800AFE14` | `0x80038F14..0x80039060` | VERIFIED-SOURCE; pause/results/advisor share it |
| Build grid stick steps once per deflection: per-player arm bytes `0x800AFE04`/`0x800AFE08` re-arm only when the stick returns inside the threshold; row/column clamp to 0..2 | `0x80038144..0x8003831C` | VERIFIED-SOURCE; threshold constants at `0x80001B74..0x80001B80` not decoded |
| Frontend UI snapshots arrive every 2-3 fields and lag the host field by 2-4 | `ui_field` sequence in `menu-speed-main-01` pad trace | VERIFIED-NATIVE; that the pad is polled at the same cadence is a **HYPOTHESIS** consistent with the table below |
| Main menu list loses 0/136 presses at hold>=3 and neutral>=3 fields (3/3, 3/4, 4/3, 4/4, 5/5; 3/2 also 16/16); hold 1-2 or neutral 1 loses presses (e.g. 2/2: 23/24, 2/4: 6/8, 4/1: 4/8, 1/1: 3/16) | `menu-speed-main-01`, `-02` | VERIFIED-NATIVE-BOUNDED |
| Single Player rows (tab 1, ten rows): 4/4 16/16, 3/4, 4/3, 5/5 complete; 3/3 15/16 and 2/3 7/8 lose presses | `menu-speed-single-02` | VERIFIED-NATIVE-BOUNDED |
| Pause modal (edge dispatcher): 40/40 presses at 2/2..5/5 | `menu-speed-pause-01` | VERIFIED-NATIVE-BOUNDED |
| Single Player tabs show an accepted press 18-21 fields later and ignore further presses for ~55 fields: presses 60+ fields apart all count, 50 or fewer lose every second press | `menu-speed-single-02` (4-field presses spaced 100..20 fields) | VERIFIED-NATIVE-BOUNDED; the lock is presumably the tab slide animation (**HYPOTHESIS**, code not traced) |
| Campaign side steps acknowledge through `ready` after 29-32 fields with a single 4-field press | `menu-speed-campaign-02`, 6/6 | VERIFIED-NATIVE-BOUNDED |

Discriminating experiment for the poll-cadence hypothesis: hook `0x80037E60`
read-only and log its guest field per call in the main menu. Results/Defeat
tabs and Victory were not rerun with the new timing; they use the pause
dispatcher path (source evidence only). Grid evidence is one diagonal route.

## Boot sequence, CPU framebuffers and opening VI scale (2026-10-11)

Method: `tools/research/boot_timeline.js` runs the pinned ares/angrylion
runner from power-on with a Rumble Pak, logging every field's scanout size,
SHA-256 and mean RGB, saving PNGs and optional canonical RAM snapshots.
Private output: `local/captures/boot-timeline/` (`cold-noinput-01`, the
`press-*`, `hold-*` and `control-Start-1300` runs, `snap-01`). A screen is
"visible" while mean RGB exceeds 1.0; fields are runner steps from power-on.

```sh
SC64_RESEARCH_FIRMWARE=$PWD/local/toolchains/ares-build/test-runner \
  local/toolchains/ares-build/test-runner/ares-test tools/research/boot_timeline.js \
  "$PWD/StarCraft 64 (USA).z64" out=$PWD/local/captures/boot-timeline/NEW \
  'actions=[{"pak":"Rumble Pak","fields":3000}]' png_every=15 --timeout 900
```

| Fact | Method / evidence | Status |
|---|---|---|
| Boot order: Expansion/Rumble Pak notice, Nintendo 64, Blizzard, Mass Media, title, opening animation | `cold-noinput-01/field-*.png` | VERIFIED-ORIGINAL, cold boot with Rumble Pak |
| Visible fields: 219, 130, 130, 128, 295; animation starts at step 1123 and is still running at 2996 without input | `cold-noinput-01/timeline.jsonl` | VERIFIED-ORIGINAL, emulator field timing |
| None of the five pre-animation screens ends early on A, B or Start (6-field presses at 120/360/500/680/900; 60-field Start at 150, A and B at 850) | Ten `press-*` and three `hold-*` timelines equal the no-input timeline field for field | VERIFIED-NEGATIVE for those buttons/times; a skip needs a guest change |
| Start during the opening animation reaches the main menu | `control-Start-1300`: animation ends at 1308, menu visible from 1346 | VERIFIED-ORIGINAL |
| The holds are CPU-written framebuffers: no SP task runs, and the two 640x480 RGBA16 buffers at `0x1767C0`/`0x20CCC0` contain the image | `reference-cold` and `snap-01` `sp_start` buckets; `snap-01/{pak,n64,blizzard,title}.cpu-visible.bin` decoded at both origins; PI DMA of 2 KiB chunks into `0x168D20`/`0x169520` right after each fade | VERIFIED-ORIGINAL; decoder identity not researched |
| Each screen ends with about 30 RDP tasks of fade-out into the same buffers | `sp_start` events and native traces | VERIFIED-ORIGINAL/NATIVE |
| The opening animation's 320x240 target is scanned with X scale 512 and Y scale 716: the VI raster covers 320x168 rows, a full-width letterbox about 61% of the screen high | `intro-decode/step-1.png` band at 640x240 lines 48..193; port target rows 35..133 | VERIFIED-ORIGINAL scanout geometry |

Port comparison (desktop, `local/reports/boot-issues/`): before ADR-068 each
boot screen was visible only during its fade (11-15 fields:
`port-before-trace-0001`, `-0601`) and the animation used rows 35..133 of a
240-row 4:3 image. After it (`port-final2-trace-0001`, `-0450`, `-0601`) the
screens are visible for 224, 136, 134, 135 and 305 fields, within 3-5% of the
original; the native VI clock and the original guest timers are unchanged.
CPU frames for the Nintendo, Blizzard and title holds are byte-exact against
the original RDRAM framebuffers. Field numbers differ by a constant boot
latency of about 50-80 fields, which is not a speed difference: intervals between
consecutive fade ends differ by 1-3%.

## Main-menu Encyclopedia controls (2026-10-11)

| Finding / scope | Verification method | Status |
|---|---|---|
| Main-menu item 3 ("Encyclopedia") opens frontend menu 8: the UI feedback reports `ui_mode` "menu", token 264 (`0x100+8`), 640x480 target, and no actionable items | Native run `local/reports/input/enc-07` (ui_click on the item at field 3000; `engine.log` tokens 256 -> 264 -> 256); article frames `frames/task-*.rgba` | VERIFIED-NATIVE |
| The first article is SCV; D-pad Right shows the next article (SCV -> Marine -> Firebat), D-pad Left the previous one (back to Marine). 12-field pulses advance exactly one article | Same run: pad pulses at fields 3700/3755/3810 with the article text and portrait changing by 3740/3795/3850 | VERIFIED-NATIVE for the Terran start of the list; list order, wrap-around and category changes are not measured |
| D-pad Up/Down move the highlight between the article's links (Marine: Made by Barracks, Needs Command Center, Needs Barracks) and wrap in both directions; A opens the highlighted article; L, R, C-Right and C-Down do nothing. **Correction:** the first version of this table recorded Up/Down as inert because the one-pixel highlight change was missed in a contact sheet; the port frames at fields 3830/3860 and the emulator snapshots below show it | Port run `enc-10`: Right (Marine), Down (Needs: Command Center, f3830), A (Command Center article with Made by SCV, Makes SCV, f3910), Down (Makes SCV, f3990), A (SCV article, f4069); emulator `encyclopedia-01` (`marine`, `marine-down-1..3`, `marine-up-3`): highlight 0, 1, 2, 0, 2 | VERIFIED-NATIVE + EMULATOR |
| B leaves to the main menu (token 256 again), also after following links (Marine -> Command Center -> SCV -> B shows the main menu, not the previous article); Start (Enter) leaves as well | Port run `enc-10` (B at 4100, token 256 at 4230); emulator `encyclopedia-02` `marine-start` | VERIFIED-NATIVE + EMULATOR |

| Article records form a doubly linked ring of 31 Terran entries (SCV ... Nuclear Silo) of 0x1b0 bytes: `+0x14` next, `+0x18` prev, `+0x1c` made-by, `+0x20..+0x3c` makes (8 slots), `+0x5c..+0x78` needs (8 slots), name at `+0x84`. The highlighted link counts over made-by, makes and needs in that order (Barracks: SCV 0; Marine, Firebat, Medic, Ghost 1-4; Command Center 5), wraps in both directions, and resets to 0 on every article change | Emulator snapshots `encyclopedia-01` (ring walk from `scv.cpu-visible.bin`, pointer slots of every record) and `encyclopedia-02` (`barracks`, `barracks-down-1..4`: highlight moves Marine, Firebat, Medic, Ghost) | VERIFIED-EMULATOR; Zerg/Protoss lists are not in the ROM menu seen here |
| `0x800DDB80` holds the shown record and `0x800DDB84` the highlighted link's target in both emulator and port. The highlighted *index* lives only in the menu thread's stack frame, whose address differs (emulator `0x800DDACC`/`0x800DDEBC`, port elsewhere), and a target can occur twice (Marine: Barracks as made-by and as needs), so neither is usable for input | Snapshot diffs over six selection states; port RDRAM dump during the screen | VERIFIED-SNAPSHOT; design consequence in ADR-073 |
| The article is drawn by `sc64_menu_801232C8`: label draws at `0x80123634/0x801236E4/0x801237DC` and link draws at `0x80123688` (made-by), `0x8012376C` (makes), `0x80123830` (needs), all `jal 0x8003F070` with a0..a3 = (170, row y, 333, y+24), loop index in `s4` (already advanced at the made-by and needs sites), article record in `s6`, and the highlight colour pointer `0x800A5A60` set to `0x800A5A30` for the highlighted link. Rows are 16 pixels apart | Generated C `local/generated/game/funcs_55.c` lines for `0x80123600..0x80123840`; port hook trace (`r4..r7`, `r20`, `r22` per call) | VERIFIED-SOURCE + NATIVE |
| The text is drawn only when the article or the highlight changes (four link draws for the SCV and Marine articles in a 75 s run), not every frame, and blitted at (0,38); the port presents task frames every ~8-9 fields meanwhile | Port telemetry with temporary call counters | VERIFIED-NATIVE |
| Start leaves the Encyclopedia like B, also with a link highlighted | Emulator `encyclopedia-02` `barracks-start` (main menu with Encyclopedia highlighted) | VERIFIED-EMULATOR |

The original manual scan (regvault) has no text about the Encyclopedia, so these
controls are measured, not documented. The input adapter maps buttons, swipes and
link taps to these controller edges only (ADR-072, ADR-073).
