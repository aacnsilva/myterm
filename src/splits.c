/*
 * splits.c — Split pane management.
 *
 * Binary tree of split panes. Each leaf holds a terminal+PTY pair.
 */

#include "splits.h"
#include <stdlib.h>
#include <string.h>

struct MtSplitManager {
    MtSplitNode *root;
    MtSplitNode *focused;
    int          count;
};

static MtSplitNode *node_new_leaf(MtTerminal *term, MtPty *pty)
{
    MtSplitNode *n = calloc(1, sizeof(MtSplitNode));
    if (!n) return NULL;
    n->is_leaf = true;
    n->terminal = term;
    n->pty = pty;
    n->ratio = 0.5f;
    return n;
}

static void node_destroy(MtSplitNode *n)
{
    if (!n) return;
    if (n->is_leaf) {
        /* Terminal and PTY are owned by the tab manager; don't destroy here
         * unless this is standalone usage */
    } else {
        node_destroy(n->first);
        node_destroy(n->second);
    }
    free(n);
}

static void node_layout(MtSplitNode *n, float x, float y, float w, float h)
{
    if (!n) return;
    n->x = x; n->y = y; n->w = w; n->h = h;

    if (n->is_leaf) return;

    if (n->dir == MT_SPLIT_VERTICAL) {
        float split_x = w * n->ratio;
        node_layout(n->first, x, y, split_x - 1, h);
        node_layout(n->second, x + split_x + 1, y, w - split_x - 1, h);
    } else {
        float split_y = h * n->ratio;
        node_layout(n->first, x, y, w, split_y - 1);
        node_layout(n->second, x, y + split_y + 1, w, h - split_y - 1);
    }
}

/* Find next leaf in depth-first order */
static MtSplitNode *node_next_leaf(MtSplitNode *n, MtSplitNode *target, bool *found)
{
    if (!n) return NULL;
    if (n->is_leaf) {
        if (*found) return n;
        if (n == target) *found = true;
        return NULL;
    }
    MtSplitNode *r = node_next_leaf(n->first, target, found);
    if (r) return r;
    return node_next_leaf(n->second, target, found);
}

/* Find first leaf */
static MtSplitNode *node_first_leaf(MtSplitNode *n)
{
    if (!n) return NULL;
    if (n->is_leaf) return n;
    return node_first_leaf(n->first);
}

/* Find last leaf */
static MtSplitNode *node_last_leaf(MtSplitNode *n)
{
    if (!n) return NULL;
    if (n->is_leaf) return n;
    return node_last_leaf(n->second);
}

/* Find parent of a node */
static MtSplitNode *node_find_parent(MtSplitNode *root, MtSplitNode *target)
{
    if (!root || root->is_leaf) return NULL;
    if (root->first == target || root->second == target) return root;
    MtSplitNode *p = node_find_parent(root->first, target);
    if (p) return p;
    return node_find_parent(root->second, target);
}

MtSplitManager *mt_splits_new(MtTerminal *term, MtPty *pty)
{
    MtSplitManager *sm = calloc(1, sizeof(MtSplitManager));
    if (!sm) return NULL;

    sm->root = node_new_leaf(term, pty);
    if (!sm->root) {
        free(sm);
        return NULL;
    }
    sm->focused = sm->root;
    sm->count = 1;
    return sm;
}

void mt_splits_destroy(MtSplitManager *sm)
{
    if (!sm) return;
    node_destroy(sm->root);
    free(sm);
}

bool mt_splits_split(MtSplitManager *sm, MtSplitDir dir, int cols, int rows)
{
    if (!sm || !sm->focused || !sm->focused->is_leaf) return false;

    MtTerminal *new_term = mt_terminal_new(cols, rows);
    if (!new_term) return false;

    MtPty *new_pty = mt_pty_new(cols, rows);
    if (!new_pty) {
        mt_terminal_destroy(new_term);
        return false;
    }

    /* Create new leaf for the new pane */
    MtSplitNode *new_leaf = node_new_leaf(new_term, new_pty);
    if (!new_leaf) {
        mt_pty_destroy(new_pty);
        mt_terminal_destroy(new_term);
        return false;
    }

    /* Create a copy of the current focused leaf */
    MtSplitNode *old_leaf = node_new_leaf(sm->focused->terminal, sm->focused->pty);
    if (!old_leaf) {
        free(new_leaf);
        mt_pty_destroy(new_pty);
        mt_terminal_destroy(new_term);
        return false;
    }

    /* Turn the focused node into a branch */
    sm->focused->is_leaf = false;
    sm->focused->terminal = NULL;
    sm->focused->pty = NULL;
    sm->focused->dir = dir;
    sm->focused->ratio = 0.5f;
    sm->focused->first = old_leaf;
    sm->focused->second = new_leaf;

    sm->focused = new_leaf;
    sm->count++;
    return true;
}

bool mt_splits_close_focused(MtSplitManager *sm)
{
    if (!sm || sm->count <= 1) return false;

    MtSplitNode *parent = node_find_parent(sm->root, sm->focused);
    if (!parent) return false;

    /* The sibling takes over the parent's position */
    MtSplitNode *sibling = (parent->first == sm->focused)
        ? parent->second : parent->first;

    /* Destroy the focused pane's terminal and PTY */
    mt_pty_destroy(sm->focused->pty);
    mt_terminal_destroy(sm->focused->terminal);
    free(sm->focused);

    /* Copy sibling data into parent */
    parent->is_leaf = sibling->is_leaf;
    parent->dir = sibling->dir;
    parent->ratio = sibling->ratio;
    parent->terminal = sibling->terminal;
    parent->pty = sibling->pty;
    parent->first = sibling->first;
    parent->second = sibling->second;
    free(sibling);

    sm->count--;
    sm->focused = node_first_leaf(parent);
    return true;
}

void mt_splits_focus_next(MtSplitManager *sm)
{
    if (!sm || sm->count <= 1) return;
    bool found = false;
    MtSplitNode *next = node_next_leaf(sm->root, sm->focused, &found);
    if (!next) next = node_first_leaf(sm->root);
    if (next) sm->focused = next;
}

void mt_splits_focus_prev(MtSplitManager *sm)
{
    if (!sm || sm->count <= 1) return;
    /* Simple approach: iterate all leaves and pick the one before focused */
    MtSplitNode *prev = NULL;
    MtSplitNode *cur = node_first_leaf(sm->root);

    while (cur) {
        if (cur == sm->focused) {
            if (prev) {
                sm->focused = prev;
                return;
            } else {
                sm->focused = node_last_leaf(sm->root);
                return;
            }
        }
        prev = cur;
        bool found = false;
        cur = node_next_leaf(sm->root, cur, &found);
    }
}

void mt_splits_focus_dir(MtSplitManager *sm, MtSplitDir dir, bool forward)
{
    (void)dir;
    if (forward) mt_splits_focus_next(sm);
    else         mt_splits_focus_prev(sm);
}

void mt_splits_resize(MtSplitManager *sm, float delta)
{
    if (!sm || !sm->focused) return;
    MtSplitNode *parent = node_find_parent(sm->root, sm->focused);
    if (!parent) return;

    parent->ratio += delta;
    if (parent->ratio < 0.1f) parent->ratio = 0.1f;
    if (parent->ratio > 0.9f) parent->ratio = 0.9f;
}

void mt_splits_layout(MtSplitManager *sm, float x, float y, float w, float h)
{
    if (!sm) return;
    node_layout(sm->root, x, y, w, h);
}

MtTerminal *mt_splits_focused_terminal(MtSplitManager *sm)
{
    return (sm && sm->focused) ? sm->focused->terminal : NULL;
}

MtPty *mt_splits_focused_pty(MtSplitManager *sm)
{
    return (sm && sm->focused) ? sm->focused->pty : NULL;
}

const MtSplitNode *mt_splits_root(const MtSplitManager *sm)
{
    return sm ? sm->root : NULL;
}

int mt_splits_count(const MtSplitManager *sm)
{
    return sm ? sm->count : 0;
}
