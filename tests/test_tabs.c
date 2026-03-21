/*
 * test_tabs.c — Unit tests for tab management.
 */

#include "test_harness.h"
#include "tabs.h"

/* --- Tests --- */

TEST(create_tab_manager)
{
    MtTabManager *tm = mt_tabs_new();
    ASSERT(tm != NULL);
    ASSERT_EQ(mt_tabs_count(tm), 0);
    mt_tabs_destroy(tm);
}

TEST(null_safety)
{
    ASSERT_EQ(mt_tabs_count(NULL), 0);
    ASSERT_EQ(mt_tabs_active_index(NULL), -1);
    ASSERT(mt_tabs_active(NULL) == NULL);
    ASSERT(mt_tabs_active_terminal(NULL) == NULL);
    ASSERT(mt_tabs_active_pty(NULL) == NULL);
    ASSERT(mt_tabs_get(NULL, 0) == NULL);

    mt_tabs_destroy(NULL);
    mt_tabs_select(NULL, 0);
    mt_tabs_select_next(NULL);
    mt_tabs_select_prev(NULL);
    mt_tabs_close(NULL, 0);
    mt_tabs_move(NULL, 0, 1);
    mt_tabs_set_title(NULL, 0, "test");
    mt_tabs_mark_activity(NULL, 0);
}

TEST(add_single_tab)
{
    MtTabManager *tm = mt_tabs_new();
    int idx = mt_tabs_add(tm, 80, 24);
    ASSERT_EQ(idx, 0);
    ASSERT_EQ(mt_tabs_count(tm), 1);
    ASSERT_EQ(mt_tabs_active_index(tm), 0);

    MtTab *tab = mt_tabs_get(tm, 0);
    ASSERT(tab != NULL);
    ASSERT(tab->active == true);
    ASSERT(tab->terminal != NULL);
    ASSERT(tab->pty != NULL);

    mt_tabs_destroy(tm);
}

TEST(add_multiple_tabs)
{
    MtTabManager *tm = mt_tabs_new();

    for (int i = 0; i < 5; i++) {
        int idx = mt_tabs_add(tm, 80, 24);
        ASSERT_EQ(idx, i);
    }

    ASSERT_EQ(mt_tabs_count(tm), 5);
    /* First tab should still be active */
    ASSERT_EQ(mt_tabs_active_index(tm), 0);

    mt_tabs_destroy(tm);
}

TEST(select_tab)
{
    MtTabManager *tm = mt_tabs_new();
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);

    mt_tabs_select(tm, 2);
    ASSERT_EQ(mt_tabs_active_index(tm), 2);
    ASSERT(mt_tabs_get(tm, 2)->active == true);
    ASSERT(mt_tabs_get(tm, 0)->active == false);

    mt_tabs_select(tm, 0);
    ASSERT_EQ(mt_tabs_active_index(tm), 0);
    ASSERT(mt_tabs_get(tm, 0)->active == true);
    ASSERT(mt_tabs_get(tm, 2)->active == false);

    mt_tabs_destroy(tm);
}

TEST(select_out_of_bounds)
{
    MtTabManager *tm = mt_tabs_new();
    mt_tabs_add(tm, 80, 24);

    mt_tabs_select(tm, -1);
    ASSERT_EQ(mt_tabs_active_index(tm), 0);

    mt_tabs_select(tm, 100);
    ASSERT_EQ(mt_tabs_active_index(tm), 0);

    mt_tabs_destroy(tm);
}

TEST(select_next_prev)
{
    MtTabManager *tm = mt_tabs_new();
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);

    /* Start at tab 0 */
    ASSERT_EQ(mt_tabs_active_index(tm), 0);

    mt_tabs_select_next(tm);
    ASSERT_EQ(mt_tabs_active_index(tm), 1);

    mt_tabs_select_next(tm);
    ASSERT_EQ(mt_tabs_active_index(tm), 2);

    /* Wrap around */
    mt_tabs_select_next(tm);
    ASSERT_EQ(mt_tabs_active_index(tm), 0);

    /* Go backward */
    mt_tabs_select_prev(tm);
    ASSERT_EQ(mt_tabs_active_index(tm), 2);

    mt_tabs_select_prev(tm);
    ASSERT_EQ(mt_tabs_active_index(tm), 1);

    mt_tabs_destroy(tm);
}

TEST(close_tab_middle)
{
    MtTabManager *tm = mt_tabs_new();
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);

    mt_tabs_set_title(tm, 0, "Tab A");
    mt_tabs_set_title(tm, 1, "Tab B");
    mt_tabs_set_title(tm, 2, "Tab C");

    /* Close middle tab */
    mt_tabs_close(tm, 1);
    ASSERT_EQ(mt_tabs_count(tm), 2);
    ASSERT_STR_EQ(mt_tabs_get(tm, 0)->title, "Tab A");
    ASSERT_STR_EQ(mt_tabs_get(tm, 1)->title, "Tab C");

    mt_tabs_destroy(tm);
}

TEST(close_last_remaining_tab_rejected)
{
    MtTabManager *tm = mt_tabs_new();
    mt_tabs_add(tm, 80, 24);

    /* Closing the only tab should be a no-op */
    mt_tabs_close(tm, 0);
    ASSERT_EQ(mt_tabs_count(tm), 1);

    mt_tabs_destroy(tm);
}

TEST(close_active_tab)
{
    MtTabManager *tm = mt_tabs_new();
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);

    mt_tabs_select(tm, 1);
    mt_tabs_close(tm, 1);
    ASSERT_EQ(mt_tabs_count(tm), 2);
    /* Active should move to valid index */
    ASSERT(mt_tabs_active_index(tm) >= 0);
    ASSERT(mt_tabs_active_index(tm) < mt_tabs_count(tm));

    mt_tabs_destroy(tm);
}

TEST(move_tab_forward)
{
    MtTabManager *tm = mt_tabs_new();
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);

    mt_tabs_set_title(tm, 0, "A");
    mt_tabs_set_title(tm, 1, "B");
    mt_tabs_set_title(tm, 2, "C");

    mt_tabs_move(tm, 0, 2);
    ASSERT_STR_EQ(mt_tabs_get(tm, 0)->title, "B");
    ASSERT_STR_EQ(mt_tabs_get(tm, 1)->title, "C");
    ASSERT_STR_EQ(mt_tabs_get(tm, 2)->title, "A");

    mt_tabs_destroy(tm);
}

TEST(move_tab_backward)
{
    MtTabManager *tm = mt_tabs_new();
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);

    mt_tabs_set_title(tm, 0, "A");
    mt_tabs_set_title(tm, 1, "B");
    mt_tabs_set_title(tm, 2, "C");

    mt_tabs_move(tm, 2, 0);
    ASSERT_STR_EQ(mt_tabs_get(tm, 0)->title, "C");
    ASSERT_STR_EQ(mt_tabs_get(tm, 1)->title, "A");
    ASSERT_STR_EQ(mt_tabs_get(tm, 2)->title, "B");

    mt_tabs_destroy(tm);
}

TEST(move_tab_same_position)
{
    MtTabManager *tm = mt_tabs_new();
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);

    mt_tabs_set_title(tm, 0, "A");
    mt_tabs_set_title(tm, 1, "B");

    /* Move to same position — no-op */
    mt_tabs_move(tm, 0, 0);
    ASSERT_STR_EQ(mt_tabs_get(tm, 0)->title, "A");
    ASSERT_STR_EQ(mt_tabs_get(tm, 1)->title, "B");

    mt_tabs_destroy(tm);
}

TEST(set_title)
{
    MtTabManager *tm = mt_tabs_new();
    mt_tabs_add(tm, 80, 24);

    mt_tabs_set_title(tm, 0, "My Custom Title");
    ASSERT_STR_EQ(mt_tabs_get(tm, 0)->title, "My Custom Title");

    /* Invalid index should not crash */
    mt_tabs_set_title(tm, 5, "nope");
    mt_tabs_set_title(tm, -1, "nope");

    mt_tabs_destroy(tm);
}

TEST(mark_activity)
{
    MtTabManager *tm = mt_tabs_new();
    mt_tabs_add(tm, 80, 24);
    mt_tabs_add(tm, 80, 24);

    /* Mark activity on inactive tab */
    mt_tabs_mark_activity(tm, 1);
    ASSERT(mt_tabs_get(tm, 1)->has_activity == true);

    /* Mark activity on active tab — should be ignored */
    mt_tabs_mark_activity(tm, 0);
    ASSERT(mt_tabs_get(tm, 0)->has_activity == false);

    /* Selecting tab clears activity */
    mt_tabs_select(tm, 1);
    ASSERT(mt_tabs_get(tm, 1)->has_activity == false);

    mt_tabs_destroy(tm);
}

TEST(active_terminal_and_pty)
{
    MtTabManager *tm = mt_tabs_new();
    mt_tabs_add(tm, 80, 24);

    ASSERT(mt_tabs_active_terminal(tm) != NULL);
    ASSERT(mt_tabs_active_pty(tm) != NULL);

    mt_tabs_destroy(tm);
}

TEST(boundary_get)
{
    MtTabManager *tm = mt_tabs_new();
    mt_tabs_add(tm, 80, 24);

    ASSERT(mt_tabs_get(tm, 0) != NULL);
    ASSERT(mt_tabs_get(tm, -1) == NULL);
    ASSERT(mt_tabs_get(tm, 1) == NULL);
    ASSERT(mt_tabs_get(tm, 100) == NULL);

    mt_tabs_destroy(tm);
}

int main(void)
{
    TEST_SUITE("Tab Management");

    RUN_TEST(create_tab_manager);
    RUN_TEST(null_safety);
    RUN_TEST(add_single_tab);
    RUN_TEST(add_multiple_tabs);
    RUN_TEST(select_tab);
    RUN_TEST(select_out_of_bounds);
    RUN_TEST(select_next_prev);
    RUN_TEST(close_tab_middle);
    RUN_TEST(close_last_remaining_tab_rejected);
    RUN_TEST(close_active_tab);
    RUN_TEST(move_tab_forward);
    RUN_TEST(move_tab_backward);
    RUN_TEST(move_tab_same_position);
    RUN_TEST(set_title);
    RUN_TEST(mark_activity);
    RUN_TEST(active_terminal_and_pty);
    RUN_TEST(boundary_get);

    TEST_REPORT();
}
