/*
 * input.c — Keyboard and mouse input handling.
 *
 * Translates Raylib key/mouse events into VT escape sequences
 * using libghostty-vt's key and mouse encoders, then writes
 * the encoded bytes to the PTY.
 */

#include "myterm.h"
#include "terminal_internal.h"
#include <raylib.h>
#include <ghostty/vt.h>
#include <string.h>

/* Map a Raylib key to a ghostty key code.
 * Returns GHOSTTY_KEY_UNIDENTIFIED if no mapping exists. */
static GhosttyKey raylib_to_ghostty_key(int key)
{
    /* Printable ASCII range */
    if (key >= KEY_A && key <= KEY_Z)
        return GHOSTTY_KEY_A + (key - KEY_A);
    if (key >= KEY_ZERO && key <= KEY_NINE)
        return GHOSTTY_KEY_DIGIT_0 + (key - KEY_ZERO);

    switch (key) {
    case KEY_SPACE:         return GHOSTTY_KEY_SPACE;
    case KEY_ENTER:         return GHOSTTY_KEY_ENTER;
    case KEY_TAB:           return GHOSTTY_KEY_TAB;
    case KEY_BACKSPACE:     return GHOSTTY_KEY_BACKSPACE;
    case KEY_ESCAPE:        return GHOSTTY_KEY_ESCAPE;
    case KEY_UP:            return GHOSTTY_KEY_ARROW_UP;
    case KEY_DOWN:          return GHOSTTY_KEY_ARROW_DOWN;
    case KEY_LEFT:          return GHOSTTY_KEY_ARROW_LEFT;
    case KEY_RIGHT:         return GHOSTTY_KEY_ARROW_RIGHT;
    case KEY_HOME:          return GHOSTTY_KEY_HOME;
    case KEY_END:           return GHOSTTY_KEY_END;
    case KEY_PAGE_UP:       return GHOSTTY_KEY_PAGE_UP;
    case KEY_PAGE_DOWN:     return GHOSTTY_KEY_PAGE_DOWN;
    case KEY_INSERT:        return GHOSTTY_KEY_INSERT;
    case KEY_DELETE:        return GHOSTTY_KEY_DELETE;
    case KEY_F1:            return GHOSTTY_KEY_F1;
    case KEY_F2:            return GHOSTTY_KEY_F2;
    case KEY_F3:            return GHOSTTY_KEY_F3;
    case KEY_F4:            return GHOSTTY_KEY_F4;
    case KEY_F5:            return GHOSTTY_KEY_F5;
    case KEY_F6:            return GHOSTTY_KEY_F6;
    case KEY_F7:            return GHOSTTY_KEY_F7;
    case KEY_F8:            return GHOSTTY_KEY_F8;
    case KEY_F9:            return GHOSTTY_KEY_F9;
    case KEY_F10:           return GHOSTTY_KEY_F10;
    case KEY_F11:           return GHOSTTY_KEY_F11;
    case KEY_F12:           return GHOSTTY_KEY_F12;
    case KEY_MINUS:         return GHOSTTY_KEY_MINUS;
    case KEY_EQUAL:         return GHOSTTY_KEY_EQUAL;
    case KEY_LEFT_BRACKET:  return GHOSTTY_KEY_BRACKET_LEFT;
    case KEY_RIGHT_BRACKET: return GHOSTTY_KEY_BRACKET_RIGHT;
    case KEY_BACKSLASH:     return GHOSTTY_KEY_BACKSLASH;
    case KEY_SEMICOLON:     return GHOSTTY_KEY_SEMICOLON;
    case KEY_APOSTROPHE:    return GHOSTTY_KEY_QUOTE;
    case KEY_COMMA:         return GHOSTTY_KEY_COMMA;
    case KEY_PERIOD:        return GHOSTTY_KEY_PERIOD;
    case KEY_SLASH:         return GHOSTTY_KEY_SLASH;
    case KEY_GRAVE:         return GHOSTTY_KEY_BACKQUOTE;
    default:                return GHOSTTY_KEY_UNIDENTIFIED;
    }
}

/* Build modifier flags from current Raylib keyboard state */
static GhosttyMods get_current_mods(void)
{
    GhosttyMods mods = 0;
    if (IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT))   mods |= GHOSTTY_MODS_SHIFT;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) mods |= GHOSTTY_MODS_CTRL;
    if (IsKeyDown(KEY_LEFT_ALT)     || IsKeyDown(KEY_RIGHT_ALT))     mods |= GHOSTTY_MODS_ALT;
    if (IsKeyDown(KEY_LEFT_SUPER)   || IsKeyDown(KEY_RIGHT_SUPER))   mods |= GHOSTTY_MODS_SUPER;
    return mods;
}

int mt_input_process(MtTerminal *term, MtPty *pty)
{
    int total_written = 0;
    char encode_buf[128];

    GhosttyTerminal     vt      = mt_terminal_get_vt(term);
    GhosttyKeyEncoder   kenc    = mt_terminal_get_key_encoder(term);
    GhosttyKeyEvent     kevt    = mt_terminal_get_key_event(term);
    GhosttyMouseEncoder menc    = mt_terminal_get_mouse_encoder(term);
    GhosttyMouseEvent   mevt    = mt_terminal_get_mouse_event(term);

    /* Sync encoder options with current terminal modes */
    ghostty_key_encoder_setopt_from_terminal(kenc, vt);
    ghostty_mouse_encoder_setopt_from_terminal(menc, vt);

    /* --- Keyboard input --- */

    /* Handle character input (typed text including shifted characters) */
    int ch;
    while ((ch = GetCharPressed()) != 0) {
        /* For simple printable characters, just UTF-8 encode and send */
        char utf8[5] = {0};
        int len = 0;
        if (ch < 0x80) {
            utf8[0] = (char)ch;
            len = 1;
        } else if (ch < 0x800) {
            utf8[0] = (char)(0xC0 | (ch >> 6));
            utf8[1] = (char)(0x80 | (ch & 0x3F));
            len = 2;
        } else if (ch < 0x10000) {
            utf8[0] = (char)(0xE0 | (ch >> 12));
            utf8[1] = (char)(0x80 | ((ch >> 6) & 0x3F));
            utf8[2] = (char)(0x80 | (ch & 0x3F));
            len = 3;
        } else {
            utf8[0] = (char)(0xF0 | (ch >> 18));
            utf8[1] = (char)(0x80 | ((ch >> 12) & 0x3F));
            utf8[2] = (char)(0x80 | ((ch >> 6) & 0x3F));
            utf8[3] = (char)(0x80 | (ch & 0x3F));
            len = 4;
        }

        /* Only send directly if no control modifiers are active
         * (Ctrl+key is handled below via key encoder) */
        GhosttyMods mods = get_current_mods();
        if (!(mods & (GHOSTTY_MODS_CTRL | GHOSTTY_MODS_ALT))) {
            int w = mt_pty_write(pty, utf8, (size_t)len);
            if (w > 0) total_written += w;
        }
    }

    /* Handle special/modifier keys via the ghostty key encoder */
    int key;
    while ((key = GetKeyPressed()) != 0) {
        GhosttyKey gk = raylib_to_ghostty_key(key);
        if (gk == GHOSTTY_KEY_UNIDENTIFIED) continue;

        GhosttyMods mods = get_current_mods();

        /* Skip plain printable keys (already handled above) */
        if (gk >= GHOSTTY_KEY_SPACE && gk <= GHOSTTY_KEY_BACKQUOTE &&
            !(mods & (GHOSTTY_MODS_CTRL | GHOSTTY_MODS_ALT))) {
            continue;
        }

        ghostty_key_event_set_key(kevt, gk);
        ghostty_key_event_set_action(kevt, GHOSTTY_KEY_ACTION_PRESS);
        ghostty_key_event_set_mods(kevt, mods);

        size_t written = 0;
        GhosttyResult res = ghostty_key_encoder_encode(
            kenc, kevt, encode_buf, sizeof(encode_buf), &written);
        if (res == GHOSTTY_SUCCESS && written > 0) {
            int w = mt_pty_write(pty, encode_buf, written);
            if (w > 0) total_written += w;
        }
    }

    /* --- Mouse input --- */

    /* Scroll */
    Vector2 wheel = GetMouseWheelMoveV();
    if (wheel.y != 0) {
        /* Check if the terminal has mouse mode enabled */
        bool mouse_tracking = false;
        ghostty_terminal_get(vt, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING,
                             &mouse_tracking);

        if (mouse_tracking) {
            /* Mouse tracking active → encode as mouse event.
             * Scroll up = button 4, scroll down = button 5 in xterm protocol. */
            Vector2 pos = GetMousePosition();
            GhosttyMouseButton btn = (wheel.y > 0)
                ? GHOSTTY_MOUSE_BUTTON_FOUR
                : GHOSTTY_MOUSE_BUTTON_FIVE;

            ghostty_mouse_event_set_button(mevt, btn);
            ghostty_mouse_event_set_action(mevt, GHOSTTY_MOUSE_ACTION_PRESS);
            ghostty_mouse_event_set_mods(mevt, get_current_mods());
            ghostty_mouse_event_set_position(mevt,
                (GhosttyMousePosition){ pos.x, pos.y });

            size_t written = 0;
            GhosttyResult res = ghostty_mouse_encoder_encode(
                menc, mevt, encode_buf, sizeof(encode_buf), &written);
            if (res == GHOSTTY_SUCCESS && written > 0) {
                int w = mt_pty_write(pty, encode_buf, written);
                if (w > 0) total_written += w;
            }
        } else {
            /* No mouse tracking → scroll the viewport */
            int lines = (int)(-wheel.y * 3);
            GhosttyTerminalScrollViewport scroll = {
                .tag = GHOSTTY_SCROLL_VIEWPORT_DELTA,
                .value = { .delta = lines },
            };
            ghostty_terminal_scroll_viewport(vt, scroll);
        }
    }

    /* Mouse button press/release (when mouse tracking is enabled) */
    bool mouse_mode = false;
    ghostty_terminal_get(vt, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &mouse_mode);

    if (mouse_mode) {
        /* Left button */
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {

            Vector2 pos = GetMousePosition();
            GhosttyMouseAction action = IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
                ? GHOSTTY_MOUSE_ACTION_PRESS
                : GHOSTTY_MOUSE_ACTION_RELEASE;

            ghostty_mouse_event_set_button(mevt, GHOSTTY_MOUSE_BUTTON_LEFT);
            ghostty_mouse_event_set_action(mevt, action);
            ghostty_mouse_event_set_mods(mevt, get_current_mods());
            ghostty_mouse_event_set_position(mevt,
                (GhosttyMousePosition){ pos.x, pos.y });

            size_t written = 0;
            GhosttyResult res = ghostty_mouse_encoder_encode(
                menc, mevt, encode_buf, sizeof(encode_buf), &written);
            if (res == GHOSTTY_SUCCESS && written > 0) {
                int w = mt_pty_write(pty, encode_buf, written);
                if (w > 0) total_written += w;
            }
        }

        /* Right button */
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ||
            IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {

            Vector2 pos = GetMousePosition();
            GhosttyMouseAction action = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)
                ? GHOSTTY_MOUSE_ACTION_PRESS
                : GHOSTTY_MOUSE_ACTION_RELEASE;

            ghostty_mouse_event_set_button(mevt, GHOSTTY_MOUSE_BUTTON_RIGHT);
            ghostty_mouse_event_set_action(mevt, action);
            ghostty_mouse_event_set_mods(mevt, get_current_mods());
            ghostty_mouse_event_set_position(mevt,
                (GhosttyMousePosition){ pos.x, pos.y });

            size_t written = 0;
            GhosttyResult res = ghostty_mouse_encoder_encode(
                menc, mevt, encode_buf, sizeof(encode_buf), &written);
            if (res == GHOSTTY_SUCCESS && written > 0) {
                int w = mt_pty_write(pty, encode_buf, written);
                if (w > 0) total_written += w;
            }
        }
    }

    /* --- Focus events --- */
    if (IsWindowFocused()) {
        static bool was_focused = false;
        if (!was_focused) {
            size_t written = 0;
            GhosttyResult res = ghostty_focus_encode(
                GHOSTTY_FOCUS_GAINED, encode_buf, sizeof(encode_buf), &written);
            if (res == GHOSTTY_SUCCESS && written > 0) {
                int w = mt_pty_write(pty, encode_buf, written);
                if (w > 0) total_written += w;
            }
            was_focused = true;
        }
    } else {
        static bool sent_blur = false;
        if (!sent_blur) {
            size_t written = 0;
            GhosttyResult res = ghostty_focus_encode(
                GHOSTTY_FOCUS_LOST, encode_buf, sizeof(encode_buf), &written);
            if (res == GHOSTTY_SUCCESS && written > 0) {
                int w = mt_pty_write(pty, encode_buf, written);
                if (w > 0) total_written += w;
            }
            sent_blur = true;
        }
    }

    return total_written;
}
