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

#define MT_MAX_TABS       32
#define MT_TAB_BAR_HEIGHT 32.0f
#define MT_TAB_MAX_TITLE  64

typedef struct MtSplitManager MtSplitManager;

typedef struct {
    MtTerminal      *terminal;
    MtPty           *pty;
    MtSplitManager  *splits;
    char             title[MT_TAB_MAX_TITLE];
    char             rename_buf[MT_TAB_MAX_TITLE];
    int              id;
    bool             active;
    bool             has_activity; /* Bell or unseen output in background tab */
    bool             renaming;
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
int              mt_tabs_count(const MtTabManager *tm);
int              mt_tabs_active_index(const MtTabManager *tm);
MtTab           *mt_tabs_get(MtTabManager *tm, int index);
MtTab           *mt_tabs_active(MtTabManager *tm);
MtTerminal      *mt_tabs_active_terminal(MtTabManager *tm);
MtPty           *mt_tabs_active_pty(MtTabManager *tm);
MtSplitManager  *mt_tabs_get_splits(MtTabManager *tm, int index);
MtSplitManager  *mt_tabs_active_splits(MtTabManager *tm);

/* Set the title for a tab (e.g. from OSC sequence) */
void mt_tabs_set_title(MtTabManager *tm, int index, const char *title);

/* Mark a background tab as having activity */
void mt_tabs_mark_activity(MtTabManager *tm, int index);

/* Inline rename support */
void mt_tabs_begin_rename(MtTabManager *tm, int index);
void mt_tabs_cancel_rename(MtTabManager *tm, int index);
void mt_tabs_commit_rename(MtTabManager *tm, int index);
void mt_tabs_rename_backspace(MtTabManager *tm, int index);
void mt_tabs_rename_append(MtTabManager *tm, int index, int ch);
bool mt_tabs_is_renaming(const MtTabManager *tm, int index);

#endif /* TABS_H */
