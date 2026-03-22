/*
 * test_config.c — Unit tests for configuration and theme system.
 */

#include "test_harness.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void make_temp_path(char *out, size_t out_size, const char *filename)
{
    const char *base = getenv("TMPDIR");
#ifdef _WIN32
    if (!base || !*base) base = getenv("TEMP");
    if (!base || !*base) base = getenv("TMP");
    if (!base || !*base) base = ".";
    snprintf(out, out_size, "%s\\%s", base, filename);
#else
    if (!base || !*base) base = "/tmp";
    snprintf(out, out_size, "%s/%s", base, filename);
#endif
}

TEST(default_config_values)
{
    MtConfig cfg = mt_config_load();

    ASSERT(cfg.font_size > 0);
    ASSERT(cfg.initial_width > 0);
    ASSERT(cfg.initial_height > 0);
    ASSERT(cfg.scrollback_lines > 0);
    ASSERT(cfg.opacity > 0);
    ASSERT(cfg.shell[0] != '\0');
    ASSERT(cfg.cursor_shape[0] != '\0');
}

TEST(theme_defaults)
{
    MtConfig cfg = mt_config_load();

    /* Theme should have valid colors (non-zero alpha) */
    ASSERT(cfg.theme.bg.a == 255);
    ASSERT(cfg.theme.fg.a == 255);
    ASSERT(cfg.theme.cursor.a == 255);
    ASSERT(cfg.theme.name[0] != '\0');
}

TEST(builtin_themes_exist)
{
    for (int i = 0; i < MT_THEME_COUNT; i++) {
        MtTheme theme = mt_theme_get((MtThemeId)i);
        ASSERT(theme.name[0] != '\0');
        ASSERT(theme.bg.a == 255);
        ASSERT(theme.fg.a == 255);
    }
}

TEST(theme_names)
{
    ASSERT_STR_EQ(mt_theme_name(MT_THEME_CATPPUCCIN_MOCHA), "Catppuccin Mocha");
    ASSERT_STR_EQ(mt_theme_name(MT_THEME_DRACULA), "Dracula");
    ASSERT_STR_EQ(mt_theme_name(MT_THEME_NORD), "Nord");
    ASSERT_STR_EQ(mt_theme_name(MT_THEME_SOLARIZED_DARK), "Solarized Dark");
    ASSERT_STR_EQ(mt_theme_name(MT_THEME_GRUVBOX_DARK), "Gruvbox Dark");
    ASSERT_STR_EQ(mt_theme_name(MT_THEME_ONE_DARK), "One Dark");
    ASSERT_STR_EQ(mt_theme_name(MT_THEME_TOKYO_NIGHT), "Tokyo Night");
}

TEST(theme_invalid_id)
{
    const char *name = mt_theme_name(-1);
    ASSERT_STR_EQ(name, "Unknown");

    name = mt_theme_name(999);
    ASSERT_STR_EQ(name, "Unknown");

    /* Invalid ID should return default theme */
    MtTheme theme = mt_theme_get(-1);
    ASSERT_STR_EQ(theme.name, "Catppuccin Mocha");
}

TEST(theme_lookup_by_name)
{
    MtTheme theme = mt_theme_builtin("Dracula");
    ASSERT_STR_EQ(theme.name, "Dracula");

    theme = mt_theme_builtin("Nord");
    ASSERT_STR_EQ(theme.name, "Nord");

    /* Unknown name falls back to default */
    theme = mt_theme_builtin("Nonexistent Theme");
    ASSERT_STR_EQ(theme.name, "Catppuccin Mocha");
}

TEST(theme_palette_populated)
{
    for (int i = 0; i < MT_THEME_COUNT; i++) {
        MtTheme theme = mt_theme_get((MtThemeId)i);
        /* Every theme should have a populated 16-color palette */
        bool has_nonzero = false;
        for (int c = 0; c < 16; c++) {
            if (theme.palette[c].r != 0 || theme.palette[c].g != 0 || theme.palette[c].b != 0) {
                has_nonzero = true;
                break;
            }
        }
        ASSERT(has_nonzero);
    }
}

TEST(config_load_nonexistent_file)
{
    char path[1024];
    make_temp_path(path, sizeof(path), "nonexistent_myterm_config_12345");

    MtConfig cfg = mt_config_load_from(path);
    /* Should return defaults without crashing */
    ASSERT(cfg.font_size > 0);
    ASSERT(cfg.initial_width > 0);
}

TEST(config_load_from_file)
{
    /* Write a test config file */
    char path[1024];
    make_temp_path(path, sizeof(path), "myterm_test_config");

    FILE *f = fopen(path, "w");
    ASSERT(f != NULL);
    fprintf(f, "# Test config\n");
    fprintf(f, "font_size = 20.0\n");
    fprintf(f, "width = 1280\n");
    fprintf(f, "height = 800\n");
    fprintf(f, "scrollback = 5000\n");
    fprintf(f, "cursor_blink = false\n");
    fprintf(f, "cursor_shape = bar\n");
    fprintf(f, "theme = Dracula\n");
    fprintf(f, "copy_on_select = true\n");
    fclose(f);

    MtConfig cfg = mt_config_load_from(path);
    ASSERT(cfg.font_size == 20.0f);
    ASSERT_EQ(cfg.initial_width, 1280);
    ASSERT_EQ(cfg.initial_height, 800);
    ASSERT_EQ(cfg.scrollback_lines, 5000);
    ASSERT(cfg.cursor_blink == false);
    ASSERT_STR_EQ(cfg.cursor_shape, "bar");
    ASSERT_STR_EQ(cfg.theme.name, "Dracula");
    ASSERT(cfg.copy_on_select == true);

    remove(path);
}

int main(void)
{
    TEST_SUITE("Configuration & Themes");

    RUN_TEST(default_config_values);
    RUN_TEST(theme_defaults);
    RUN_TEST(builtin_themes_exist);
    RUN_TEST(theme_names);
    RUN_TEST(theme_invalid_id);
    RUN_TEST(theme_lookup_by_name);
    RUN_TEST(theme_palette_populated);
    RUN_TEST(config_load_nonexistent_file);
    RUN_TEST(config_load_from_file);

    TEST_REPORT();
}
