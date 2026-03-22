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
           !(IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) &&
           IsKeyPressed(key);
}

static bool ctrl_alt_pressed(int key)
{
    return (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
           (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) &&
           !(IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
           IsKeyPressed(key);
}

static bool alt_shift_pressed(int key)
{
    return !(IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) &&
           (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) &&
           (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) &&
           IsKeyPressed(key);
}

static const MtSplitNode *read_split_output(const MtSplitNode *node, char *buf, size_t buf_len,
                                            bool mark_activity, bool *had_output)
{
    if (!node) return NULL;

    if (node->is_leaf) {
        if (!node->pty || !node->terminal) return NULL;

        int n = mt_pty_read(node->pty, buf, buf_len);
        if (n > 0) {
            mt_terminal_feed(node->terminal, buf, (size_t)n);
            if (mark_activity && had_output) *had_output = true;
        } else if (n < 0 || !mt_pty_is_alive(node->pty)) {
            return node;
        }
        return NULL;
    }

    const MtSplitNode *dead = read_split_output(node->first, buf, buf_len, mark_activity, had_output);
    if (dead) return dead;
    return read_split_output(node->second, buf, buf_len, mark_activity, had_output);
}

static void resize_split_tree(const MtSplitNode *node, int cols, int rows)
{
    if (!node) return;
    if (node->is_leaf) {
        if (node->terminal) mt_terminal_resize(node->terminal, cols, rows);
        if (node->pty) mt_pty_resize(node->pty, cols, rows);
        return;
    }
    resize_split_tree(node->first, cols, rows);
    resize_split_tree(node->second, cols, rows);
}

static void handle_tab_rename_input(MtTabManager *tabs)
{
    int active_index = mt_tabs_active_index(tabs);
    if (active_index < 0 || !mt_tabs_is_renaming(tabs, active_index)) return;

    if (IsKeyPressed(KEY_ESCAPE)) {
        mt_tabs_cancel_rename(tabs, active_index);
        return;
    }

    if (IsKeyPressed(KEY_ENTER)) {
        mt_tabs_commit_rename(tabs, active_index);
        return;
    }

    int ch;
    while ((ch = GetCharPressed()) != 0) {
        mt_tabs_rename_append(tabs, active_index, ch);
    }

    if (IsKeyPressed(KEY_BACKSPACE)) {
        mt_tabs_rename_backspace(tabs, active_index);
    }
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    MtConfig config = mt_config_load();

    if (config.shell[0] != '\0') {
#ifdef _WIN32
        _putenv_s("MYTERM_SHELL", config.shell);
#else
        setenv("MYTERM_SHELL", config.shell, 1);
#endif
    }

    MtRenderer *renderer = mt_renderer_new(
        config.initial_width, config.initial_height, MYTERM_WINDOW_TITLE);
    if (!renderer) {
        fprintf(stderr, "myterm: failed to create renderer\n");
        return 1;
    }

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

    MtSearch *search = mt_search_new();

    char read_buf[16384];
    int prev_cols = cols;
    int prev_rows = rows;

    while (true) {
        MtTab *active = mt_tabs_active(tabs);
        if (!active) break;

        for (int i = 0; i < mt_tabs_count(tabs); i++) {
            MtSplitManager *splits = mt_tabs_get_splits(tabs, i);
            if (!splits) continue;

            bool had_output = false;
            const MtSplitNode *dead_leaf = read_split_output(
                mt_splits_root(splits),
                read_buf, sizeof(read_buf),
                i != mt_tabs_active_index(tabs),
                &had_output
            );

            if (had_output) {
                mt_tabs_mark_activity(tabs, i);
            }

            if (dead_leaf) {
                if (mt_splits_count(splits) > 1) {
                    mt_splits_close_leaf(splits, dead_leaf);
                } else if (mt_tabs_count(tabs) > 1) {
                    mt_tabs_close(tabs, i);
                    i--;
                    continue;
                } else {
                    goto done;
                }
            }
        }

        active = mt_tabs_active(tabs);
        if (!active) break;

        if (mt_search_is_active(search)) {
            mt_search_execute(search, mt_tabs_active_terminal(tabs));
        }

        if (mt_tabs_is_renaming(tabs, mt_tabs_active_index(tabs))) {
            handle_tab_rename_input(tabs);
            goto render;
        }

        if (IsKeyPressed(KEY_F2) || ctrl_shift_pressed(KEY_R)) {
            mt_tabs_begin_rename(tabs, mt_tabs_active_index(tabs));
            goto render;
        }

        if (ctrl_shift_pressed(KEY_T)) {
            if (mt_tabs_add(tabs, prev_cols, prev_rows) >= 0) {
                mt_tabs_select(tabs, mt_tabs_count(tabs) - 1);
                mt_renderer_clear_selection(renderer);
            }
            goto render;
        }

        if (ctrl_shift_pressed(KEY_W)) {
            if (mt_tabs_count(tabs) > 1) {
                mt_tabs_close(tabs, mt_tabs_active_index(tabs));
            } else {
                goto done;
            }
            mt_renderer_clear_selection(renderer);
            goto render;
        }

        if (ctrl_shift_pressed(KEY_PAGE_UP)) {
            int idx = mt_tabs_active_index(tabs);
            if (idx > 0) {
                mt_tabs_move(tabs, idx, idx - 1);
                mt_tabs_select(tabs, idx - 1);
                mt_renderer_clear_selection(renderer);
            }
            goto render;
        }

        if (ctrl_shift_pressed(KEY_PAGE_DOWN)) {
            int idx = mt_tabs_active_index(tabs);
            if (idx >= 0 && idx < mt_tabs_count(tabs) - 1) {
                mt_tabs_move(tabs, idx, idx + 1);
                mt_tabs_select(tabs, idx + 1);
                mt_renderer_clear_selection(renderer);
            }
            goto render;
        }

        if (ctrl_pressed(KEY_TAB)) {
            mt_tabs_select_next(tabs);
            mt_renderer_clear_selection(renderer);
            goto render;
        }

        if (ctrl_shift_pressed(KEY_TAB)) {
            mt_tabs_select_prev(tabs);
            mt_renderer_clear_selection(renderer);
            goto render;
        }

        for (int n = KEY_ONE; n <= KEY_NINE; n++) {
            if (ctrl_pressed(n)) {
                int idx = n - KEY_ONE;
                if (idx < mt_tabs_count(tabs)) {
                    mt_tabs_select(tabs, idx);
                    mt_renderer_clear_selection(renderer);
                }
                goto render;
            }
        }

        MtSplitManager *active_splits = mt_tabs_active_splits(tabs);
        MtTerminal *focused_term = mt_tabs_active_terminal(tabs);
        MtPty *focused_pty = mt_tabs_active_pty(tabs);

        if (ctrl_shift_pressed(KEY_D)) {
            int split_cols = focused_term ? mt_terminal_cols(focused_term) : prev_cols;
            int split_rows = focused_term ? mt_terminal_rows(focused_term) : prev_rows;
            if (active_splits) {
                mt_splits_split(active_splits, MT_SPLIT_VERTICAL, split_cols, split_rows);
                mt_renderer_clear_selection(renderer);
            }
            goto render;
        }

        if (ctrl_shift_pressed(KEY_E)) {
            int split_cols = focused_term ? mt_terminal_cols(focused_term) : prev_cols;
            int split_rows = focused_term ? mt_terminal_rows(focused_term) : prev_rows;
            if (active_splits) {
                mt_splits_split(active_splits, MT_SPLIT_HORIZONTAL, split_cols, split_rows);
                mt_renderer_clear_selection(renderer);
            }
            goto render;
        }

        if (ctrl_alt_pressed(KEY_RIGHT) && active_splits) {
            mt_splits_focus_dir(active_splits, MT_SPLIT_VERTICAL, true);
            mt_renderer_clear_selection(renderer);
            goto render;
        }
        if (ctrl_alt_pressed(KEY_LEFT) && active_splits) {
            mt_splits_focus_dir(active_splits, MT_SPLIT_VERTICAL, false);
            mt_renderer_clear_selection(renderer);
            goto render;
        }
        if (ctrl_alt_pressed(KEY_DOWN) && active_splits) {
            mt_splits_focus_dir(active_splits, MT_SPLIT_HORIZONTAL, true);
            mt_renderer_clear_selection(renderer);
            goto render;
        }
        if (ctrl_alt_pressed(KEY_UP) && active_splits) {
            mt_splits_focus_dir(active_splits, MT_SPLIT_HORIZONTAL, false);
            mt_renderer_clear_selection(renderer);
            goto render;
        }

        if (alt_shift_pressed(KEY_RIGHT) && active_splits) {
            mt_splits_resize_dir(active_splits, MT_SPLIT_VERTICAL, true, 0.05f);
            goto render;
        }
        if (alt_shift_pressed(KEY_LEFT) && active_splits) {
            mt_splits_resize_dir(active_splits, MT_SPLIT_VERTICAL, false, 0.05f);
            goto render;
        }
        if (alt_shift_pressed(KEY_DOWN) && active_splits) {
            mt_splits_resize_dir(active_splits, MT_SPLIT_HORIZONTAL, true, 0.05f);
            goto render;
        }
        if (alt_shift_pressed(KEY_UP) && active_splits) {
            mt_splits_resize_dir(active_splits, MT_SPLIT_HORIZONTAL, false, 0.05f);
            goto render;
        }

        if (ctrl_alt_pressed(KEY_W) && active_splits) {
            mt_splits_close_focused(active_splits);
            mt_renderer_clear_selection(renderer);
            goto render;
        }

        if (ctrl_shift_pressed(KEY_F)) {
            if (mt_search_is_active(search)) {
                mt_search_close(search);
            } else {
                mt_search_open(search);
                mt_search_execute(search, focused_term);
            }
            goto render;
        }

        if (mt_search_is_active(search)) {
            if (IsKeyPressed(KEY_ESCAPE)) {
                mt_search_close(search);
                goto render;
            }

            if (IsKeyPressed(KEY_ENTER)) {
                if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
                    mt_search_prev(search);
                } else {
                    mt_search_next(search);
                }
                goto render;
            }

            int ch_in;
            while ((ch_in = GetCharPressed()) != 0) {
                char query[MT_SEARCH_MAX_QUERY];
                strncpy(query, mt_search_get_query(search), MT_SEARCH_MAX_QUERY - 1);
                query[MT_SEARCH_MAX_QUERY - 1] = '\0';
                size_t len = strlen(query);
                if (ch_in >= 32 && len < MT_SEARCH_MAX_QUERY - 2) {
                    query[len] = (char)ch_in;
                    query[len + 1] = '\0';
                    mt_search_set_query(search, query);
                    mt_search_execute(search, focused_term);
                }
            }

            if (IsKeyPressed(KEY_BACKSPACE)) {
                char query[MT_SEARCH_MAX_QUERY];
                strncpy(query, mt_search_get_query(search), MT_SEARCH_MAX_QUERY - 1);
                query[MT_SEARCH_MAX_QUERY - 1] = '\0';
                size_t len = strlen(query);
                if (len > 0) {
                    query[len - 1] = '\0';
                    mt_search_set_query(search, query);
                    mt_search_execute(search, focused_term);
                }
            }

            goto render;
        }

        if (ctrl_shift_pressed(KEY_C)) {
            mt_renderer_copy_selection(renderer);
            goto render;
        }

        if (ctrl_shift_pressed(KEY_V)) {
            const char *clip = GetClipboardText();
            if (clip && *clip && focused_pty) {
                mt_pty_write(focused_pty, clip, strlen(clip));
            }
            goto render;
        }

        if (focused_term && focused_pty) {
            mt_input_process(focused_term, focused_pty);
        }

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
                    for (int i = 0; i < mt_tabs_count(tabs); i++) {
                        MtSplitManager *splits = mt_tabs_get_splits(tabs, i);
                        if (splits) {
                            resize_split_tree(mt_splits_root(splits), new_cols, new_rows);
                        }
                    }
                    prev_cols = new_cols;
                    prev_rows = new_rows;
                }
            }
        }

render:
        if (!mt_renderer_frame_full(renderer, tabs, search, &config)) {
            break;
        }
    }

done:
    mt_search_destroy(search);
    mt_tabs_destroy(tabs);
    mt_renderer_destroy(renderer);

    return 0;
}
