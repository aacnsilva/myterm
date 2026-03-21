/*
 * myterm — A terminal emulator for Windows 11 powered by libghostty-vt.
 *
 * Architecture:
 *   PTY (ConPTY on Windows) <-> libghostty-vt (VT parsing) <-> Raylib (rendering)
 *
 * Features:
 *   - Tabs (Ctrl+Shift+T new, Ctrl+Shift+W close, Ctrl+Tab/Ctrl+Shift+Tab switch)
 *   - Split panes (Ctrl+Shift+D vertical, Ctrl+Shift+E horizontal)
 *   - In-terminal search (Ctrl+Shift+F)
 *   - Copy/paste (Ctrl+Shift+C / Ctrl+Shift+V)
 *   - Configurable themes and settings
 */

#include "myterm.h"
#include "tabs.h"
#include "config.h"
#include "search.h"
#include "splits.h"
#include <raylib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Forward declarations for UI rendering */
void mt_render_tab_bar(MtRenderer *r, MtTabManager *tabs, const MtTheme *theme);
void mt_render_search_bar(MtRenderer *r, MtSearch *search, const MtTheme *theme);

/* Check if Ctrl+Shift+<key> is pressed */
static bool ctrl_shift_pressed(int key)
{
    return (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
           (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
           IsKeyPressed(key);
}

static bool ctrl_pressed(int key)
{
    return (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
           !(IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
           IsKeyPressed(key);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    /* Load configuration */
    MtConfig config = mt_config_load();

    /* --- Create components --- */
    MtRenderer *renderer = mt_renderer_new(
        config.initial_width, config.initial_height, MYTERM_WINDOW_TITLE);
    if (!renderer) {
        fprintf(stderr, "myterm: failed to create renderer\n");
        return 1;
    }

    /* Compute initial grid size */
    float cw = mt_renderer_cell_width(renderer);
    float ch = mt_renderer_cell_height(renderer);
    int cols = MYTERM_DEFAULT_COLS;
    int rows = MYTERM_DEFAULT_ROWS;
    if (cw > 0 && ch > 0) {
        float usable_h = (float)config.initial_height - MT_TAB_BAR_HEIGHT;
        cols = (int)((float)config.initial_width / cw);
        rows = (int)(usable_h / ch);
        if (cols < 1) cols = 1;
        if (rows < 1) rows = 1;
    }

    /* Create tab manager and first tab */
    MtTabManager *tabs = mt_tabs_new();
    if (!tabs) {
        fprintf(stderr, "myterm: failed to create tab manager\n");
        mt_renderer_destroy(renderer);
        return 1;
    }

    if (mt_tabs_add(tabs, cols, rows) < 0) {
        fprintf(stderr, "myterm: failed to create initial tab\n");
        mt_tabs_destroy(tabs);
        mt_renderer_destroy(renderer);
        return 1;
    }

    /* Create search */
    MtSearch *search = mt_search_new();

    /* --- Main loop --- */
    char read_buf[16384];
    int prev_cols = cols;
    int prev_rows = rows;

    while (true) {
        MtTab *active = mt_tabs_active(tabs);
        if (!active) break;

        /* Read PTY output for ALL tabs (so background tabs process output) */
        for (int i = 0; i < mt_tabs_count(tabs); i++) {
            MtTab *tab = mt_tabs_get(tabs, i);
            if (!tab || !mt_pty_is_alive(tab->pty)) continue;

            int n = mt_pty_read(tab->pty, read_buf, sizeof(read_buf));
            if (n > 0) {
                mt_terminal_feed(tab->terminal, read_buf, (size_t)n);
                if (i != mt_tabs_active_index(tabs)) {
                    mt_tabs_mark_activity(tabs, i);
                }
            } else if (n < 0) {
                /* Child exited — close this tab */
                if (mt_tabs_count(tabs) > 1) {
                    mt_tabs_close(tabs, i);
                    i--;
                    continue;
                } else {
                    goto done;
                }
            }
        }

        /* Refresh active tab reference after possible closes */
        active = mt_tabs_active(tabs);
        if (!active) break;

        /* --- Handle keyboard shortcuts (before passing to terminal) --- */

        /* Ctrl+Shift+T: New tab */
        if (ctrl_shift_pressed(KEY_T)) {
            mt_tabs_add(tabs, prev_cols, prev_rows);
            mt_tabs_select(tabs, mt_tabs_count(tabs) - 1);
            goto render;
        }

        /* Ctrl+Shift+W: Close current tab */
        if (ctrl_shift_pressed(KEY_W)) {
            if (mt_tabs_count(tabs) > 1) {
                mt_tabs_close(tabs, mt_tabs_active_index(tabs));
            } else {
                goto done;
            }
            goto render;
        }

        /* Ctrl+Tab: Next tab */
        if (ctrl_pressed(KEY_TAB)) {
            mt_tabs_select_next(tabs);
            goto render;
        }

        /* Ctrl+Shift+Tab: Previous tab (use KEY_TAB with shift check) */
        if (ctrl_shift_pressed(KEY_TAB)) {
            mt_tabs_select_prev(tabs);
            goto render;
        }

        /* Ctrl+1-9: Switch to tab N */
        for (int n = KEY_ONE; n <= KEY_NINE; n++) {
            if (ctrl_pressed(n)) {
                int idx = n - KEY_ONE;
                if (idx < mt_tabs_count(tabs)) {
                    mt_tabs_select(tabs, idx);
                }
                break;
            }
        }

        /* Ctrl+Shift+F: Toggle search */
        if (ctrl_shift_pressed(KEY_F)) {
            if (mt_search_is_active(search)) {
                mt_search_close(search);
            } else {
                mt_search_open(search);
            }
            goto render;
        }

        /* Search mode input handling */
        if (mt_search_is_active(search)) {
            /* Escape to close search */
            if (IsKeyPressed(KEY_ESCAPE)) {
                mt_search_close(search);
                goto render;
            }

            /* Enter: next match, Shift+Enter: prev match */
            if (IsKeyPressed(KEY_ENTER)) {
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                    mt_search_prev(search);
                } else {
                    mt_search_next(search);
                }
                goto render;
            }

            /* Typing into search box */
            int ch;
            while ((ch = GetCharPressed()) != 0) {
                char query[MT_SEARCH_MAX_QUERY];
                strncpy(query, mt_search_get_query(search), MT_SEARCH_MAX_QUERY - 1);
                query[MT_SEARCH_MAX_QUERY - 1] = '\0';
                size_t len = strlen(query);
                if (ch >= 32 && len < MT_SEARCH_MAX_QUERY - 2) {
                    query[len] = (char)ch;
                    query[len + 1] = '\0';
                    mt_search_set_query(search, query);
                    mt_search_execute(search, active->terminal);
                }
            }

            /* Backspace in search */
            if (IsKeyPressed(KEY_BACKSPACE)) {
                char query[MT_SEARCH_MAX_QUERY];
                strncpy(query, mt_search_get_query(search), MT_SEARCH_MAX_QUERY - 1);
                query[MT_SEARCH_MAX_QUERY - 1] = '\0';
                size_t len = strlen(query);
                if (len > 0) {
                    query[len - 1] = '\0';
                    mt_search_set_query(search, query);
                    mt_search_execute(search, active->terminal);
                }
            }

            goto render;
        }

        /* Ctrl+Shift+C: Copy */
        if (ctrl_shift_pressed(KEY_C)) {
            /* TODO: Copy selected text to clipboard via SetClipboardText() */
            goto render;
        }

        /* Ctrl+Shift+V: Paste */
        if (ctrl_shift_pressed(KEY_V)) {
            const char *clip = GetClipboardText();
            if (clip && *clip) {
                mt_pty_write(active->pty, clip, strlen(clip));
            }
            goto render;
        }

        /* Pass input to active terminal */
        mt_input_process(active->terminal, active->pty);

        /* Handle window resize */
        if (IsWindowResized()) {
            int sw = GetScreenWidth();
            int sh = GetScreenHeight();
            float usable_h = (float)sh - MT_TAB_BAR_HEIGHT;
            if (cw > 0 && ch > 0) {
                int new_cols = (int)((float)sw / cw);
                int new_rows = (int)(usable_h / ch);
                if (new_cols < 1) new_cols = 1;
                if (new_rows < 1) new_rows = 1;

                if (new_cols != prev_cols || new_rows != prev_rows) {
                    /* Resize all tabs */
                    for (int i = 0; i < mt_tabs_count(tabs); i++) {
                        MtTab *tab = mt_tabs_get(tabs, i);
                        if (tab) {
                            mt_terminal_resize(tab->terminal, new_cols, new_rows);
                            mt_pty_resize(tab->pty, new_cols, new_rows);
                        }
                    }
                    prev_cols = new_cols;
                    prev_rows = new_rows;
                }
            }
        }

render:
        /* Render frame */
        if (!mt_renderer_frame_full(renderer, tabs, search, &config.theme)) {
            break;
        }
    }

done:
    /* --- Cleanup --- */
    mt_search_destroy(search);
    mt_tabs_destroy(tabs);
    mt_renderer_destroy(renderer);

    return 0;
}
