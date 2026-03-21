#ifndef MYTERM_H
#define MYTERM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* --------------------------------------------------------------------------
 * Configuration constants
 * -------------------------------------------------------------------------- */
#define MYTERM_DEFAULT_COLS       80
#define MYTERM_DEFAULT_ROWS       24
#define MYTERM_MAX_SCROLLBACK     10000
#define MYTERM_FONT_SIZE          16.0f
#define MYTERM_WINDOW_TITLE       "myterm"
#define MYTERM_DEFAULT_WIDTH      960
#define MYTERM_DEFAULT_HEIGHT     640

/* --------------------------------------------------------------------------
 * Color type (platform-independent RGBA)
 * -------------------------------------------------------------------------- */
typedef struct {
    uint8_t r, g, b, a;
} MtColor;

/* --------------------------------------------------------------------------
 * PTY abstraction  (implemented per-platform)
 * -------------------------------------------------------------------------- */
typedef struct MtPty MtPty;

/* Create a new PTY with the given initial size.
 * On Windows this uses ConPTY; on Unix it uses forkpty(). */
MtPty *mt_pty_new(int cols, int rows);
void   mt_pty_destroy(MtPty *pty);

/* Non-blocking read from the PTY.  Returns bytes read, 0 if nothing
 * available, or -1 on error / child exit. */
int    mt_pty_read(MtPty *pty, char *buf, size_t len);

/* Write user input to the PTY. */
int    mt_pty_write(MtPty *pty, const char *buf, size_t len);

/* Notify the PTY of a terminal resize. */
void   mt_pty_resize(MtPty *pty, int cols, int rows);

/* Check if the child process is still alive. */
bool   mt_pty_is_alive(MtPty *pty);

/* --------------------------------------------------------------------------
 * Terminal state  (wraps libghostty-vt)
 * -------------------------------------------------------------------------- */
typedef struct MtTerminal MtTerminal;

MtTerminal *mt_terminal_new(int cols, int rows);
void        mt_terminal_destroy(MtTerminal *term);

/* Feed raw bytes (from PTY) into the VT parser. */
void mt_terminal_feed(MtTerminal *term, const char *data, size_t len);

/* Resize the virtual terminal. */
void mt_terminal_resize(MtTerminal *term, int cols, int rows);

/* Query current dimensions. */
int  mt_terminal_cols(const MtTerminal *term);
int  mt_terminal_rows(const MtTerminal *term);

/* --------------------------------------------------------------------------
 * Renderer  (Raylib-based)
 * -------------------------------------------------------------------------- */
typedef struct MtRenderer MtRenderer;

MtRenderer *mt_renderer_new(int width, int height, const char *title);
void        mt_renderer_destroy(MtRenderer *r);

/* Render one frame.  Returns false if the window was closed. */
bool mt_renderer_frame(MtRenderer *r, MtTerminal *term);

/* Get cell dimensions for the current font. */
float mt_renderer_cell_width(const MtRenderer *r);
float mt_renderer_cell_height(const MtRenderer *r);

/* --------------------------------------------------------------------------
 * Input handling
 * -------------------------------------------------------------------------- */

/* Process Raylib input events and write encoded VT sequences to the PTY.
 * Returns the number of bytes written. */
int mt_input_process(MtTerminal *term, MtPty *pty);

#endif /* MYTERM_H */
