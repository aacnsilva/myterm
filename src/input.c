/*
 * input.c — Keyboard and mouse input handling.
 *
 * Translates Raylib key and mouse events into VT escape sequences
 * using libghostty-vt's encoders, then writes the encoded bytes
 * to the PTY.
 */

#include "myterm.h"
#include "terminal_internal.h"
#include <raylib.h>
#include <ghostty/ghostty.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static int codepoint_to_utf8(uint32_t codepoint, char out[5])
{
    if (codepoint < 0x80) {
        out[0] = (char)codepoint;
        out[1] = '\0';
        return 1;
    }
    if (codepoint < 0x800) {
        out[0] = (char)(0xC0 | (codepoint >> 6));
        out[1] = (char)(0x80 | (codepoint & 0x3F));
        out[2] = '\0';
        return 2;
    }
    if (codepoint < 0x10000) {
        out[0] = (char)(0xE0 | (codepoint >> 12));
        out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        out[2] = (char)(0x80 | (codepoint & 0x3F));
        out[3] = '\0';
        return 3;
    }

    out[0] = (char)(0xF0 | (codepoint >> 18));
    out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[3] = (char)(0x80 | (codepoint & 0x3F));
    out[4] = '\0';
    return 4;
}

static bool altgr_active(void)
{
#ifdef _WIN32
    return IsKeyDown(KEY_RIGHT_ALT) &&
           (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL));
#else
    return false;
#endif
}

/* Map a Raylib key to a Ghostty key code. */
static GhosttyKey raylib_to_ghostty_key(int key)
{
    if (key >= KEY_A && key <= KEY_Z)
        return GHOSTTY_KEY_A + (key - KEY_A);
    if (key >= KEY_ZERO && key <= KEY_NINE)
        return GHOSTTY_KEY_0 + (key - KEY_ZERO);

    switch (key) {
    case KEY_SPACE:         return GHOSTTY_KEY_SPACE;
    case KEY_ENTER:         return GHOSTTY_KEY_ENTER;
    case KEY_TAB:           return GHOSTTY_KEY_TAB;
    case KEY_BACKSPACE:     return GHOSTTY_KEY_BACKSPACE;
    case KEY_ESCAPE:        return GHOSTTY_KEY_ESCAPE;
    case KEY_UP:            return GHOSTTY_KEY_UP;
    case KEY_DOWN:          return GHOSTTY_KEY_DOWN;
    case KEY_LEFT:          return GHOSTTY_KEY_LEFT;
    case KEY_RIGHT:         return GHOSTTY_KEY_RIGHT;
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
    case KEY_LEFT_BRACKET:  return GHOSTTY_KEY_LEFT_BRACKET;
    case KEY_RIGHT_BRACKET: return GHOSTTY_KEY_RIGHT_BRACKET;
    case KEY_BACKSLASH:     return GHOSTTY_KEY_BACKSLASH;
    case KEY_SEMICOLON:     return GHOSTTY_KEY_SEMICOLON;
    case KEY_APOSTROPHE:    return GHOSTTY_KEY_APOSTROPHE;
    case KEY_COMMA:         return GHOSTTY_KEY_COMMA;
    case KEY_PERIOD:        return GHOSTTY_KEY_PERIOD;
    case KEY_SLASH:         return GHOSTTY_KEY_SLASH;
    case KEY_GRAVE:         return GHOSTTY_KEY_GRAVE;
    default:                return GHOSTTY_KEY_NONE;
    }
}

static GhosttyMods get_current_mods(void)
{
    GhosttyMods mods = 0;

    if (IsKeyDown(KEY_LEFT_SHIFT)   || IsKeyDown(KEY_RIGHT_SHIFT))   mods |= GHOSTTY_MODS_SHIFT;
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) mods |= GHOSTTY_MODS_CTRL;
    if (IsKeyDown(KEY_LEFT_ALT)     || IsKeyDown(KEY_RIGHT_ALT))     mods |= GHOSTTY_MODS_ALT;
    if (IsKeyDown(KEY_LEFT_SUPER)   || IsKeyDown(KEY_RIGHT_SUPER))   mods |= GHOSTTY_MODS_SUPER;

    return mods;
}

static bool key_pressed_or_repeat(int key)
{
    return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

static void sync_mouse_encoder_geometry(MtTerminal *term, GhosttyMouseEncoder encoder)
{
    int cols = mt_terminal_cols(term);
    int rows = mt_terminal_rows(term);
    if (cols <= 0) cols = 1;
    if (rows <= 0) rows = 1;

    int screen_width = GetScreenWidth();
    int screen_height = GetScreenHeight();
    if (screen_width <= 0) screen_width = cols;
    if (screen_height <= 0) screen_height = rows;

    GhosttyMouseEncoderSize size = GHOSTTY_INIT_SIZED(GhosttyMouseEncoderSize);
    size.screen_width = (uint32_t)screen_width;
    size.screen_height = (uint32_t)screen_height;
    size.cell_width = (uint32_t)(screen_width / cols);
    size.cell_height = (uint32_t)(screen_height / rows);
    if (size.cell_width == 0) size.cell_width = 1;
    if (size.cell_height == 0) size.cell_height = 1;

    ghostty_mouse_encoder_setopt(encoder, GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &size);
}

static int write_encoded(MtPty *pty, const char *buf, int n)
{
    if (!pty || !buf || n <= 0) return 0;

    int w = mt_pty_write(pty, buf, (size_t)n);
    return w > 0 ? w : 0;
}

int mt_input_process(MtTerminal *term, MtPty *pty)
{
    if (!term || !pty) return 0;

    int total_written = 0;
    char encode_buf[128];

    GhosttyTerminal     vt   = mt_terminal_get_vt(term);
    GhosttyKeyEncoder   kenc = mt_terminal_get_key_encoder(term);
    GhosttyKeyEvent     kevt = mt_terminal_get_key_event(term);
    GhosttyMouseEncoder menc = mt_terminal_get_mouse_encoder(term);
    GhosttyMouseEvent   mevt = mt_terminal_get_mouse_event(term);

    ghostty_key_encoder_setopt_from_terminal(kenc, vt);
    ghostty_mouse_encoder_setopt_from_terminal(menc, vt);
    sync_mouse_encoder_geometry(term, menc);

    /* Typed character input */
    int ch;
    while ((ch = GetCharPressed()) != 0) {
        char utf8[5] = {0};
        int len = codepoint_to_utf8((uint32_t)ch, utf8);

        GhosttyMods mods = get_current_mods();
        if (!(mods & (GHOSTTY_MODS_CTRL | GHOSTTY_MODS_ALT)) || altgr_active()) {
            total_written += write_encoded(pty, utf8, len);
        }
    }

    /* Special keys + Ctrl/Alt modified printable keys.
     * Use repeat-aware polling so held keys repeat input. */
    static const int keys_to_poll[] = {
        KEY_SPACE,
        KEY_ENTER,
        KEY_TAB,
        KEY_BACKSPACE,
        KEY_ESCAPE,
        KEY_UP,
        KEY_DOWN,
        KEY_LEFT,
        KEY_RIGHT,
        KEY_HOME,
        KEY_END,
        KEY_PAGE_UP,
        KEY_PAGE_DOWN,
        KEY_INSERT,
        KEY_DELETE,
        KEY_F1,
        KEY_F2,
        KEY_F3,
        KEY_F4,
        KEY_F5,
        KEY_F6,
        KEY_F7,
        KEY_F8,
        KEY_F9,
        KEY_F10,
        KEY_F11,
        KEY_F12,
        KEY_MINUS,
        KEY_EQUAL,
        KEY_LEFT_BRACKET,
        KEY_RIGHT_BRACKET,
        KEY_BACKSLASH,
        KEY_SEMICOLON,
        KEY_APOSTROPHE,
        KEY_COMMA,
        KEY_PERIOD,
        KEY_SLASH,
        KEY_GRAVE,
        KEY_ZERO,
        KEY_ONE,
        KEY_TWO,
        KEY_THREE,
        KEY_FOUR,
        KEY_FIVE,
        KEY_SIX,
        KEY_SEVEN,
        KEY_EIGHT,
        KEY_NINE,
        KEY_A,
        KEY_B,
        KEY_C,
        KEY_D,
        KEY_E,
        KEY_F,
        KEY_G,
        KEY_H,
        KEY_I,
        KEY_J,
        KEY_K,
        KEY_L,
        KEY_M,
        KEY_N,
        KEY_O,
        KEY_P,
        KEY_Q,
        KEY_R,
        KEY_S,
        KEY_T,
        KEY_U,
        KEY_V,
        KEY_W,
        KEY_X,
        KEY_Y,
        KEY_Z,
    };

    for (size_t i = 0; i < sizeof(keys_to_poll) / sizeof(keys_to_poll[0]); i++) {
        int key = keys_to_poll[i];
        if (!key_pressed_or_repeat(key)) continue;

        GhosttyKey gk = raylib_to_ghostty_key(key);
        if (gk == GHOSTTY_KEY_NONE) continue;

        GhosttyMods mods = get_current_mods();
        bool printable_key =
            (key >= KEY_A && key <= KEY_Z) ||
            (key >= KEY_ZERO && key <= KEY_NINE) ||
            key == KEY_SPACE || key == KEY_MINUS || key == KEY_EQUAL ||
            key == KEY_LEFT_BRACKET || key == KEY_RIGHT_BRACKET ||
            key == KEY_BACKSLASH || key == KEY_SEMICOLON ||
            key == KEY_APOSTROPHE || key == KEY_COMMA ||
            key == KEY_PERIOD || key == KEY_SLASH || key == KEY_GRAVE;

        if (printable_key && (!(mods & (GHOSTTY_MODS_CTRL | GHOSTTY_MODS_ALT)) || altgr_active())) {
            continue;
        }

        ghostty_key_event_set(kevt, gk, GHOSTTY_KEY_ACTION_PRESS, mods);
        total_written += write_encoded(
            pty,
            encode_buf,
            ghostty_key_encoder_encode(kenc, kevt, encode_buf, sizeof(encode_buf))
        );
    }

    bool mouse_tracking = false;
    ghostty_terminal_get(vt, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING, &mouse_tracking);

    /* Mouse wheel */
    Vector2 wheel = GetMouseWheelMoveV();
    if (wheel.y != 0) {
        if (mouse_tracking) {
            Vector2 pos = GetMousePosition();

            ghostty_mouse_event_set_action(mevt, GHOSTTY_MOUSE_ACTION_PRESS);
            ghostty_mouse_event_set_button(
                mevt,
                wheel.y > 0 ? GHOSTTY_MOUSE_BUTTON_FOUR : GHOSTTY_MOUSE_BUTTON_FIVE
            );
            ghostty_mouse_event_set_mods(mevt, get_current_mods());
            ghostty_mouse_event_set_position(mevt, (GhosttyMousePosition){ pos.x, pos.y });

            total_written += write_encoded(
                pty,
                encode_buf,
                ghostty_mouse_encoder_encode(menc, mevt, encode_buf, sizeof(encode_buf))
            );
        } else {
            GhosttyTerminalScrollViewport behavior = {
                .tag = GHOSTTY_SCROLL_VIEWPORT_DELTA,
                .value.delta = (intptr_t)(-wheel.y * 3.0f),
            };
            ghostty_terminal_scroll_viewport(vt, behavior);
        }
    }

    /* Mouse buttons */
    if (mouse_tracking) {
        Vector2 pos = GetMousePosition();
        GhosttyMousePosition gpos = { pos.x, pos.y };
        GhosttyMods mods = get_current_mods();

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
            ghostty_mouse_event_set_action(
                mevt,
                IsMouseButtonPressed(MOUSE_BUTTON_LEFT)
                    ? GHOSTTY_MOUSE_ACTION_PRESS
                    : GHOSTTY_MOUSE_ACTION_RELEASE
            );
            ghostty_mouse_event_set_button(mevt, GHOSTTY_MOUSE_BUTTON_LEFT);
            ghostty_mouse_event_set_mods(mevt, mods);
            ghostty_mouse_event_set_position(mevt, gpos);

            total_written += write_encoded(
                pty,
                encode_buf,
                ghostty_mouse_encoder_encode(menc, mevt, encode_buf, sizeof(encode_buf))
            );
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) ||
            IsMouseButtonReleased(MOUSE_BUTTON_RIGHT)) {
            ghostty_mouse_event_set_action(
                mevt,
                IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)
                    ? GHOSTTY_MOUSE_ACTION_PRESS
                    : GHOSTTY_MOUSE_ACTION_RELEASE
            );
            ghostty_mouse_event_set_button(mevt, GHOSTTY_MOUSE_BUTTON_RIGHT);
            ghostty_mouse_event_set_mods(mevt, mods);
            ghostty_mouse_event_set_position(mevt, gpos);

            total_written += write_encoded(
                pty,
                encode_buf,
                ghostty_mouse_encoder_encode(menc, mevt, encode_buf, sizeof(encode_buf))
            );
        }
    }

    /* Focus in/out */
    bool focus_reporting = false;
    ghostty_terminal_mode_get(vt, GHOSTTY_MODE_FOCUS_EVENT, &focus_reporting);

    if (focus_reporting) {
        static bool was_focused = false;
        bool focused = IsWindowFocused();

        if (focused != was_focused) {
            total_written += write_encoded(
                pty,
                encode_buf,
                ghostty_focus_encode(vt, focused, encode_buf, sizeof(encode_buf))
            );
            was_focused = focused;
        }
    }

    return total_written;
}
