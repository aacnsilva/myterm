#ifndef CONFIG_H
#define CONFIG_H

/*
 * config.h — Configuration and theming for myterm.
 *
 * Supports loading settings from a TOML-like config file
 * at ~/.config/myterm/config or %APPDATA%\myterm\config.
 */

#include <stdbool.h>
#include <stdint.h>

#define MT_MAX_FONT_PATH 512

typedef struct {
    uint8_t r, g, b, a;
} MtRgba;

typedef struct {
    char    name[64];
    MtRgba  bg;
    MtRgba  fg;
    MtRgba  cursor;
    MtRgba  selection;
    MtRgba  tab_bar_bg;
    MtRgba  tab_active_bg;
    MtRgba  tab_active_fg;
    MtRgba  tab_inactive_bg;
    MtRgba  tab_inactive_fg;
    MtRgba  tab_activity;
    MtRgba  search_match;
    MtRgba  search_current;
    MtRgba  split_border;
    MtRgba  scrollbar_bg;
    MtRgba  scrollbar_thumb;
    MtRgba  palette[16];  /* ANSI 16-color palette */
} MtTheme;

typedef struct {
    /* Font */
    char    font_path[MT_MAX_FONT_PATH];
    float   font_size;

    /* Window */
    int     initial_width;
    int     initial_height;
    float   opacity;
    bool    start_maximized;

    /* Terminal */
    int     scrollback_lines;
    bool    cursor_blink;
    int     cursor_blink_ms;
    char    cursor_shape[16]; /* "block", "bar", "underline" */

    /* Shell */
    char    shell[512];

    /* Behavior */
    bool    confirm_close;
    bool    copy_on_select;
    bool    bell_visual;
    bool    bell_audio;

    /* Active theme */
    MtTheme theme;
} MtConfig;

/* Load config from default path, or fall back to built-in defaults */
MtConfig mt_config_load(void);

/* Load config from specific file path */
MtConfig mt_config_load_from(const char *path);

/* Get a built-in theme by name. Returns default theme if not found. */
MtTheme mt_theme_builtin(const char *name);

/* Available built-in theme names */
typedef enum {
    MT_THEME_CATPPUCCIN_MOCHA,
    MT_THEME_DRACULA,
    MT_THEME_NORD,
    MT_THEME_SOLARIZED_DARK,
    MT_THEME_GRUVBOX_DARK,
    MT_THEME_ONE_DARK,
    MT_THEME_TOKYO_NIGHT,
    MT_THEME_COUNT,
} MtThemeId;

const char *mt_theme_name(MtThemeId id);
MtTheme     mt_theme_get(MtThemeId id);

#endif /* CONFIG_H */
