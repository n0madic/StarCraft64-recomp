# Toolchain and API source audit

Inspected on 2026-10-07. These are source and reproducibility anchors. The ares/Angrylion research
harness and reference libraries were built locally; N64Recomp native
port compatibility has not been tested. Future builds must pin the
complete dependency graph, including submodules, compilers and build services.

| Component | Inspected revision | Authoritative source and relevant files |
|---|---|---|
| N64Recomp | `ffb39cdad1da5de07eaaa48bd1db4a89a7986771` | [config parser](https://github.com/N64Recomp/N64Recomp/blob/ffb39cdad1da5de07eaaa48bd1db4a89a7986771/src/config.cpp), [driver and symbol emission](https://github.com/N64Recomp/N64Recomp/blob/ffb39cdad1da5de07eaaa48bd1db4a89a7986771/src/main.cpp), [exact replacement names](https://github.com/N64Recomp/N64Recomp/blob/ffb39cdad1da5de07eaaa48bd1db4a89a7986771/src/symbol_lists.cpp) |
| N64ModernRuntime | `cdf5abbd5026fef5c364c676e4667c45e42b6863` | [renderer interface](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/ultramodern/include/ultramodern/renderer_context.hpp), [event threads](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/ultramodern/src/events.cpp), [ROM loading](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/librecomp/src/recomp.cpp), [CMake](https://github.com/N64Recomp/N64ModernRuntime/blob/cdf5abbd5026fef5c364c676e4667c45e42b6863/CMakeLists.txt) |
| splat | `1d09139b886f0bece632bd8f367ccc1d3318ca2a` | [create_config](https://github.com/AngheloAlf/splat/blob/1d09139b886f0bece632bd8f367ccc1d3318ca2a/src/splat/scripts/create_config.py), [N64 metadata](https://github.com/AngheloAlf/splat/blob/1d09139b886f0bece632bd8f367ccc1d3318ca2a/src/splat/util/n64/rominfo.py) |
| n64sym | `ccf4600f3389f1a84bde23339225cf372fdf7712` | [README](https://github.com/shygoo/n64sym/blob/ccf4600f3389f1a84bde23339225cf372fdf7712/README.md), [Makefile](https://github.com/shygoo/n64sym/blob/ccf4600f3389f1a84bde23339225cf372fdf7712/Makefile) |
| ultralib | `e24c836796df4bf520ff8b11a5c9d2cea3a66cbd` | [version/build matrix](https://github.com/decompals/ultralib/blob/e24c836796df4bf520ff8b11a5c9d2cea3a66cbd/README.md) |
| Mupen64Plus core | `ba95bab92a76744753bfe61470823a4937850ab0` | [plugin ABI](https://github.com/mupen64plus/mupen64plus-core/blob/ba95bab92a76744753bfe61470823a4937850ab0/src/api/m64p_plugin.h), [ROM normalization](https://github.com/mupen64plus/mupen64plus-core/blob/ba95bab92a76744753bfe61470823a4937850ab0/src/main/rom.c), [save/peripheral database](https://github.com/mupen64plus/mupen64plus-core/blob/ba95bab92a76744753bfe61470823a4937850ab0/data/mupen64plus.ini) |
| Mupen64Plus RSP HLE | `8a7a472a7172eb2c8725b305eae26818ed7b51a2` | [task classification and unknown-task dump implementation](https://github.com/mupen64plus/mupen64plus-rsp-hle/blob/8a7a472a7172eb2c8725b305eae26818ed7b51a2/src/hle.c) |
| libgfxd | `49ec1bb893a16b769ddfd0b329d238a658dc0ea2` | [API and supported targets](https://github.com/glankk/libgfxd/tree/49ec1bb893a16b769ddfd0b329d238a658dc0ea2) |
| glankk N64 tools | `5d35e3aa505080250b4f3eec0edafb0d24365da8` | [gfxdis project](https://github.com/glankk/n64/tree/5d35e3aa505080250b4f3eec0edafb0d24365da8) |

| ares-64 research runner | `6b65e1beacf8b2af66aaee72541ab154382b7e6a` | [pinned fork](https://github.com/HailToDodongo/ares-64/tree/6b65e1beacf8b2af66aaee72541ab154382b7e6a), authored hooks and fixes in `tools/research/patch_ares.py` |
| Original Angrylion core | `9c8b9ed3e7d7f00dff8bc872ccdd3fba1a3673fc` | [reference rasterizer](https://github.com/ata4/angrylion-rdp-plus/tree/9c8b9ed3e7d7f00dff8bc872ccdd3fba1a3673fc); replaces the fork's altered texture loader |

The built ares source pins SDL `c568c46f51e863e9c4658b31bfd6c455cc875c5f`,
ImGui `5a76f2adf1b0403b86a45010121fb32a6bff8680` and QuickJS-NG
`fd0a0210b7be00957751871e7e01b8291268fc29` through its gitlinks. The headless
runner uses QuickJS; the recorded compiler is LLVM 23.1.2, CMake 4.4.4 and
Python 3.14.8. Owned Python syntax remains compatible with the 3.10+ requirement;
a separate Python 3.10 interpreter run was not performed.

The user-mentioned GGA-Recomp reference was investigated. The candidate
`dantheman11294/GGA-Recomp` returned HTTP 404 during source lookup. No converter
or game-specific fixes from an inaccessible repository were assumed available.
The symbol conversion plan instead follows the inspected N64Recomp emitter.

## Shell dependencies (ADR-074)

The former engine shell was used through Stage 6 and removed by ADR-074; its
pins remain only in Git history. The SDL3 shell uses these pinned public inputs, fetched and verified by
`tools/desktop/bootstrap.py` into ignored `local/toolchains`:

| Dependency | Official source | Verified SHA-256 |
|---|---|---|
| SDL 3.4.18 source release (2026-10-02), revision `release-3.4.18-0-g829a65d76` | [SDL3-3.4.18.tar.gz](https://github.com/libsdl-org/SDL/releases/download/release-3.4.18/SDL3-3.4.18.tar.gz) | `9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3` |
| stb_truetype at commit `2c980bb59875b0d32144a71867fbdebb2f77cd20` | [stb_truetype.h](https://raw.githubusercontent.com/nothings/stb/2c980bb59875b0d32144a71867fbdebb2f77cd20/stb_truetype.h) | `ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab` |
| DejaVu fonts 2.37 (Bitstream Vera derived license) | [dejavu-fonts-ttf-2.37.zip](https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.zip) | `7576310b219e04159d35ff61dd4a4ec4cdba4f35c00e002a136f00e96a908b0a` |
| miniaudio 0.11.23 | [Pinned single header](https://raw.githubusercontent.com/mackron/miniaudio/0.11.23/miniaudio.h) | `7e4f3f13c8fe66df2080ac3dd12a89193e3c2463cb7f067c798abd7331cd8ee6` |
| AppImage type2 runtime 20251108 (x86_64) | [runtime-x86_64](https://github.com/AppImage/type2-runtime/releases/download/20251108/runtime-x86_64) | `2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d` |
| Debian trixie-slim base image (linux/arm64) for the Linux cross image | `debian@sha256:a29215f6a35e51e22adffa17f89e9d2ef06214e64a2bad10d765c46aea49f11f` | digest-pinned in `tools/desktop/linux-cross.Dockerfile` |

The overlay uses `DejaVuSansMono-Bold.ttf` from the font archive; its LICENSE
ships beside it. SDL is built statically from source with `add_subdirectory`;
no system or Homebrew SDL is linked into the shell. Debian packages installed in
the cross image are taken from the distribution at image build time and are not
individually pinned. Toolchains used on 2026-10-11: Homebrew LLVM 23.1.2
(macOS), Homebrew mingw-w64 GCC 16.2.0 (Windows), Debian GCC 14 cross compiler
(Linux), Android NDK r29 `29.0.14206865`, Build-Tools 36.0.0, Homebrew OpenJDK 21.

### Verified SDL surface

| Surface | Source check (SDL 3.4.18) | Remaining proof |
|---|---|---|
| Main callbacks | `SDL_MAIN_USE_CALLBACKS` drives init/iterate/event/quit on macOS, Windows, Linux and Android | — |
| Android library loading | `SDLActivity.getLibraries()` may return only `main`; JNI entry points come from the static archive when linked whole (`nativeSetupJNI`, `JNI_OnLoad` exported from `libmain.so`) | — |
| Android file dialog | `SDL_ShowOpenFileDialog` uses `ACTION_OPEN_DOCUMENT` and returns `content://` URIs; `SDL_IOFromFile` opens them through a file descriptor | Physical-device SAF providers |
| Android lifecycle | `SDL_EVENT_WILL_ENTER_BACKGROUND`/`DID_ENTER_FOREGROUND` reach event watches immediately | Physical surface recreation |
| Storage | `SDL_GetPrefPath("", app)` omits the organization directory; Android uses `getFilesDir()` | — |
| Renderer readback | `SDL_RenderReadPixels` reads the current target before present | Per-backend parity beyond Metal |

Known defect: the Vulkan renderer crashes on Android surface loss (D-053).

## Runtime integration constraints observed in source

`RendererContext::send_dl(const OSTask*)` is the task interface. The event system
invokes it from its own graphics thread and signals DP completion afterwards.
That callback must remain CPU-only in this project. Its presentation methods
must publish snapshots rather than draw into a native window.

The upstream VI loop uses a nominal 60 Hz schedule. This does not establish the
original game's exact field timing. Its desktop/window abstractions and pause
semantics require a narrowly scoped adapter/fork, not an invented callback API.

The upstream ROM validation code uses XXH3, not the required SHA-1. Add the
normalized SHA-1 check at the project's import/start boundary and test that a
failure prevents execution. Audit the startup RDRAM and OS globals for the
required 8 MiB guest view.

The runtime CMake requires C++20. The user explicitly approved C++20 for this
project. Generated game functions remain C as emitted by N64Recomp.
