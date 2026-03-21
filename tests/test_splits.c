/*
 * test_splits.c — Unit tests for split pane management.
 *
 * Note: Split operations that create terminals/PTYs require libghostty,
 * so we test the structural logic with NULL terminals where possible
 * and test the public API with proper null checks.
 */

#include "test_harness.h"
#include "splits.h"

TEST(null_safety)
{
    ASSERT_EQ(mt_splits_count(NULL), 0);
    ASSERT(mt_splits_focused_terminal(NULL) == NULL);
    ASSERT(mt_splits_focused_pty(NULL) == NULL);
    ASSERT(mt_splits_root(NULL) == NULL);

    mt_splits_destroy(NULL);
    mt_splits_focus_next(NULL);
    mt_splits_focus_prev(NULL);
    mt_splits_resize(NULL, 0.1f);
    mt_splits_layout(NULL, 0, 0, 100, 100);
}

TEST(create_with_null_terminal)
{
    /* Create a split manager with NULL terminal/pty (structural test) */
    MtSplitManager *sm = mt_splits_new(NULL, NULL);
    ASSERT(sm != NULL);
    ASSERT_EQ(mt_splits_count(sm), 1);
    ASSERT(mt_splits_root(sm) != NULL);
    ASSERT(mt_splits_root(sm)->is_leaf == true);
    ASSERT(mt_splits_focused_terminal(sm) == NULL);
    ASSERT(mt_splits_focused_pty(sm) == NULL);
    mt_splits_destroy(sm);
}

TEST(close_last_pane_rejected)
{
    MtSplitManager *sm = mt_splits_new(NULL, NULL);
    ASSERT(sm != NULL);

    /* Should not close the last pane */
    bool result = mt_splits_close_focused(sm);
    ASSERT(result == false);
    ASSERT_EQ(mt_splits_count(sm), 1);

    mt_splits_destroy(sm);
}

TEST(layout_single_pane)
{
    MtSplitManager *sm = mt_splits_new(NULL, NULL);
    mt_splits_layout(sm, 10, 20, 800, 600);

    const MtSplitNode *root = mt_splits_root(sm);
    ASSERT(root != NULL);
    ASSERT(root->x == 10.0f);
    ASSERT(root->y == 20.0f);
    ASSERT(root->w == 800.0f);
    ASSERT(root->h == 600.0f);

    mt_splits_destroy(sm);
}

TEST(focus_next_single_pane)
{
    MtSplitManager *sm = mt_splits_new(NULL, NULL);

    /* Focus next/prev with single pane should be a no-op */
    mt_splits_focus_next(sm);
    mt_splits_focus_prev(sm);
    ASSERT_EQ(mt_splits_count(sm), 1);

    mt_splits_destroy(sm);
}

TEST(resize_no_parent)
{
    MtSplitManager *sm = mt_splits_new(NULL, NULL);

    /* Resize with single pane (root has no parent) should not crash */
    mt_splits_resize(sm, 0.1f);
    mt_splits_resize(sm, -0.1f);

    mt_splits_destroy(sm);
}

TEST(focus_dir)
{
    MtSplitManager *sm = mt_splits_new(NULL, NULL);

    /* Focus direction with single pane should not crash */
    mt_splits_focus_dir(sm, MT_SPLIT_HORIZONTAL, true);
    mt_splits_focus_dir(sm, MT_SPLIT_VERTICAL, false);

    mt_splits_destroy(sm);
}

int main(void)
{
    TEST_SUITE("Split Panes");

    RUN_TEST(null_safety);
    RUN_TEST(create_with_null_terminal);
    RUN_TEST(close_last_pane_rejected);
    RUN_TEST(layout_single_pane);
    RUN_TEST(focus_next_single_pane);
    RUN_TEST(resize_no_parent);
    RUN_TEST(focus_dir);

    TEST_REPORT();
}
