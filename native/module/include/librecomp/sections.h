#ifndef __SECTIONS_H__
#define __SECTIONS_H__
/* Layout-compatible copy of the section tables in N64ModernRuntime's librecomp/sections.h,
 * for building the game module without the runtime tree (ADR-086). The shell checks the
 * struct sizes the module reports against the real header and refuses a mismatch. */

#include <stddef.h>
#include <stdint.h>
#include "recomp.h"

#ifndef __cplusplus
/* recomp_overlays.inl is written for C++ and uses nullptr. */
#ifndef nullptr
#define nullptr ((void*)0)
#endif
#endif

#define ARRLEN(x) (sizeof(x) / sizeof((x)[0]))

typedef struct {
    recomp_func_t* func;
    uint32_t offset;
    uint32_t rom_size;
} FuncEntry;

typedef enum {
    R_MIPS_NONE = 0,
    R_MIPS_16,
    R_MIPS_32,
    R_MIPS_REL32,
    R_MIPS_26,
    R_MIPS_HI16,
    R_MIPS_LO16,
    R_MIPS_GPREL16,
} RelocEntryType;

typedef struct {
    uint32_t offset;
    uint32_t target_section_offset;
    uint16_t target_section;
    RelocEntryType type;
} RelocEntry;

typedef struct {
    uint32_t rom_addr;
    uint32_t ram_addr;
    uint32_t size;
    FuncEntry *funcs;
    size_t num_funcs;
    RelocEntry* relocs;
    size_t num_relocs;
    size_t index;
} SectionTableEntry;

#endif
