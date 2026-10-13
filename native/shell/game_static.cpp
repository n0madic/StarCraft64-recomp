// Game linked into the application (Android, ADR-086): the same Sc64ModuleInfo the module
// provides, built from the generated tables instead of loaded.
#include "game_module.hpp"

extern "C" {
#include "funcs.h"
#include "recomp_overlays.inl"
}

namespace sc64::desktop::game {
std::string module_file_name() { return {}; }
const Sc64ModuleInfo& load(const std::filesystem::path&) { return info(); }
const Sc64ModuleInfo& info() {
    static const Sc64ModuleInfo value = {
        SC64_MODULE_ABI, sizeof(Sc64ModuleInfo), sizeof(SectionTableEntry), sizeof(FuncEntry), sizeof(recomp_context),
        0x80000400u, recomp_entrypoint, section_table, ARRLEN(section_table), sc64_resident_8000B988, ""};
    return value;
}
}
