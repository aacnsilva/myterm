/*
 * renderer.c — Raylib-based terminal renderer with tab bar, search, and splits.
 *
 * Layout:
 *   ┌─────────────────────────────────────┐
 *   │  Tab Bar (32px)                     │
 *   ├─────────────────────────────────────┤
 *   │                                     │
 *   │  Terminal Content                   │
 *   │  (with optional split panes)        │
 *   │                                     │
 *   ├─────────────────────────────────────┤
 *   │  Search Bar (when active, 32px)     │
 *   └─────────────────────────────────────┘
 */

#include "myterm.h"
#include "tabs.h"
#include "splits.h"
#include "config.h"
#include "search.h"
#include "terminal_internal.h"
#include <raylib.h>
#include <ghostty/ghostty.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "native_tabs_windows.h"

#define SEARCH_BAR_HEIGHT 32.0f
#define TAB_PADDING       12.0f
#define TAB_CLOSE_SIZE    14.0f
#define RENAME_CURSOR_PAD   2.0f
#define UI_DOUBLE_CLICK_S   0.35
#define PANE_INSET          4.0f
#define PANE_HEADER_HEIGHT 22.0f

typedef struct {
    int row0, col0;
    int row1, col1;
} MtSelectionRange;

struct MtRenderer {
    Font    font;
    float   cell_w;
    float   cell_h;
    float   font_size;
    bool    font_loaded;

    bool        has_selection;
    bool        selecting;
    MtTerminal *selection_term;
    int         sel_row0;
    int         sel_col0;
    int         sel_row1;
    int         sel_col1;

    bool   dragging_tab;
    int    drag_tab_index;
    int    last_tab_click;
    double last_tab_click_time;

#ifdef _WIN32
    struct MtNativeTabs *native_tabs;
#endif
};

static Color rgba_to_color(MtRgba c)
{
    return (Color){ c.r, c.g, c.b, c.a };
}

static Color ghostty_to_raylib_color(GhosttyColorRgb gc)
{
    return (Color){ gc.r, gc.g, gc.b, 255 };
}

static GhosttyColorRgb rgba_to_ghostty_rgb(MtRgba c)
{
    return (GhosttyColorRgb){ c.r, c.g, c.b };
}

static GhosttyColorRgb resolve_style_color(GhosttyStyleColor color,
                                           const GhosttyRenderStateColors *colors,
                                           GhosttyColorRgb fallback)
{
    switch (color.tag) {
    case GHOSTTY_STYLE_COLOR_RGB:
        return color.value.rgb;
    case GHOSTTY_STYLE_COLOR_PALETTE:
        return colors->palette[color.value.palette];
    default:
        return fallback;
    }
}

static int codepoint_to_utf8(uint32_t codepoint, char out[5])
{
    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        out[1] = '\0';
        return 1;
    }
    if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        out[2] = '\0';
        return 2;
    }
    if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        out[3] = '\0';
        return 3;
    }

    out[0] = (char)(0xF0 | (codepoint >> 18));
    out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[3] = (char)(0x80 | (codepoint & 0x3F));
    out[4] = '\0';
    return 4;
}

static void selection_clear(MtRenderer *r);

static int grapheme_to_utf8_len(const uint32_t *codepoints, uint32_t len,
                                char *out, size_t out_size)
{
    size_t used = 0;
    if (!out || out_size == 0) return 0;

    out[0] = '\0';
    for (uint32_t i = 0; i < len; i++) {
        char tmp[5] = {0};
        int n = codepoint_to_utf8(codepoints[i], tmp);
        if (used + (size_t)n >= out_size) break;
        memcpy(out + used, tmp, (size_t)n);
        used += (size_t)n;
    }
    out[used] = '\0';
    return (int)used;
}

static int *build_font_codepoints(int *out_count)
{
    if (out_count) *out_count = 0;

    const struct {
        int start;
        int end;
    } ranges[] = {
        { 0x0020, 0x00FF },
        { 0x0100, 0x024F },
        { 0x2000, 0x206F },
        { 0x2190, 0x21FF },
        { 0x2500, 0x259F },
        { 0x25A0, 0x25FF },
    };

    int total = 0;
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
        total += ranges[i].end - ranges[i].start + 1;
    }

    int *codepoints = calloc((size_t)total, sizeof(int));
    if (!codepoints) return NULL;

    int idx = 0;
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
        for (int cp = ranges[i].start; cp <= ranges[i].end; cp++) {
            codepoints[idx++] = cp;
        }
    }

    if (out_count) *out_count = idx;
    return codepoints;
}

MtRenderer *mt_renderer_new(int width, int height, const char *title)
{
    MtRenderer *r = calloc(1, sizeof(MtRenderer));
    if (!r) return NULL;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(width, height, title);
    SetTargetFPS(60);

    r->font_size = MYTERM_FONT_SIZE;
    r->last_tab_click = -1;

    const char *font_paths[] = {
        "C:\\Windows\\Fonts\\CascadiaMono.ttf",
        "C:\\Windows\\Fonts\\consola.ttf",
        "C:\\Windows\\Fonts\\lucon.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
        "/System/Library/Fonts/SFMono-Regular.otf",
        NULL
    };

    r->font_loaded = false;
    int font_codepoint_count = 0;
    int *font_codepoints = build_font_codepoints(&font_codepoint_count);
    for (int i = 0; font_paths[i] != NULL; i++) {
        if (FileExists(font_paths[i])) {
            r->font = LoadFontEx(font_paths[i], (int)r->font_size,
                                 font_codepoints, font_codepoint_count);
            if (r->font.glyphCount > 0) {
                r->font_loaded = true;
                SetTextureFilter(r->font.texture, TEXTURE_FILTER_BILINEAR);
                break;
            }
        }
    }
    free(font_codepoints);

    if (!r->font_loaded) {
        r->font = GetFontDefault();
    }

    Vector2 m = MeasureTextEx(r->font, "M", r->font_size, 0);
    r->cell_w = m.x;
    r->cell_h = r->font_size + 2.0f;

    return r;
}

void mt_renderer_destroy(MtRenderer *r)
{
    if (!r) return;
#ifdef _WIN32
    if (r->native_tabs) {
        mt_native_tabs_destroy(r->native_tabs);
        r->native_tabs = NULL;
    }
#endif
    if (r->font_loaded) {
        UnloadFont(r->font);
    }
    CloseWindow();
    free(r);
}

float mt_renderer_cell_width(const MtRenderer *r)
{
    return r ? r->cell_w : 0;
}

float mt_renderer_cell_height(const MtRenderer *r)
{
    return r ? r->cell_h : 0;
}

static void selection_clear(MtRenderer *r)
{
    if (!r) return;
    r->has_selection = false;
    r->selecting = false;
    r->selection_term = NULL;
    r->sel_row0 = r->sel_col0 = r->sel_row1 = r->sel_col1 = 0;
}

void mt_renderer_clear_selection(MtRenderer *r)
{
    selection_clear(r);
}

bool mt_renderer_has_selection(const MtRenderer *r)
{
    return r ? r->has_selection : false;
}

static MtSelectionRange selection_range(const MtRenderer *r)
{
    MtSelectionRange range = {0, 0, 0, 0};
    if (!r) return range;

    range.row0 = r->sel_row0;
    range.col0 = r->sel_col0;
    range.row1 = r->sel_row1;
    range.col1 = r->sel_col1;

    if (range.row0 > range.row1 ||
        (range.row0 == range.row1 && range.col0 > range.col1)) {
        int tmp;
        tmp = range.row0; range.row0 = range.row1; range.row1 = tmp;
        tmp = range.col0; range.col0 = range.col1; range.col1 = tmp;
    }

    return range;
}

static bool cell_selected(const MtRenderer *r, MtTerminal *term, int row, int col, int span)
{
    if (!r || !r->has_selection || r->selection_term != term) return false;

    MtSelectionRange range = selection_range(r);
    if (row < range.row0 || row > range.row1) return false;

    int row_start = (row == range.row0) ? range.col0 : 0;
    int row_end = (row == range.row1) ? range.col1 : 1000000;

    return col < row_end && (col + span) > row_start;
}

static bool point_in_rect(Vector2 p, float x, float y, float w, float h)
{
    return p.x >= x && p.x < x + w && p.y >= y && p.y < y + h;
}

static bool pixel_to_cell(const MtRenderer *r, MtTerminal *term,
                          float ox, float oy, float area_w, float area_h,
                          Vector2 mouse, int *out_row, int *out_col)
{
    if (!r || !term || !out_row || !out_col) return false;
    if (!point_in_rect(mouse, ox, oy, area_w, area_h)) return false;

    int cols = mt_terminal_cols(term);
    int rows = mt_terminal_rows(term);
    if (cols <= 0 || rows <= 0) return false;

    int col = (int)((mouse.x - ox) / r->cell_w);
    int row = (int)((mouse.y - oy) / r->cell_h);
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (col >= cols) col = cols - 1;
    if (row >= rows) row = rows - 1;

    *out_row = row;
    *out_col = col;
    return true;
}

static bool terminal_mouse_tracking(MtTerminal *term)
{
    if (!term) return false;
    GhosttyTerminal vt = mt_terminal_get_vt(term);
    bool mouse_tracking = false;
    ghostty_terminal_get(vt, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &mouse_tracking);
    return mouse_tracking;
}

static void handle_terminal_selection(MtRenderer *r, MtTerminal *term,
                                      float ox, float oy, float area_w, float area_h,
                                      bool copy_on_select)
{
    if (!r || !term) return;
    if (terminal_mouse_tracking(term)) return;

    Vector2 mouse = GetMousePosition();

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        int row = 0, col = 0;
        if (pixel_to_cell(r, term, ox, oy, area_w, area_h, mouse, &row, &col)) {
            r->has_selection = true;
            r->selecting = true;
            r->selection_term = term;
            r->sel_row0 = r->sel_row1 = row;
            r->sel_col0 = r->sel_col1 = col;
        } else if (!point_in_rect(mouse, 0, 0, (float)GetScreenWidth(), MT_TAB_BAR_HEIGHT)) {
            selection_clear(r);
        }
    }

    if (r->selecting && r->selection_term == term) {
        int row = r->sel_row1;
        int col = r->sel_col1;
        if (pixel_to_cell(r, term, ox, oy, area_w, area_h, mouse, &row, &col)) {
            r->sel_row1 = row;
            r->sel_col1 = col + 1;
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            r->selecting = false;
            if (copy_on_select) {
                mt_renderer_copy_selection(r);
            }
        }
    }
}

static char *extract_selection_text(MtRenderer *r)
{
    if (!r || !r->has_selection || !r->selection_term) return NULL;

    MtTerminal *term = r->selection_term;
    GhosttyTerminal vt = mt_terminal_get_vt(term);
    GhosttyRenderState rs = mt_terminal_get_render_state(term);
    if (ghostty_render_state_update(rs, vt) != GHOSTTY_SUCCESS) {
        return NULL;
    }

    int term_rows = mt_terminal_rows(term);
    int term_cols = mt_terminal_cols(term);
    if (term_rows <= 0 || term_cols <= 0) return NULL;

    MtSelectionRange range = selection_range(r);
    if (range.row0 < 0) range.row0 = 0;
    if (range.row1 >= term_rows) range.row1 = term_rows - 1;

    size_t out_cap = (size_t)(term_rows + 1) * (size_t)(term_cols * 8 + 2);
    char *out = calloc(out_cap, 1);
    if (!out) return NULL;
    size_t out_len = 0;

    GhosttyRenderStateRowIterator row_iter = NULL;
    if (ghostty_render_state_row_iterator_new(mt_terminal_get_allocator(), &row_iter) != GHOSTTY_SUCCESS) {
        free(out);
        return NULL;
    }

    if (ghostty_render_state_get(rs, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &row_iter) != GHOSTTY_SUCCESS) {
        ghostty_render_state_row_iterator_free(row_iter);
        free(out);
        return NULL;
    }

    GhosttyRenderStateRowCells cells = NULL;
    if (ghostty_render_state_row_cells_new(mt_terminal_get_allocator(), &cells) != GHOSTTY_SUCCESS) {
        ghostty_render_state_row_iterator_free(row_iter);
        free(out);
        return NULL;
    }

    int row = 0;
    while (row < term_rows && ghostty_render_state_row_iterator_next(row_iter)) {
        if (row < range.row0 || row > range.row1) {
            row++;
            continue;
        }

        if (ghostty_render_state_row_get(row_iter, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &cells) != GHOSTTY_SUCCESS) {
            row++;
            continue;
        }

        size_t line_start = out_len;
        int col = 0;
        while (col < term_cols && ghostty_render_state_row_cells_next(cells)) {
            GhosttyCell raw_cell = 0;
            GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
            uint32_t grapheme_len = 0;

            ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw_cell);
            ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &grapheme_len);
            ghostty_cell_get(raw_cell, GHOSTTY_CELL_DATA_WIDE, &wide);

            int cell_span = (wide == GHOSTTY_CELL_WIDE_WIDE) ? 2 : 1;
            if (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL || wide == GHOSTTY_CELL_WIDE_SPACER_HEAD) {
                col += 1;
                continue;
            }

            if (cell_selected(r, term, row, col, cell_span)) {
                char utf8[64] = {0};
                int utf8_len = 0;

                if (grapheme_len > 0) {
                    uint32_t stack_codepoints[8];
                    uint32_t *codepoints = stack_codepoints;
                    if (grapheme_len > (uint32_t)(sizeof(stack_codepoints) / sizeof(stack_codepoints[0]))) {
                        codepoints = calloc(grapheme_len, sizeof(uint32_t));
                    }

                    if (codepoints) {
                        ghostty_render_state_row_cells_get(
                            cells,
                            GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF,
                            codepoints
                        );
                        utf8_len = grapheme_to_utf8_len(codepoints, grapheme_len, utf8, sizeof(utf8));
                        if (codepoints != stack_codepoints) free(codepoints);
                    }
                }

                if (utf8_len <= 0) {
                    utf8[0] = ' ';
                    utf8[1] = '\0';
                    utf8_len = 1;
                }

                if (out_len + (size_t)utf8_len + 2 < out_cap) {
                    memcpy(out + out_len, utf8, (size_t)utf8_len);
                    out_len += (size_t)utf8_len;
                    out[out_len] = '\0';
                }
            }

            col += cell_span;
        }

        while (out_len > line_start && (out[out_len - 1] == ' ' || out[out_len - 1] == '\t')) {
            out[--out_len] = '\0';
        }

        if (row < range.row1 && out_len + 2 < out_cap) {
            out[out_len++] = '\n';
            out[out_len] = '\0';
        }

        row++;
    }

    ghostty_render_state_row_cells_free(cells);
    ghostty_render_state_row_iterator_free(row_iter);

    if (out_len == 0) {
        free(out);
        return NULL;
    }

    return out;
}

bool mt_renderer_copy_selection(MtRenderer *r)
{
    char *text = extract_selection_text(r);
    if (!text) return false;
    SetClipboardText(text);
    free(text);
    return true;
}

/* --------------------------------------------------------------------------
 * Tab bar rendering
 * -------------------------------------------------------------------------- */

#ifndef _WIN32
static void render_tab_bar(MtRenderer *r, MtTabManager *tabs, const MtTheme *theme)
{
    int sw = GetScreenWidth();

    DrawRectangle(0, 0, sw, (int)MT_TAB_BAR_HEIGHT, rgba_to_color(theme->tab_bar_bg));
    DrawLine(0, (int)MT_TAB_BAR_HEIGHT - 1, sw, (int)MT_TAB_BAR_HEIGHT - 1,
             rgba_to_color(theme->split_border));

    int tab_count = mt_tabs_count(tabs);
    if (tab_count == 0) return;

    float max_tab_w = 200.0f;
    float total_w = (float)sw - 40.0f;
    float tab_w = total_w / (float)tab_count;
    if (tab_w > max_tab_w) tab_w = max_tab_w;

    for (int i = 0; i < tab_count; i++) {
        MtTab *tab = mt_tabs_get(tabs, i);
        if (!tab) continue;

        float x = (float)i * tab_w;
        Color bg = tab->active
            ? rgba_to_color(theme->tab_active_bg)
            : rgba_to_color(theme->tab_inactive_bg);
        Color fg = tab->active
            ? rgba_to_color(theme->tab_active_fg)
            : rgba_to_color(theme->tab_inactive_fg);

        DrawRectangle((int)x, 0, (int)tab_w, (int)MT_TAB_BAR_HEIGHT, bg);
        if (tab->active) {
            DrawRectangle((int)x, (int)(MT_TAB_BAR_HEIGHT - 2), (int)tab_w, 2,
                          rgba_to_color(theme->cursor));
        }

        if (tab->has_activity && !tab->active) {
            DrawCircle((int)(x + 8), (int)(MT_TAB_BAR_HEIGHT / 2), 3,
                       rgba_to_color(theme->tab_activity));
        }

        float text_x = x + TAB_PADDING + (tab->has_activity ? 8.0f : 0.0f);
        float text_y = (MT_TAB_BAR_HEIGHT - r->font_size) / 2.0f;
        float close_x = x + tab_w - TAB_CLOSE_SIZE - 6;
        float avail_w = close_x - text_x - 6;

        char display_title[MT_TAB_MAX_TITLE];
        const char *title_src = tab->renaming ? tab->rename_buf : tab->title;
        strncpy(display_title, title_src, MT_TAB_MAX_TITLE - 1);
        display_title[MT_TAB_MAX_TITLE - 1] = '\0';

        Vector2 ts = MeasureTextEx(r->font, display_title, r->font_size, 0);
        while (ts.x > avail_w && strlen(display_title) > 3) {
            size_t len = strlen(display_title);
            display_title[len - 1] = '\0';
            len = strlen(display_title);
            if (len >= 3) {
                display_title[len - 3] = '.';
                display_title[len - 2] = '.';
                display_title[len - 1] = '.';
            }
            ts = MeasureTextEx(r->font, display_title, r->font_size, 0);
        }

        if (tab->renaming) {
            DrawRectangleRounded(
                (Rectangle){ text_x - 4, 5, avail_w + 8, MT_TAB_BAR_HEIGHT - 10 },
                0.2f, 4, rgba_to_color(theme->bg)
            );
            DrawRectangleRoundedLinesEx(
                (Rectangle){ text_x - 4, 5, avail_w + 8, MT_TAB_BAR_HEIGHT - 10 },
                0.2f, 4, 1, rgba_to_color(theme->cursor)
            );
            DrawTextEx(r->font, display_title, (Vector2){ text_x, text_y }, r->font_size, 0, fg);
            Vector2 rename_size = MeasureTextEx(r->font, display_title, r->font_size, 0);
            DrawRectangle((int)(text_x + rename_size.x + RENAME_CURSOR_PAD), (int)text_y,
                          2, (int)r->font_size, rgba_to_color(theme->cursor));
        } else {
            DrawTextEx(r->font, display_title, (Vector2){ text_x, text_y }, r->font_size, 0, fg);
        }

        Vector2 mouse = GetMousePosition();
        if (mouse.x >= x && mouse.x < x + tab_w && mouse.y >= 0 && mouse.y < MT_TAB_BAR_HEIGHT) {
            float cy = (MT_TAB_BAR_HEIGHT - TAB_CLOSE_SIZE) / 2.0f;
            Color close_color = fg;
            close_color.a = 150;
            if (mouse.x >= close_x && mouse.x <= close_x + TAB_CLOSE_SIZE &&
                mouse.y >= cy && mouse.y <= cy + TAB_CLOSE_SIZE) {
                close_color.a = 255;
                DrawRectangleRounded(
                    (Rectangle){ close_x - 2, cy - 2, TAB_CLOSE_SIZE + 4, TAB_CLOSE_SIZE + 4 },
                    0.3f, 4, (Color){ 255, 80, 80, 40 }
                );
            }

            DrawLine((int)close_x + 2, (int)cy + 2,
                     (int)(close_x + TAB_CLOSE_SIZE - 2), (int)(cy + TAB_CLOSE_SIZE - 2),
                     close_color);
            DrawLine((int)(close_x + TAB_CLOSE_SIZE - 2), (int)cy + 2,
                     (int)close_x + 2, (int)(cy + TAB_CLOSE_SIZE - 2),
                     close_color);
        }

        if (i < tab_count - 1) {
            DrawLine((int)(x + tab_w), 4, (int)(x + tab_w), (int)(MT_TAB_BAR_HEIGHT - 4),
                     rgba_to_color(theme->split_border));
        }
    }

    float plus_x = (float)tab_count * tab_w + 8;
    if (plus_x < (float)sw - 32) {
        float plus_y = (MT_TAB_BAR_HEIGHT - r->font_size) / 2.0f;
        Color plus_color = rgba_to_color(theme->tab_inactive_fg);

        Vector2 mouse = GetMousePosition();
        if (mouse.x >= plus_x && mouse.x <= plus_x + 24 && mouse.y >= 0 && mouse.y < MT_TAB_BAR_HEIGHT) {
            plus_color = rgba_to_color(theme->tab_active_fg);
        }

        DrawTextEx(r->font, "+", (Vector2){ plus_x + 4, plus_y }, r->font_size, 0, plus_color);
    }
}

static bool handle_tab_bar_ui(MtRenderer *r, MtTabManager *tabs, int sw)
{
    if (!r || !tabs) return false;

    int tab_count = mt_tabs_count(tabs);
    if (tab_count <= 0) return false;

    float max_tab_w = 200.0f;
    float total_w = (float)sw - 40.0f;
    float tab_w = total_w / (float)tab_count;
    if (tab_w > max_tab_w) tab_w = max_tab_w;

    Vector2 mouse = GetMousePosition();
    if (mouse.y < 0 || mouse.y >= MT_TAB_BAR_HEIGHT) {
        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            r->dragging_tab = false;
            r->drag_tab_index = -1;
        }
        return false;
    }

    if (r->dragging_tab && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        for (int i = 0; i < tab_count; i++) {
            float x = (float)i * tab_w;
            if (mouse.x >= x && mouse.x < x + tab_w && i != r->drag_tab_index) {
                mt_tabs_move(tabs, r->drag_tab_index, i);
                r->drag_tab_index = i;
                return true;
            }
        }
    }

    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        r->dragging_tab = false;
        r->drag_tab_index = -1;
    }

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;

    for (int i = 0; i < tab_count; i++) {
        float x = (float)i * tab_w;
        if (mouse.x < x || mouse.x >= x + tab_w) continue;

        float cx = x + tab_w - TAB_CLOSE_SIZE - 6;
        float cy = (MT_TAB_BAR_HEIGHT - TAB_CLOSE_SIZE) / 2.0f;
        if (mouse.x >= cx && mouse.x <= cx + TAB_CLOSE_SIZE && mouse.y >= cy && mouse.y <= cy + TAB_CLOSE_SIZE) {
            if (tab_count > 1) mt_tabs_close(tabs, i);
            return true;
        }

        double now = GetTime();
        mt_tabs_select(tabs, i);
        selection_clear(r);
        if (r->last_tab_click == i && (now - r->last_tab_click_time) <= UI_DOUBLE_CLICK_S) {
            mt_tabs_begin_rename(tabs, i);
        } else {
            r->dragging_tab = true;
            r->drag_tab_index = i;
        }
        r->last_tab_click = i;
        r->last_tab_click_time = now;
        return true;
    }

    float plus_x = (float)tab_count * tab_w + 8;
    if (mouse.x >= plus_x && mouse.x <= plus_x + 24) {
        MtTerminal *active_term = mt_tabs_active_terminal(tabs);
        int cols = active_term ? mt_terminal_cols(active_term) : MYTERM_DEFAULT_COLS;
        int rows = active_term ? mt_terminal_rows(active_term) : MYTERM_DEFAULT_ROWS;
        int idx = mt_tabs_add(tabs, cols, rows);
        selection_clear(r);
        if (idx >= 0) mt_tabs_select(tabs, idx);
        return true;
    }

    return false;
}
#endif

/* --------------------------------------------------------------------------
 * Search bar rendering
 * -------------------------------------------------------------------------- */

static void render_search_bar(MtRenderer *r, MtSearch *search, const MtTheme *theme)
{
    if (!search || !mt_search_is_active(search)) return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float y = (float)sh - SEARCH_BAR_HEIGHT;

    DrawRectangle(0, (int)y, sw, (int)SEARCH_BAR_HEIGHT, rgba_to_color(theme->tab_bar_bg));
    DrawLine(0, (int)y, sw, (int)y, rgba_to_color(theme->split_border));

    Color fg = rgba_to_color(theme->tab_active_fg);
    float text_y = y + (SEARCH_BAR_HEIGHT - r->font_size) / 2.0f;
    DrawTextEx(r->font, "Find:", (Vector2){ 8, text_y }, r->font_size, 0, fg);

    const char *query = mt_search_get_query(search);
    float query_x = 60.0f;
    float box_w = (float)sw * 0.4f;
    if (box_w > 400) box_w = 400;

    DrawRectangleRounded(
        (Rectangle){ query_x - 4, y + 4, box_w + 8, SEARCH_BAR_HEIGHT - 8 },
        0.2f, 4, rgba_to_color(theme->bg)
    );
    DrawRectangleRoundedLinesEx(
        (Rectangle){ query_x - 4, y + 4, box_w + 8, SEARCH_BAR_HEIGHT - 8 },
        0.2f, 4, 1, rgba_to_color(theme->split_border)
    );

    if (query[0]) {
        DrawTextEx(r->font, query, (Vector2){ query_x, text_y }, r->font_size, 0, fg);
        Vector2 qs = MeasureTextEx(r->font, query, r->font_size, 0);
        Color cursor = rgba_to_color(theme->cursor);
        cursor.a = 200;
        DrawRectangle((int)(query_x + qs.x + 1), (int)(text_y), 2, (int)r->font_size, cursor);
    } else {
        DrawTextEx(r->font, "Type to search...", (Vector2){ query_x, text_y }, r->font_size, 0,
                   rgba_to_color(theme->tab_inactive_fg));
    }

    int count = mt_search_match_count(search);
    int current = mt_search_current_index(search);
    char info[64];
    if (count > 0) {
        snprintf(info, sizeof(info), "%d/%d", current + 1, count);
    } else if (query[0]) {
        snprintf(info, sizeof(info), "No matches");
    } else {
        info[0] = '\0';
    }

    if (info[0]) {
        float info_x = query_x + box_w + 16;
        Color info_color = (count > 0) ? fg : (Color){ 255, 100, 100, 255 };
        DrawTextEx(r->font, info, (Vector2){ info_x, text_y }, r->font_size, 0, info_color);
    }

    const char *hint_text = "Enter/Shift+Enter  Esc to close";
    Vector2 hs = MeasureTextEx(r->font, hint_text, r->font_size * 0.8f, 0);
    DrawTextEx(r->font, hint_text, (Vector2){ (float)sw - hs.x - 8, text_y + 1 },
               r->font_size * 0.8f, 0, rgba_to_color(theme->tab_inactive_fg));
}

/* --------------------------------------------------------------------------
 * Terminal content rendering
 * -------------------------------------------------------------------------- */

static void render_terminal(MtRenderer *r, MtTerminal *term, const MtTheme *theme,
                            MtSearch *search, float ox, float oy, float area_w, float area_h,
                            bool copy_on_select)
{
    GhosttyTerminal vt = mt_terminal_get_vt(term);
    GhosttyRenderState rs = mt_terminal_get_render_state(term);

    handle_terminal_selection(r, term, ox, oy, area_w, area_h, copy_on_select);

    if (ghostty_render_state_update(rs, vt) != GHOSTTY_SUCCESS) {
        DrawRectangle((int)ox, (int)oy, (int)area_w, (int)area_h, rgba_to_color(theme->bg));
        return;
    }

    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    ghostty_render_state_colors_get(rs, &colors);

    GhosttyColorRgb default_bg = rgba_to_ghostty_rgb(theme->bg);
    GhosttyColorRgb default_fg = rgba_to_ghostty_rgb(theme->fg);

    DrawRectangle((int)ox, (int)oy, (int)area_w, (int)area_h, rgba_to_color(theme->bg));

    int term_rows = mt_terminal_rows(term);
    int term_cols = mt_terminal_cols(term);
    int search_match_count = 0;
    int search_current = -1;
    if (search && mt_search_is_active(search) && term == r->selection_term) {
        /* no-op: selection is per-terminal, search still handled below */
    }
    if (search && mt_search_is_active(search)) {
        search_match_count = mt_search_match_count(search);
        search_current = mt_search_current_index(search);
    }

    GhosttyRenderStateRowIterator row_iter = NULL;
    if (ghostty_render_state_row_iterator_new(mt_terminal_get_allocator(), &row_iter) != GHOSTTY_SUCCESS) {
        return;
    }

    if (ghostty_render_state_get(rs, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &row_iter) != GHOSTTY_SUCCESS) {
        ghostty_render_state_row_iterator_free(row_iter);
        return;
    }

    GhosttyRenderStateRowCells cells = NULL;
    if (ghostty_render_state_row_cells_new(mt_terminal_get_allocator(), &cells) != GHOSTTY_SUCCESS) {
        ghostty_render_state_row_iterator_free(row_iter);
        return;
    }

    int row = 0;
    while (row < term_rows && ghostty_render_state_row_iterator_next(row_iter)) {
        float y = oy + (float)row * r->cell_h;
        if (y > oy + area_h) break;

        if (ghostty_render_state_row_get(row_iter, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &cells) != GHOSTTY_SUCCESS) {
            row++;
            continue;
        }

        int col = 0;
        while (col < term_cols && ghostty_render_state_row_cells_next(cells)) {
            GhosttyStyle style = GHOSTTY_INIT_SIZED(GhosttyStyle);
            GhosttyCell raw_cell = 0;
            GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
            uint32_t grapheme_len = 0;

            ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE, &style);
            ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW, &raw_cell);
            ghostty_render_state_row_cells_get(cells, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN, &grapheme_len);
            ghostty_cell_get(raw_cell, GHOSTTY_CELL_DATA_WIDE, &wide);

            int cell_span = (wide == GHOSTTY_CELL_WIDE_WIDE) ? 2 : 1;
            if (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL || wide == GHOSTTY_CELL_WIDE_SPACER_HEAD) {
                col += 1;
                continue;
            }

            float x = ox + (float)col * r->cell_w;
            float cell_w = r->cell_w * (float)cell_span;

            Color bg = ghostty_to_raylib_color(resolve_style_color(style.bg_color, &colors, default_bg));
            DrawRectangle((int)x, (int)y, (int)cell_w, (int)r->cell_h, bg);

            if (search_match_count > 0) {
                for (int m = 0; m < search_match_count; m++) {
                    const MtSearchMatch *match = mt_search_get_match(search, m);
                    if (match && match->row == row && col < match->col_end && (col + cell_span) > match->col_start) {
                        Color hl = (m == search_current)
                            ? rgba_to_color(theme->search_current)
                            : rgba_to_color(theme->search_match);
                        DrawRectangle((int)x, (int)y, (int)cell_w, (int)r->cell_h, hl);
                        break;
                    }
                }
            }

            if (cell_selected(r, term, row, col, cell_span)) {
                DrawRectangle((int)x, (int)y, (int)cell_w, (int)r->cell_h, rgba_to_color(theme->selection));
            }

            Color fg = ghostty_to_raylib_color(resolve_style_color(style.fg_color, &colors, default_fg));
            if (style.bold) {
                fg.r = (uint8_t)(fg.r + (255 - fg.r) / 3);
                fg.g = (uint8_t)(fg.g + (255 - fg.g) / 3);
                fg.b = (uint8_t)(fg.b + (255 - fg.b) / 3);
            }

            if (grapheme_len > 0) {
                uint32_t stack_codepoints[8];
                uint32_t *codepoints = stack_codepoints;
                if (grapheme_len > (uint32_t)(sizeof(stack_codepoints) / sizeof(stack_codepoints[0]))) {
                    codepoints = calloc(grapheme_len, sizeof(uint32_t));
                }

                if (codepoints) {
                    char utf8[64];
                    ghostty_render_state_row_cells_get(
                        cells,
                        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF,
                        codepoints
                    );
                    grapheme_to_utf8_len(codepoints, grapheme_len, utf8, sizeof(utf8));

                    Vector2 pos = { x, y };
                    if (style.italic) pos.x += 2.0f;

                    DrawTextEx(r->font, utf8, pos, r->font_size, 0, fg);
                    if (style.bold) {
                        pos.x += 1.0f;
                        DrawTextEx(r->font, utf8, pos, r->font_size, 0, fg);
                    }

                    if (codepoints != stack_codepoints) free(codepoints);
                }
            }

            if (style.underline) {
                float uy = y + r->cell_h - 2.0f;
                DrawLine((int)x, (int)uy, (int)(x + cell_w), (int)uy, fg);
            }

            if (style.strikethrough) {
                float sy = y + r->cell_h / 2.0f;
                DrawLine((int)x, (int)sy, (int)(x + cell_w), (int)sy, fg);
            }

            col += cell_span;
        }

        row++;
    }

    ghostty_render_state_row_cells_free(cells);
    ghostty_render_state_row_iterator_free(row_iter);

    bool cursor_visible = false;
    bool cursor_in_viewport = false;
    ghostty_render_state_get(rs, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE, &cursor_visible);
    ghostty_render_state_get(rs, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &cursor_in_viewport);

    if (cursor_visible && cursor_in_viewport) {
        uint16_t cursor_x = 0;
        uint16_t cursor_y = 0;
        GhosttyRenderStateCursorVisualStyle cursor_style = GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK;
        Color cursor_color = rgba_to_color(theme->cursor);

        ghostty_render_state_get(rs, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cursor_x);
        ghostty_render_state_get(rs, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cursor_y);
        ghostty_render_state_get(rs, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISUAL_STYLE, &cursor_style);

        float cx = ox + (float)cursor_x * r->cell_w;
        float cy = oy + (float)cursor_y * r->cell_h;

        switch (cursor_style) {
        case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BAR:
            DrawRectangle((int)cx, (int)cy, 2, (int)r->cell_h, cursor_color);
            break;
        case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_UNDERLINE:
            DrawRectangle((int)cx, (int)(cy + r->cell_h - 2), (int)r->cell_w, 2, cursor_color);
            break;
        case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK_HOLLOW:
            DrawRectangleLines((int)cx, (int)cy, (int)r->cell_w, (int)r->cell_h, cursor_color);
            break;
        case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK:
        default:
            DrawRectangle((int)cx, (int)cy, (int)r->cell_w, (int)r->cell_h, cursor_color);
            break;
        }
    }

    GhosttyTerminalScrollbar scrollbar = {0};
    if (ghostty_terminal_get(vt, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &scrollbar) == GHOSTTY_SUCCESS &&
        scrollbar.total > scrollbar.len && scrollbar.total > 0) {
        int bar_w = 6;
        int bar_x = (int)(ox + area_w) - bar_w - 2;
        float thumb_h = area_h * ((float)scrollbar.len / (float)scrollbar.total);
        if (thumb_h < 16.0f) thumb_h = 16.0f;
        if (thumb_h > area_h) thumb_h = area_h;

        float scroll_range = (float)(scrollbar.total - scrollbar.len);
        float track_range = area_h - thumb_h;
        float thumb_y = oy;
        if (scroll_range > 0.0f && track_range > 0.0f) {
            thumb_y += ((float)scrollbar.offset / scroll_range) * track_range;
        }

        DrawRectangle(bar_x, (int)oy, bar_w, (int)area_h, rgba_to_color(theme->scrollbar_bg));
        DrawRectangleRounded((Rectangle){ (float)bar_x, thumb_y, (float)bar_w, thumb_h },
                             0.5f, 4, rgba_to_color(theme->scrollbar_thumb));
    }
}

static const MtSplitNode *split_leaf_at_point(const MtSplitNode *node, Vector2 mouse)
{
    if (!node || !point_in_rect(mouse, node->x, node->y, node->w, node->h)) return NULL;
    if (node->is_leaf) return node;

    const MtSplitNode *leaf = split_leaf_at_point(node->first, mouse);
    if (leaf) return leaf;
    return split_leaf_at_point(node->second, mouse);
}

static void handle_split_focus_clicks(MtRenderer *r, MtSplitManager *splits)
{
    if (!r || !splits || !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    Vector2 mouse = GetMousePosition();
    const MtSplitNode *leaf = split_leaf_at_point(mt_splits_root(splits), mouse);
    if (!leaf) return;

    if (leaf != mt_splits_focused_node(splits)) {
        mt_splits_focus_leaf(splits, leaf);
        selection_clear(r);
    }
}

static void render_pane_header(MtRenderer *r, const MtSplitNode *node, bool focused,
                               const MtTheme *theme, int pane_index, int pane_count)
{
    Color header_bg = focused ? rgba_to_color(theme->tab_active_bg)
                              : rgba_to_color(theme->tab_inactive_bg);
    Color text = focused ? rgba_to_color(theme->tab_active_fg)
                         : rgba_to_color(theme->tab_inactive_fg);
    Color border = rgba_to_color(theme->split_border);
    Color accent = rgba_to_color(theme->cursor);

    Rectangle header = {
        node->x + PANE_INSET,
        node->y + PANE_INSET,
        node->w - PANE_INSET * 2.0f,
        PANE_HEADER_HEIGHT - 4.0f
    };

    DrawRectangleRounded(header, 0.28f, 8, header_bg);
    DrawRectangleRoundedLinesEx(header, 0.28f, 8, 1.0f, focused ? accent : border);

    char label[32];
    snprintf(label, sizeof(label), "Pane %d", pane_index + 1);
    DrawTextEx(r->font, label,
               (Vector2){ header.x + 8.0f, header.y + 3.0f },
               r->font_size * 0.78f, 0, text);

    if (pane_count > 1) {
        char info[32];
        snprintf(info, sizeof(info), "%d panes", pane_count);
        Vector2 info_sz = MeasureTextEx(r->font, info, r->font_size * 0.68f, 0);
        Color info_color = rgba_to_color(theme->tab_inactive_fg);
        DrawTextEx(r->font, info,
                   (Vector2){ header.x + header.width - info_sz.x - 8.0f, header.y + 4.0f },
                   r->font_size * 0.68f, 0, info_color);
    }

    if (focused) {
        Rectangle chip = {
            header.x + 62.0f,
            header.y + 3.0f,
            52.0f,
            header.height - 6.0f
        };
        DrawRectangleRounded(chip, 0.45f, 8, accent);
        DrawTextEx(r->font, "ACTIVE",
                   (Vector2){ chip.x + 8.0f, chip.y + 2.0f },
                   r->font_size * 0.58f, 0, rgba_to_color(theme->bg));
    }
}

static void render_split_tree(MtRenderer *r, const MtSplitNode *node, const MtSplitNode *focused,
                              const MtTheme *theme, MtSearch *search, bool copy_on_select,
                              int pane_count, int *pane_index)
{
    if (!node) return;

    if (node->is_leaf) {
        int this_pane = pane_index ? *pane_index : 0;
        if (pane_index) (*pane_index)++;

        float content_x = node->x;
        float content_y = node->y;
        float content_w = node->w;
        float content_h = node->h;

        if (pane_count > 1) {
            render_pane_header(r, node, node == focused, theme, this_pane, pane_count);
            content_x += PANE_INSET;
            content_y += PANE_HEADER_HEIGHT;
            content_w -= PANE_INSET * 2.0f;
            content_h -= (PANE_HEADER_HEIGHT + PANE_INSET);
        }

        if (content_w < r->cell_w * 2.0f) content_w = r->cell_w * 2.0f;
        if (content_h < r->cell_h * 2.0f) content_h = r->cell_h * 2.0f;

        render_terminal(r, node->terminal, theme,
                        node == focused ? search : NULL,
                        content_x, content_y, content_w, content_h, copy_on_select);
        if (node == focused) {
            Color border = rgba_to_color(theme->cursor);
            border.a = 210;
            Rectangle focus_rect = { node->x + 0.5f, node->y + 0.5f, node->w - 1.0f, node->h - 1.0f };
            DrawRectangleLinesEx(focus_rect, 1.0f, border);
            DrawRectangle((int)(node->x + 2.0f), (int)(node->y + 2.0f), 14, 2, border);
            DrawRectangle((int)(node->x + 2.0f), (int)(node->y + 2.0f), 2, 14, border);
        }
        return;
    }

    render_split_tree(r, node->first, focused, theme, search, copy_on_select, pane_count, pane_index);
    render_split_tree(r, node->second, focused, theme, search, copy_on_select, pane_count, pane_index);

    Color divider = rgba_to_color(theme->split_border);
    if (node->dir == MT_SPLIT_VERTICAL) {
        float x = node->second ? node->second->x - 1.0f : node->x + node->w * node->ratio;
        DrawRectangle((int)x, (int)node->y, 2, (int)node->h, divider);
        Color accent = rgba_to_color(theme->cursor);
        accent.a = 180;
        DrawRectangle((int)x, (int)(node->y + 6.0f), 2, 28, accent);
    } else {
        float y = node->second ? node->second->y - 1.0f : node->y + node->h * node->ratio;
        DrawRectangle((int)node->x, (int)y, (int)node->w, 2, divider);
        Color accent = rgba_to_color(theme->cursor);
        accent.a = 180;
        DrawRectangle((int)(node->x + 6.0f), (int)y, 28, 2, accent);
    }
}

/* --------------------------------------------------------------------------
 * Full frame rendering (public API)
 * -------------------------------------------------------------------------- */

bool mt_renderer_frame(MtRenderer *r, MtTerminal *term)
{
    if (WindowShouldClose()) return false;

    MtTheme theme = mt_theme_get(MT_THEME_CATPPUCCIN_MOCHA);

    BeginDrawing();
    ClearBackground(rgba_to_color(theme.bg));
    render_terminal(r, term, &theme, NULL, 0, 0,
                    (float)GetScreenWidth(), (float)GetScreenHeight(), false);
    EndDrawing();
    return true;
}

bool mt_renderer_frame_full(MtRenderer *r, MtTabManager *tabs,
                            MtSearch *search, const MtConfig *config)
{
    if (WindowShouldClose()) return false;
    if (!r || !tabs || !config) return false;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    const MtTheme *theme = &config->theme;

#ifdef _WIN32
    if (!r->native_tabs) {
        r->native_tabs = mt_native_tabs_new(GetWindowHandle());
    }
    if (r->native_tabs) {
        mt_native_tabs_sync(r->native_tabs, tabs, theme, sw);
        MtNativeTabsEvents events = mt_native_tabs_poll(r->native_tabs);
        if (events.move_from_index >= 0 && events.move_to_index >= 0 &&
            events.move_from_index < mt_tabs_count(tabs) &&
            events.move_to_index < mt_tabs_count(tabs) &&
            events.move_from_index != events.move_to_index) {
            mt_tabs_move(tabs, events.move_from_index, events.move_to_index);
            mt_native_tabs_sync(r->native_tabs, tabs, theme, sw);
        }
        if (events.selected_index >= 0 && events.selected_index < mt_tabs_count(tabs) &&
            events.selected_index != mt_tabs_active_index(tabs)) {
            mt_tabs_select(tabs, events.selected_index);
            selection_clear(r);
            mt_native_tabs_sync(r->native_tabs, tabs, theme, sw);
        }
        if (events.rename_requested_index >= 0 && events.rename_requested_index < mt_tabs_count(tabs)) {
            mt_tabs_select(tabs, events.rename_requested_index);
            mt_tabs_begin_rename(tabs, events.rename_requested_index);
            selection_clear(r);
            mt_native_tabs_sync(r->native_tabs, tabs, theme, sw);
        }
        if (events.close_requested_index >= 0 && events.close_requested_index < mt_tabs_count(tabs) &&
            mt_tabs_count(tabs) > 1) {
            mt_tabs_close(tabs, events.close_requested_index);
            selection_clear(r);
            mt_native_tabs_sync(r->native_tabs, tabs, theme, sw);
        }
        if (events.add_clicked) {
            MtTerminal *active_term = mt_tabs_active_terminal(tabs);
            int cols = active_term ? mt_terminal_cols(active_term) : MYTERM_DEFAULT_COLS;
            int rows = active_term ? mt_terminal_rows(active_term) : MYTERM_DEFAULT_ROWS;
            int idx = mt_tabs_add(tabs, cols, rows);
            if (idx >= 0) {
                mt_tabs_select(tabs, idx);
                selection_clear(r);
                mt_native_tabs_sync(r->native_tabs, tabs, theme, sw);
            }
        }
        if (events.close_clicked && mt_tabs_count(tabs) > 1) {
            mt_tabs_close(tabs, mt_tabs_active_index(tabs));
            selection_clear(r);
            mt_native_tabs_sync(r->native_tabs, tabs, theme, sw);
        }
    }
#else
    handle_tab_bar_ui(r, tabs, sw);
#endif

    BeginDrawing();
    ClearBackground(rgba_to_color(theme->bg));

#ifdef _WIN32
    DrawRectangle(0, 0, sw, (int)MT_TAB_BAR_HEIGHT, rgba_to_color(theme->tab_bar_bg));
    DrawLine(0, (int)MT_TAB_BAR_HEIGHT - 1, sw, (int)MT_TAB_BAR_HEIGHT - 1,
             rgba_to_color(theme->split_border));
#else
    render_tab_bar(r, tabs, theme);
#endif

    float content_y = MT_TAB_BAR_HEIGHT;
    float content_h = (float)sh - MT_TAB_BAR_HEIGHT;
    if (search && mt_search_is_active(search)) {
        content_h -= SEARCH_BAR_HEIGHT;
    }

    MtTab *active_tab = mt_tabs_active(tabs);
    if (active_tab && active_tab->splits) {
        mt_splits_layout(active_tab->splits, 0.0f, content_y, (float)sw, content_h);
        handle_split_focus_clicks(r, active_tab->splits);
        int pane_index = 0;
        int pane_count = mt_splits_count(active_tab->splits);
        render_split_tree(r,
                          mt_splits_root(active_tab->splits),
                          mt_splits_focused_node(active_tab->splits),
                          theme, search, config->copy_on_select,
                          pane_count, &pane_index);
    } else {
        MtTerminal *active_term = mt_tabs_active_terminal(tabs);
        if (active_term) {
            render_terminal(r, active_term, theme, search,
                            0, content_y, (float)sw, content_h, config->copy_on_select);
        }
    }

    render_search_bar(r, search, theme);

    EndDrawing();
    return true;
}
