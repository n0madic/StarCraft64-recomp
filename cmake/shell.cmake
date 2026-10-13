# SDL3 shell, shared by the desktop (root CMakeLists.txt) and Android profiles.
# Expects sc64_game, sc64_gfx_core, librecomp and ultramodern, plus the guarded
# shell runtime copy in SC64_RUNTIME_DIR. SDL is the pinned source release,
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
target_link_libraries(sc64_shell PUBLIC sc64_game sc64_gfx_core sc64_scripts sc64_sha librecomp ultramodern
    SDL3::SDL3-static Threads::Threads)

if(ANDROID)
    add_library(sc64_app SHARED "${SC64_ROOT}/native/shell/app.cpp" "${SC64_ROOT}/native/android/platform.cpp")
    set_target_properties(sc64_app PROPERTIES OUTPUT_NAME main)
    target_compile_definitions(sc64_shell PRIVATE
        SC64_PRIVATE_ROOT="/data/data/com.nomadic.sc64.personal/files")
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
endif()
