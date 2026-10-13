# Stage 0 libultra inventory

This directory contains authored research tooling, not game code. Generated
reports, source references, object files, and all dumps stay in ignored `local/`
or `third_party/`. ROM bytes are read locally and are never written into reports.

## Revisions and toolchain

- `shygoo/n64sym`: `ccf4600f3389f1a84bde23339225cf372fdf7712`.
- `N64Recomp/N64Recomp`: `ffb39cdad1da5de07eaaa48bd1db4a89a7986771`.
- `N64Recomp/N64ModernRuntime`: `cdf5abbd5026fef5c364c676e4667c45e42b6863`.
- `decompals/ultralib`: `e24c836796df4bf520ff8b11a5c9d2cea3a66cbd`.
- Debian container image: `sha256:7c7b2c966bc9ee8cedfeef67e0e279108992c77681fa595db4a9d65c06ccc587`.
- GCC 2.7.2 macOS release asset SHA-256:
  `fa25cd694fc7e5e986210afd7dfa4e48f23e53a210e183e9b6851cc9e230c1f9`.
- Binutils 2.6 macOS release asset SHA-256:
  `9e7ab09bb1663da824d39025acec645061032f6223d39ed022f8b669a9895ba7`.
- IDO 5.3 Linux ARM v1.2 release asset SHA-256:
  `b6844c986fa65d7de4ce10b27583769a5ec7a88ae37a151f6dc66c929d3e1c92`.

`n64sym` builds unmodified on Linux aarch64 with `g++` and `make`.
IDO reference builds use the Linux ARM tool. GCC reference objects use the
macOS x86_64 tool through the host's existing Rosetta support. Reference builds
use `COMPARE=0`: no original Nintendo SDK archive was obtained or compared.
GCC builds omit the unrelated `monutil` debug-metadata object because its
separate IDO command would otherwise try to execute the Linux ARM compiler on
macOS. All libultra OS and I/O source objects are built. This omission does not
affect the examined initialization, exception, controller, VI or PI objects.

Example reference build from `third_party/ultralib`:

```sh
make VERSION=J TARGET=libgultra_rom COMPARE=0 setup
make VERSION=J TARGET=libgultra_rom COMPARE=0 MDEBUG_FILES= \
  AR="$PWD/tools/gcc/ar" -j8
```

Repeat for `I`, `K`, and `L`. For IDO references, run in the Linux container:

```sh
make VERSION=J TARGET=libultra_rom COMPARE=0 setup
make VERSION=J TARGET=libultra_rom COMPARE=0 -j8
```

## Automatic generation (resident section)

`make inventory ROM=...` (`python tools/sc64.py inventory --rom ...`, also run by
`make recompile` when the file is missing) executes `ensure_inventory.py`, which
chains everything the resident-section command below needs and publishes
`local/reports/libultra-resident/` only after the result matches the accepted Stage 0
profile in `accepted.py` (346 reference matches, 121 masked-CRC hits, all equal to the
loaded RAM, 51 replacement names):

1. `n64sym` scans the ROM into `local/reports/libultra/n64sym-rom.splat.txt`. A missing
   binary is built in place (`make`, or the local `n64sym.Dockerfile` image); an existing
   binary that the host cannot execute (the shipped one is a static aarch64 Linux ELF) runs
   in Docker on its own platform. Existing output is reused.
2. `tools/research/run_capture.py reference-cold` records the cold-boot RDRAM
   (`local/captures/research/reference-cold/end.rdram.bin`, about two minutes, reused when
   present; `--dump` supplies another). The capture is deterministic: two runs gave the same
   dump and the same inventory.
3. `inventory.py` runs with the resident arguments below.

The research emulator (`ares-test`, `docs/reference-capture.md` sections 1-2) and the
reference objects in `third_party/ultralib/build` for versions I/J/K/L (see above) are
prepared inputs; if one is missing the command stops and names it. Nothing existing is
overwritten: an invalid inventory or a partial capture directory is reported and left for
you to move aside. The pinned `symbol_lists.cpp` is read from the N64Recomp commit, not the
working tree, so the Stage 1 recompiler patch does not change the replacement set. The menu
and game sections stay manual.

## Reproduction

Run the pinned `n64sym` binary against a normalized local image:

```sh
n64sym "StarCraft 64 (USA).z64" -s -f splat \
  -o local/reports/libultra/n64sym-rom.splat.txt
```

Python requires `pyelftools` for object comparisons. It was already available
in this workspace environment. The script independently checks each signature's
first-eight-byte and full-body CRC after masking only documented `R_MIPS_26`,
`R_MIPS_HI16` and `R_MIPS_LO16` fields. It then searches the reference objects'
whole `.text`, requires every unrelocated byte to match, and verifies relocation
targets against calls from uniquely identified reference objects. Padding-only
objects and tiny non-identifying stubs are excluded from reference comparisons.

```sh
python3 tools/research_libultra/inventory.py "StarCraft 64 (USA).z64" \
  --n64sym third_party/n64sym \
  --recompiler third_party/N64Recomp \
  --runtime third_party/N64ModernRuntime \
  --reference-root third_party/ultralib/build \
  --candidates local/reports/libultra/n64sym-rom.splat.txt \
  --candidate-displacement 0x7FFFF400 \
  --scan-start 0x1000 --scan-end 0xD6640 --displacement 0x7FFFF400 \
  --dump local/captures/research/reference-cold/end.rdram.bin \
  --output local/reports/libultra-resident
```

Add `--dump local/path/to/canonical-big-endian-rdram.bin` after a runtime capture.
Every reference object then gets its own `ram_bytes_equal` check; a false value
must be investigated rather than treated as a confirmed ROM/RAM mapping.

The resident displacement is `0x7FFFF400`; the corrected resident scan is
`[0x1000,0xD6640)`, ending before the menu overlay. Use the same original raw
candidate file for each section with `--candidate-displacement 0x7FFFF400`:

| Section | Scan start/end | Displacement | Loaded RAM dump |
|---|---|---|---|
| Resident | `0x1000 / 0xD6640` | `0x7FFFF400` | `reference-cold/end.rdram.bin` |
| Menu | `0xD6640 / 0xE38E0` | `0x800495B0` | `reference-cold/end.rdram.bin` |
| Game | `0xE38E0 / 0x12CA10` | `0x8003C310` | `game-2p-01/end.rdram.bin` |

Give each scan a separate output directory. The output contains metadata only;
reference-byte equality does not imply execution of every matched function.
The complete overlay intervals also match ROM byte for byte, independently of
this library scan. Never apply the resident mapping to the later overlays.

## Findings and evidence limits

Normalized ROM SHA-1: `a36d91b5aac7bb95df0d7de5f9814f5270e25c2b`.

The following complete reference objects match the supplied ROM after masking
only relocation fields and enforcing unique incoming/outgoing target constraints:

| Object | ROM start | Guest start under resident mapping | Bytes | Relocations | Matching family among I/J/K/L |
|---|---:|---:|---:|---:|---|
| `initialize.o` | `0x00091BE0` | `0x80090FE0` | 736 | 56 | GCC `libgultra_rom` 2.0J only |
| `exceptasm.o` | `0x0009A960` | `0x80099D60` | 2416 | 76 | GCC `libgultra_rom` 2.0J only |

The masked SHA-256 values are respectively
`a22b5b2968c007b7934a8e6d419be79f731e11607c5a79f04203749a5c8d43d8`
and `52b4d7a0ea592fe8bb40041d332767736450dc90923956a3bdb50247dcc63f7c`.
These independent discriminants strongly support the GCC OS 2.0J family.
The exact historical SDK archive supplied to the game's linker is an inference;
other functions share identical bodies across versions, and customized SDKs
cannot be excluded. The built-in signature set alone has no version provenance.

The accepted cold original-emulator capture
`local/captures/research/reference-cold/end.rdram.bin` is canonical big-endian 8 MiB
RDRAM. Its SHA-256 is stored in the regenerated inventory and acceptance manifest.
All 346 accepted versioned object matches, representing 90 distinct code-body
ranges, are byte-identical between ROM and captured RAM under the displacement
above. This includes every raw relocation word and both OS 2.0J discriminants.
Thus these particular addresses are now verified in loaded RAM. Matching
captured bytes does not establish that the CPU executed each identified
function. The root capture also observed IPL3 DMA copying ROM `0x1000` to
physical RDRAM `0x400`, consistent with the mapping.

The final inventory has 121 resident full masked-CRC body matches and 51 distinct
exact-name body candidates in N64Recomp's reimplemented set. Those 51 names
have wrapper definitions in N64ModernRuntime; the semantics caveats below still
apply. `reimplemented-body-candidates.tsv` excludes relocation-only candidates.
`osEPiRawStartDma` remains a relocation-derived alias of the reference-validated
`__osEPiRawStartDma` at `0x8009AC70`; a rename is a Stage 1 policy decision,
and its runtime wrapper would quick-exit if called. The extra relocation-only
`__osSpSetPc` candidate at `0x8007DF24` is unaccepted; the reference-validated
OS routine is at `0x8009EBD0`.
All 121 resident CRC hits agree with captured RAM. The separate menu scan has
one generic 32-byte CRC candidate `n_alResampleParam`, ROM `0xD7238`, actual
menu-overlay guest address `0x801207E8`. It lacks full-reference validation and
must not be adopted as an audio-library function. Its old resident mapping
`0x800D6638` was wrong and lies in BSS. The separate game scan has zero accepted
reference bodies. A signature hit in ROM is insufficient for symbol promotion.

Important scanner issue: pinned `n64sym` forms relocated J/JAL targets using
its header displacement's high nibble. For this ROM the displacement is
`0x7FFFF400`, so many relocation labels incorrectly appear at `0x700...`.
The authored script resolves targets using the calling instruction's guest PC,
as required by MIPS. It also retains relocation-only hints separately from body
matches. Several PI/SI routines have identical masked bodies, so raw scanner
names must not be copied into a final symbol file without graph validation.

`inventory.json` contains full matching evidence, source revisions, rejected
alias candidates, exact-name intersections, wrapper source paths and omissions.
`inventory.tsv` is a compact index. `REFERENCE-BYTES`, `MASKED-CRC` and
`RELOCATION-CANDIDATE` are distinct evidence classes. A static match proves
neither execution nor complete runtime support.

## Runtime coverage constraints

- `osEPiWriteIo` at guest `0x800A05D0`, ROM `0x000A11D0`, has reference-byte
  validation. It is only in N64Recomp's ignored list, not its reimplemented list,
  and has no `osEPiWriteIo_recomp` wrapper in the pinned runtime.
- `osEPiReadIo` at guest `0x800A0630`, ROM `0x000A1230`, has reference-byte
  validation. Its runtime wrapper handles ROM PIO and asserts for save-memory
  PIO. It cannot implement a custom Flash command/status protocol unmodified.
- No complete built-in or versioned reference `osFlash*` body matched within
  the resident search interval. This is not a proof of no FlashRAM: custom save
  routines may instead use lower-level PI commands. Trace those routines before
  choosing the interception boundary.
- The `osPiRawStartDma_recomp` and `osEPiRawStartDma_recomp` wrappers quick-exit;
  `__osSpSetPc_recomp` asserts. Their names appearing in a wrapper inventory do
  not make them functional hardware emulation.
- Controller Pak wrapper functions return `PFS_ERR_NOPACK`; this is an absent
  Pak policy, not implemented Pak storage. Linked low-level Pak routines also
  serve Rumble and are not proof that the game uses Controller Pak saves.

Stage 1 must decide and record interception/rename policy for each accepted
function. No recompiled game code or runtime patch is implemented here.
