/*
 * test_search.c — Unit tests for in-terminal search.
 */

#include "test_harness.h"
#include "search.h"

TEST(create_destroy)
{
    MtSearch *s = mt_search_new();
    ASSERT(s != NULL);
    ASSERT(mt_search_is_active(s) == false);
    ASSERT_EQ(mt_search_match_count(s), 0);
    ASSERT_EQ(mt_search_current_index(s), -1);
    mt_search_destroy(s);
}

TEST(null_safety)
{
    ASSERT(mt_search_is_active(NULL) == false);
    ASSERT_EQ(mt_search_match_count(NULL), 0);
    ASSERT_EQ(mt_search_current_index(NULL), -1);
    ASSERT(mt_search_get_match(NULL, 0) == NULL);

    mt_search_destroy(NULL);
    mt_search_open(NULL);
    mt_search_close(NULL);
    mt_search_set_query(NULL, "test");
    mt_search_next(NULL);
    mt_search_prev(NULL);
}

TEST(open_close)
{
    MtSearch *s = mt_search_new();

    mt_search_open(s);
    ASSERT(mt_search_is_active(s) == true);

    mt_search_close(s);
    ASSERT(mt_search_is_active(s) == false);

    mt_search_destroy(s);
}

TEST(set_query)
{
    MtSearch *s = mt_search_new();
    mt_search_open(s);

    mt_search_set_query(s, "hello");
    ASSERT_STR_EQ(mt_search_get_query(s), "hello");

    mt_search_set_query(s, "world");
    ASSERT_STR_EQ(mt_search_get_query(s), "world");

    /* Empty query */
    mt_search_set_query(s, "");
    ASSERT_STR_EQ(mt_search_get_query(s), "");

    mt_search_destroy(s);
}

TEST(query_cleared_on_close)
{
    MtSearch *s = mt_search_new();
    mt_search_open(s);
    mt_search_set_query(s, "test");
    mt_search_close(s);
    ASSERT_STR_EQ(mt_search_get_query(s), "");
    mt_search_destroy(s);
}

TEST(navigate_no_matches)
{
    MtSearch *s = mt_search_new();
    mt_search_open(s);

    /* Navigate with no matches should not crash */
    mt_search_next(s);
    mt_search_prev(s);
    ASSERT_EQ(mt_search_current_index(s), -1);

    mt_search_destroy(s);
}

TEST(get_match_out_of_bounds)
{
    MtSearch *s = mt_search_new();
    ASSERT(mt_search_get_match(s, -1) == NULL);
    ASSERT(mt_search_get_match(s, 0) == NULL);
    ASSERT(mt_search_get_match(s, 100) == NULL);
    mt_search_destroy(s);
}

TEST(query_max_length)
{
    MtSearch *s = mt_search_new();
    mt_search_open(s);

    /* Set a very long query — should be truncated */
    char long_query[MT_SEARCH_MAX_QUERY + 100];
    memset(long_query, 'a', sizeof(long_query) - 1);
    long_query[sizeof(long_query) - 1] = '\0';

    mt_search_set_query(s, long_query);
    const char *q = mt_search_get_query(s);
    ASSERT(strlen(q) <= MT_SEARCH_MAX_QUERY - 1);

    mt_search_destroy(s);
}

int main(void)
{
    TEST_SUITE("Search");

    RUN_TEST(create_destroy);
    RUN_TEST(null_safety);
    RUN_TEST(open_close);
    RUN_TEST(set_query);
    RUN_TEST(query_cleared_on_close);
    RUN_TEST(navigate_no_matches);
    RUN_TEST(get_match_out_of_bounds);
    RUN_TEST(query_max_length);

    TEST_REPORT();
}
