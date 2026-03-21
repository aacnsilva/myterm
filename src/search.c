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

void mt_search_execute(MtSearch *s, MtTerminal *term)
{
    /*
     * Search execution requires iterating the terminal's render state
     * via libghostty-vt. This is implemented in search_ghostty.c
     * when building with libghostty. For the stub/test build,
     * this is a no-op.
     */
    (void)term;
    if (!s || s->query[0] == '\0') {
        if (s) { s->match_count = 0; s->current = -1; }
        return;
    }

    /* The actual ghostty-based search is done in search_ghostty.c.
     * This base implementation clears results when the query changes,
     * as a fallback. */
}

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
