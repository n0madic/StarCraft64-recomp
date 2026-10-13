# sc64_setup: ROM -> game module on the user's machine (ADR-087). Needs sc64_sha and SDL3 from shell.cmake.
set(SC64_SETUP_SOURCES process.cpp recipe.cpp toolchain.cpp zig.cpp setup.cpp locate.cpp)
list(TRANSFORM SC64_SETUP_SOURCES PREPEND "${SC64_ROOT}/native/setup/")
add_library(sc64_setup_lib STATIC ${SC64_SETUP_SOURCES})
target_include_directories(sc64_setup_lib PUBLIC "${SC64_ROOT}/native/setup" "${SC64_ROOT}/native/runtime"
    "${SC64_RUNTIME_DIR}/thirdparty")
target_link_libraries(sc64_setup_lib PUBLIC sc64_sha Threads::Threads)

add_executable(sc64_setup "${SC64_ROOT}/native/setup/main.cpp")
target_link_libraries(sc64_setup PRIVATE sc64_setup_lib SDL3::SDL3-static)

# Resources the tool reads at run time, staged beside the executable: the recipe, and the module
# sources with recomp.h (the generated C includes it).
set(SC64_SETUP_RES "${CMAKE_BINARY_DIR}/setup-resources")
add_custom_target(sc64_setup_resources
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SC64_SETUP_RES}/recipe/ntsc-u" "${SC64_SETUP_RES}/module/include/librecomp"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SC64_ROOT}/recipe/ntsc-u/sc64.syms.toml" "${SC64_ROOT}/recipe/ntsc-u/sc64.toml"
        "${SC64_ROOT}/recipe/ntsc-u/generated.json" "${SC64_SETUP_RES}/recipe/ntsc-u"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SC64_ROOT}/native/module/module_main.c" "${SC64_ROOT}/native/module/sc64_module.h"
        "${SC64_ROOT}/native/module/sc64_imports.def" "${SC64_RUNTIME_DIR}/N64Recomp/include/recomp.h"
        "${SC64_SETUP_RES}/module"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SC64_ROOT}/native/module/include/librecomp/sections.h"
        "${SC64_SETUP_RES}/module/include/librecomp"
    VERBATIM)
add_dependencies(sc64_setup sc64_setup_resources)

# Orchestration, recipe and toolchain logic against a fake process runner (no ROM, no compiler).
add_executable(sc64_setup_test "${SC64_ROOT}/tests/native/test_setup.cpp")
target_link_libraries(sc64_setup_test PRIVATE sc64_setup_lib)
target_compile_options(sc64_setup_test PRIVATE -UNDEBUG)
add_test(NAME setup COMMAND sc64_setup_test)

# The shell builds the game module itself on first run when the resources are beside it.
target_sources(sc64_shell PRIVATE "${SC64_ROOT}/native/shell/game_build.cpp")
target_compile_definitions(sc64_shell PUBLIC SC64_HAS_SETUP=1)
target_link_libraries(sc64_shell PUBLIC sc64_setup_lib)
