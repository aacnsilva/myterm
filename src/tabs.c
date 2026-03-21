/*
 * tabs.c — Tab management implementation.
 *
 * Manages multiple terminal sessions, each with its own
 * MtTerminal + MtPty pair, in a tabbed interface.
 */

#include "tabs.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct MtTabManager {
    MtTab  tabs[MT_MAX_TABS];
    int    count;
    int    active;
    int    next_id;
};

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
        mt_pty_destroy(tm->tabs[i].pty);
        mt_terminal_destroy(tm->tabs[i].terminal);
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

    int idx = tm->count;
    MtTab *tab = &tm->tabs[idx];
    tab->terminal = term;
    tab->pty = pty;
    tab->id = tm->next_id++;
    tab->active = false;
    tab->has_activity = false;
    snprintf(tab->title, MT_TAB_MAX_TITLE, "Shell %d", tab->id);

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

    mt_pty_destroy(tm->tabs[index].pty);
    mt_terminal_destroy(tm->tabs[index].terminal);

    /* Shift remaining tabs */
    for (int i = index; i < tm->count - 1; i++) {
        tm->tabs[i] = tm->tabs[i + 1];
    }
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
    return tab ? tab->terminal : NULL;
}

MtPty *mt_tabs_active_pty(MtTabManager *tm)
{
    MtTab *tab = mt_tabs_active(tm);
    return tab ? tab->pty : NULL;
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
