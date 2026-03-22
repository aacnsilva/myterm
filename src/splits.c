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
        if (n->pty) mt_pty_destroy(n->pty);
        if (n->terminal) mt_terminal_destroy(n->terminal);
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

static bool node_contains(MtSplitNode *root, MtSplitNode *target)
{
    if (!root || !target) return false;
    if (root == target) return true;
    if (root->is_leaf) return false;
    return node_contains(root->first, target) || node_contains(root->second, target);
}

static void clamp_parent_ratio(MtSplitNode *parent)
{
    if (!parent) return;
    if (parent->ratio < 0.1f) parent->ratio = 0.1f;
    if (parent->ratio > 0.9f) parent->ratio = 0.9f;
}

static float mt_absf(float v)
{
    return v < 0.0f ? -v : v;
}

static float overlap_amount(float a0, float a1, float b0, float b1)
{
    float lo = (a0 > b0) ? a0 : b0;
    float hi = (a1 < b1) ? a1 : b1;
    return hi - lo;
}

static void focus_dir_find_best(MtSplitNode *node,
                                MtSplitNode *focused,
                                MtSplitDir dir,
                                bool forward,
                                MtSplitNode **best,
                                float *best_score)
{
    if (!node || !focused) return;
    if (!node->is_leaf) {
        focus_dir_find_best(node->first, focused, dir, forward, best, best_score);
        focus_dir_find_best(node->second, focused, dir, forward, best, best_score);
        return;
    }
    if (node == focused) return;

    float fx0 = focused->x;
    float fy0 = focused->y;
    float fx1 = focused->x + focused->w;
    float fy1 = focused->y + focused->h;
    float nx0 = node->x;
    float ny0 = node->y;
    float nx1 = node->x + node->w;
    float ny1 = node->y + node->h;

    float primary = -1.0f;
    float secondary = 0.0f;
    float overlap = 0.0f;

    if (dir == MT_SPLIT_VERTICAL) {
        if (forward) {
            primary = nx0 - fx1;
        } else {
            primary = fx0 - nx1;
        }
        overlap = overlap_amount(fy0, fy1, ny0, ny1);
        secondary = mt_absf(((ny0 + ny1) * 0.5f) - ((fy0 + fy1) * 0.5f));
    } else {
        if (forward) {
            primary = ny0 - fy1;
        } else {
            primary = fy0 - ny1;
        }
        overlap = overlap_amount(fx0, fx1, nx0, nx1);
        secondary = mt_absf(((nx0 + nx1) * 0.5f) - ((fx0 + fx1) * 0.5f));
    }

    if (primary < 0.0f) return;

    float score = primary * 1000.0f + secondary;
    if (overlap > 0.0f) {
        score -= overlap;
    } else {
        score += 50000.0f;
    }

    if (!*best || score < *best_score) {
        *best = node;
        *best_score = score;
    }
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

bool mt_splits_close_leaf(MtSplitManager *sm, const MtSplitNode *leaf)
{
    if (!sm || !leaf || sm->count <= 1) return false;

    MtSplitNode *target = (MtSplitNode *)leaf;
    MtSplitNode *parent = node_find_parent(sm->root, target);
    if (!parent) return false;

    MtSplitNode *sibling = (parent->first == target) ? parent->second : parent->first;
    MtSplitNode *new_focus = NULL;

    if (sm->focused == target) {
        new_focus = sibling->is_leaf ? parent : node_first_leaf(sibling);
    } else if (sm->focused == sibling && sibling->is_leaf) {
        new_focus = parent;
    }

    if (target->pty) mt_pty_destroy(target->pty);
    if (target->terminal) mt_terminal_destroy(target->terminal);
    free(target);

    parent->is_leaf = sibling->is_leaf;
    parent->dir = sibling->dir;
    parent->ratio = sibling->ratio;
    parent->terminal = sibling->terminal;
    parent->pty = sibling->pty;
    parent->first = sibling->first;
    parent->second = sibling->second;
    parent->x = sibling->x;
    parent->y = sibling->y;
    parent->w = sibling->w;
    parent->h = sibling->h;
    free(sibling);

    sm->count--;
    if (new_focus) sm->focused = new_focus;
    return true;
}

bool mt_splits_close_focused(MtSplitManager *sm)
{
    return sm ? mt_splits_close_leaf(sm, sm->focused) : false;
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
    if (!sm || sm->count <= 1 || !sm->focused) return;

    MtSplitNode *best = NULL;
    float best_score = 0.0f;
    focus_dir_find_best(sm->root, sm->focused, dir, forward, &best, &best_score);
    if (best) {
        sm->focused = best;
        return;
    }

    if (forward) mt_splits_focus_next(sm);
    else         mt_splits_focus_prev(sm);
}

void mt_splits_focus_leaf(MtSplitManager *sm, const MtSplitNode *leaf)
{
    if (!sm || !leaf) return;
    MtSplitNode *target = (MtSplitNode *)leaf;
    if (!target->is_leaf) return;
    if (node_contains(sm->root, target)) {
        sm->focused = target;
    }
}

void mt_splits_resize(MtSplitManager *sm, float delta)
{
    if (!sm || !sm->focused) return;
    MtSplitNode *parent = node_find_parent(sm->root, sm->focused);
    if (!parent) return;

    parent->ratio += delta;
    clamp_parent_ratio(parent);
}

void mt_splits_resize_dir(MtSplitManager *sm, MtSplitDir dir, bool forward, float delta)
{
    if (!sm || !sm->focused || delta <= 0.0f) return;

    MtSplitNode *child = sm->focused;
    MtSplitNode *parent = node_find_parent(sm->root, child);
    while (parent && parent->dir != dir) {
        child = parent;
        parent = node_find_parent(sm->root, child);
    }
    if (!parent) return;

    bool in_first = node_contains(parent->first, sm->focused);
    float signed_delta = 0.0f;

    if (dir == MT_SPLIT_VERTICAL) {
        if (forward) {
            signed_delta = in_first ? delta : -delta;
        } else {
            signed_delta = in_first ? -delta : delta;
        }
    } else {
        if (forward) {
            signed_delta = in_first ? delta : -delta;
        } else {
            signed_delta = in_first ? -delta : delta;
        }
    }

    parent->ratio += signed_delta;
    clamp_parent_ratio(parent);
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

const MtSplitNode *mt_splits_focused_node(const MtSplitManager *sm)
{
    return sm ? sm->focused : NULL;
}

int mt_splits_count(const MtSplitManager *sm)
{
    return sm ? sm->count : 0;
}
