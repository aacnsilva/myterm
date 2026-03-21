# myterm

A terminal emulator for Windows 11 powered by [libghostty-vt](https://github.com/ghostty-org/ghostty).

## Architecture

```
┌─────────────┐     ┌───────────────┐     ┌─────────┐
│  ConPTY      │◄───►│ libghostty-vt │◄───►│ Raylib  │
│ (Win32 PTY)  │     │ (VT parsing)  │     │ (render)│
└─────────────┘     └───────────────┘     └─────────┘
```

- **ConPTY** — Windows Pseudo Console API for spawning and communicating with shell processes (PowerShell, cmd.exe)
- **libghostty-vt** — Zero-dependency VT sequence parser and terminal state manager from the Ghostty project
- **Raylib** — Cross-platform 2D rendering for drawing the terminal grid, cursor, and scrollbar

## Prerequisites

- **CMake** 3.19+
- **Ninja** build system
- **Zig** 0.15.x (on PATH) — required to build libghostty-vt
- **C compiler** — MSVC (Visual Studio 2019+) or MinGW-w64
- **Windows 11** (or Windows 10 1809+)

## Building

```bash
# Configure
cmake -B build -G Ninja

# Build
cmake --build build

# Run
./build/myterm
```

### Build options

| Option | Default | Description |
|--------|---------|-------------|
| `MYTERM_USE_SYSTEM_RAYLIB` | `OFF` | Use system-installed raylib instead of fetching |
| `GHOSTTY_SOURCE_DIR` | *(auto-fetched)* | Path to local Ghostty source checkout |

### Using a local Ghostty checkout

```bash
cmake -B build -G Ninja -DGHOSTTY_SOURCE_DIR=/path/to/ghostty
cmake --build build
```

## Project structure

```
myterm/
├── CMakeLists.txt          # Build system
├── README.md
└── src/
    ├── myterm.h            # Public API and types
    ├── terminal_internal.h # Internal ghostty handle accessors
    ├── main.c              # Entry point and main loop
    ├── terminal.c          # libghostty-vt wrapper
    ├── renderer.c          # Raylib-based terminal renderer
    ├── input.c             # Keyboard/mouse input handling
    ├── pty_windows.c       # Windows ConPTY backend
    └── pty_unix.c          # Unix PTY backend (dev/test)
```

## Features

- Full VT100/VT220/xterm terminal emulation via libghostty-vt
- Windows ConPTY integration for native shell support
- TrueColor (24-bit) and 256-color palette support
- Bold, italic, underline, and strikethrough text styles
- Block, bar, and underline cursor shapes
- Mouse tracking (click, scroll)
- Scrollback buffer (configurable, default 10,000 lines)
- Scrollbar overlay
- Window resize with automatic terminal reflow
- Focus tracking events

## License

MIT
