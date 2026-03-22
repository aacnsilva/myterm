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

MtPty *mt_pty_new(int cols, int rows);
void   mt_pty_destroy(MtPty *pty);
int    mt_pty_read(MtPty *pty, char *buf, size_t len);
int    mt_pty_write(MtPty *pty, const char *buf, size_t len);
void   mt_pty_resize(MtPty *pty, int cols, int rows);
bool   mt_pty_is_alive(MtPty *pty);

/* --------------------------------------------------------------------------
 * Terminal state  (wraps libghostty-vt)
 * -------------------------------------------------------------------------- */
typedef struct MtTerminal MtTerminal;

MtTerminal *mt_terminal_new(int cols, int rows);
void        mt_terminal_destroy(MtTerminal *term);
void        mt_terminal_feed(MtTerminal *term, const char *data, size_t len);
void        mt_terminal_resize(MtTerminal *term, int cols, int rows);
int         mt_terminal_cols(const MtTerminal *term);
int         mt_terminal_rows(const MtTerminal *term);

/* --------------------------------------------------------------------------
 * Renderer  (Raylib-based)
 * -------------------------------------------------------------------------- */
typedef struct MtRenderer MtRenderer;

/* Forward declarations for renderer dependencies */
typedef struct MtTabManager MtTabManager;
typedef struct MtSearch MtSearch;
typedef struct MtTheme MtTheme;
typedef struct MtConfig MtConfig;

MtRenderer *mt_renderer_new(int width, int height, const char *title);
void        mt_renderer_destroy(MtRenderer *r);

/* Single-terminal render (backward compat) */
bool mt_renderer_frame(MtRenderer *r, MtTerminal *term);

/* Full render with tabs, search, theming */
bool mt_renderer_frame_full(MtRenderer *r, MtTabManager *tabs,
                            MtSearch *search, const struct MtConfig *config);

float mt_renderer_cell_width(const MtRenderer *r);
float mt_renderer_cell_height(const MtRenderer *r);
bool  mt_renderer_copy_selection(MtRenderer *r);
void  mt_renderer_clear_selection(MtRenderer *r);
bool  mt_renderer_has_selection(const MtRenderer *r);

/* --------------------------------------------------------------------------
 * Input handling
 * -------------------------------------------------------------------------- */
int mt_input_process(MtTerminal *term, MtPty *pty);

#endif /* MYTERM_H */
