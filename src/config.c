/*
 * config.c — Configuration and built-in themes.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------------------
 * Built-in themes
 * -------------------------------------------------------------------------- */

#define RGBA(r_, g_, b_, a_) { .r = (r_), .g = (g_), .b = (b_), .a = (a_) }
#define RGB(r_, g_, b_)      RGBA(r_, g_, b_, 255)

static const MtTheme builtin_themes[MT_THEME_COUNT] = {
    [MT_THEME_CATPPUCCIN_MOCHA] = {
        .name            = "Catppuccin Mocha",
        .bg              = RGB(30, 30, 46),
        .fg              = RGB(205, 214, 244),
        .cursor          = RGB(245, 194, 231),
        .selection       = RGBA(88, 91, 112, 128),
        .tab_bar_bg      = RGB(24, 24, 37),
        .tab_active_bg   = RGB(30, 30, 46),
        .tab_active_fg   = RGB(205, 214, 244),
        .tab_inactive_bg = RGB(24, 24, 37),
        .tab_inactive_fg = RGB(147, 153, 178),
        .tab_activity    = RGB(250, 179, 135),
        .search_match    = RGBA(249, 226, 175, 80),
        .search_current  = RGBA(166, 227, 161, 120),
        .split_border    = RGB(69, 71, 90),
        .scrollbar_bg    = RGBA(49, 50, 68, 100),
        .scrollbar_thumb = RGBA(147, 153, 178, 180),
        .palette = {
            RGB(69, 71, 90),    RGB(243, 139, 168),  RGB(166, 227, 161), RGB(249, 226, 175),
            RGB(137, 180, 250), RGB(245, 194, 231),  RGB(148, 226, 213), RGB(186, 194, 222),
            RGB(88, 91, 112),   RGB(243, 139, 168),  RGB(166, 227, 161), RGB(249, 226, 175),
            RGB(137, 180, 250), RGB(245, 194, 231),  RGB(148, 226, 213), RGB(205, 214, 244),
        },
    },
    [MT_THEME_DRACULA] = {
        .name            = "Dracula",
        .bg              = RGB(40, 42, 54),
        .fg              = RGB(248, 248, 242),
        .cursor          = RGB(248, 248, 242),
        .selection       = RGBA(68, 71, 90, 128),
        .tab_bar_bg      = RGB(33, 34, 44),
        .tab_active_bg   = RGB(40, 42, 54),
        .tab_active_fg   = RGB(248, 248, 242),
        .tab_inactive_bg = RGB(33, 34, 44),
        .tab_inactive_fg = RGB(98, 114, 164),
        .tab_activity    = RGB(255, 184, 108),
        .search_match    = RGBA(241, 250, 140, 80),
        .search_current  = RGBA(80, 250, 123, 120),
        .split_border    = RGB(68, 71, 90),
        .scrollbar_bg    = RGBA(68, 71, 90, 100),
        .scrollbar_thumb = RGBA(98, 114, 164, 180),
        .palette = {
            RGB(33, 34, 44),    RGB(255, 85, 85),   RGB(80, 250, 123),  RGB(241, 250, 140),
            RGB(189, 147, 249), RGB(255, 121, 198),  RGB(139, 233, 253), RGB(248, 248, 242),
            RGB(98, 114, 164),  RGB(255, 110, 110),  RGB(105, 255, 148), RGB(255, 255, 165),
            RGB(214, 172, 255), RGB(255, 146, 223),  RGB(164, 255, 255), RGB(255, 255, 255),
        },
    },
    [MT_THEME_NORD] = {
        .name            = "Nord",
        .bg              = RGB(46, 52, 64),
        .fg              = RGB(216, 222, 233),
        .cursor          = RGB(216, 222, 233),
        .selection       = RGBA(67, 76, 94, 128),
        .tab_bar_bg      = RGB(39, 43, 53),
        .tab_active_bg   = RGB(46, 52, 64),
        .tab_active_fg   = RGB(216, 222, 233),
        .tab_inactive_bg = RGB(39, 43, 53),
        .tab_inactive_fg = RGB(127, 140, 160),
        .tab_activity    = RGB(208, 135, 112),
        .search_match    = RGBA(235, 203, 139, 80),
        .search_current  = RGBA(163, 190, 140, 120),
        .split_border    = RGB(67, 76, 94),
        .scrollbar_bg    = RGBA(67, 76, 94, 100),
        .scrollbar_thumb = RGBA(127, 140, 160, 180),
        .palette = {
            RGB(59, 66, 82),    RGB(191, 97, 106),  RGB(163, 190, 140), RGB(235, 203, 139),
            RGB(129, 161, 193), RGB(180, 142, 173),  RGB(136, 192, 208), RGB(229, 233, 240),
            RGB(76, 86, 106),   RGB(191, 97, 106),  RGB(163, 190, 140), RGB(235, 203, 139),
            RGB(129, 161, 193), RGB(180, 142, 173),  RGB(143, 188, 187), RGB(236, 239, 244),
        },
    },
    [MT_THEME_SOLARIZED_DARK] = {
        .name            = "Solarized Dark",
        .bg              = RGB(0, 43, 54),
        .fg              = RGB(131, 148, 150),
        .cursor          = RGB(131, 148, 150),
        .selection       = RGBA(7, 54, 66, 128),
        .tab_bar_bg      = RGB(0, 36, 46),
        .tab_active_bg   = RGB(0, 43, 54),
        .tab_active_fg   = RGB(131, 148, 150),
        .tab_inactive_bg = RGB(0, 36, 46),
        .tab_inactive_fg = RGB(88, 110, 117),
        .tab_activity    = RGB(203, 75, 22),
        .search_match    = RGBA(181, 137, 0, 80),
        .search_current  = RGBA(133, 153, 0, 120),
        .split_border    = RGB(7, 54, 66),
        .scrollbar_bg    = RGBA(7, 54, 66, 100),
        .scrollbar_thumb = RGBA(88, 110, 117, 180),
        .palette = {
            RGB(7, 54, 66),     RGB(220, 50, 47),   RGB(133, 153, 0),   RGB(181, 137, 0),
            RGB(38, 139, 210),  RGB(211, 54, 130),   RGB(42, 161, 152),  RGB(238, 232, 213),
            RGB(0, 43, 54),     RGB(203, 75, 22),   RGB(88, 110, 117),  RGB(101, 123, 131),
            RGB(131, 148, 150), RGB(108, 113, 196),  RGB(147, 161, 161), RGB(253, 246, 227),
        },
    },
    [MT_THEME_GRUVBOX_DARK] = {
        .name            = "Gruvbox Dark",
        .bg              = RGB(40, 40, 40),
        .fg              = RGB(235, 219, 178),
        .cursor          = RGB(235, 219, 178),
        .selection       = RGBA(60, 56, 54, 128),
        .tab_bar_bg      = RGB(29, 32, 33),
        .tab_active_bg   = RGB(40, 40, 40),
        .tab_active_fg   = RGB(235, 219, 178),
        .tab_inactive_bg = RGB(29, 32, 33),
        .tab_inactive_fg = RGB(146, 131, 116),
        .tab_activity    = RGB(254, 128, 25),
        .search_match    = RGBA(250, 189, 47, 80),
        .search_current  = RGBA(184, 187, 38, 120),
        .split_border    = RGB(60, 56, 54),
        .scrollbar_bg    = RGBA(60, 56, 54, 100),
        .scrollbar_thumb = RGBA(146, 131, 116, 180),
        .palette = {
            RGB(40, 40, 40),    RGB(204, 36, 29),   RGB(152, 151, 26),  RGB(215, 153, 33),
            RGB(69, 133, 136),  RGB(177, 98, 134),   RGB(104, 157, 106), RGB(168, 153, 132),
            RGB(146, 131, 116), RGB(251, 73, 52),   RGB(184, 187, 38),  RGB(250, 189, 47),
            RGB(131, 165, 152), RGB(211, 134, 155),  RGB(142, 192, 124), RGB(235, 219, 178),
        },
    },
    [MT_THEME_ONE_DARK] = {
        .name            = "One Dark",
        .bg              = RGB(40, 44, 52),
        .fg              = RGB(171, 178, 191),
        .cursor          = RGB(82, 139, 255),
        .selection       = RGBA(62, 68, 81, 128),
        .tab_bar_bg      = RGB(33, 37, 43),
        .tab_active_bg   = RGB(40, 44, 52),
        .tab_active_fg   = RGB(171, 178, 191),
        .tab_inactive_bg = RGB(33, 37, 43),
        .tab_inactive_fg = RGB(92, 99, 112),
        .tab_activity    = RGB(209, 154, 102),
        .search_match    = RGBA(229, 192, 123, 80),
        .search_current  = RGBA(152, 195, 121, 120),
        .split_border    = RGB(62, 68, 81),
        .scrollbar_bg    = RGBA(62, 68, 81, 100),
        .scrollbar_thumb = RGBA(92, 99, 112, 180),
        .palette = {
            RGB(40, 44, 52),    RGB(224, 108, 117),  RGB(152, 195, 121), RGB(229, 192, 123),
            RGB(97, 175, 239),  RGB(198, 120, 221),  RGB(86, 182, 194),  RGB(171, 178, 191),
            RGB(92, 99, 112),   RGB(224, 108, 117),  RGB(152, 195, 121), RGB(229, 192, 123),
            RGB(97, 175, 239),  RGB(198, 120, 221),  RGB(86, 182, 194),  RGB(255, 255, 255),
        },
    },
    [MT_THEME_TOKYO_NIGHT] = {
        .name            = "Tokyo Night",
        .bg              = RGB(26, 27, 38),
        .fg              = RGB(169, 177, 214),
        .cursor          = RGB(199, 208, 245),
        .selection       = RGBA(42, 46, 66, 128),
        .tab_bar_bg      = RGB(22, 22, 30),
        .tab_active_bg   = RGB(26, 27, 38),
        .tab_active_fg   = RGB(169, 177, 214),
        .tab_inactive_bg = RGB(22, 22, 30),
        .tab_inactive_fg = RGB(84, 90, 122),
        .tab_activity    = RGB(255, 158, 100),
        .search_match    = RGBA(224, 175, 104, 80),
        .search_current  = RGBA(158, 206, 106, 120),
        .split_border    = RGB(42, 46, 66),
        .scrollbar_bg    = RGBA(42, 46, 66, 100),
        .scrollbar_thumb = RGBA(84, 90, 122, 180),
        .palette = {
            RGB(21, 22, 30),    RGB(247, 118, 142),  RGB(158, 206, 106), RGB(224, 175, 104),
            RGB(122, 162, 247), RGB(187, 154, 247),  RGB(125, 207, 255), RGB(169, 177, 214),
            RGB(65, 72, 104),   RGB(247, 118, 142),  RGB(158, 206, 106), RGB(224, 175, 104),
            RGB(122, 162, 247), RGB(187, 154, 247),  RGB(125, 207, 255), RGB(199, 208, 245),
        },
    },
};

const char *mt_theme_name(MtThemeId id)
{
    if (id < 0 || id >= MT_THEME_COUNT) return "Unknown";
    return builtin_themes[id].name;
}

MtTheme mt_theme_get(MtThemeId id)
{
    if (id < 0 || id >= MT_THEME_COUNT) return builtin_themes[MT_THEME_CATPPUCCIN_MOCHA];
    return builtin_themes[id];
}

MtTheme mt_theme_builtin(const char *name)
{
    for (int i = 0; i < MT_THEME_COUNT; i++) {
        if (strcmp(builtin_themes[i].name, name) == 0) {
            return builtin_themes[i];
        }
    }
    return builtin_themes[MT_THEME_CATPPUCCIN_MOCHA];
}

/* --------------------------------------------------------------------------
 * Config loading
 * -------------------------------------------------------------------------- */

static MtConfig default_config(void)
{
    MtConfig cfg = {0};

    cfg.font_size = 16.0f;
    cfg.initial_width = 960;
    cfg.initial_height = 640;
    cfg.opacity = 1.0f;
    cfg.start_maximized = false;
    cfg.scrollback_lines = 10000;
    cfg.cursor_blink = true;
    cfg.cursor_blink_ms = 530;
    strncpy(cfg.cursor_shape, "block", sizeof(cfg.cursor_shape) - 1);
    cfg.confirm_close = true;
    cfg.copy_on_select = false;
    cfg.bell_visual = true;
    cfg.bell_audio = false;
    cfg.theme = builtin_themes[MT_THEME_CATPPUCCIN_MOCHA];

#ifdef _WIN32
    strncpy(cfg.shell, "powershell.exe", sizeof(cfg.shell) - 1);
    strncpy(cfg.font_path, "C:\\Windows\\Fonts\\CascadiaMono.ttf", MT_MAX_FONT_PATH - 1);
#else
    const char *shell = getenv("SHELL");
    if (shell) {
        strncpy(cfg.shell, shell, sizeof(cfg.shell) - 1);
    } else {
        strncpy(cfg.shell, "/bin/sh", sizeof(cfg.shell) - 1);
    }
    strncpy(cfg.font_path, "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
            MT_MAX_FONT_PATH - 1);
#endif

    return cfg;
}

/* Simple key=value parser for config file */
static void parse_config_line(MtConfig *cfg, const char *line)
{
    /* Skip comments and empty lines */
    while (*line == ' ' || *line == '\t') line++;
    if (*line == '#' || *line == '\0' || *line == '\n') return;

    char key[128] = {0};
    char value[512] = {0};

    if (sscanf(line, "%127[^= ] = %511[^\n]", key, value) != 2) return;

    /* Trim trailing whitespace from value */
    size_t vlen = strlen(value);
    while (vlen > 0 && (value[vlen - 1] == ' ' || value[vlen - 1] == '\t' ||
                        value[vlen - 1] == '\r' || value[vlen - 1] == '"'))
        value[--vlen] = '\0';
    /* Trim leading quote */
    char *val = value;
    if (*val == '"') val++;

    if (strcmp(key, "font_size") == 0)        cfg->font_size = (float)atof(val);
    else if (strcmp(key, "font") == 0)         strncpy(cfg->font_path, val, MT_MAX_FONT_PATH - 1);
    else if (strcmp(key, "shell") == 0)        strncpy(cfg->shell, val, sizeof(cfg->shell) - 1);
    else if (strcmp(key, "width") == 0)        cfg->initial_width = atoi(val);
    else if (strcmp(key, "height") == 0)       cfg->initial_height = atoi(val);
    else if (strcmp(key, "opacity") == 0)      cfg->opacity = (float)atof(val);
    else if (strcmp(key, "scrollback") == 0)   cfg->scrollback_lines = atoi(val);
    else if (strcmp(key, "cursor_blink") == 0) cfg->cursor_blink = (strcmp(val, "true") == 0);
    else if (strcmp(key, "cursor_shape") == 0) strncpy(cfg->cursor_shape, val, sizeof(cfg->cursor_shape) - 1);
    else if (strcmp(key, "confirm_close") == 0) cfg->confirm_close = (strcmp(val, "true") == 0);
    else if (strcmp(key, "copy_on_select") == 0) cfg->copy_on_select = (strcmp(val, "true") == 0);
    else if (strcmp(key, "theme") == 0)        cfg->theme = mt_theme_builtin(val);
}

MtConfig mt_config_load_from(const char *path)
{
    MtConfig cfg = default_config();

    FILE *f = fopen(path, "r");
    if (!f) return cfg;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        parse_config_line(&cfg, line);
    }

    fclose(f);
    return cfg;
}

MtConfig mt_config_load(void)
{
    char path[1024] = {0};

#ifdef _WIN32
    const char *appdata = getenv("APPDATA");
    if (appdata) {
        snprintf(path, sizeof(path), "%s\\myterm\\config", appdata);
    }
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) {
        snprintf(path, sizeof(path), "%s/myterm/config", xdg);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(path, sizeof(path), "%s/.config/myterm/config", home);
        }
    }
#endif

    if (path[0]) {
        return mt_config_load_from(path);
    }
    return default_config();
}
