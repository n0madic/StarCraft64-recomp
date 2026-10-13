# SDL3 shell, shared by the desktop (root CMakeLists.txt) and Android profiles.
# Expects sc64_gfx_core, librecomp and ultramodern, plus the guarded shell runtime copy
# in SC64_RUNTIME_DIR. The game is either the loadable module sc64_game_module (desktop,
# ADR-086; the shell loads it at run time) or the static library sc64_game (Android). SDL is the pinned source release,
# linked statically; no system SDL is used.
get_filename_component(SC64_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SC64_TOOLCHAINS "${SC64_ROOT}/local/toolchains")
set(SC64_SDL3_DIR "${SC64_TOOLCHAINS}/sdl3/SDL3-3.4.18" CACHE PATH "Pinned SDL3 source release")
if(NOT EXISTS "${SC64_SDL3_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Run tools/desktop/bootstrap.py to fetch the pinned SDL3 source")
endif()

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
add_subdirectory("${SC64_SDL3_DIR}" sdl3 EXCLUDE_FROM_ALL)

if(NOT TARGET sc64_input)
    add_library(sc64_input STATIC
        ${SC64_ROOT}/native/shell/input/gestures.cpp ${SC64_ROOT}/native/shell/input/map_taps.cpp
        ${SC64_ROOT}/native/shell/input/minimap_input.cpp ${SC64_ROOT}/native/shell/input/hud_buttons.cpp
        ${SC64_ROOT}/native/shell/input/hud_input.cpp ${SC64_ROOT}/native/shell/input/ui_input.cpp
        ${SC64_ROOT}/native/shell/input/devices.cpp)
    target_include_directories(sc64_input PUBLIC "${SC64_ROOT}/native/shell/input")
    add_library(sc64_sha STATIC "${SC64_ROOT}/native/runtime/sha.cpp")
    target_include_directories(sc64_sha PUBLIC "${SC64_ROOT}/native/runtime")
    add_library(sc64_scripts STATIC "${SC64_ROOT}/native/shell/scripts.cpp")
    target_include_directories(sc64_scripts PUBLIC "${SC64_ROOT}/native/shell" "${SC64_RUNTIME_DIR}/thirdparty")
    target_link_libraries(sc64_scripts PUBLIC sc64_input)
endif()

# The shell runtime copy includes clock_bridge.hpp from the shell sources.
target_include_directories(ultramodern PRIVATE "${SC64_ROOT}/native/shell")

add_library(sc64_shell STATIC
    ${SC64_ROOT}/native/shell/session.cpp ${SC64_ROOT}/native/shell/audio.cpp
    ${SC64_ROOT}/native/shell/miniaudio.c ${SC64_ROOT}/native/shell/log.cpp
    ${SC64_ROOT}/native/shell/native.cpp ${SC64_ROOT}/native/shell/video.cpp
    ${SC64_ROOT}/native/shell/platform_sdl.cpp
    ${SC64_ROOT}/native/runtime/flash_device.cpp ${SC64_ROOT}/native/runtime/ui_feedback.cpp)
target_include_directories(sc64_shell PUBLIC "${SC64_ROOT}/native/shell" "${SC64_ROOT}/native/runtime"
    "${SC64_TOOLCHAINS}")
target_compile_definitions(sc64_shell PUBLIC SC64_SDL_VERSION="3.4.18")
if(NOT SC64_STATIC_GAME)
    target_sources(sc64_shell PRIVATE ${SC64_ROOT}/native/shell/game_module.cpp)
    # recomp.h comes with the generated game's static library otherwise.
    target_include_directories(sc64_shell PUBLIC "${SC64_ROOT}/native/module" "${SC64_RUNTIME_DIR}/N64Recomp/include")
else()
    target_sources(sc64_shell PRIVATE ${SC64_ROOT}/native/shell/game_static.cpp)
    target_include_directories(sc64_shell PUBLIC "${SC64_ROOT}/native/module")
    target_link_libraries(sc64_shell PUBLIC sc64_game)
endif()
target_link_libraries(sc64_shell PUBLIC sc64_gfx_core sc64_scripts sc64_sha librecomp ultramodern
    SDL3::SDL3-static Threads::Threads)

if(NOT SC64_STATIC_GAME AND NOT ANDROID)
    # Loader test against synthetic modules built like the real one (shim headers, C only).
    set(SC64_FIXTURE_VARIANTS good:"" bad_abi:FIXTURE_BAD_ABI bad_layout:FIXTURE_BAD_LAYOUT bad_rom:FIXTURE_BAD_ROM
        refuse:FIXTURE_REFUSE no_init:FIXTURE_NO_INIT)
    set(SC64_FIXTURE_FILES)
    set(SC64_FIXTURE_TARGETS)
    foreach(entry IN LISTS SC64_FIXTURE_VARIANTS)
        string(REPLACE ":" ";" parts "${entry}")
        list(GET parts 0 variant)
        list(GET parts 1 define)
        add_library(sc64_fixture_${variant} SHARED "${SC64_ROOT}/tests/native/fixtures/module_fixture.c")
        set_target_properties(sc64_fixture_${variant} PROPERTIES PREFIX "" OUTPUT_NAME fixture-${variant} C_VISIBILITY_PRESET hidden
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures" RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/fixtures")
        target_include_directories(sc64_fixture_${variant} PRIVATE "${SC64_ROOT}/native/module"
            "${SC64_ROOT}/native/module/include" "${SC64_RUNTIME_DIR}/N64Recomp/include")
        target_compile_options(sc64_fixture_${variant} PRIVATE -fexceptions)
        if(WIN32)
            target_link_options(sc64_fixture_${variant} PRIVATE -static-libgcc)
        endif()
        if(NOT define STREQUAL "\"\"" AND NOT define STREQUAL "")
            target_compile_definitions(sc64_fixture_${variant} PRIVATE ${define})
        endif()
        list(APPEND SC64_FIXTURE_FILES "$<TARGET_FILE:sc64_fixture_${variant}>")
        list(APPEND SC64_FIXTURE_TARGETS sc64_fixture_${variant})
    endforeach()
    add_executable(sc64_game_module_test "${SC64_ROOT}/tests/native/test_game_module.cpp")
    target_link_libraries(sc64_game_module_test PRIVATE sc64_shell)
    add_dependencies(sc64_game_module_test ${SC64_FIXTURE_TARGETS})
    add_test(NAME game_module COMMAND sc64_game_module_test ${SC64_FIXTURE_FILES} "${CMAKE_BINARY_DIR}/fixtures/absent")
endif()

if(ANDROID)
    add_library(sc64_app SHARED "${SC64_ROOT}/native/shell/app.cpp" "${SC64_ROOT}/native/android/platform.cpp")
    set_target_properties(sc64_app PROPERTIES OUTPUT_NAME main)
    target_compile_definitions(sc64_shell PRIVATE
        SC64_PRIVATE_ROOT="/data/data/${SC64_ANDROID_PACKAGE}/files")
    target_compile_definitions(sc64_app PRIVATE SC64_APP_ID="${SC64_ANDROID_PACKAGE}")
    # SDLActivity loads only libmain.so: keep SDL's JNI entry points from the
    # static archive and export them.
    target_link_libraries(sc64_app PRIVATE sc64_shell "$<LINK_LIBRARY:WHOLE_ARCHIVE,SDL3::SDL3-static>" aaudio log android)
    target_link_options(sc64_app PRIVATE -Wl,--gc-sections -Wl,--no-undefined
        -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now -Wl,-z,max-page-size=16384
        -Wl,-z,common-page-size=16384)
else()
    add_executable(sc64_app "${SC64_ROOT}/native/shell/app.cpp")
    # Shipped packages (tools/desktop/cross_build.py) must not bake in the build tree path,
    # whether they are cross-compiled or built natively on a matching host.
    option(SC64_PACKAGED_BUILD "Build a relocatable package: private root is a relative local/" OFF)
    if(CMAKE_CROSSCOMPILING OR SC64_PACKAGED_BUILD)
        # Diagnostic outputs stay below a local/ directory beside the run.
        target_compile_definitions(sc64_shell PRIVATE SC64_PRIVATE_ROOT="local")
    else()
        target_compile_definitions(sc64_shell PRIVATE SC64_PRIVATE_ROOT="${SC64_ROOT}/local")
    endif()
    if(WIN32)
        set_target_properties(sc64_app PROPERTIES WIN32_EXECUTABLE TRUE OUTPUT_NAME StarCraft64)
        # Optional generated executable icon (tools/desktop/icon.py, ADR-076); the .rc names
        # its .ico relative to its own directory.
        set(SC64_ICON_RC "" CACHE FILEPATH "Windows resource script that embeds the generated icon")
        if(SC64_ICON_RC AND EXISTS "${SC64_ICON_RC}")
            enable_language(RC)
            get_filename_component(SC64_ICON_DIR "${SC64_ICON_RC}" DIRECTORY)
            set(CMAKE_RC_FLAGS "${CMAKE_RC_FLAGS} --include-dir=${SC64_ICON_DIR}")
            target_sources(sc64_app PRIVATE "${SC64_ICON_RC}")
            set_source_files_properties("${SC64_ICON_RC}" PROPERTIES OBJECT_DEPENDS "${SC64_ICON_DIR}/StarCraft64.ico")
        endif()
    elseif(NOT APPLE)
        set_target_properties(sc64_app PROPERTIES OUTPUT_NAME StarCraft64)
    endif()
    target_link_libraries(sc64_app PRIVATE sc64_shell)
    if(TARGET sc64_game_module)
        add_dependencies(sc64_app sc64_game_module)
    endif()
endif()
