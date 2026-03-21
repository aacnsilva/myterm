/*
 * renderer.c — Raylib-based terminal renderer.
 *
 * Uses libghostty-vt's render state API to iterate rows/cells
 * and draws them with Raylib's 2D primitives.
 */

#include "myterm.h"
#include "terminal_internal.h"
#include <raylib.h>
#include <ghostty/ghostty.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Default color scheme (Ghostty defaults / dark theme) */
static const Color BG_COLOR    = { 30,  30,  46, 255 };  /* dark background */
static const Color FG_COLOR    = { 205, 214, 244, 255 }; /* light foreground */
static const Color CURSOR_COLOR = { 245, 194, 231, 255 }; /* pink cursor */

struct MtRenderer {
    Font    font;
    float   cell_w;
    float   cell_h;
    float   font_size;
    bool    font_loaded;
};

/* Convert a GhosttyColorRgb to a Raylib Color */
static Color ghostty_to_raylib_color(GhosttyColorRgb gc)
{
    return (Color){ gc.r, gc.g, gc.b, 255 };
}

MtRenderer *mt_renderer_new(int width, int height, const char *title)
{
    MtRenderer *r = calloc(1, sizeof(MtRenderer));
    if (!r) return NULL;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(width, height, title);
    SetTargetFPS(60);

    r->font_size = MYTERM_FONT_SIZE;

    /* Try to load a monospace font; fall back to Raylib default */
    const char *font_paths[] = {
        "C:\\Windows\\Fonts\\consola.ttf",       /* Windows Consolas */
        "C:\\Windows\\Fonts\\CascadiaMono.ttf",  /* Cascadia Mono */
        "C:\\Windows\\Fonts\\lucon.ttf",         /* Lucida Console */
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", /* Linux */
        "/System/Library/Fonts/SFMono-Regular.otf",            /* macOS */
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

    /* Measure a reference character to determine cell size */
    Vector2 m = MeasureTextEx(r->font, "M", r->font_size, 0);
    r->cell_w = m.x;
    r->cell_h = r->font_size + 2.0f; /* small line gap */

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

/*
 * Render one frame of the terminal.
 *
 * Flow:
 *   1. Update the ghostty render state (snapshot of terminal)
 *   2. Iterate rows → cells
 *   3. For each cell: draw background, then draw glyph
 *   4. Draw cursor
 *   5. Draw scrollbar (if applicable)
 */
bool mt_renderer_frame(MtRenderer *r, MtTerminal *term)
{
    if (WindowShouldClose()) return false;

    GhosttyTerminal    vt    = mt_terminal_get_vt(term);
    GhosttyRenderState rs    = mt_terminal_get_render_state(term);

    /* Snapshot terminal state for rendering */
    ghostty_render_state_update(rs, vt);

    /* Get the color palette */
    GhosttyColorPalette palette;
    ghostty_render_state_colors_get(rs, &palette);

    BeginDrawing();
    ClearBackground(BG_COLOR);

    int term_rows = mt_terminal_rows(term);
    int term_cols = mt_terminal_cols(term);

    /* Iterate rows */
    GhosttyRowIterator row_iter;
    ghostty_render_state_row_iterator_new(rs, &row_iter);

    for (int row = 0; row < term_rows; row++) {
        if (!ghostty_render_state_row_iterator_next(&row_iter)) break;

        float y = (float)row * r->cell_h;

        /* Iterate cells in this row */
        GhosttyCell cell;
        int col = 0;
        while (col < term_cols &&
               ghostty_render_state_row_cells_next(&row_iter, &cell)) {

            float x = (float)col * r->cell_w;

            /* Resolve background color */
            Color bg = BG_COLOR;
            if (cell.style.flags & GHOSTTY_STYLE_FLAG_BG) {
                if (cell.style.bg.type == GHOSTTY_COLOR_TYPE_RGB) {
                    bg = ghostty_to_raylib_color(cell.style.bg.rgb);
                } else if (cell.style.bg.type == GHOSTTY_COLOR_TYPE_PALETTE) {
                    bg = ghostty_to_raylib_color(
                        palette.colors[cell.style.bg.palette_index]);
                }
            }

            /* Draw cell background */
            DrawRectangle((int)x, (int)y, (int)r->cell_w, (int)r->cell_h, bg);

            /* Resolve foreground color */
            Color fg = FG_COLOR;
            if (cell.style.flags & GHOSTTY_STYLE_FLAG_FG) {
                if (cell.style.fg.type == GHOSTTY_COLOR_TYPE_RGB) {
                    fg = ghostty_to_raylib_color(cell.style.fg.rgb);
                } else if (cell.style.fg.type == GHOSTTY_COLOR_TYPE_PALETTE) {
                    fg = ghostty_to_raylib_color(
                        palette.colors[cell.style.fg.palette_index]);
                }
            }

            /* Bold → brighter color */
            if (cell.style.flags & GHOSTTY_STYLE_FLAG_BOLD) {
                fg.r = (uint8_t)(fg.r + (255 - fg.r) / 3);
                fg.g = (uint8_t)(fg.g + (255 - fg.g) / 3);
                fg.b = (uint8_t)(fg.b + (255 - fg.b) / 3);
            }

            /* Draw the glyph (if non-space) */
            if (cell.codepoint != 0 && cell.codepoint != ' ') {
                char utf8[5] = {0};
                if (cell.codepoint < 0x80) {
                    utf8[0] = (char)cell.codepoint;
                } else if (cell.codepoint < 0x800) {
                    utf8[0] = (char)(0xC0 | (cell.codepoint >> 6));
                    utf8[1] = (char)(0x80 | (cell.codepoint & 0x3F));
                } else if (cell.codepoint < 0x10000) {
                    utf8[0] = (char)(0xE0 | (cell.codepoint >> 12));
                    utf8[1] = (char)(0x80 | ((cell.codepoint >> 6) & 0x3F));
                    utf8[2] = (char)(0x80 | (cell.codepoint & 0x3F));
                } else {
                    utf8[0] = (char)(0xF0 | (cell.codepoint >> 18));
                    utf8[1] = (char)(0x80 | ((cell.codepoint >> 12) & 0x3F));
                    utf8[2] = (char)(0x80 | ((cell.codepoint >> 6) & 0x3F));
                    utf8[3] = (char)(0x80 | (cell.codepoint & 0x3F));
                }

                Vector2 pos = { x, y };

                /* Italic → slight horizontal offset */
                if (cell.style.flags & GHOSTTY_STYLE_FLAG_ITALIC) {
                    pos.x += 2.0f;
                }

                DrawTextEx(r->font, utf8, pos, r->font_size, 0, fg);

                /* Bold → double-draw for extra weight */
                if (cell.style.flags & GHOSTTY_STYLE_FLAG_BOLD) {
                    pos.x += 1.0f;
                    DrawTextEx(r->font, utf8, pos, r->font_size, 0, fg);
                }
            }

            /* Underline */
            if (cell.style.flags & GHOSTTY_STYLE_FLAG_UNDERLINE) {
                float uy = y + r->cell_h - 2.0f;
                DrawLine((int)x, (int)uy,
                         (int)(x + r->cell_w), (int)uy, fg);
            }

            /* Strikethrough */
            if (cell.style.flags & GHOSTTY_STYLE_FLAG_STRIKETHROUGH) {
                float sy = y + r->cell_h / 2.0f;
                DrawLine((int)x, (int)sy,
                         (int)(x + r->cell_w), (int)sy, fg);
            }

            col += cell.wide ? 2 : 1;
        }
    }

    /* Draw cursor */
    GhosttyTerminalCursor cursor;
    ghostty_terminal_get_cursor(vt, &cursor);
    if (cursor.visible) {
        float cx = (float)cursor.col * r->cell_w;
        float cy = (float)cursor.row * r->cell_h;

        switch (cursor.shape) {
        case GHOSTTY_CURSOR_BLOCK:
            DrawRectangle((int)cx, (int)cy,
                          (int)r->cell_w, (int)r->cell_h, CURSOR_COLOR);
            break;
        case GHOSTTY_CURSOR_BAR:
            DrawRectangle((int)cx, (int)cy, 2, (int)r->cell_h, CURSOR_COLOR);
            break;
        case GHOSTTY_CURSOR_UNDERLINE:
            DrawRectangle((int)cx, (int)(cy + r->cell_h - 2),
                          (int)r->cell_w, 2, CURSOR_COLOR);
            break;
        }
    }

    /* Draw scrollbar */
    GhosttyTerminalScrollbar scrollbar;
    ghostty_terminal_get_scrollbar(vt, &scrollbar);
    if (scrollbar.visible) {
        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        int bar_w   = 8;
        int bar_x   = sw - bar_w - 2;
        float thumb_h = (float)sh * scrollbar.thumb_size;
        float thumb_y = (float)sh * scrollbar.thumb_offset;

        Color bar_bg = { 50, 50, 70, 100 };
        Color bar_fg = { 150, 150, 170, 180 };
        DrawRectangle(bar_x, 0, bar_w, sh, bar_bg);
        DrawRectangle(bar_x, (int)thumb_y, bar_w, (int)thumb_h, bar_fg);
    }

    EndDrawing();
    return true;
}
