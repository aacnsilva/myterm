cmake_minimum_required(VERSION 3.19)
project(myterm C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# --------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------
option(MYTERM_USE_SYSTEM_RAYLIB "Use system-installed raylib instead of FetchContent" OFF)

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
# myterm executable
# --------------------------------------------------------------------------
add_executable(myterm
    src/main.c
    src/terminal.c
    src/renderer.c
    src/input.c
)

if(WIN32)
    target_sources(myterm PRIVATE src/pty_windows.c)
else()
    target_sources(myterm PRIVATE src/pty_unix.c)
endif()

target_include_directories(myterm PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${GHOSTTY_SOURCE_DIR}/include
)

target_link_libraries(myterm PRIVATE
    ghostty_vt
    raylib
)

# Windows-specific libraries
if(WIN32)
    target_link_libraries(myterm PRIVATE
        ws2_32
        user32
        gdi32
        shell32
        advapi32
    )
    target_compile_definitions(myterm PRIVATE
        _WIN32_WINNT=0x0A00   # Windows 10+ (ConPTY)
        UNICODE
        _UNICODE
    )
endif()

# Compiler warnings
if(MSVC)
    target_compile_options(myterm PRIVATE /W4)
else()
    target_compile_options(myterm PRIVATE -Wall -Wextra -Wpedantic)
endif()
