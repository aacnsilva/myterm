#ifndef SPLITS_H
#define SPLITS_H

/*
 * splits.h — Split pane management.
 *
 * Each split pane contains a tab manager (or a terminal directly).
 * Splits form a binary tree: each node is either a leaf (terminal)
 * or a branch with two children and a split direction.
 */

#include "myterm.h"
#include <stdbool.h>

typedef enum {
    MT_SPLIT_HORIZONTAL,  /* top/bottom */
    MT_SPLIT_VERTICAL,    /* left/right */
} MtSplitDir;

typedef struct MtSplitNode MtSplitNode;

struct MtSplitNode {
    bool         is_leaf;
    MtSplitDir   dir;
    float        ratio;       /* 0.0-1.0, position of the divider */

    /* Leaf data */
    MtTerminal  *terminal;
    MtPty       *pty;

    /* Branch data */
    MtSplitNode *first;
    MtSplitNode *second;

    /* Bounds (set during layout) */
    float x, y, w, h;
};

typedef struct MtSplitManager MtSplitManager;

MtSplitManager *mt_splits_new(MtTerminal *term, MtPty *pty);
void            mt_splits_destroy(MtSplitManager *sm);

/* Split the currently focused pane */
bool mt_splits_split(MtSplitManager *sm, MtSplitDir dir, int cols, int rows);

/* Close the currently focused pane (returns false if last pane) */
bool mt_splits_close_focused(MtSplitManager *sm);
bool mt_splits_close_leaf(MtSplitManager *sm, const MtSplitNode *leaf);

/* Navigate focus */
void mt_splits_focus_next(MtSplitManager *sm);
void mt_splits_focus_prev(MtSplitManager *sm);
void mt_splits_focus_dir(MtSplitManager *sm, MtSplitDir dir, bool forward);
void mt_splits_focus_leaf(MtSplitManager *sm, const MtSplitNode *leaf);

/* Resize split ratio */
void mt_splits_resize(MtSplitManager *sm, float delta);
void mt_splits_resize_dir(MtSplitManager *sm, MtSplitDir dir, bool forward, float delta);

/* Layout: recompute bounds given the available area */
void mt_splits_layout(MtSplitManager *sm, float x, float y, float w, float h);

/* Get the focused pane's terminal/PTY */
MtTerminal *mt_splits_focused_terminal(MtSplitManager *sm);
MtPty      *mt_splits_focused_pty(MtSplitManager *sm);

/* Get the root for rendering traversal */
const MtSplitNode *mt_splits_root(const MtSplitManager *sm);
const MtSplitNode *mt_splits_focused_node(const MtSplitManager *sm);

/* Count total panes */
int mt_splits_count(const MtSplitManager *sm);

#endif /* SPLITS_H */
