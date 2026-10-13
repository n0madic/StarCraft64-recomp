# Stage 1 symbol and recompilation tools

These tools use the verified NTSC-U ROM profile and the pinned source APIs.
Run them from the repository root. All outputs are private under `local/`.
The original ROM is read only and remains unchanged.

One command runs the whole chain below (patch and build N64Recomp, `prepare.py`,
`tools/input/add_ui_hooks.py`, `run_recompiler.py --replace`, observer validation)
and leaves `local/generated/game` ready for `make build`:

```sh
make venv                                # once: .venv with the pinned spimdisasm/rabbitizer
make recompile ROM="/path/to/your.z64"   # = python tools/sc64.py recompile --rom ...
```

Both commands run unchanged on macOS, Linux and Windows (ADR-082); `RECOMP_PYTHON`
or `--recomp-python` selects another interpreter than `.venv`. The recompiler is
built with LLVM when it is found (Homebrew or `LLVM_BIN`), otherwise with `CC`/`CXX`
or the system compiler. It needs the project `.venv` (spimdisasm, pyelftools). The libultra inventory
`local/reports/libultra-resident/inventory.json` is generated first when it is
missing (`make inventory` / `sc64.py inventory` runs only that step; see
[the inventory tool README](../research_libultra/README.md) and
[libultra-inventory.md](../../docs/libultra-inventory.md)); an existing one is only
verified, never rebuilt. Each run
analyzes into a fresh `local/recompile/symbols-<timestamp>/`; an existing
generated game is replaced transactionally and its previous output stays in the
scratch directory. The result was verified byte-identical to the previous
generation on the NTSC-U ROM. The manual steps follow.

## Recipe (ADR-084)

The analysis result that depends only on the NTSC-U ROM is committed as `recipe/ntsc-u/`
(names, addresses, sizes, hook text and hashes; no game bytes). It lets N64Recomp run without
Python, spimdisasm or the inventory:

```sh
make recipe-verify ROM="/path/to/your.z64"   # recipe.py check + generate, compared with generated.json
make recipe-export ANALYSIS=local/recompile/symbols-<stamp>   # maintainers, after `make recompile`
python tools/recompile/recipe.py generate ROM --output local/generated/game --replace
```

The recompiler itself is built by `make build-recompiler` (`build_recompiler.py`, ADR-085) for
`macos-arm64`, `linux-x86_64` or `windows-x86_64` (`RECOMPILER_TARGET=...`; default: this host) into
`local/dist/recompiler/<target>/` with a `receipt.json` (source commit, patch hash, compiler,
binary SHA-256). `make verify-recompiler ROM=...` runs that binary on the recipe and records the
result in the receipt. The Linux binary is fully static and runs in a pinned Debian container
when the host is not Linux x86_64; the macOS one targets macOS 13 and links only system libraries.

`check` accepts only whitelisted line forms (no instruction words or byte data) and `generate`
rejects a generated file whose SHA-256 differs from `generated.json`. Export needs the analysis
directory and the generated game of the same `make recompile` run; run it again whenever the
analysis, hooks or pinned N64Recomp change, then commit the diff.

```sh
git -C third_party/N64Recomp submodule update --init --recursive
python3 tools/recompile/patch_recompiler.py third_party/N64Recomp
cmake -S third_party/N64Recomp -B local/build/recompiler -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_CXX_FLAGS='-include cstdlib'
cmake --build local/build/recompiler --target N64RecompCLI -j 10
.venv/bin/python tools/recompile/prepare.py 'StarCraft 64 (USA).z64' \
  --output local/recompile/symbols
python3 tools/recompile/run_recompiler.py local/recompile/symbols/sc64.toml
python3 tools/recompile/audit_indirect_entries.py 'StarCraft 64 (USA).z64' \
  local/recompile/symbols/manifest.json local/recompile/symbols/indirect-audit.json
python3 -m unittest discover -s tests -p 'test_recompile_symbols.py' -v
```

The compiler paths above describe the verified macOS development machine;
`recompile_all.py` itself discovers LLVM and falls back to the system compiler.
Choose installed C/C++ compilers on another host (the N64Recomp build has been
run on macOS only, D-057). On Windows the recompiler binary is `N64Recomp.exe`. The forced `cstdlib` include
works around the pinned Rabbitizer source omitting declarations of `malloc`
and `free`; it does not modify the library source or generated guest code.

`prepare.py` refuses an existing analysis directory. `run_recompiler.py`
refuses an existing game directory unless `--replace` is passed. A replacement
is published only after successful generation and artifact validation. The
previous output and full generation diagnostics remain in the analysis scratch
directory. The pinned CLI can report a failure while returning exit status
zero, so invoking it without checking diagnostics is insufficient validation.

The successful native host uses `local/generated/game` by default. Emitted
entrypoint is `recomp_entrypoint`, and `recomp_overlays.inl` supplies three
section tables. Menu and game share guest addresses but retain distinct C
names, ROM intervals, and section identities. Their native registration must
follow the actual PI loads.

`splat_to_syms.py` also accepts a private manifest and CSV files separately:

```sh
python3 tools/recompile/splat_to_syms.py local/recompile/symbols/manifest.json \
  local/recompile/symbols/converted-again.syms.toml
```

The `--function-info` CSV format is verified against spimdisasm 1.42.4 from the
pinned splat environment. The converter validates section mapping, alignment,
function sizes, unique C names, and overlap. It rejects `T_*` unknown-instruction
labels as unclassified data. The analysis uses separate section-qualified
contexts, rather than merging overlays into one address map.

The resident load has embedded data and RSP microcode among CPU text. CPU code
intervals in this profile are ROM `[1000,1118)`, `[5370,8F8F0)`, and
`[91290,A2360)`. Menu and game executable intervals are `[D6640,E38E0)` and
`[E38E0,12CA10)`. Function graph recovery includes cross-section direct calls
and two confirmed thread entry points following never-returning loops. Extra
runtime-confirmed indirect entries can be supplied as private JSON via
`--extra-functions`; each needs section, address, and evidence reason.

Only reviewed OS bodies are named from the inventory. Relocation-only labels
and generic audio-library CRC matches are excluded. Additional PI wrapper names
are deliberately added to the pinned recompiler replacement list; they map to
real host implementations, not empty stubs. The source patch also corrects
cross-section switch-table analysis and bounds its ROM reads. Both changes are
idempotent and fail if the revision or unrelated local source changes differ.

Guest `CACHE` maintenance in startup and two other CPU routines is recorded as
eleven individual NOP instruction patches because the native runtime has coherent host RDRAM
(D-021). The idle loop invokes `sc64_idle_poll` at guest `80004994` for real host
event delivery and cooperative scheduling (D-023). The original ROM bytes are
never patched on disk. There are no game-function stubs in the generated config.

Memory wait loops additionally invoke `sc64_guest_poll` at their repeating
heads (D-025). The detector includes branch delay slots when rejecting string
scans, linked-list walks, and loops with local comparison progress. The host
poll delivers queued events and schedules the original guest workers; it must
preserve guest-owned counters. The original game worker increments its counter
at `80111670`, which is different from libultra's relocation-verified
`__osViIntrCount` at `80112100`. The private `poll-hooks.json` records every
adopted hook and its loop-body hash.

The indirect-entry audit reports candidates, including interior branch/table
targets. It does not promote candidates to functions or assert that they run.
Bounded linear propagation finds the original seven direct `osCreateThread`
call sites but does not prove complete interprocedural control flow. Generation
success, native compilation, and a successful game run are separate evidence.
