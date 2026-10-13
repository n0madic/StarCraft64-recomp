cmake_minimum_required(VERSION 3.24)
project(sc64 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()
add_subdirectory(native/gfx_core)

# Engine-independent pointer/controller adapters and digests: no SDL, no game.
add_library(sc64_input STATIC
    native/shell/input/gestures.cpp native/shell/input/map_taps.cpp native/shell/input/minimap_input.cpp
    native/shell/input/hud_buttons.cpp native/shell/input/hud_input.cpp native/shell/input/ui_input.cpp
    native/shell/input/devices.cpp)
target_include_directories(sc64_input PUBLIC native/shell/input)
target_compile_options(sc64_input PRIVATE -Wall -Wextra)
add_library(sc64_sha STATIC native/runtime/sha.cpp)
target_include_directories(sc64_sha PUBLIC native/runtime)
# Host routing/overlay scripts use the pinned runtime's vendored nlohmann/json.
set(SC64_JSON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/N64ModernRuntime/thirdparty" CACHE PATH
    "Directory containing json/json.hpp")
add_library(sc64_scripts STATIC native/shell/scripts.cpp)
target_include_directories(sc64_scripts PUBLIC native/shell "${SC64_JSON_DIR}")
target_link_libraries(sc64_scripts PUBLIC sc64_input)
target_compile_options(sc64_scripts PRIVATE -Wall -Wextra)

add_executable(sc64_desktop_bridge_test tests/native/test_desktop_bridge.cpp)
target_include_directories(sc64_desktop_bridge_test PRIVATE native/shell native/gfx_core)
target_link_libraries(sc64_desktop_bridge_test PRIVATE sc64_gfx_core)
target_compile_options(sc64_desktop_bridge_test PRIVATE -UNDEBUG)
add_test(NAME desktop_bridge COMMAND sc64_desktop_bridge_test)
add_executable(sc64_rom_import_test tests/native/test_rom_import.cpp)
target_include_directories(sc64_rom_import_test PRIVATE native/shell)
target_compile_options(sc64_rom_import_test PRIVATE -UNDEBUG)
add_test(NAME rom_import COMMAND sc64_rom_import_test "${CMAKE_CURRENT_BINARY_DIR}/rom-import-test-storage")
add_executable(sc64_sha_test tests/native/test_sha.cpp)
target_link_libraries(sc64_sha_test PRIVATE sc64_sha)
target_compile_options(sc64_sha_test PRIVATE -UNDEBUG)
add_test(NAME sha COMMAND sc64_sha_test)
foreach(name desktop_input touch_input gestures map_taps hud_input hud_buttons menu_input haptics)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/native/test_${name}.cpp")
        add_executable(sc64_${name}_test tests/native/test_${name}.cpp)
        target_link_libraries(sc64_${name}_test PRIVATE sc64_input sc64_scripts)
        target_compile_options(sc64_${name}_test PRIVATE -UNDEBUG)
        add_test(NAME ${name} COMMAND sc64_${name}_test)
    endif()
endforeach()

# Script telemetry merge, built with sanitizers so lifetime defects fail here.
add_executable(sc64_host_telemetry_test tests/native/test_host_telemetry.cpp native/shell/scripts.cpp)
target_include_directories(sc64_host_telemetry_test PRIVATE native/shell "${SC64_JSON_DIR}")
target_link_libraries(sc64_host_telemetry_test PRIVATE sc64_input)
if(NOT WIN32 AND NOT CMAKE_CROSSCOMPILING)
    target_compile_options(sc64_host_telemetry_test PRIVATE -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(sc64_host_telemetry_test PRIVATE -fsanitize=address,undefined)
endif()
add_test(NAME host_telemetry COMMAND sc64_host_telemetry_test)

add_executable(sc64_loop_watch_test tests/native/test_loop_watch.cpp)
target_include_directories(sc64_loop_watch_test PRIVATE native/shell)
target_compile_options(sc64_loop_watch_test PRIVATE -UNDEBUG)
add_test(NAME loop_watch COMMAND sc64_loop_watch_test)

option(SC64_BUILD_HOST "Build the private recompiled runtime, capture host and shell" ON)
if(NOT SC64_BUILD_HOST)
    return()
endif()
set(SC64_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/local/deps/N64ModernRuntime" CACHE PATH "Prepared private runtime")
set(SC64_GENERATED_DIR "${CMAKE_CURRENT_SOURCE_DIR}/local/generated/game" CACHE PATH "Private N64Recomp output")
option(SC64_BUILD_SHELL "Build the SDL3 application against the guarded shell runtime copy" OFF)

find_package(Threads REQUIRED)
if(NOT SC64_BUILD_SHELL)
    find_package(OpenSSL REQUIRED)
    find_package(SDL2 CONFIG REQUIRED)
endif()

if(NOT EXISTS "${SC64_RUNTIME_DIR}/sc64-host-patch.json")
    message(FATAL_ERROR "Run tools/recompile/patch_runtime.py and patch_host_runtime.py first")
endif()
if(NOT EXISTS "${SC64_GENERATED_DIR}/recomp_overlays.inl")
    message(FATAL_ERROR "Complete private N64Recomp generation before configuring")
endif()

# The pinned Rabbitizer header omits cstdlib on current libc++; include it for
# dependency C++ translation units without altering upstream header contents.
add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-include>" "$<$<COMPILE_LANGUAGE:CXX>:cstdlib>")
add_compile_options(-UNDEBUG)
include(cmake/runtime.cmake)
sc64_add_runtime("${SC64_RUNTIME_DIR}" "runtime")
target_include_directories(librecomp PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native/runtime")
target_include_directories(ultramodern PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native/runtime")

# The shell loads the game as a module (cmake/game_module.cmake, ADR-086); the capture host
# still links the generated C statically, and SC64_STATIC_GAME does the same for the shell
# (A/B timing against the module, and the Android linkage).
option(SC64_STATIC_GAME "Link the generated game statically into the shell instead of loading a module" OFF)
if(SC64_BUILD_SHELL AND NOT SC64_STATIC_GAME)
    include(cmake/game_module.cmake)
else()
    file(GLOB SC64_GAME_SOURCES CONFIGURE_DEPENDS "${SC64_GENERATED_DIR}/*.c")
    add_library(sc64_game STATIC ${SC64_GAME_SOURCES})
    target_include_directories(sc64_game PUBLIC "${SC64_GENERATED_DIR}" "${SC64_RUNTIME_DIR}/N64Recomp/include")
    target_compile_options(sc64_game PRIVATE -O2 -fno-strict-aliasing -ffp-contract=off -fexceptions)
endif()

add_executable(sc64_flash_test tests/native/test_flash_device.cpp native/runtime/flash_device.cpp)
target_include_directories(sc64_flash_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native/runtime")
target_link_libraries(sc64_flash_test PRIVATE Threads::Threads)
add_test(NAME flash_protocol COMMAND sc64_flash_test "${CMAKE_CURRENT_BINARY_DIR}/flash-test-storage")
add_executable(sc64_vi_test tests/native/test_vi_scale.cpp)
target_include_directories(sc64_vi_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native/runtime")
add_test(NAME vi_scale COMMAND sc64_vi_test)
add_executable(sc64_ui_observers_test tests/native/test_ui_observers.cpp native/runtime/ui_feedback.cpp)
target_include_directories(sc64_ui_observers_test PRIVATE native/runtime "${SC64_RUNTIME_DIR}/N64Recomp/include")
add_test(NAME ui_observers COMMAND sc64_ui_observers_test)
add_executable(sc64_guest_fpu_test tests/native/test_guest_fpu.cpp)
target_include_directories(sc64_guest_fpu_test PRIVATE native/runtime "${SC64_RUNTIME_DIR}/N64Recomp/include")
target_compile_options(sc64_guest_fpu_test PRIVATE -UNDEBUG -ffp-contract=off -frounding-math)
add_test(NAME guest_fpu COMMAND sc64_guest_fpu_test)

if(NOT SC64_BUILD_SHELL)
    # Stage 1 capture host: its own runtime copy, without the shell clock bridge.
    add_executable(sc64_capture native/host/main.cpp native/host/capture.cpp native/host/audio.cpp
        native/runtime/flash_device.cpp native/runtime/ui_feedback.cpp)
    target_include_directories(sc64_capture PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/native/runtime")
    target_link_libraries(sc64_capture PRIVATE sc64_game librecomp ultramodern OpenSSL::Crypto SDL2::SDL2 Threads::Threads)
    return()
endif()

if(NOT EXISTS "${SC64_RUNTIME_DIR}/sc64-shell-patch.json")
    message(FATAL_ERROR "Use the separate tools/desktop/patch_shell_runtime.py dependency copy")
endif()
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/desktop/game_validation.py"
    "${SC64_GENERATED_DIR}" RESULT_VARIABLE SC64_GAME_VALIDATION_RESULT
    ERROR_VARIABLE SC64_GAME_VALIDATION_ERROR)
if(NOT SC64_GAME_VALIDATION_RESULT EQUAL 0)
    message(FATAL_ERROR "${SC64_GAME_VALIDATION_ERROR}")
endif()
include(cmake/shell.cmake)
include(cmake/setup.cmake)
