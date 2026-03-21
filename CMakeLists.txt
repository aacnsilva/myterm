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
# libghostty-vt  (built via Zig, pulled as a CMake ExternalProject)
# --------------------------------------------------------------------------
include(ExternalProject)
include(FetchContent)

# Ghostty source — we only need the libghostty-vt portion.
# Users can override GHOSTTY_SOURCE_DIR to point at a local checkout.
if(NOT DEFINED GHOSTTY_SOURCE_DIR)
    FetchContent_Declare(
        ghostty
        GIT_REPOSITORY https://github.com/ghostty-org/ghostty.git
        GIT_TAG        main
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(ghostty)
    set(GHOSTTY_SOURCE_DIR ${ghostty_SOURCE_DIR})
endif()

# Build libghostty-vt static library via Zig
set(GHOSTTY_LIB_DIR ${CMAKE_BINARY_DIR}/ghostty-lib)
file(MAKE_DIRECTORY ${GHOSTTY_LIB_DIR})

# Detect Zig target triple for the host platform
if(WIN32)
    set(ZIG_TARGET "x86_64-windows")
elseif(APPLE)
    set(ZIG_TARGET "x86_64-macos")
else()
    set(ZIG_TARGET "x86_64-linux")
endif()

ExternalProject_Add(
    ghostty_vt_build
    SOURCE_DIR      ${GHOSTTY_SOURCE_DIR}
    CONFIGURE_COMMAND ""
    BUILD_COMMAND   zig build
                    -Demit-lib
                    -Doptimize=ReleaseFast
                    --prefix ${GHOSTTY_LIB_DIR}
    BUILD_IN_SOURCE TRUE
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS
        ${GHOSTTY_LIB_DIR}/lib/libghostty-vt.a
        ${GHOSTTY_LIB_DIR}/lib/ghostty-vt.lib
)

# Create imported target for libghostty-vt
add_library(ghostty_vt STATIC IMPORTED)
if(WIN32)
    set_target_properties(ghostty_vt PROPERTIES
        IMPORTED_LOCATION ${GHOSTTY_LIB_DIR}/lib/ghostty-vt.lib
    )
else()
    set_target_properties(ghostty_vt PROPERTIES
        IMPORTED_LOCATION ${GHOSTTY_LIB_DIR}/lib/libghostty-vt.a
    )
endif()
target_include_directories(ghostty_vt INTERFACE
    ${GHOSTTY_SOURCE_DIR}/include
)
add_dependencies(ghostty_vt ghostty_vt_build)

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
    ${GHOSTTY_SOURCE_DIR}/include
)

target_link_libraries(myterm_lib PUBLIC
    ghostty_vt
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
# Tests
# --------------------------------------------------------------------------
if(MYTERM_BUILD_TESTS)
    enable_testing()

    # Test executables
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
