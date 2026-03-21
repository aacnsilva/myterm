#ifndef TERMINAL_INTERNAL_H
#define TERMINAL_INTERNAL_H

/*
 * Internal accessors for the ghostty handles inside MtTerminal.
 * Used by renderer.c and input.c — not part of the public API.
 */

#include "myterm.h"
#include <ghostty/ghostty.h>

GhosttyTerminal      mt_terminal_get_vt(const MtTerminal *t);
GhosttyRenderState   mt_terminal_get_render_state(const MtTerminal *t);
GhosttyKeyEncoder    mt_terminal_get_key_encoder(const MtTerminal *t);
GhosttyKeyEvent      mt_terminal_get_key_event(const MtTerminal *t);
GhosttyMouseEncoder  mt_terminal_get_mouse_encoder(const MtTerminal *t);
GhosttyMouseEvent    mt_terminal_get_mouse_event(const MtTerminal *t);

#endif /* TERMINAL_INTERNAL_H */
