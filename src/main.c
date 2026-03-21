/*
 * myterm — A terminal emulator for Windows 11 powered by libghostty-vt.
 *
 * Architecture:
 *   PTY (ConPTY on Windows) ←→ libghostty-vt (VT parsing) ←→ Raylib (rendering)
 *
 * The main loop:
 *   1. Read bytes from the PTY (non-blocking)
 *   2. Feed them to the ghostty terminal for VT parsing
 *   3. Process keyboard/mouse input, encode to VT sequences, write to PTY
 *   4. Render the terminal state via Raylib
 */

#include "myterm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Initial terminal dimensions */
    int cols = MYTERM_DEFAULT_COLS;
    int rows = MYTERM_DEFAULT_ROWS;

    /* --- Create components --- */
    MtRenderer *renderer = mt_renderer_new(
        MYTERM_DEFAULT_WIDTH, MYTERM_DEFAULT_HEIGHT, MYTERM_WINDOW_TITLE);
    if (!renderer) {
        fprintf(stderr, "myterm: failed to create renderer\n");
        return 1;
    }

    /* Compute initial grid size from window and cell dimensions */
    float cw = mt_renderer_cell_width(renderer);
    float ch = mt_renderer_cell_height(renderer);
    if (cw > 0 && ch > 0) {
        cols = (int)(MYTERM_DEFAULT_WIDTH  / cw);
        rows = (int)(MYTERM_DEFAULT_HEIGHT / ch);
        if (cols < 1) cols = 1;
        if (rows < 1) rows = 1;
    }

    MtTerminal *terminal = mt_terminal_new(cols, rows);
    if (!terminal) {
        fprintf(stderr, "myterm: failed to create terminal\n");
        mt_renderer_destroy(renderer);
        return 1;
    }

    MtPty *pty = mt_pty_new(cols, rows);
    if (!pty) {
        fprintf(stderr, "myterm: failed to create PTY\n");
        mt_terminal_destroy(terminal);
        mt_renderer_destroy(renderer);
        return 1;
    }

    /* --- Main loop --- */
    char read_buf[16384];
    int prev_cols = cols;
    int prev_rows = rows;

    while (mt_pty_is_alive(pty)) {
        /* 1. Read PTY output (non-blocking) */
        int n = mt_pty_read(pty, read_buf, sizeof(read_buf));
        if (n > 0) {
            mt_terminal_feed(terminal, read_buf, (size_t)n);
        } else if (n < 0) {
            break; /* child exited or error */
        }

        /* 2. Process user input */
        mt_input_process(terminal, pty);

        /* 3. Handle window resize → terminal resize */
        float new_cw = mt_renderer_cell_width(renderer);
        float new_ch = mt_renderer_cell_height(renderer);
        if (new_cw > 0 && new_ch > 0) {
            /* GetScreenWidth/Height are Raylib globals available after BeginDrawing */
            int new_cols = (int)(MYTERM_DEFAULT_WIDTH  / new_cw);
            int new_rows = (int)(MYTERM_DEFAULT_HEIGHT / new_ch);
            if (new_cols < 1) new_cols = 1;
            if (new_rows < 1) new_rows = 1;

            if (new_cols != prev_cols || new_rows != prev_rows) {
                mt_terminal_resize(terminal, new_cols, new_rows);
                mt_pty_resize(pty, new_cols, new_rows);
                prev_cols = new_cols;
                prev_rows = new_rows;
            }
        }

        /* 4. Render frame (returns false when window is closed) */
        if (!mt_renderer_frame(renderer, terminal)) {
            break;
        }
    }

    /* --- Cleanup --- */
    mt_pty_destroy(pty);
    mt_terminal_destroy(terminal);
    mt_renderer_destroy(renderer);

    return 0;
}
