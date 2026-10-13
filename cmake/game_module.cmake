# The game as a loadable module built from the generated C (ADR-086). Plain C: it needs only
# libc, so any C compiler can build it from the generated sources and native/module.
# Expects SC64_GENERATED_DIR and SC64_RUNTIME_DIR (for recomp.h).
set(SC64_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}/../native/module")
file(GLOB SC64_GAME_SOURCES CONFIGURE_DEPENDS "${SC64_GENERATED_DIR}/*.c")
add_library(sc64_game_module SHARED ${SC64_GAME_SOURCES} "${SC64_MODULE_DIR}/module_main.c")
set_target_properties(sc64_game_module PROPERTIES OUTPUT_NAME sc64-game PREFIX "" C_VISIBILITY_PRESET hidden)
target_include_directories(sc64_game_module PRIVATE "${SC64_MODULE_DIR}" "${SC64_MODULE_DIR}/include"
    "${SC64_GENERATED_DIR}" "${SC64_RUNTIME_DIR}/N64Recomp/include")
target_compile_options(sc64_game_module PRIVATE -O2 -fno-strict-aliasing -ffp-contract=off -fexceptions)
# ROM_SHA1 comes from the recipe the C was generated from.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../recipe/ntsc-u/generated.json" SC64_RECIPE_MANIFEST)
string(JSON SC64_MODULE_ROM_SHA1 GET "${SC64_RECIPE_MANIFEST}" rom_sha1)
target_compile_definitions(sc64_game_module PRIVATE SC64_MODULE_ROM_SHA1="${SC64_MODULE_ROM_SHA1}")
if(WIN32)
    target_link_options(sc64_game_module PRIVATE -static-libgcc)
endif()
