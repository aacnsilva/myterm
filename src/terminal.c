/*
 * terminal.c — libghostty-vt wrapper for VT parsing and terminal state.
 *
 * This wraps the ghostty C API to provide terminal emulation:
 *   - VT sequence parsing
 *   - Screen buffer management (cursor, scrollback, styles)
 *   - Render state snapshotting for the renderer
 */

#include "myterm.h"
#include <ghostty/ghostty.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

struct MtTerminal {
    GhosttyTerminal     vt;
    GhosttyRenderState  render_state;
    GhosttyKeyEncoder   key_encoder;
    GhosttyMouseEncoder mouse_encoder;
    GhosttyKeyEvent     key_event;
    GhosttyMouseEvent   mouse_event;
    int                 cols;
    int                 rows;
    bool                valid;
};

static void *mt_ghostty_alloc(void *ctx, size_t len, uint8_t alignment, uintptr_t ret_addr)
{
    (void)ctx;
    (void)alignment;
    (void)ret_addr;
    if (len == 0) len = 1;
    return malloc(len);
}

static bool mt_ghostty_resize(void *ctx, void *memory, size_t memory_len,
                              uint8_t alignment, size_t new_len, uintptr_t ret_addr)
{
    (void)ctx;
    (void)memory;
    (void)memory_len;
    (void)alignment;
    (void)new_len;
    (void)ret_addr;
    return false;
}

static void *mt_ghostty_remap(void *ctx, void *memory, size_t memory_len,
                              uint8_t alignment, size_t new_len, uintptr_t ret_addr)
{
    (void)ctx;
    (void)memory_len;
    (void)alignment;
    (void)ret_addr;
    if (new_len == 0) new_len = 1;
    return realloc(memory, new_len);
}

static void mt_ghostty_free(void *ctx, void *memory, size_t memory_len,
                            uint8_t alignment, uintptr_t ret_addr)
{
    (void)ctx;
    (void)memory_len;
    (void)alignment;
    (void)ret_addr;
    free(memory);
}

static const GhosttyAllocatorVtable mt_ghostty_allocator_vtable = {
    .alloc = mt_ghostty_alloc,
    .resize = mt_ghostty_resize,
    .remap = mt_ghostty_remap,
    .free = mt_ghostty_free,
};

static const GhosttyAllocator mt_ghostty_allocator = {
    .ctx = NULL,
    .vtable = &mt_ghostty_allocator_vtable,
};

MtTerminal *mt_terminal_new(int cols, int rows)
{
    MtTerminal *t = calloc(1, sizeof(MtTerminal));
    if (!t) return NULL;

    GhosttyTerminalOptions opts = {
        .cols          = (uint16_t)cols,
        .rows          = (uint16_t)rows,
        .max_scrollback = MYTERM_MAX_SCROLLBACK,
    };

    GhosttyResult err = ghostty_terminal_new(&mt_ghostty_allocator, &t->vt, opts);
    if (err != GHOSTTY_SUCCESS) {
        free(t);
        return NULL;
    }

    /* Render state — used to snapshot the terminal for drawing */
    err = ghostty_render_state_new(&mt_ghostty_allocator, &t->render_state);
    if (err != GHOSTTY_SUCCESS) {
        ghostty_terminal_destroy(t->vt);
        free(t);
        return NULL;
    }

    /* Input encoders — translate platform key/mouse events to VT sequences */
    err = ghostty_key_encoder_new(&mt_ghostty_allocator, &t->key_encoder);
    if (err != GHOSTTY_SUCCESS) {
        ghostty_render_state_destroy(t->render_state);
        ghostty_terminal_destroy(t->vt);
        free(t);
        return NULL;
    }

    err = ghostty_key_event_new(&mt_ghostty_allocator, &t->key_event);
    if (err != GHOSTTY_SUCCESS) {
        ghostty_key_encoder_destroy(t->key_encoder);
        ghostty_render_state_destroy(t->render_state);
        ghostty_terminal_destroy(t->vt);
        free(t);
        return NULL;
    }

    err = ghostty_mouse_encoder_new(&mt_ghostty_allocator, &t->mouse_encoder);
    if (err != GHOSTTY_SUCCESS) {
        ghostty_key_encoder_event_destroy(t->key_event);
        ghostty_key_encoder_destroy(t->key_encoder);
        ghostty_render_state_destroy(t->render_state);
        ghostty_terminal_destroy(t->vt);
        free(t);
        return NULL;
    }

    err = ghostty_mouse_event_new(&mt_ghostty_allocator, &t->mouse_event);
    if (err != GHOSTTY_SUCCESS) {
        ghostty_mouse_encoder_destroy(t->mouse_encoder);
        ghostty_key_encoder_event_destroy(t->key_event);
        ghostty_key_encoder_destroy(t->key_encoder);
        ghostty_render_state_destroy(t->render_state);
        ghostty_terminal_destroy(t->vt);
        free(t);
        return NULL;
    }

    t->cols  = cols;
    t->rows  = rows;
    t->valid = true;

    return t;
}

void mt_terminal_destroy(MtTerminal *t)
{
    if (!t) return;
    if (t->valid) {
        ghostty_mouse_encoder_event_destroy(t->mouse_event);
        ghostty_mouse_encoder_destroy(t->mouse_encoder);
        ghostty_key_encoder_event_destroy(t->key_event);
        ghostty_key_encoder_destroy(t->key_encoder);
        ghostty_render_state_destroy(t->render_state);
        ghostty_terminal_destroy(t->vt);
    }
    free(t);
}

void mt_terminal_feed(MtTerminal *t, const char *data, size_t len)
{
    if (!t || !t->valid || !data || len == 0) return;
    ghostty_terminal_vt_write(t->vt, (const uint8_t *)data, len);
}

void mt_terminal_resize(MtTerminal *t, int cols, int rows)
{
    if (!t || !t->valid) return;
    ghostty_terminal_resize(t->vt, (uint16_t)cols, (uint16_t)rows);
    t->cols = cols;
    t->rows = rows;
}

int mt_terminal_cols(const MtTerminal *t)
{
    return t ? t->cols : 0;
}

int mt_terminal_rows(const MtTerminal *t)
{
    return t ? t->rows : 0;
}

/* --- Internal accessors used by renderer.c and input.c --- */

const GhosttyAllocator *mt_terminal_get_allocator(void)
{
    return &mt_ghostty_allocator;
}

GhosttyTerminal mt_terminal_get_vt(const MtTerminal *t)
{
    return t->vt;
}

GhosttyRenderState mt_terminal_get_render_state(const MtTerminal *t)
{
    return t->render_state;
}

GhosttyKeyEncoder mt_terminal_get_key_encoder(const MtTerminal *t)
{
    return t->key_encoder;
}

GhosttyKeyEvent mt_terminal_get_key_event(const MtTerminal *t)
{
    return t->key_event;
}

GhosttyMouseEncoder mt_terminal_get_mouse_encoder(const MtTerminal *t)
{
    return t->mouse_encoder;
}

GhosttyMouseEvent mt_terminal_get_mouse_event(const MtTerminal *t)
{
    return t->mouse_event;
}
