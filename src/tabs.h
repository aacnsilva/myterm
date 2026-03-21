#ifndef TABS_H
#define TABS_H

/*
 * tabs.h — Tab management for myterm.
 *
 * Each tab owns its own MtTerminal + MtPty pair, allowing
 * independent shell sessions within a single window.
 */

#include "myterm.h"
#include <stdbool.h>

#define MT_MAX_TABS      32
#define MT_TAB_BAR_HEIGHT 32.0f
#define MT_TAB_MAX_TITLE  64

typedef struct {
    MtTerminal *terminal;
    MtPty      *pty;
    char        title[MT_TAB_MAX_TITLE];
    int         id;
    bool        active;
    bool        has_activity; /* Bell or unseen output in background tab */
} MtTab;

typedef struct MtTabManager MtTabManager;

/* Create/destroy */
MtTabManager *mt_tabs_new(void);
void          mt_tabs_destroy(MtTabManager *tm);

/* Tab operations */
int   mt_tabs_add(MtTabManager *tm, int cols, int rows);
void  mt_tabs_close(MtTabManager *tm, int index);
void  mt_tabs_select(MtTabManager *tm, int index);
void  mt_tabs_select_next(MtTabManager *tm);
void  mt_tabs_select_prev(MtTabManager *tm);
void  mt_tabs_move(MtTabManager *tm, int from, int to);

/* Accessors */
int          mt_tabs_count(const MtTabManager *tm);
int          mt_tabs_active_index(const MtTabManager *tm);
MtTab       *mt_tabs_get(MtTabManager *tm, int index);
MtTab       *mt_tabs_active(MtTabManager *tm);
MtTerminal  *mt_tabs_active_terminal(MtTabManager *tm);
MtPty       *mt_tabs_active_pty(MtTabManager *tm);

/* Set the title for a tab (e.g. from OSC sequence) */
void mt_tabs_set_title(MtTabManager *tm, int index, const char *title);

/* Mark a background tab as having activity */
void mt_tabs_mark_activity(MtTabManager *tm, int index);

#endif /* TABS_H */
