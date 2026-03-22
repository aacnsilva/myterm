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
#include "config.h"
#include "search.h"
#include "terminal_internal.h"
#include <raylib.h>
#include <ghostty/ghostty.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define SEARCH_BAR_HEIGHT 32.0f
#define TAB_PADDING       12.0f
#define TAB_CLOSE_SIZE    14.0f

struct MtRenderer {
    Font    font;
    float   cell_w;
    float   cell_h;
    float   font_size;
    bool    font_loaded;
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

static void grapheme_to_utf8(const uint32_t *codepoints, uint32_t len,
                             char *out, size_t out_size)
{
    size_t used = 0;
    if (!out || out_size == 0) return;

    out[0] = '\0';
    for (uint32_t i = 0; i < len; i++) {
        char tmp[5] = {0};
        int n = codepoint_to_utf8(codepoints[i], tmp);
        if (used + (size_t)n >= out_size) break;
        memcpy(out + used, tmp, (size_t)n);
        used += (size_t)n;
    }
    out[used] = '\0';
}

MtRenderer *mt_renderer_new(int width, int height, const char *title)
{
    MtRenderer *r = calloc(1, sizeof(MtRenderer));
    if (!r) return NULL;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(width, height, title);
    SetTargetFPS(60);

    r->font_size = MYTERM_FONT_SIZE;

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
    for (int i = 0; font_paths[i] != NULL; i++) {
        if (FileExists(font_paths[i])) {
            r->font = LoadFontEx(font_paths[i], (int)r->font_size, NULL, 0);
            if (r->font.glyphCount > 0) {
                r->font_loaded = true;
                SetTextureFilter(r->font.texture, TEXTURE_FILTER_BILINEAR);
                break;
            }
        }
    }

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

/* --------------------------------------------------------------------------
 * Tab bar rendering
 * -------------------------------------------------------------------------- */

static void render_tab_bar(MtRenderer *r, MtTabManager *tabs, const MtTheme *theme)
{
    int sw = GetScreenWidth();

    /* Tab bar background */
    DrawRectangle(0, 0, sw, (int)MT_TAB_BAR_HEIGHT, rgba_to_color(theme->tab_bar_bg));

    /* Bottom border */
    Color border = rgba_to_color(theme->split_border);
    DrawLine(0, (int)MT_TAB_BAR_HEIGHT - 1, sw, (int)MT_TAB_BAR_HEIGHT - 1, border);

    int tab_count = mt_tabs_count(tabs);
    if (tab_count == 0) return;

    /* Calculate tab width (flexible, max 200px) */
    float max_tab_w = 200.0f;
    float total_w = (float)sw - 40.0f; /* Reserve space for + button */
    float tab_w = total_w / (float)tab_count;
    if (tab_w > max_tab_w) tab_w = max_tab_w;

    for (int i = 0; i < tab_count; i++) {
        MtTab *tab = mt_tabs_get(tabs, i);
        if (!tab) continue;

        float x = (float)i * tab_w;
        float y = 0;

        /* Tab background */
        Color bg, fg;
        if (tab->active) {
            bg = rgba_to_color(theme->tab_active_bg);
            fg = rgba_to_color(theme->tab_active_fg);
            /* Active tab indicator — bottom highlight */
            DrawRectangle((int)x, (int)(MT_TAB_BAR_HEIGHT - 2),
                          (int)tab_w, 2, rgba_to_color(theme->cursor));
        } else {
            bg = rgba_to_color(theme->tab_inactive_bg);
            fg = rgba_to_color(theme->tab_inactive_fg);
        }

        DrawRectangle((int)x, (int)y, (int)tab_w, (int)MT_TAB_BAR_HEIGHT, bg);

        /* Activity indicator dot */
        if (tab->has_activity && !tab->active) {
            Color activity = rgba_to_color(theme->tab_activity);
            DrawCircle((int)(x + 8), (int)(MT_TAB_BAR_HEIGHT / 2), 3, activity);
        }

        /* Tab title */
        float text_x = x + TAB_PADDING + (tab->has_activity ? 8.0f : 0.0f);
        float text_y = (MT_TAB_BAR_HEIGHT - r->font_size) / 2.0f;
        float avail_w = tab_w - TAB_PADDING * 2 - TAB_CLOSE_SIZE - 4;

        /* Truncate title to fit */
        char display_title[MT_TAB_MAX_TITLE];
        strncpy(display_title, tab->title, MT_TAB_MAX_TITLE - 1);
        display_title[MT_TAB_MAX_TITLE - 1] = '\0';

        Vector2 ts = MeasureTextEx(r->font, display_title, r->font_size, 0);
        while (ts.x > avail_w && strlen(display_title) > 3) {
            display_title[strlen(display_title) - 1] = '\0';
            display_title[strlen(display_title) - 1] = '.';
            display_title[strlen(display_title) - 0] = '.';
            ts = MeasureTextEx(r->font, display_title, r->font_size, 0);
        }

        DrawTextEx(r->font, display_title,
                   (Vector2){ text_x, text_y }, r->font_size, 0, fg);

        /* Close button (x) — only show on hover */
        Vector2 mouse = GetMousePosition();
        if (mouse.x >= x && mouse.x < x + tab_w &&
            mouse.y >= 0 && mouse.y < MT_TAB_BAR_HEIGHT) {
            float cx = x + tab_w - TAB_CLOSE_SIZE - 6;
            float cy = (MT_TAB_BAR_HEIGHT - TAB_CLOSE_SIZE) / 2.0f;
            Color close_color = fg;
            close_color.a = 150;

            /* Hover highlight on close button */
            if (mouse.x >= cx && mouse.x <= cx + TAB_CLOSE_SIZE &&
                mouse.y >= cy && mouse.y <= cy + TAB_CLOSE_SIZE) {
                close_color.a = 255;
                DrawRectangleRounded(
                    (Rectangle){ cx - 2, cy - 2, TAB_CLOSE_SIZE + 4, TAB_CLOSE_SIZE + 4 },
                    0.3f, 4, (Color){ 255, 80, 80, 40 });
            }

            /* Draw X */
            DrawLine((int)cx + 2, (int)cy + 2,
                     (int)(cx + TAB_CLOSE_SIZE - 2), (int)(cy + TAB_CLOSE_SIZE - 2),
                     close_color);
            DrawLine((int)(cx + TAB_CLOSE_SIZE - 2), (int)cy + 2,
                     (int)cx + 2, (int)(cy + TAB_CLOSE_SIZE - 2),
                     close_color);
        }

        /* Tab separator */
        if (i < tab_count - 1) {
            DrawLine((int)(x + tab_w), 4,
                     (int)(x + tab_w), (int)(MT_TAB_BAR_HEIGHT - 4), border);
        }
    }

    /* "+" new tab button */
    float plus_x = (float)tab_count * tab_w + 8;
    if (plus_x < (float)sw - 32) {
        float plus_y = (MT_TAB_BAR_HEIGHT - r->font_size) / 2.0f;
        Color plus_color = rgba_to_color(theme->tab_inactive_fg);

        Vector2 mouse = GetMousePosition();
        if (mouse.x >= plus_x && mouse.x <= plus_x + 24 &&
            mouse.y >= 0 && mouse.y < MT_TAB_BAR_HEIGHT) {
            plus_color = rgba_to_color(theme->tab_active_fg);
        }

        DrawTextEx(r->font, "+", (Vector2){ plus_x + 4, plus_y }, r->font_size, 0, plus_color);
    }
}

/* Handle tab bar mouse clicks. Returns true if a click was consumed. */
static bool handle_tab_bar_clicks(MtTabManager *tabs, int tab_count, int sw)
{
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return false;

    Vector2 mouse = GetMousePosition();
    if (mouse.y < 0 || mouse.y >= MT_TAB_BAR_HEIGHT) return false;

    float max_tab_w = 200.0f;
    float total_w = (float)sw - 40.0f;
    float tab_w = total_w / (float)tab_count;
    if (tab_w > max_tab_w) tab_w = max_tab_w;

    /* Check tab clicks */
    for (int i = 0; i < tab_count; i++) {
        float x = (float)i * tab_w;
        if (mouse.x >= x && mouse.x < x + tab_w) {
            /* Check close button */
            float cx = x + tab_w - TAB_CLOSE_SIZE - 6;
            float cy = (MT_TAB_BAR_HEIGHT - TAB_CLOSE_SIZE) / 2.0f;
            if (mouse.x >= cx && mouse.x <= cx + TAB_CLOSE_SIZE &&
                mouse.y >= cy && mouse.y <= cy + TAB_CLOSE_SIZE) {
                if (tab_count > 1) {
                    mt_tabs_close(tabs, i);
                }
                return true;
            }

            /* Select tab */
            mt_tabs_select(tabs, i);
            return true;
        }
    }

    /* Check "+" button */
    float plus_x = (float)tab_count * tab_w + 8;
    if (mouse.x >= plus_x && mouse.x <= plus_x + 24) {
        /* The caller will create the tab with proper dimensions */
        return false; /* Let main handle this since it knows cols/rows */
    }

    return false;
}

/* --------------------------------------------------------------------------
 * Search bar rendering
 * -------------------------------------------------------------------------- */

static void render_search_bar(MtRenderer *r, MtSearch *search, const MtTheme *theme)
{
    if (!search || !mt_search_is_active(search)) return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float y = (float)sh - SEARCH_BAR_HEIGHT;

    /* Background */
    DrawRectangle(0, (int)y, sw, (int)SEARCH_BAR_HEIGHT, rgba_to_color(theme->tab_bar_bg));
    DrawLine(0, (int)y, sw, (int)y, rgba_to_color(theme->split_border));

    /* Search icon / label */
    Color fg = rgba_to_color(theme->tab_active_fg);
    float text_y = y + (SEARCH_BAR_HEIGHT - r->font_size) / 2.0f;
    DrawTextEx(r->font, "Find:", (Vector2){ 8, text_y }, r->font_size, 0, fg);

    /* Query text */
    const char *query = mt_search_get_query(search);
    float query_x = 60.0f;

    /* Input box */
    float box_w = (float)sw * 0.4f;
    if (box_w > 400) box_w = 400;
    DrawRectangleRounded(
        (Rectangle){ query_x - 4, y + 4, box_w + 8, SEARCH_BAR_HEIGHT - 8 },
        0.2f, 4, rgba_to_color(theme->bg));
    DrawRectangleRoundedLinesEx(
        (Rectangle){ query_x - 4, y + 4, box_w + 8, SEARCH_BAR_HEIGHT - 8 },
        0.2f, 4, 1, rgba_to_color(theme->split_border));

    if (query[0]) {
        DrawTextEx(r->font, query, (Vector2){ query_x, text_y }, r->font_size, 0, fg);

        /* Cursor */
        Vector2 qs = MeasureTextEx(r->font, query, r->font_size, 0);
        Color cursor = rgba_to_color(theme->cursor);
        cursor.a = 200;
        DrawRectangle((int)(query_x + qs.x + 1), (int)(text_y), 2,
                      (int)r->font_size, cursor);
    } else {
        /* Placeholder */
        Color placeholder = rgba_to_color(theme->tab_inactive_fg);
        DrawTextEx(r->font, "Type to search...",
                   (Vector2){ query_x, text_y }, r->font_size, 0, placeholder);
    }

    /* Match count */
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

    /* Navigation hint */
    Color hint = rgba_to_color(theme->tab_inactive_fg);
    const char *hint_text = "Enter/Shift+Enter  Esc to close";
    Vector2 hs = MeasureTextEx(r->font, hint_text, r->font_size * 0.8f, 0);
    DrawTextEx(r->font, hint_text,
               (Vector2){ (float)sw - hs.x - 8, text_y + 1 },
               r->font_size * 0.8f, 0, hint);
}

/* --------------------------------------------------------------------------
 * Terminal content rendering
 * -------------------------------------------------------------------------- */

static void render_terminal(MtRenderer *r, MtTerminal *term, const MtTheme *theme,
                            MtSearch *search, float ox, float oy, float area_w, float area_h)
{
    GhosttyTerminal    vt = mt_terminal_get_vt(term);
    GhosttyRenderState rs = mt_terminal_get_render_state(term);

    if (ghostty_render_state_update(rs, vt) != GHOSTTY_SUCCESS) {
        DrawRectangle((int)ox, (int)oy, (int)area_w, (int)area_h, rgba_to_color(theme->bg));
        return;
    }

    GhosttyRenderStateColors colors = GHOSTTY_INIT_SIZED(GhosttyRenderStateColors);
    ghostty_render_state_colors_get(rs, &colors);

    Color bg_color = rgba_to_color(theme->bg);
    Color fg_color = rgba_to_color(theme->fg);
    GhosttyColorRgb default_bg = rgba_to_ghostty_rgb(theme->bg);
    GhosttyColorRgb default_fg = rgba_to_ghostty_rgb(theme->fg);

    DrawRectangle((int)ox, (int)oy, (int)area_w, (int)area_h, bg_color);

    int term_rows = mt_terminal_rows(term);
    int term_cols = mt_terminal_cols(term);

    int search_match_count = 0;
    int search_current = -1;
    if (search && mt_search_is_active(search)) {
        search_match_count = mt_search_match_count(search);
        search_current = mt_search_current_index(search);
    }

    GhosttyRenderStateRowIterator row_iter = NULL;
    if (ghostty_render_state_row_iterator_new(NULL, &row_iter) != GHOSTTY_SUCCESS) {
        return;
    }

    if (ghostty_render_state_get(rs, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &row_iter) != GHOSTTY_SUCCESS) {
        ghostty_render_state_row_iterator_free(row_iter);
        return;
    }

    GhosttyRenderStateRowCells cells = NULL;
    if (ghostty_render_state_row_cells_new(NULL, &cells) != GHOSTTY_SUCCESS) {
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
                cell_span = 1;
            }

            float x = ox + (float)col * r->cell_w;
            float cell_w = r->cell_w * (float)cell_span;

            Color bg = ghostty_to_raylib_color(resolve_style_color(style.bg_color, &colors, default_bg));
            DrawRectangle((int)x, (int)y, (int)cell_w, (int)r->cell_h, bg);

            if (search_match_count > 0) {
                for (int m = 0; m < search_match_count; m++) {
                    const MtSearchMatch *match = mt_search_get_match(search, m);
                    if (match && match->row == row &&
                        col < match->col_end && (col + cell_span) > match->col_start) {
                        Color hl = (m == search_current)
                            ? rgba_to_color(theme->search_current)
                            : rgba_to_color(theme->search_match);
                        DrawRectangle((int)x, (int)y, (int)cell_w, (int)r->cell_h, hl);
                        break;
                    }
                }
            }

            Color fg = ghostty_to_raylib_color(resolve_style_color(style.fg_color, &colors, default_fg));
            if (style.bold) {
                fg.r = (uint8_t)(fg.r + (255 - fg.r) / 3);
                fg.g = (uint8_t)(fg.g + (255 - fg.g) / 3);
                fg.b = (uint8_t)(fg.b + (255 - fg.b) / 3);
            }

            if (grapheme_len > 0 && wide != GHOSTTY_CELL_WIDE_SPACER_TAIL && wide != GHOSTTY_CELL_WIDE_SPACER_HEAD) {
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
                    grapheme_to_utf8(codepoints, grapheme_len, utf8, sizeof(utf8));

                    Vector2 pos = { x, y };
                    if (style.italic) pos.x += 2.0f;

                    DrawTextEx(r->font, utf8, pos, r->font_size, 0, fg);
                    if (style.bold) {
                        pos.x += 1.0f;
                        DrawTextEx(r->font, utf8, pos, r->font_size, 0, fg);
                    }

                    if (codepoints != stack_codepoints) {
                        free(codepoints);
                    }
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
        DrawRectangleRounded(
            (Rectangle){ (float)bar_x, thumb_y, (float)bar_w, thumb_h },
            0.5f, 4, rgba_to_color(theme->scrollbar_thumb)
        );
    }
}

/* --------------------------------------------------------------------------
 * Full frame rendering (public API)
 * -------------------------------------------------------------------------- */

/* Legacy single-terminal render (kept for backward compat) */
bool mt_renderer_frame(MtRenderer *r, MtTerminal *term)
{
    if (WindowShouldClose()) return false;

    MtTheme theme = mt_theme_get(MT_THEME_CATPPUCCIN_MOCHA);

    BeginDrawing();
    ClearBackground(rgba_to_color(theme.bg));
    render_terminal(r, term, &theme, NULL,
                    0, 0, (float)GetScreenWidth(), (float)GetScreenHeight());
    EndDrawing();
    return true;
}

/* Full render with tabs, search, and theming */
bool mt_renderer_frame_full(MtRenderer *r, MtTabManager *tabs,
                            MtSearch *search, const MtTheme *theme)
{
    if (WindowShouldClose()) return false;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    /* Handle tab bar clicks */
    handle_tab_bar_clicks(tabs, mt_tabs_count(tabs), sw);

    BeginDrawing();
    ClearBackground(rgba_to_color(theme->bg));

    /* Tab bar */
    render_tab_bar(r, tabs, theme);

    /* Terminal content area */
    float content_y = MT_TAB_BAR_HEIGHT;
    float content_h = (float)sh - MT_TAB_BAR_HEIGHT;

    if (search && mt_search_is_active(search)) {
        content_h -= SEARCH_BAR_HEIGHT;
    }

    /* Render active tab's terminal */
    MtTerminal *active_term = mt_tabs_active_terminal(tabs);
    if (active_term) {
        render_terminal(r, active_term, theme, search,
                        0, content_y, (float)sw, content_h);
    }

    /* Search bar (bottom) */
    render_search_bar(r, search, theme);

    EndDrawing();
    return true;
}
