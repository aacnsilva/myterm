/*
 * search.c — In-terminal text search.
 *
 * Searches visible terminal content and scrollback for a query string.
 * Supports forward/backward navigation between matches.
 */

#include "search.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef MYTERM_WITH_GHOSTTY
#include "terminal_internal.h"
#include <ghostty/vt/render.h>
#endif

struct MtSearch {
    bool          active;
    char          query[MT_SEARCH_MAX_QUERY];
    MtSearchMatch matches[MT_SEARCH_MAX_MATCHES];
    int           match_count;
    int           current;
};

MtSearch *mt_search_new(void)
{
    MtSearch *s = calloc(1, sizeof(MtSearch));
    if (s) s->current = -1;
    return s;
}

void mt_search_destroy(MtSearch *s)
{
    free(s);
}

void mt_search_open(MtSearch *s)
{
    if (!s) return;
    s->active = true;
    s->query[0] = '\0';
    s->match_count = 0;
    s->current = -1;
}

void mt_search_close(MtSearch *s)
{
    if (!s) return;
    s->active = false;
    s->query[0] = '\0';
    s->match_count = 0;
    s->current = -1;
}

bool mt_search_is_active(const MtSearch *s)
{
    return s ? s->active : false;
}

void mt_search_set_query(MtSearch *s, const char *query)
{
    if (!s || !query) return;
    strncpy(s->query, query, MT_SEARCH_MAX_QUERY - 1);
    s->query[MT_SEARCH_MAX_QUERY - 1] = '\0';
}

const char *mt_search_get_query(const MtSearch *s)
{
    return s ? s->query : "";
}

void mt_search_next(MtSearch *s)
{
    if (!s || s->match_count == 0) return;
    s->current = (s->current + 1) % s->match_count;
}

void mt_search_prev(MtSearch *s)
{
    if (!s || s->match_count == 0) return;
    s->current = (s->current - 1 + s->match_count) % s->match_count;
}

#ifdef MYTERM_WITH_GHOSTTY

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

static const char *strcasestr_ascii(const char *haystack, const char *needle)
{
    size_t needle_len = strlen(needle);
    if (needle_len == 0) return haystack;

    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < needle_len && p[i] &&
               tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) {
            i++;
        }
        if (i == needle_len) return p;
    }
    return NULL;
}

void mt_search_execute(MtSearch *s, MtTerminal *term)
{
    if (!s) return;

    s->match_count = 0;
    s->current = -1;

    if (!term || s->query[0] == '\0') {
        return;
    }

    GhosttyTerminal vt = mt_terminal_get_vt(term);
    GhosttyRenderState rs = mt_terminal_get_render_state(term);
    if (ghostty_render_state_update(rs, vt) != GHOSTTY_SUCCESS) {
        return;
    }

    int term_rows = mt_terminal_rows(term);
    int term_cols = mt_terminal_cols(term);
    if (term_rows <= 0 || term_cols <= 0) {
        return;
    }

    size_t max_line_bytes = (size_t)term_cols * 32 + 1;
    char *line = calloc(max_line_bytes, 1);
    int *byte_col_start = calloc(max_line_bytes, sizeof(int));
    int *byte_col_end = calloc(max_line_bytes, sizeof(int));
    if (!line || !byte_col_start || !byte_col_end) {
        free(line);
        free(byte_col_start);
        free(byte_col_end);
        return;
    }

    GhosttyRenderStateRowIterator row_iter = NULL;
    if (ghostty_render_state_row_iterator_new(mt_terminal_get_allocator(), &row_iter) != GHOSTTY_SUCCESS) {
        free(line);
        free(byte_col_start);
        free(byte_col_end);
        return;
    }

    if (ghostty_render_state_get(rs, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR, &row_iter) != GHOSTTY_SUCCESS) {
        ghostty_render_state_row_iterator_free(row_iter);
        free(line);
        free(byte_col_start);
        free(byte_col_end);
        return;
    }

    GhosttyRenderStateRowCells cells = NULL;
    if (ghostty_render_state_row_cells_new(mt_terminal_get_allocator(), &cells) != GHOSTTY_SUCCESS) {
        ghostty_render_state_row_iterator_free(row_iter);
        free(line);
        free(byte_col_start);
        free(byte_col_end);
        return;
    }

    int row = 0;
    while (row < term_rows && ghostty_render_state_row_iterator_next(row_iter)) {
        memset(line, 0, max_line_bytes);
        memset(byte_col_start, 0, max_line_bytes * sizeof(int));
        memset(byte_col_end, 0, max_line_bytes * sizeof(int));
        size_t line_len = 0;
        int col = 0;

        if (ghostty_render_state_row_get(row_iter, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &cells) != GHOSTTY_SUCCESS) {
            row++;
            continue;
        }

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

            if (line_len + (size_t)utf8_len >= max_line_bytes) break;
            memcpy(line + line_len, utf8, (size_t)utf8_len);
            for (int i = 0; i < utf8_len; i++) {
                byte_col_start[line_len + (size_t)i] = col;
                byte_col_end[line_len + (size_t)i] = col + cell_span;
            }
            line_len += (size_t)utf8_len;
            line[line_len] = '\0';
            col += cell_span;
        }

        const char *match = line;
        while ((match = strcasestr_ascii(match, s->query)) != NULL) {
            size_t start = (size_t)(match - line);
            size_t len = strlen(s->query);
            size_t end = start + len - 1;
            if (end >= line_len) break;

            if (s->match_count < MT_SEARCH_MAX_MATCHES) {
                s->matches[s->match_count].row = row;
                s->matches[s->match_count].col_start = byte_col_start[start];
                s->matches[s->match_count].col_end = byte_col_end[end];
                if (s->matches[s->match_count].col_end <= s->matches[s->match_count].col_start) {
                    s->matches[s->match_count].col_end = s->matches[s->match_count].col_start + 1;
                }
                s->match_count++;
            }
            match += 1;
        }

        row++;
    }

    ghostty_render_state_row_cells_free(cells);
    ghostty_render_state_row_iterator_free(row_iter);
    free(line);
    free(byte_col_start);
    free(byte_col_end);

    if (s->match_count > 0) {
        s->current = 0;
    }
}

#else

void mt_search_execute(MtSearch *s, MtTerminal *term)
{
    /*
     * Search execution requires iterating the terminal's render state
     * via libghostty-vt. This is implemented only in the full build.
     */
    (void)term;
    if (!s || s->query[0] == '\0') {
        if (s) { s->match_count = 0; s->current = -1; }
        return;
    }
}

#endif

int mt_search_match_count(const MtSearch *s)
{
    return s ? s->match_count : 0;
}

int mt_search_current_index(const MtSearch *s)
{
    return s ? s->current : -1;
}

const MtSearchMatch *mt_search_get_match(const MtSearch *s, int index)
{
    if (!s || index < 0 || index >= s->match_count) return NULL;
    return &s->matches[index];
}
