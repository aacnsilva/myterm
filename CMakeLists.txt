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
# Ghostty's CMakeLists.txt creates an IMPORTED SHARED library target
# called "ghostty-vt" with proper include dirs and library location.
# Requires: zig 0.15.x on PATH.
#
# Reference: https://github.com/ghostty-org/ghostling
# --------------------------------------------------------------------------
include(FetchContent)

# Pin to a known-good commit for reproducible builds.
# Update this hash when upgrading ghostty.
FetchContent_Declare(ghostty
    GIT_REPOSITORY https://github.com/ghostty-org/ghostty.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ghostty)

# Ghostty's CMakeLists.txt provides two targets:
#   ghostty-vt        — SHARED IMPORTED (.dll on Windows)
#   ghostty-vt-static — STATIC IMPORTED (.lib on Windows)
# On Windows, linking against the shared target requires an import library
# (.lib) that Zig does not produce.  Use the static target instead, which
# also simplifies distribution (no DLL to ship alongside the .exe).
if(WIN32)
    set(GHOSTTY_TARGET ghostty-vt-static)
else()
    set(GHOSTTY_TARGET ghostty-vt)
endif()

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
)

target_link_libraries(myterm_lib PUBLIC
    ${GHOSTTY_TARGET}
    raylib
)

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
