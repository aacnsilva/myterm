/*
 * stubs.c — Stub implementations for testing without libghostty/Raylib.
 *
 * Provides minimal no-op implementations of MtTerminal and MtPty functions
 * so that tabs.c, search.c, and splits.c can be compiled and tested
 * for their pure logic.
 */

#include "myterm.h"
#include <stdlib.h>
#include <string.h>

/* --- Stub MtTerminal --- */

struct MtTerminal {
    int cols;
    int rows;
};

MtTerminal *mt_terminal_new(int cols, int rows)
{
    MtTerminal *t = calloc(1, sizeof(MtTerminal));
    if (!t) return NULL;
    t->cols = cols;
    t->rows = rows;
    return t;
}

void mt_terminal_destroy(MtTerminal *t)
{
    free(t);
}

void mt_terminal_feed(MtTerminal *t, const char *data, size_t len)
{
    (void)t; (void)data; (void)len;
}

void mt_terminal_resize(MtTerminal *t, int cols, int rows)
{
    if (t) { t->cols = cols; t->rows = rows; }
}

int mt_terminal_cols(const MtTerminal *t)
{
    return t ? t->cols : 0;
}

int mt_terminal_rows(const MtTerminal *t)
{
    return t ? t->rows : 0;
}

/* --- Stub MtPty --- */

struct MtPty {
    bool alive;
};

MtPty *mt_pty_new(int cols, int rows)
{
    (void)cols; (void)rows;
    MtPty *p = calloc(1, sizeof(MtPty));
    if (p) p->alive = true;
    return p;
}

void mt_pty_destroy(MtPty *p)
{
    free(p);
}

int mt_pty_read(MtPty *p, char *buf, size_t len)
{
    (void)p; (void)buf; (void)len;
    return 0;
}

int mt_pty_write(MtPty *p, const char *buf, size_t len)
{
    (void)p; (void)buf; (void)len;
    return (int)len;
}

void mt_pty_resize(MtPty *p, int cols, int rows)
{
    (void)p; (void)cols; (void)rows;
}

bool mt_pty_is_alive(MtPty *p)
{
    return p ? p->alive : false;
}
