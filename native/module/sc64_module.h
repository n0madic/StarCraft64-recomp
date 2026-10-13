/* ABI between the prebuilt shell and the game module built from the generated C (ADR-086).
 * Plain C so that any C compiler can build the module. The module exports one function,
 * sc64_module_init; every function the game calls on the host comes through Sc64HostApi. */
#ifndef SC64_MODULE_H
#define SC64_MODULE_H

#include <stddef.h>
#include <stdint.h>
#include "recomp.h"
#include "librecomp/sections.h"

/* Bump whenever a struct below or sc64_imports.def changes. */
#define SC64_MODULE_ABI 1
#define SC64_MODULE_INIT "sc64_module_init"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sc64HostApi {
    uint32_t abi;
    uint32_t size; /* sizeof(Sc64HostApi) as the host built it */
#define SC64_GUEST_CALL(name) void (*name)(uint8_t* rdram, recomp_context* ctx);
#define SC64_IMPORT_V(name, params, args) void (*name) params;
#define SC64_IMPORT_R(ret, name, params, args) ret (*name) params;
#include "sc64_imports.def"
#undef SC64_GUEST_CALL
#undef SC64_IMPORT_V
#undef SC64_IMPORT_R
} Sc64HostApi;

typedef struct Sc64ModuleInfo {
    uint32_t abi;
    uint32_t size; /* sizeof(Sc64ModuleInfo) as the module was built */
    /* Layout checks against the host's own headers. */
    uint32_t section_entry_size;
    uint32_t func_entry_size;
    uint32_t context_size;
    uint32_t entrypoint_vram;
    recomp_func_t* entrypoint;
    SectionTableEntry* sections;
    size_t num_sections;
    /* Original minimap position setter, run on a guest thread by the shell. */
    recomp_func_t* minimap_set_position;
    /* SHA-1 of the ROM the C was generated from. */
    const char* rom_sha1;
} Sc64ModuleInfo;

typedef const Sc64ModuleInfo* (*Sc64ModuleInitFn)(const Sc64HostApi* host);

#ifdef __cplusplus
}
#endif

#endif
