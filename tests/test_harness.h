#ifndef TEST_HARNESS_H
#define TEST_HARNESS_H

/*
 * Minimal test harness — no dependencies, just assert + report.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int _tests_run    = 0;
static int _tests_passed = 0;
static int _tests_failed = 0;

#define TEST(name)                                                      \
    static void test_##name(void);                                      \
    static void run_test_##name(void) {                                 \
        _tests_run++;                                                   \
        printf("  %-50s", #name);                                       \
        test_##name();                                                  \
        _tests_passed++;                                                \
        printf(" PASS\n");                                              \
    }                                                                   \
    static void test_##name(void)

#define ASSERT(cond)                                                    \
    do {                                                                \
        if (!(cond)) {                                                  \
            printf(" FAIL\n    assertion failed: %s\n"                  \
                   "    at %s:%d\n", #cond, __FILE__, __LINE__);        \
            _tests_failed++;                                            \
            _tests_passed--; /* undo pre-increment in run_test */       \
            return;                                                     \
        }                                                               \
    } while (0)

#define ASSERT_EQ(a, b)                                                 \
    do {                                                                \
        if ((a) != (b)) {                                               \
            printf(" FAIL\n    expected %d == %d\n"                     \
                   "    at %s:%d\n", (int)(a), (int)(b),                \
                   __FILE__, __LINE__);                                 \
            _tests_failed++;                                            \
            _tests_passed--;                                            \
            return;                                                     \
        }                                                               \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                             \
    do {                                                                \
        if (strcmp((a), (b)) != 0) {                                    \
            printf(" FAIL\n    expected \"%s\" == \"%s\"\n"             \
                   "    at %s:%d\n", (a), (b), __FILE__, __LINE__);    \
            _tests_failed++;                                            \
            _tests_passed--;                                            \
            return;                                                     \
        }                                                               \
    } while (0)

#define RUN_TEST(name) run_test_##name()

#define TEST_REPORT()                                                   \
    do {                                                                \
        printf("\n  %d tests, %d passed, %d failed\n",                  \
               _tests_run, _tests_passed, _tests_failed);              \
        return _tests_failed > 0 ? 1 : 0;                              \
    } while (0)

#define TEST_SUITE(name) \
    printf("\n=== %s ===\n", name)

#endif /* TEST_HARNESS_H */
