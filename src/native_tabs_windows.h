#ifndef NATIVE_TABS_WINDOWS_H
#define NATIVE_TABS_WINDOWS_H

#include <stdbool.h>

struct MtTabManager;
struct MtTheme;

typedef struct MtNativeTabs MtNativeTabs;

typedef struct {
    bool add_clicked;
    bool close_clicked;
    int  selected_index;
    int  rename_requested_index;
    int  close_requested_index;
    int  move_from_index;
    int  move_to_index;
} MtNativeTabsEvents;

#ifdef _WIN32
MtNativeTabs *mt_native_tabs_new(void *parent_handle);
void          mt_native_tabs_destroy(MtNativeTabs *tabs);
void          mt_native_tabs_sync(MtNativeTabs *tabs, struct MtTabManager *manager,
                                  const struct MtTheme *theme, int width);
MtNativeTabsEvents mt_native_tabs_poll(MtNativeTabs *tabs);
#else
static inline MtNativeTabs *mt_native_tabs_new(void *parent_handle) { (void)parent_handle; return NULL; }
static inline void mt_native_tabs_destroy(MtNativeTabs *tabs) { (void)tabs; }
static inline void mt_native_tabs_sync(MtNativeTabs *tabs, struct MtTabManager *manager,
                                       const struct MtTheme *theme, int width)
{ (void)tabs; (void)manager; (void)theme; (void)width; }
static inline MtNativeTabsEvents mt_native_tabs_poll(MtNativeTabs *tabs)
{
    (void)tabs;
    MtNativeTabsEvents e = { false, false, -1, -1, -1, -1, -1 };
    return e;
}
#endif

#endif
