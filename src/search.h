#ifndef SEARCH_H
#define SEARCH_H

/*
 * search.h — In-terminal text search (Ctrl+Shift+F).
 */

#include "myterm.h"
#include <stdbool.h>

#define MT_SEARCH_MAX_QUERY 256
#define MT_SEARCH_MAX_MATCHES 4096

typedef struct {
    int row;
    int col_start;
    int col_end;
} MtSearchMatch;

typedef struct MtSearch MtSearch;

MtSearch *mt_search_new(void);
void      mt_search_destroy(MtSearch *s);

/* Start/stop search mode */
void mt_search_open(MtSearch *s);
void mt_search_close(MtSearch *s);
bool mt_search_is_active(const MtSearch *s);

/* Update query text (triggers re-search) */
void mt_search_set_query(MtSearch *s, const char *query);
const char *mt_search_get_query(const MtSearch *s);

/* Navigate between matches */
void mt_search_next(MtSearch *s);
void mt_search_prev(MtSearch *s);

/* Execute search against terminal content */
void mt_search_execute(MtSearch *s, MtTerminal *term);

/* Results */
int              mt_search_match_count(const MtSearch *s);
int              mt_search_current_index(const MtSearch *s);
const MtSearchMatch *mt_search_get_match(const MtSearch *s, int index);

#endif /* SEARCH_H */
