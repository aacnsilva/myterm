/*
 * tabs.c — Tab management implementation.
 *
 * Manages multiple terminal sessions, each with its own
 * MtTerminal + MtPty pair, in a tabbed interface.
 */

#include "tabs.h"
#include "splits.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

struct MtTabManager {
    MtTab  tabs[MT_MAX_TABS];
    int    count;
    int    active;
    int    next_id;
};

static void destroy_tab(MtTab *tab)
{
    if (!tab) return;

    if (tab->splits) {
        mt_splits_destroy(tab->splits);
    } else {
        if (tab->pty) mt_pty_destroy(tab->pty);
        if (tab->terminal) mt_terminal_destroy(tab->terminal);
    }

    memset(tab, 0, sizeof(*tab));
}

MtTabManager *mt_tabs_new(void)
{
    MtTabManager *tm = calloc(1, sizeof(MtTabManager));
    if (!tm) return NULL;
    tm->next_id = 1;
    return tm;
}

void mt_tabs_destroy(MtTabManager *tm)
{
    if (!tm) return;
    for (int i = 0; i < tm->count; i++) {
        destroy_tab(&tm->tabs[i]);
    }
    free(tm);
}

int mt_tabs_add(MtTabManager *tm, int cols, int rows)
{
    if (!tm || tm->count >= MT_MAX_TABS) return -1;

    MtTerminal *term = mt_terminal_new(cols, rows);
    if (!term) return -1;

    MtPty *pty = mt_pty_new(cols, rows);
    if (!pty) {
        mt_terminal_destroy(term);
        return -1;
    }

    MtSplitManager *splits = mt_splits_new(term, pty);
    if (!splits) {
        mt_pty_destroy(pty);
        mt_terminal_destroy(term);
        return -1;
    }

    int idx = tm->count;
    MtTab *tab = &tm->tabs[idx];
    tab->terminal = term;
    tab->pty = pty;
    tab->splits = splits;
    tab->id = tm->next_id++;
    tab->active = false;
    tab->has_activity = false;
    tab->renaming = false;
    snprintf(tab->title, MT_TAB_MAX_TITLE, "Shell %d", tab->id);
    tab->rename_buf[0] = '\0';

    tm->count++;

    /* Auto-select if it's the first tab */
    if (tm->count == 1) {
        mt_tabs_select(tm, 0);
    }

    return idx;
}

void mt_tabs_close(MtTabManager *tm, int index)
{
    if (!tm || index < 0 || index >= tm->count) return;
    if (tm->count == 1) return; /* Don't close last tab */

    destroy_tab(&tm->tabs[index]);

    /* Shift remaining tabs */
    for (int i = index; i < tm->count - 1; i++) {
        tm->tabs[i] = tm->tabs[i + 1];
    }
    memset(&tm->tabs[tm->count - 1], 0, sizeof(tm->tabs[tm->count - 1]));
    tm->count--;

    /* Adjust active index */
    if (tm->active >= tm->count) {
        tm->active = tm->count - 1;
    }
    if (tm->active == index && tm->active > 0) {
        tm->active--;
    }
    tm->tabs[tm->active].active = true;
}

void mt_tabs_select(MtTabManager *tm, int index)
{
    if (!tm || index < 0 || index >= tm->count) return;

    /* Deselect current */
    if (tm->active >= 0 && tm->active < tm->count) {
        tm->tabs[tm->active].active = false;
        tm->tabs[tm->active].renaming = false;
    }

    tm->active = index;
    tm->tabs[index].active = true;
    tm->tabs[index].has_activity = false;
}

void mt_tabs_select_next(MtTabManager *tm)
{
    if (!tm || tm->count <= 1) return;
    mt_tabs_select(tm, (tm->active + 1) % tm->count);
}

void mt_tabs_select_prev(MtTabManager *tm)
{
    if (!tm || tm->count <= 1) return;
    mt_tabs_select(tm, (tm->active - 1 + tm->count) % tm->count);
}

void mt_tabs_move(MtTabManager *tm, int from, int to)
{
    if (!tm || from < 0 || from >= tm->count ||
        to < 0 || to >= tm->count || from == to) return;

    MtTab temp = tm->tabs[from];

    if (from < to) {
        for (int i = from; i < to; i++)
            tm->tabs[i] = tm->tabs[i + 1];
    } else {
        for (int i = from; i > to; i--)
            tm->tabs[i] = tm->tabs[i - 1];
    }

    tm->tabs[to] = temp;

    /* Update active index */
    if (tm->active == from) {
        tm->active = to;
    } else if (from < to && tm->active > from && tm->active <= to) {
        tm->active--;
    } else if (from > to && tm->active >= to && tm->active < from) {
        tm->active++;
    }
}

int mt_tabs_count(const MtTabManager *tm)
{
    return tm ? tm->count : 0;
}

int mt_tabs_active_index(const MtTabManager *tm)
{
    return tm ? tm->active : -1;
}

MtTab *mt_tabs_get(MtTabManager *tm, int index)
{
    if (!tm || index < 0 || index >= tm->count) return NULL;
    return &tm->tabs[index];
}

MtTab *mt_tabs_active(MtTabManager *tm)
{
    if (!tm || tm->count == 0) return NULL;
    return &tm->tabs[tm->active];
}

MtTerminal *mt_tabs_active_terminal(MtTabManager *tm)
{
    MtTab *tab = mt_tabs_active(tm);
    if (!tab) return NULL;
    return tab->splits ? mt_splits_focused_terminal(tab->splits) : tab->terminal;
}

MtPty *mt_tabs_active_pty(MtTabManager *tm)
{
    MtTab *tab = mt_tabs_active(tm);
    if (!tab) return NULL;
    return tab->splits ? mt_splits_focused_pty(tab->splits) : tab->pty;
}

MtSplitManager *mt_tabs_get_splits(MtTabManager *tm, int index)
{
    MtTab *tab = mt_tabs_get(tm, index);
    return tab ? tab->splits : NULL;
}

MtSplitManager *mt_tabs_active_splits(MtTabManager *tm)
{
    MtTab *tab = mt_tabs_active(tm);
    return tab ? tab->splits : NULL;
}

void mt_tabs_set_title(MtTabManager *tm, int index, const char *title)
{
    if (!tm || index < 0 || index >= tm->count || !title) return;
    strncpy(tm->tabs[index].title, title, MT_TAB_MAX_TITLE - 1);
    tm->tabs[index].title[MT_TAB_MAX_TITLE - 1] = '\0';
}

void mt_tabs_mark_activity(MtTabManager *tm, int index)
{
    if (!tm || index < 0 || index >= tm->count) return;
    if (index != tm->active) {
        tm->tabs[index].has_activity = true;
    }
}

void mt_tabs_begin_rename(MtTabManager *tm, int index)
{
    if (!tm || index < 0 || index >= tm->count) return;
    MtTab *tab = &tm->tabs[index];
    tab->renaming = true;
    strncpy(tab->rename_buf, tab->title, MT_TAB_MAX_TITLE - 1);
    tab->rename_buf[MT_TAB_MAX_TITLE - 1] = '\0';
}

void mt_tabs_cancel_rename(MtTabManager *tm, int index)
{
    if (!tm || index < 0 || index >= tm->count) return;
    tm->tabs[index].renaming = false;
    tm->tabs[index].rename_buf[0] = '\0';
}

void mt_tabs_commit_rename(MtTabManager *tm, int index)
{
    if (!tm || index < 0 || index >= tm->count) return;

    MtTab *tab = &tm->tabs[index];
    size_t start = 0;
    size_t end = strlen(tab->rename_buf);

    while (start < end && isspace((unsigned char)tab->rename_buf[start])) start++;
    while (end > start && isspace((unsigned char)tab->rename_buf[end - 1])) end--;

    if (end > start) {
        size_t len = end - start;
        if (len >= MT_TAB_MAX_TITLE) len = MT_TAB_MAX_TITLE - 1;
        memmove(tab->title, tab->rename_buf + start, len);
        tab->title[len] = '\0';
    }

    tab->renaming = false;
    tab->rename_buf[0] = '\0';
}

void mt_tabs_rename_backspace(MtTabManager *tm, int index)
{
    if (!tm || index < 0 || index >= tm->count) return;
    MtTab *tab = &tm->tabs[index];
    size_t len = strlen(tab->rename_buf);
    if (len > 0) {
        tab->rename_buf[len - 1] = '\0';
    }
}

void mt_tabs_rename_append(MtTabManager *tm, int index, int ch)
{
    if (!tm || index < 0 || index >= tm->count) return;
    MtTab *tab = &tm->tabs[index];
    size_t len = strlen(tab->rename_buf);
    if (ch < 32 || len >= MT_TAB_MAX_TITLE - 2) return;
    tab->rename_buf[len] = (char)ch;
    tab->rename_buf[len + 1] = '\0';
}

bool mt_tabs_is_renaming(const MtTabManager *tm, int index)
{
    if (!tm || index < 0 || index >= tm->count) return false;
    return tm->tabs[index].renaming;
}
