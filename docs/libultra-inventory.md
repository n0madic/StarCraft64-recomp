# libultra identification and replacement inventory

ROM SHA-1: `a36d91b5aac7bb95df0d7de5f9814f5270e25c2b`.

## Method and version evidence

`make inventory` (also run by `make recompile` when the file is missing) regenerates the resident
inventory and checks it against the counts below; the menu and game sections and the manual steps
are in [the inventory tool README](../tools/research_libultra/README.md). Pinned sources and privately built I/J/K/L reference objects are recorded there. The inventory compares complete masked instruction bodies, resolves incoming/outgoing relocations, and independently compares the loaded canonical RDRAM with ROM. CRC-only matches remain candidates. Relocation-only symbols are excluded from the replacement body table below.

GCC OS **libultra 2.0J family** is strongly identified by two independent, J-only complete reference-object matches:

| Reference | ROM | Guest address | Text bytes | Relocations | Masked SHA-256 |
|---|---:|---:|---:|---:|---|
| initialize.o | `0x91BE0` | `0x80090FE0` | 736 | 56 | `a22b5b2968c007b7934a8e6d419be79f731e11607c5a79f04203749a5c8d43d8` |
| exceptasm.o | `0x9A960` | `0x80099D60` | 2416 | 76 | `52b4d7a0ea592fe8bb40041d332767736450dc90923956a3bdb50247dcc63f7c` |

The exact historical SDK archive is an inference, not recovered build provenance. The accepted reference set contains 346 versioned matches / 90 distinct body ranges; all agree with captured RAM, including relocation words. All 121 resident masked-CRC hits agree with loaded RAM. The separate menu-overlay scan reports a generic 32-byte `n_alResampleParam` candidate at ROM `0xD7238` / actual `0x801207E8`. It has no accepted full-reference match, so do not adopt the name. The former presumed resident address `0x800D6638` was inside BSS and incorrect. The game-overlay scan yields no accepted reference body.

## Exact-name replacement body candidates

These 51 names occur in the pinned N64Recomp replacement set and have definitions in the pinned runtime. This is a reviewed **interception inventory**, not proof that all wrappers implement the hardware behavior needed by this game. `REFERENCE-BYTES` means a full relocation-aware reference object supports the name; `CRC-BODY` is a full masked-CRC candidate pending Stage 1 symbol-boundary review. Sizes below are named function-symbol sizes when available; an object text span is not silently called a function size.

| Name | Guest address | ROM | Function bytes | Evidence | Default ignored | Runtime source |
|---|---:|---:|---:|---|---|---|
| `osWritebackDCacheAll` | `0x80004F00` | `0x005B00` | 40 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osInvalDCache` | `0x80004F30` | `0x005B30` | unresolved | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osInvalICache` | `0x80004FE0` | `0x005BE0` | unresolved | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osStartThread` | `0x80090690` | `0x091290` | 280 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osAiSetFrequency` | `0x800907B0` | `0x0913B0` | 280 | REFERENCE-BYTES | yes | `ai.cpp` |
| `osCreateThread` | `0x800908D0` | `0x0914D0` | 208 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osSetThreadPri` | `0x800909A0` | `0x0915A0` | 200 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osCreatePiManager` | `0x80090A70` | `0x091670` | 400 | REFERENCE-BYTES | yes | `pi.cpp` |
| `osCreateViManager` | `0x80090C00` | `0x091800` | 416 | REFERENCE-BYTES | yes | `vi.cpp` |
| `osAiSetNextBuffer` | `0x80090F40` | `0x091B40` | 148 | REFERENCE-BYTES | yes | `ai.cpp` |
| `osInitialize` | `0x80090FE0` | `0x091BE0` | 552 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osViSetYScale` | `0x800974E0` | `0x0980E0` | 68 | REFERENCE-BYTES | yes | `vi.cpp` |
| `osViBlack` | `0x80097CE0` | `0x0988E0` | 92 | REFERENCE-BYTES | yes | `vi.cpp` |
| `osPiStartDma` | `0x80097D40` | `0x098940` | 168 | REFERENCE-BYTES | yes | `pi.cpp` |
| `osRecvMesg` | `0x80097DF0` | `0x0989F0` | 292 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osViGetCurrentFramebuffer` | `0x800990C0` | `0x099CC0` | 56 | REFERENCE-BYTES | yes | `vi.cpp` |
| `osGetTime` | `0x80099700` | `0x09A300` | 132 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osViSwapBuffer` | `0x80099790` | `0x09A390` | 68 | REFERENCE-BYTES | yes | `vi.cpp` |
| `osSetEventMesg` | `0x800997E0` | `0x09A3E0` | 172 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osSpTaskLoad` | `0x80099890` | `0x09A490` | 524 | REFERENCE-BYTES | yes | `sp.cpp` |
| `osSpTaskStartGo` | `0x80099A9C` | `0x09A69C` | 44 | REFERENCE-BYTES | yes | `sp.cpp` |
| `osViGetNextFramebuffer` | `0x80099AD0` | `0x09A6D0` | 56 | REFERENCE-BYTES | yes | `vi.cpp` |
| `osCreateMesgQueue` | `0x80099B10` | `0x09A710` | 36 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `__osDisableInt` | `0x80099C90` | `0x09A890` | 112 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `__osRestoreInt` | `0x80099D00` | `0x09A900` | 28 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osGetThreadPri` | `0x8009A7C0` | `0x09B3C0` | 24 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osSendMesg` | `0x8009B590` | `0x09C190` | 304 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osGetCount` | `0x8009B6C0` | `0x09C2C0` | 12 | CRC-BODY | yes | `ultra_translation.cpp` |
| `osVirtualToPhysical` | `0x8009B6F0` | `0x09C2F0` | 84 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `__osSetFpcCsr` | `0x8009B770` | `0x09C370` | 16 | CRC-BODY | yes | `ultra_translation.cpp` |
| `osWritebackDCache` | `0x8009B820` | `0x09C420` | unresolved | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osGetMemSize` | `0x8009BA00` | `0x09C600` | 160 | REFERENCE-BYTES | yes | `recomp.cpp` |
| `osViSetMode` | `0x8009C4D0` | `0x09D0D0` | 76 | REFERENCE-BYTES | yes | `vi.cpp` |
| `osViSetSpecialFeatures` | `0x8009C520` | `0x09D120` | 356 | REFERENCE-BYTES | yes | `vi.cpp` |
| `osSetIntMask` | `0x8009C8A0` | `0x09D4A0` | 160 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osEPiStartDma` | `0x8009CB10` | `0x09D710` | 148 | REFERENCE-BYTES | yes | `pi.cpp` |
| `osCartRomInit` | `0x8009CBB0` | `0x09D7B0` | 376 | REFERENCE-BYTES | yes | `pi.cpp` |
| `osViSetEvent` | `0x8009CD30` | `0x09D930` | 88 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osStopThread` | `0x8009CDB0` | `0x09D9B0` | 180 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `__osMotorAccess` | `0x8009CE70` | `0x09DA70` | 324 | REFERENCE-BYTES | yes | `cont.cpp` |
| `osMotorInit` | `0x8009CFB4` | `0x09DBB4` | 584 | REFERENCE-BYTES | yes | `cont.cpp` |
| `osContInit` | `0x8009D200` | `0x09DE00` | 412 | REFERENCE-BYTES | yes | `cont.cpp` |
| `osContStartReadData` | `0x8009D510` | `0x09E110` | 136 | REFERENCE-BYTES | yes | `cont.cpp` |
| `osContGetReadData` | `0x8009D598` | `0x09E198` | 156 | REFERENCE-BYTES | yes | `cont.cpp` |
| `osContStartQuery` | `0x8009D6F0` | `0x09E2F0` | 128 | REFERENCE-BYTES | yes | `cont.cpp` |
| `osContGetQuery` | `0x8009D770` | `0x09E370` | 32 | REFERENCE-BYTES | yes | `cont.cpp` |
| `osJamMesg` | `0x8009E760` | `0x09F360` | 308 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `__osSpSetPc` | `0x8009EBD0` | `0x09F7D0` | 48 | REFERENCE-BYTES | yes | `sp.cpp` |
| `osDestroyThread` | `0x8009ECB0` | `0x09F8B0` | 216 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osSetTimer` | `0x800A04E0` | `0x0A10E0` | 148 | REFERENCE-BYTES | yes | `ultra_translation.cpp` |
| `osEPiReadIo` | `0x800A0630` | `0x0A1230` | 88 | REFERENCE-BYTES | yes | `pi.cpp` |

## Mandatory runtime changes before native boot

| Function / family | Evidence and consequence |
|---|---|
| `osEPiWriteIo`, `0x800A05D0`, ROM `0xA11D0` | Reference-validated, ignored by default in N64Recomp, no runtime wrapper. Supply an actual Flash-capable replacement; an empty stub would discard save commands. |
| `osEPiReadIo`, `0x800A0630`, ROM `0xA1230` | Reference-validated; current wrapper supports ROM PIO and asserts on save PIO. Extend it for observed Flash status/ID reads. |
| `osPiRawStartDma` / `osEPiRawStartDma` | Upstream quick-exit paths. Resolve aliases through reference relocations and audit executed callers before replacement. |
| `__osSpSetPc`, `0x8009EBD0` | Reference-validated; runtime asserts. Graphics-task startup must get a functioning replacement. Reject the unrelated relocation-only candidate at `0x8007DF24`. |
| `osPfs*` | Runtime returns `PFS_ERR_NOPACK`; this is not Controller Pak persistence. Current observed saves use cartridge FlashRAM. |

No complete resident `osFlash*` body was accepted. The game demonstrably issues the Flash protocol through lower-level PI calls. Identifying standard `osFlash*` names would not, by itself, cover this save path.

Raw n64sym output is not a final symbol map: its header-derived displacement gave a wrong high address nibble; masked aliases confused PI/SI and SI/SP raw I/O helpers. The inventory resolves these using reference relocations. Preserve its rejected candidates and evidence classes. Stage 1 must also bound every game function, jump table and overlay; that work has not been done here.
