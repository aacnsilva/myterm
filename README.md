# myterm

A terminal emulator for Windows 11 powered by [libghostty-vt](https://github.com/ghostty-org/ghostty).

## Architecture

```
┌─────────────┐     ┌───────────────┐     ┌─────────┐
│  ConPTY      │<--->│ libghostty-vt │<--->│ Raylib  │
│ (Win32 PTY)  │     │ (VT parsing)  │     │ (render)│
└─────────────┘     └───────────────┘     └─────────┘
```

- **ConPTY** — Windows Pseudo Console API for spawning and communicating with shell processes (PowerShell, cmd.exe)
- **libghostty-vt** — Zero-dependency VT sequence parser and terminal state manager from the Ghostty project
- **Raylib** — Cross-platform 2D rendering for drawing the terminal grid, cursor, and scrollbar

## Features

- **Tabs** — Create, close, switch, reorder, and rename terminal tabs
  - `Ctrl+Shift+T` — New tab
  - `Ctrl+Shift+W` — Close tab
  - `Ctrl+Tab` / `Ctrl+Shift+Tab` — Next/previous tab
  - `Ctrl+1-9` — Jump to tab N
  - Activity indicator for background tabs with new output
  - Click-to-select and close button on hover

- **Split Panes** — Horizontal and vertical splits with independent terminals
  - `Ctrl+Shift+D` — Split vertically
  - `Ctrl+Shift+E` — Split horizontally
  - Focus navigation between panes
  - Adjustable split ratios

- **In-terminal Search** — Find text in terminal output
  - `Ctrl+Shift+F` — Toggle search bar
  - `Enter` / `Shift+Enter` — Next/previous match
  - Live search with match highlighting and count

- **Clipboard** — Copy and paste support
  - `Ctrl+Shift+C` — Copy selected text
  - `Ctrl+Shift+V` — Paste from clipboard

- **Themes** — 7 built-in color schemes
  - Catppuccin Mocha (default), Dracula, Nord, Solarized Dark, Gruvbox Dark, One Dark, Tokyo Night
  - Full 16-color ANSI palette per theme
  - Themed tab bar, search bar, scrollbar, cursor, and selection

- **Configuration** — Settings file with simple key=value format
  - `~/.config/myterm/config` (Linux/macOS) or `%APPDATA%\myterm\config` (Windows)
  - Font, font size, shell, theme, scrollback, cursor shape, and more

- **Terminal Emulation** — Full VT100/VT220/xterm via libghostty-vt
  - TrueColor (24-bit) and 256-color palette
  - Bold, italic, underline, strikethrough text styles
  - Block, bar, and underline cursor shapes
  - Mouse tracking (click, scroll)
  - Scrollback buffer (configurable, default 10,000 lines)
  - Window resize with automatic terminal reflow
  - Focus tracking events

## UI Layout

```
┌──────────────────────────────────────────────┐
│ [Shell 1] [Shell 2] [Shell 3]  [+]          │ <- Tab Bar
├──────────────────────────────────────────────┤
│                                              │
│  $ echo "Hello, myterm!"                     │
│  Hello, myterm!                              │
│  $  █                                        │ <- Terminal
│                                              │
│                                              │
├──────────────────────────────────────────────┤
│ Find: [search query___]  2/5  Enter  Esc    │ <- Search Bar
└──────────────────────────────────────────────┘
```

## Prerequisites

- **CMake** 3.19+
- **Ninja** build system
- **Zig** 0.15.x (on PATH) — required to build libghostty-vt
- **C compiler** — MSVC (Visual Studio 2019+) or GCC/MinGW-w64
- **Windows 11** (or Windows 10 1809+)

## Building

```bash
cmake -B build -G Ninja
cmake --build build
./build/myterm
```

### Build options

| Option | Default | Description |
|--------|---------|-------------|
| `MYTERM_USE_SYSTEM_RAYLIB` | `OFF` | Use system-installed raylib |
| `MYTERM_BUILD_TESTS` | `ON` | Build unit tests |
| `GHOSTTY_SOURCE_DIR` | *(auto-fetched)* | Path to local Ghostty source |

## Testing

Tests can be run standalone without libghostty or Raylib:

```bash
cd tests
make test
```

Or via CMake/CTest (requires full build):

```bash
cmake -B build -G Ninja
cmake --build build
cd build && ctest
```

### Test suites

| Suite | Tests | Coverage |
|-------|-------|----------|
| `test_tabs` | 17 | Tab create/close/select/reorder/move/activity |
| `test_config` | 9 | Config defaults, file parsing, theme lookup |
| `test_search` | 8 | Search open/close/query/navigation |
| `test_splits` | 7 | Split create/layout/focus/resize |

## Configuration

Example `~/.config/myterm/config`:

```ini
# Font
font = C:\Windows\Fonts\CascadiaMono.ttf
font_size = 16.0

# Window
width = 1280
height = 800

# Terminal
scrollback = 10000
cursor_shape = block
cursor_blink = true

# Theme (Catppuccin Mocha, Dracula, Nord, Solarized Dark,
#         Gruvbox Dark, One Dark, Tokyo Night)
theme = Catppuccin Mocha

# Shell
shell = powershell.exe

# Behavior
copy_on_select = false
confirm_close = true
```

## Project structure

```
myterm/
├── CMakeLists.txt
├── README.md
├── src/
│   ├── myterm.h              # Public API and types
│   ├── terminal_internal.h   # Internal ghostty handle accessors
│   ├── main.c                # Entry point, main loop, keyboard shortcuts
│   ├── terminal.c            # libghostty-vt wrapper
│   ├── renderer.c            # Raylib renderer (terminal, tab bar, search bar)
│   ├── input.c               # Keyboard/mouse input → VT sequences
│   ├── tabs.h / tabs.c       # Tab management (create, close, switch, reorder)
│   ├── config.h / config.c   # Configuration, themes, config file parsing
│   ├── search.h / search.c   # In-terminal text search
│   ├── splits.h / splits.c   # Split pane management (binary tree)
│   ├── pty_windows.c         # Windows ConPTY backend
│   └── pty_unix.c            # Unix PTY backend (dev/test)
└── tests/
    ├── test_harness.h         # Minimal test framework
    ├── stubs.c                # Mock terminal/PTY for unit tests
    ├── Makefile               # Standalone test runner
    ├── test_tabs.c            # Tab management tests
    ├── test_config.c          # Config/theme tests
    ├── test_search.c          # Search tests
    └── test_splits.c          # Split pane tests
```

## License

MIT
