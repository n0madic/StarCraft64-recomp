/* Entry point and host trampolines of the game module (ADR-086). Built together with the
 * generated C; it is the only authored source in the module. Needs only libc. */
#include "sc64_module.h"
#include "funcs.h"
#include "recomp_overlays.inl"

#ifndef SC64_MODULE_ROM_SHA1
#error "define SC64_MODULE_ROM_SHA1 as the SHA-1 string of the ROM the C was generated from"
#endif

#if defined(_WIN32)
#define SC64_EXPORT __declspec(dllexport)
#else
#define SC64_EXPORT __attribute__((visibility("default")))
#endif

static const Sc64HostApi* host;

#define SC64_GUEST_CALL(name) \
    void name(uint8_t* rdram, recomp_context* ctx) { host->name(rdram, ctx); }
#define SC64_IMPORT_V(name, params, args) \
    void name params { host->name args; }
#define SC64_IMPORT_R(ret, name, params, args) \
    ret name params { return host->name args; }
#include "sc64_imports.def"
#undef SC64_GUEST_CALL
#undef SC64_IMPORT_V
#undef SC64_IMPORT_R

static const Sc64ModuleInfo module_info = {
    SC64_MODULE_ABI,
    sizeof(Sc64ModuleInfo),
    sizeof(SectionTableEntry),
    sizeof(FuncEntry),
    sizeof(recomp_context),
    0x80000400u,
    recomp_entrypoint,
    section_table,
    ARRLEN(section_table),
    sc64_resident_8000B988,
    SC64_MODULE_ROM_SHA1,
};

SC64_EXPORT const Sc64ModuleInfo* sc64_module_init(const Sc64HostApi* api) {
    if (api == NULL || api->abi != SC64_MODULE_ABI || api->size != sizeof(Sc64HostApi)) {
        return NULL;
    }
    host = api;
    return &module_info;
}
