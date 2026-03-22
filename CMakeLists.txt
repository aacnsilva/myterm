cmake_minimum_required(VERSION 3.19)
project(myterm C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------
option(MYTERM_USE_SYSTEM_RAYLIB "Use system-installed raylib instead of FetchContent" OFF)
option(MYTERM_BUILD_TESTS "Build unit tests" ON)

# --------------------------------------------------------------------------
# libghostty-vt  (uses Ghostty's own CMakeLists.txt which invokes zig build)
#
# Ghostty exposes both a shared target (ghostty-vt) and a static target
# (ghostty-vt-static). On Windows we intentionally link the static target:
#   - the shared target requires an import library path that Ghostty's CMake
#     wrapper does not model as a proper build output for Ninja
#   - release artifacts should stay self-contained and not require shipping
#     ghostty-vt.dll alongside myterm.exe
#   - building the static library with -Dsimd=false avoids extra transitive
#     C++/SIMD dependencies in downstream consumers
#
# Requires: zig 0.15.2+ on PATH.
#
# Reference: https://github.com/ghostty-org/ghostling
# --------------------------------------------------------------------------
include(FetchContent)

find_program(MYTERM_ZIG_EXECUTABLE zig)
if(NOT MYTERM_ZIG_EXECUTABLE)
    message(FATAL_ERROR "zig not found on PATH (required: Zig 0.15.2+)")
endif()

execute_process(
    COMMAND "${MYTERM_ZIG_EXECUTABLE}" version
    OUTPUT_VARIABLE MYTERM_ZIG_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(MYTERM_ZIG_VERSION VERSION_LESS "0.15.2")
    message(FATAL_ERROR
        "myterm requires Zig 0.15.2+ because the pinned Ghostty dependency requires it; found ${MYTERM_ZIG_VERSION}"
    )
endif()

set(MYTERM_GHOSTTY_TARGET ghostty-vt)
if(WIN32)
    set(MYTERM_GHOSTTY_TARGET ghostty-vt-static)

    if(NOT DEFINED GHOSTTY_ZIG_BUILD_FLAGS)
        set(GHOSTTY_ZIG_BUILD_FLAGS "-Dsimd=false" CACHE STRING
            "Additional flags to pass to Ghostty's zig build"
            FORCE
        )
    endif()
endif()

# Pin to a known-good commit for reproducible builds.
# Update this hash when upgrading ghostty.
FetchContent_Declare(ghostty
    GIT_REPOSITORY https://github.com/ghostty-org/ghostty.git
    GIT_TAG        8a788a350e105398b1c32df74a4a962e3d723032
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ghostty)

# --------------------------------------------------------------------------
# Raylib  (cross-platform windowing + 2D rendering)
# --------------------------------------------------------------------------
if(NOT MYTERM_USE_SYSTEM_RAYLIB)
    FetchContent_Declare(
        raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG        5.5
        GIT_SHALLOW    TRUE
    )
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(raylib)
else()
    find_package(raylib REQUIRED)
endif()

# --------------------------------------------------------------------------
# Common source library (shared between main executable and tests)
# --------------------------------------------------------------------------
add_library(myterm_lib STATIC
    src/terminal.c
    src/renderer.c
    src/input.c
    src/tabs.c
    src/config.c
    src/search.c
    src/splits.c
)

if(WIN32)
    target_sources(myterm_lib PRIVATE src/pty_windows.c)
else()
    target_sources(myterm_lib PRIVATE src/pty_unix.c)
endif()

target_include_directories(myterm_lib PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${ghostty_SOURCE_DIR}/include
)

target_link_libraries(myterm_lib PUBLIC
    ${MYTERM_GHOSTTY_TARGET}
    raylib
)
if(TARGET zig_build_lib_vt)
    add_dependencies(myterm_lib zig_build_lib_vt)
endif()

if(WIN32)
    target_link_libraries(myterm_lib PUBLIC
        ws2_32
        user32
        gdi32
        shell32
        advapi32
    )
    target_compile_definitions(myterm_lib PUBLIC
        _WIN32_WINNT=0x0A00
        UNICODE
        _UNICODE
    )
endif()

if(MSVC)
    target_compile_options(myterm_lib PRIVATE /W4)
else()
    target_compile_options(myterm_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --------------------------------------------------------------------------
# myterm executable
# --------------------------------------------------------------------------
add_executable(myterm src/main.c)
target_link_libraries(myterm PRIVATE myterm_lib)

# --------------------------------------------------------------------------
# Tests  (standalone via tests/Makefile — no ghostty/raylib needed)
# --------------------------------------------------------------------------
# CMake tests link against myterm_lib and thus need the full build.
# For fast CI testing, use the standalone Makefile: cd tests && make test
if(MYTERM_BUILD_TESTS)
    enable_testing()

    add_executable(test_tabs     tests/test_tabs.c)
    add_executable(test_config   tests/test_config.c)
    add_executable(test_search   tests/test_search.c)
    add_executable(test_splits   tests/test_splits.c)

    target_link_libraries(test_tabs     PRIVATE myterm_lib)
    target_link_libraries(test_config   PRIVATE myterm_lib)
    target_link_libraries(test_search   PRIVATE myterm_lib)
    target_link_libraries(test_splits   PRIVATE myterm_lib)

    add_test(NAME tabs     COMMAND test_tabs)
    add_test(NAME config   COMMAND test_config)
    add_test(NAME search   COMMAND test_search)
    add_test(NAME splits   COMMAND test_splits)
endif()
