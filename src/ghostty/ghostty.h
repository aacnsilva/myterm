#ifndef MYTERM_GHOSTTY_COMPAT_H
#define MYTERM_GHOSTTY_COMPAT_H

#include <ghostty/vt.h>

/* --------------------------------------------------------------------------
 * Compatibility aliases for the libghostty-vt C API.
 * myterm was originally written against an older Ghostty C API surface.
 * -------------------------------------------------------------------------- */

#define ghostty_terminal_destroy      ghostty_terminal_free
#define ghostty_render_state_destroy  ghostty_render_state_free
#define ghostty_key_encoder_destroy   ghostty_key_encoder_free
#define ghostty_mouse_encoder_destroy ghostty_mouse_encoder_free

#define GHOSTTY_MOD_SHIFT GHOSTTY_MODS_SHIFT
#define GHOSTTY_MOD_CTRL  GHOSTTY_MODS_CTRL
#define GHOSTTY_MOD_ALT   GHOSTTY_MODS_ALT
#define GHOSTTY_MOD_SUPER GHOSTTY_MODS_SUPER

#define GHOSTTY_KEY_NONE          GHOSTTY_KEY_UNIDENTIFIED
#define GHOSTTY_KEY_0             GHOSTTY_KEY_DIGIT_0
#define GHOSTTY_KEY_LEFT          GHOSTTY_KEY_ARROW_LEFT
#define GHOSTTY_KEY_RIGHT         GHOSTTY_KEY_ARROW_RIGHT
#define GHOSTTY_KEY_UP            GHOSTTY_KEY_ARROW_UP
#define GHOSTTY_KEY_DOWN          GHOSTTY_KEY_ARROW_DOWN
#define GHOSTTY_KEY_LEFT_BRACKET  GHOSTTY_KEY_BRACKET_LEFT
#define GHOSTTY_KEY_RIGHT_BRACKET GHOSTTY_KEY_BRACKET_RIGHT
#define GHOSTTY_KEY_APOSTROPHE    GHOSTTY_KEY_QUOTE
#define GHOSTTY_KEY_GRAVE         GHOSTTY_KEY_BACKQUOTE

static inline GhosttyResult ghostty_key_encoder_event_new(GhosttyKeyEvent *event)
{
    return ghostty_key_event_new(NULL, event);
}

static inline void ghostty_key_encoder_event_destroy(GhosttyKeyEvent event)
{
    ghostty_key_event_free(event);
}

static inline GhosttyResult ghostty_mouse_encoder_event_new(GhosttyMouseEvent *event)
{
    return ghostty_mouse_event_new(NULL, event);
}

static inline void ghostty_mouse_encoder_event_destroy(GhosttyMouseEvent event)
{
    ghostty_mouse_event_free(event);
}

static inline void ghostty_key_event_set(GhosttyKeyEvent event,
                                         GhosttyKey key,
                                         GhosttyKeyAction action,
                                         GhosttyMods mods)
{
    ghostty_key_event_set_key(event, key);
    ghostty_key_event_set_action(event, action);
    ghostty_key_event_set_mods(event, mods);
}

static inline int myterm_ghostty_key_encoder_encode_compat(GhosttyKeyEncoder encoder,
                                                           GhosttyKeyEvent event,
                                                           char *out_buf,
                                                           size_t out_buf_size)
{
    size_t out_len = 0;
    GhosttyResult result = ghostty_key_encoder_encode(encoder, event, out_buf, out_buf_size, &out_len);
    return result == GHOSTTY_SUCCESS ? (int)out_len : 0;
}

#define ghostty_key_encoder_encode(encoder, event, out_buf, out_buf_size) \
    myterm_ghostty_key_encoder_encode_compat((encoder), (event), (out_buf), (out_buf_size))

static inline int myterm_ghostty_mouse_encoder_encode_compat(GhosttyMouseEncoder encoder,
                                                             GhosttyMouseEvent event,
                                                             char *out_buf,
                                                             size_t out_buf_size)
{
    size_t out_len = 0;
    GhosttyResult result = ghostty_mouse_encoder_encode(encoder, event, out_buf, out_buf_size, &out_len);
    return result == GHOSTTY_SUCCESS ? (int)out_len : 0;
}

#define ghostty_mouse_encoder_encode(encoder, event, out_buf, out_buf_size) \
    myterm_ghostty_mouse_encoder_encode_compat((encoder), (event), (out_buf), (out_buf_size))

static inline int myterm_ghostty_focus_encode_compat(GhosttyTerminal terminal,
                                                     bool focused,
                                                     char *buf,
                                                     size_t buf_len)
{
    (void)terminal;

    size_t out_len = 0;
    GhosttyResult result = ghostty_focus_encode(
        focused ? GHOSTTY_FOCUS_GAINED : GHOSTTY_FOCUS_LOST,
        buf,
        buf_len,
        &out_len
    );

    return result == GHOSTTY_SUCCESS ? (int)out_len : 0;
}

#define ghostty_focus_encode(terminal, focused, buf, buf_len) \
    myterm_ghostty_focus_encode_compat((terminal), (focused), (buf), (buf_len))

#endif /* MYTERM_GHOSTTY_COMPAT_H */
