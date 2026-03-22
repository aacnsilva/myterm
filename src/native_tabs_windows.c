#ifdef _WIN32

#include "native_tabs_windows.h"
#include "tabs.h"
#include "config.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <ctype.h>

#define MT_NATIVE_TAB_ID_ADD        1001
#define MT_NATIVE_TAB_ID_CLOSE      1002
#define MT_NATIVE_TAB_ID_MENU_NEW   1101
#define MT_NATIVE_TAB_ID_MENU_RENAME 1102
#define MT_NATIVE_TAB_ID_MENU_CLOSE 1103
#define MT_NATIVE_TAB_ID_MENU_LEFT  1104
#define MT_NATIVE_TAB_ID_MENU_RIGHT 1105

#define MT_NATIVE_TAB_SIDE_PAD   8
#define MT_NATIVE_TAB_TOP_PAD    3
#define MT_NATIVE_TAB_GAP        6
#define MT_NATIVE_TAB_BUTTON_W   26
#define MT_NATIVE_TAB_BUTTON_H   24
#define MT_NATIVE_TAB_MIN_W      88
#define MT_NATIVE_TAB_MAX_W      220
#define MT_NATIVE_TAB_CLOSE_SZ   16

struct MtNativeTabs {
    HWND parent_hwnd;
    HWND tab_hwnd;
    HWND add_hwnd;
    HWND close_hwnd;
    HWND rename_hwnd;

    HFONT ui_font;
    HBRUSH edit_bg_brush;

    struct MtTabManager *bound_tabs;

    COLORREF bar_bg;
    COLORREF active_bg;
    COLORREF inactive_bg;
    COLORREF active_fg;
    COLORREF inactive_fg;
    COLORREF accent;
    COLORREF border;
    COLORREF activity;

    bool add_hot;
    bool add_down;
    bool close_hot;
    bool close_down;

    bool add_clicked;
    bool close_clicked;
    int selected_index;
    int rename_requested_index;
    int close_requested_index;
    int move_from_index;
    int move_to_index;

    int context_tab_index;
    int drag_index;
    int rename_index;
    int hot_tab_index;
    int hot_close_index;
    int pressed_close_index;

    bool rename_commit;
    bool rename_cancel;

    int cached_width;
    int cached_count;
    int cached_active;
    char cached_titles[MT_MAX_TABS][MT_TAB_MAX_TITLE];
    bool cached_activity[MT_MAX_TABS];
};

static COLORREF rgba_to_colorref(MtRgba c)
{
    return RGB(c.r, c.g, c.b);
}

static void utf8_to_utf16_lossy(const char *src, wchar_t *dst, size_t dst_len)
{
    if (!dst || dst_len == 0) return;
    dst[0] = L'\0';
    if (!src || !*src) return;

    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int)dst_len) > 0) return;
    MultiByteToWideChar(CP_ACP, 0, src, -1, dst, (int)dst_len);
}

static void utf16_to_utf8_lossy(const wchar_t *src, char *dst, size_t dst_len)
{
    if (!dst || dst_len == 0) return;
    dst[0] = '\0';
    if (!src || !*src) return;

    if (WideCharToMultiByte(CP_UTF8, 0, src, -1, dst, (int)dst_len, NULL, NULL) > 0) return;
    WideCharToMultiByte(CP_ACP, 0, src, -1, dst, (int)dst_len, NULL, NULL);
}

static void trim_ascii(char *s)
{
    if (!s) return;
    size_t start = 0;
    size_t end = strlen(s);
    while (start < end && isspace((unsigned char)s[start])) start++;
    while (end > start && isspace((unsigned char)s[end - 1])) end--;
    if (start > 0) memmove(s, s + start, end - start);
    s[end - start] = '\0';
}

static COLORREF lighten_color(COLORREF c, int amount)
{
    int r = (int)GetRValue(c) + amount;
    int g = (int)GetGValue(c) + amount;
    int b = (int)GetBValue(c) + amount;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

static void invalidate(HWND hwnd)
{
    if (hwnd) InvalidateRect(hwnd, NULL, TRUE);
}

static void start_track_mouse_leave(HWND hwnd)
{
    TRACKMOUSEEVENT tme;
    ZeroMemory(&tme, sizeof(tme));
    tme.cbSize = sizeof(tme);
    tme.dwFlags = TME_LEAVE;
    tme.hwndTrack = hwnd;
    TrackMouseEvent(&tme);
}

static int hit_test_tab(HWND tab_hwnd, LPARAM lparam)
{
    TCHITTESTINFO info;
    ZeroMemory(&info, sizeof(info));
    info.pt.x = GET_X_LPARAM(lparam);
    info.pt.y = GET_Y_LPARAM(lparam);
    return TabCtrl_HitTest(tab_hwnd, &info);
}

static void adjust_tab_draw_rect(RECT *rc, bool active)
{
    if (!rc) return;
    rc->left += 3;
    rc->right -= 3;
    rc->top += active ? 2 : 4;
    rc->bottom -= 3;
}

static bool get_tab_close_rect(MtNativeTabs *tabs, int index, RECT *out_rc)
{
    if (!tabs || !out_rc || !tabs->bound_tabs) return false;
    if (index < 0 || index >= mt_tabs_count(tabs->bound_tabs)) return false;

    RECT rc;
    if (!TabCtrl_GetItemRect(tabs->tab_hwnd, index, &rc)) return false;
    adjust_tab_draw_rect(&rc, index == mt_tabs_active_index(tabs->bound_tabs));

    out_rc->right = rc.right - 8;
    out_rc->left = out_rc->right - MT_NATIVE_TAB_CLOSE_SZ;
    out_rc->top = rc.top + ((rc.bottom - rc.top) - MT_NATIVE_TAB_CLOSE_SZ) / 2;
    out_rc->bottom = out_rc->top + MT_NATIVE_TAB_CLOSE_SZ;
    return true;
}

static void set_tab_hot_state(MtNativeTabs *tabs, int hot_tab_index, int hot_close_index)
{
    if (!tabs) return;
    if (tabs->hot_tab_index == hot_tab_index && tabs->hot_close_index == hot_close_index) return;
    tabs->hot_tab_index = hot_tab_index;
    tabs->hot_close_index = hot_close_index;
    invalidate(tabs->tab_hwnd);
}

static void snapshot_tabs(MtNativeTabs *tabs, MtTabManager *manager, int width)
{
    tabs->cached_width = width;
    tabs->cached_count = mt_tabs_count(manager);
    tabs->cached_active = mt_tabs_active_index(manager);
    for (int i = 0; i < MT_MAX_TABS; i++) {
        tabs->cached_titles[i][0] = '\0';
        tabs->cached_activity[i] = false;
    }
    for (int i = 0; i < tabs->cached_count; i++) {
        MtTab *tab = mt_tabs_get(manager, i);
        if (!tab) continue;
        strncpy(tabs->cached_titles[i], tab->title, MT_TAB_MAX_TITLE - 1);
        tabs->cached_titles[i][MT_TAB_MAX_TITLE - 1] = '\0';
        tabs->cached_activity[i] = tab->has_activity;
    }
}

static bool tab_cache_differs(MtNativeTabs *tabs, MtTabManager *manager, int width)
{
    if (!tabs || !manager) return false;
    if (tabs->cached_width != width) return true;
    if (tabs->cached_count != mt_tabs_count(manager)) return true;
    if (tabs->cached_active != mt_tabs_active_index(manager)) return true;

    int count = mt_tabs_count(manager);
    for (int i = 0; i < count; i++) {
        MtTab *tab = mt_tabs_get(manager, i);
        if (!tab) return true;
        if (strncmp(tabs->cached_titles[i], tab->title, MT_TAB_MAX_TITLE) != 0) return true;
        if (tabs->cached_activity[i] != tab->has_activity) return true;
    }

    return false;
}

static void sync_button_state(MtNativeTabs *tabs, HWND hwnd, bool hover, bool down)
{
    if (!tabs) return;
    if (hwnd == tabs->add_hwnd) {
        tabs->add_hot = hover;
        tabs->add_down = down;
    } else if (hwnd == tabs->close_hwnd) {
        tabs->close_hot = hover;
        tabs->close_down = down;
    }
    invalidate(hwnd);
}

static LRESULT CALLBACK mt_native_button_subclass_proc(HWND hwnd, UINT msg,
                                                       WPARAM wparam, LPARAM lparam,
                                                       UINT_PTR id, DWORD_PTR ref_data);
static LRESULT CALLBACK mt_native_edit_subclass_proc(HWND hwnd, UINT msg,
                                                     WPARAM wparam, LPARAM lparam,
                                                     UINT_PTR id, DWORD_PTR ref_data);
static LRESULT CALLBACK mt_native_tab_subclass_proc(HWND hwnd, UINT msg,
                                                    WPARAM wparam, LPARAM lparam,
                                                    UINT_PTR id, DWORD_PTR ref_data);
static LRESULT CALLBACK mt_native_tabs_parent_subclass_proc(HWND hwnd, UINT msg,
                                                            WPARAM wparam, LPARAM lparam,
                                                            UINT_PTR id, DWORD_PTR ref_data);

static LRESULT CALLBACK mt_native_button_subclass_proc(HWND hwnd, UINT msg,
                                                       WPARAM wparam, LPARAM lparam,
                                                       UINT_PTR id, DWORD_PTR ref_data)
{
    (void)id;
    MtNativeTabs *tabs = (MtNativeTabs *)ref_data;
    if (!tabs) return DefSubclassProc(hwnd, msg, wparam, lparam);

    switch (msg) {
    case WM_MOUSEMOVE:
        start_track_mouse_leave(hwnd);
        sync_button_state(tabs, hwnd, true,
                          (GetKeyState(VK_LBUTTON) & 0x8000) != 0);
        break;
    case WM_MOUSELEAVE:
        sync_button_state(tabs, hwnd, false, false);
        break;
    case WM_LBUTTONDOWN:
        sync_button_state(tabs, hwnd, true, true);
        break;
    case WM_LBUTTONUP:
        sync_button_state(tabs, hwnd, true, false);
        break;
    default:
        break;
    }

    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

static void destroy_rename_editor(MtNativeTabs *tabs)
{
    if (!tabs) return;
    if (tabs->rename_hwnd) {
        RemoveWindowSubclass(tabs->rename_hwnd, mt_native_edit_subclass_proc, 1);
        DestroyWindow(tabs->rename_hwnd);
        tabs->rename_hwnd = NULL;
    }
    tabs->rename_index = -1;
    tabs->rename_commit = false;
    tabs->rename_cancel = false;
}

static LRESULT CALLBACK mt_native_edit_subclass_proc(HWND hwnd, UINT msg,
                                                     WPARAM wparam, LPARAM lparam,
                                                     UINT_PTR id, DWORD_PTR ref_data)
{
    (void)id;
    MtNativeTabs *tabs = (MtNativeTabs *)ref_data;
    if (!tabs) return DefSubclassProc(hwnd, msg, wparam, lparam);

    switch (msg) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;
    case WM_KEYDOWN:
        if (wparam == VK_RETURN) {
            tabs->rename_commit = true;
            return 0;
        }
        if (wparam == VK_ESCAPE) {
            tabs->rename_cancel = true;
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        tabs->rename_commit = true;
        break;
    default:
        break;
    }

    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

static void apply_rename_if_needed(MtNativeTabs *tabs)
{
    if (!tabs || !tabs->bound_tabs) return;
    if (tabs->rename_index < 0 || !tabs->rename_hwnd) return;

    if (tabs->rename_cancel) {
        mt_tabs_cancel_rename(tabs->bound_tabs, tabs->rename_index);
        destroy_rename_editor(tabs);
        SetFocus(tabs->parent_hwnd);
        return;
    }

    if (tabs->rename_commit) {
        wchar_t wide[MT_TAB_MAX_TITLE];
        char utf8[MT_TAB_MAX_TITLE * 4];
        GetWindowTextW(tabs->rename_hwnd, wide, (int)(sizeof(wide) / sizeof(wide[0])));
        utf16_to_utf8_lossy(wide, utf8, sizeof(utf8));
        trim_ascii(utf8);
        if (utf8[0] != '\0') {
            mt_tabs_set_title(tabs->bound_tabs, tabs->rename_index, utf8);
        }
        mt_tabs_cancel_rename(tabs->bound_tabs, tabs->rename_index);
        destroy_rename_editor(tabs);
        SetFocus(tabs->parent_hwnd);
    }
}

static void ensure_rename_editor(MtNativeTabs *tabs, MtTabManager *manager)
{
    if (!tabs || !manager) return;

    apply_rename_if_needed(tabs);

    int active = mt_tabs_active_index(manager);
    if (active < 0 || !mt_tabs_is_renaming(manager, active)) {
        destroy_rename_editor(tabs);
        return;
    }

    RECT item_rc;
    if (!TabCtrl_GetItemRect(tabs->tab_hwnd, active, &item_rc)) return;
    MapWindowPoints(tabs->tab_hwnd, tabs->parent_hwnd, (LPPOINT)&item_rc, 2);

    item_rc.left += 12;
    item_rc.right -= 12;
    item_rc.top += 5;
    item_rc.bottom -= 5;
    if (item_rc.right <= item_rc.left + 20) item_rc.right = item_rc.left + 20;

    if (!tabs->rename_hwnd || tabs->rename_index != active) {
        destroy_rename_editor(tabs);

        wchar_t title[MT_TAB_MAX_TITLE];
        MtTab *tab = mt_tabs_get(manager, active);
        utf8_to_utf16_lossy(tab ? tab->title : "", title, sizeof(title) / sizeof(title[0]));

        tabs->rename_hwnd = CreateWindowExW(
            0,
            L"EDIT",
            title,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            item_rc.left,
            item_rc.top,
            item_rc.right - item_rc.left,
            item_rc.bottom - item_rc.top,
            tabs->parent_hwnd,
            NULL,
            GetModuleHandleW(NULL),
            NULL
        );
        if (!tabs->rename_hwnd) return;

        tabs->rename_index = active;
        SendMessageW(tabs->rename_hwnd, WM_SETFONT, (WPARAM)tabs->ui_font, TRUE);
        SendMessageW(tabs->rename_hwnd, EM_LIMITTEXT, MT_TAB_MAX_TITLE - 1, 0);
        SetWindowSubclass(tabs->rename_hwnd, mt_native_edit_subclass_proc, 1, (DWORD_PTR)tabs);
        SetFocus(tabs->rename_hwnd);
        SendMessageW(tabs->rename_hwnd, EM_SETSEL, 0, -1);
    } else {
        MoveWindow(tabs->rename_hwnd,
                   item_rc.left, item_rc.top,
                   item_rc.right - item_rc.left,
                   item_rc.bottom - item_rc.top,
                   TRUE);
    }
}

static void draw_round_rect(HDC hdc, RECT rc, COLORREF fill, COLORREF outline, int radius)
{
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, outline);
    HGDIOBJ old_brush = SelectObject(hdc, brush);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, old_pen);
    SelectObject(hdc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void draw_close_glyph(HDC hdc, RECT rc, COLORREF color)
{
    HPEN pen = CreatePen(PS_SOLID, 2, color);
    HGDIOBJ old_pen = SelectObject(hdc, pen);
    MoveToEx(hdc, rc.left + 4, rc.top + 4, NULL);
    LineTo(hdc, rc.right - 4, rc.bottom - 4);
    MoveToEx(hdc, rc.right - 4, rc.top + 4, NULL);
    LineTo(hdc, rc.left + 4, rc.bottom - 4);
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
}

static void draw_button(MtNativeTabs *tabs, const DRAWITEMSTRUCT *dis, bool is_close)
{
    bool enabled = (dis->itemState & ODS_DISABLED) == 0;
    bool hot = is_close ? tabs->close_hot : tabs->add_hot;
    bool down = is_close ? tabs->close_down : tabs->add_down;

    RECT rc = dis->rcItem;
    InflateRect(&rc, -1, -1);

    COLORREF fill = tabs->inactive_bg;
    COLORREF outline = tabs->border;
    COLORREF fg = tabs->inactive_fg;

    if (enabled && hot) {
        fill = tabs->active_bg;
        fg = tabs->active_fg;
        outline = tabs->accent;
    }
    if (enabled && down) {
        fill = tabs->bar_bg;
    }
    if (!enabled) {
        fg = lighten_color(fg, 40);
    }

    draw_round_rect(dis->hDC, rc, fill, outline, 10);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, fg);
    SelectObject(dis->hDC, tabs->ui_font);
    DrawTextW(dis->hDC, is_close ? L"×" : L"+", -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void draw_tab_item(MtNativeTabs *tabs, const DRAWITEMSTRUCT *dis)
{
    if (!tabs || !tabs->bound_tabs) return;
    int index = (int)dis->itemID;
    MtTab *tab = mt_tabs_get(tabs->bound_tabs, index);
    if (!tab) return;

    RECT rc = dis->rcItem;
    bool active = (index == mt_tabs_active_index(tabs->bound_tabs));
    bool hovered = (index == tabs->hot_tab_index);
    bool close_hovered = (index == tabs->hot_close_index);
    bool close_pressed = (index == tabs->pressed_close_index);

    adjust_tab_draw_rect(&rc, active);

    COLORREF fill = active ? tabs->active_bg : tabs->inactive_bg;
    COLORREF outline = active ? tabs->accent : tabs->border;
    COLORREF fg = active ? tabs->active_fg : tabs->inactive_fg;

    if (hovered && !active) {
        fill = lighten_color(fill, 8);
        outline = lighten_color(outline, 12);
    } else if (hovered && active) {
        fill = lighten_color(fill, 4);
    }

    draw_round_rect(dis->hDC, rc, fill, outline, 12);

    if (active || hovered) {
        RECT accent = rc;
        accent.top = accent.bottom - (active ? 3 : 2);
        HBRUSH accent_brush = CreateSolidBrush(active ? tabs->accent : lighten_color(tabs->accent, 18));
        FillRect(dis->hDC, &accent, accent_brush);
        DeleteObject(accent_brush);
    }

    RECT text_rc = rc;
    text_rc.left += 12;
    text_rc.right -= 12;

    if (tab->has_activity && !active) {
        HBRUSH dot = CreateSolidBrush(tabs->activity);
        HBRUSH old_brush = SelectObject(dis->hDC, dot);
        HPEN pen = CreatePen(PS_SOLID, 1, tabs->activity);
        HGDIOBJ old_pen = SelectObject(dis->hDC, pen);
        Ellipse(dis->hDC, text_rc.left, rc.top + 9, text_rc.left + 7, rc.top + 16);
        SelectObject(dis->hDC, old_pen);
        SelectObject(dis->hDC, old_brush);
        DeleteObject(pen);
        DeleteObject(dot);
        text_rc.left += 12;
    }

    if (mt_tabs_count(tabs->bound_tabs) > 1) {
        RECT close_rc;
        if (get_tab_close_rect(tabs, index, &close_rc)) {
            if (close_hovered || close_pressed || active) {
                RECT bubble = close_rc;
                InflateRect(&bubble, 2, 2);
                draw_round_rect(dis->hDC, bubble,
                                close_pressed ? tabs->bar_bg : (close_hovered ? lighten_color(fill, 10) : fill),
                                close_hovered ? tabs->accent : outline,
                                10);
            }

            COLORREF close_fg = close_hovered ? tabs->active_fg : fg;
            if (!active && !close_hovered && !hovered) {
                close_fg = lighten_color(fg, 20);
            }
            draw_close_glyph(dis->hDC, close_rc, close_fg);
            text_rc.right = close_rc.left - 8;
        }
    }

    wchar_t title[MT_TAB_MAX_TITLE];
    utf8_to_utf16_lossy(tab->title, title, sizeof(title) / sizeof(title[0]));

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, fg);
    SelectObject(dis->hDC, tabs->ui_font);
    DrawTextW(dis->hDC, title, -1, &text_rc,
              DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);
}

static void show_tab_context_menu(MtNativeTabs *tabs, int index, POINT screen_pt)
{
    if (!tabs || !tabs->bound_tabs || index < 0) return;

    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    int count = mt_tabs_count(tabs->bound_tabs);
    AppendMenuW(menu, MF_STRING, MT_NATIVE_TAB_ID_MENU_NEW, L"New tab");
    AppendMenuW(menu, MF_STRING, MT_NATIVE_TAB_ID_MENU_RENAME, L"Rename tab");
    AppendMenuW(menu, MF_STRING | (count > 1 ? 0 : MF_GRAYED), MT_NATIVE_TAB_ID_MENU_CLOSE, L"Close tab");
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING | (index > 0 ? 0 : MF_GRAYED), MT_NATIVE_TAB_ID_MENU_LEFT, L"Move left");
    AppendMenuW(menu, MF_STRING | (index < count - 1 ? 0 : MF_GRAYED), MT_NATIVE_TAB_ID_MENU_RIGHT, L"Move right");

    tabs->context_tab_index = index;
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, screen_pt.x, screen_pt.y, 0, tabs->parent_hwnd, NULL);
    DestroyMenu(menu);
}

static LRESULT CALLBACK mt_native_tab_subclass_proc(HWND hwnd, UINT msg,
                                                    WPARAM wparam, LPARAM lparam,
                                                    UINT_PTR id, DWORD_PTR ref_data)
{
    (void)id;
    MtNativeTabs *tabs = (MtNativeTabs *)ref_data;
    if (!tabs) return DefSubclassProc(hwnd, msg, wparam, lparam);

    switch (msg) {
    case WM_MOUSEMOVE: {
        start_track_mouse_leave(hwnd);
        int index = hit_test_tab(hwnd, lparam);
        int close_index = -1;
        if (index >= 0 && tabs->bound_tabs && mt_tabs_count(tabs->bound_tabs) > 1) {
            RECT close_rc;
            POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            if (get_tab_close_rect(tabs, index, &close_rc) && PtInRect(&close_rc, pt)) {
                close_index = index;
            }
        }
        set_tab_hot_state(tabs, index, close_index);

        if (tabs->drag_index >= 0 && (wparam & MK_LBUTTON) && close_index < 0) {
            int over = index;
            if (over >= 0 && over != tabs->drag_index) {
                tabs->move_from_index = tabs->drag_index;
                tabs->move_to_index = over;
                tabs->drag_index = over;
            }
        }
        break;
    }
    case WM_MOUSELEAVE:
        set_tab_hot_state(tabs, -1, -1);
        break;
    case WM_LBUTTONDOWN: {
        int index = hit_test_tab(hwnd, lparam);
        if (index >= 0) {
            RECT close_rc;
            POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            if (tabs->bound_tabs && mt_tabs_count(tabs->bound_tabs) > 1 &&
                get_tab_close_rect(tabs, index, &close_rc) && PtInRect(&close_rc, pt)) {
                tabs->pressed_close_index = index;
                SetCapture(hwnd);
                invalidate(hwnd);
                return 0;
            }
            tabs->drag_index = index;
            SetCapture(hwnd);
        }
        break;
    }
    case WM_LBUTTONUP:
        if (tabs->pressed_close_index >= 0) {
            int pressed = tabs->pressed_close_index;
            if (tabs->hot_close_index == pressed) {
                tabs->close_requested_index = pressed;
            }
            tabs->pressed_close_index = -1;
            if (GetCapture() == hwnd) ReleaseCapture();
            invalidate(hwnd);
            return 0;
        }
        if (GetCapture() == hwnd) ReleaseCapture();
        tabs->drag_index = -1;
        break;
    case WM_CAPTURECHANGED:
        tabs->drag_index = -1;
        tabs->pressed_close_index = -1;
        invalidate(hwnd);
        break;
    case WM_LBUTTONDBLCLK: {
        int index = hit_test_tab(hwnd, lparam);
        if (index >= 0) {
            RECT close_rc;
            POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            if (get_tab_close_rect(tabs, index, &close_rc) && PtInRect(&close_rc, pt)) {
                return 0;
            }
            tabs->rename_requested_index = index;
            return 0;
        }
        break;
    }
    case WM_RBUTTONUP: {
        int index = hit_test_tab(hwnd, lparam);
        if (index >= 0) {
            POINT pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            ClientToScreen(hwnd, &pt);
            show_tab_context_menu(tabs, index, pt);
            return 0;
        }
        break;
    }
    case WM_CONTEXTMENU: {
        POINT screen_pt = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
        if (screen_pt.x == -1 && screen_pt.y == -1) {
            int active = tabs->bound_tabs ? mt_tabs_active_index(tabs->bound_tabs) : -1;
            if (active >= 0) {
                RECT rc;
                if (TabCtrl_GetItemRect(hwnd, active, &rc)) {
                    POINT pt = { rc.left + 16, rc.bottom };
                    ClientToScreen(hwnd, &pt);
                    show_tab_context_menu(tabs, active, pt);
                    return 0;
                }
            }
        }
        break;
    }
    default:
        break;
    }

    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK mt_native_tabs_parent_subclass_proc(HWND hwnd, UINT msg,
                                                            WPARAM wparam, LPARAM lparam,
                                                            UINT_PTR id, DWORD_PTR ref_data)
{
    (void)id;
    MtNativeTabs *tabs = (MtNativeTabs *)ref_data;
    if (!tabs) return DefSubclassProc(hwnd, msg, wparam, lparam);

    switch (msg) {
    case WM_COMMAND:
        if (HIWORD(wparam) == BN_CLICKED) {
            switch (LOWORD(wparam)) {
            case MT_NATIVE_TAB_ID_ADD:
                tabs->add_clicked = true;
                return 0;
            case MT_NATIVE_TAB_ID_CLOSE:
                tabs->close_clicked = true;
                return 0;
            default:
                break;
            }
        }
        switch (LOWORD(wparam)) {
        case MT_NATIVE_TAB_ID_MENU_NEW:
            tabs->add_clicked = true;
            return 0;
        case MT_NATIVE_TAB_ID_MENU_RENAME:
            tabs->rename_requested_index = tabs->context_tab_index;
            return 0;
        case MT_NATIVE_TAB_ID_MENU_CLOSE:
            tabs->close_requested_index = tabs->context_tab_index;
            return 0;
        case MT_NATIVE_TAB_ID_MENU_LEFT:
            tabs->move_from_index = tabs->context_tab_index;
            tabs->move_to_index = tabs->context_tab_index - 1;
            return 0;
        case MT_NATIVE_TAB_ID_MENU_RIGHT:
            tabs->move_from_index = tabs->context_tab_index;
            tabs->move_to_index = tabs->context_tab_index + 1;
            return 0;
        default:
            break;
        }
        break;
    case WM_NOTIFY: {
        NMHDR *hdr = (NMHDR *)lparam;
        if (hdr && hdr->hwndFrom == tabs->tab_hwnd && hdr->code == TCN_SELCHANGE) {
            tabs->selected_index = TabCtrl_GetCurSel(tabs->tab_hwnd);
            return 0;
        }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lparam;
        if (!dis) break;
        if (dis->hwndItem == tabs->tab_hwnd || dis->CtlType == ODT_TAB) {
            draw_tab_item(tabs, dis);
            return TRUE;
        }
        if (dis->hwndItem == tabs->add_hwnd) {
            draw_button(tabs, dis, false);
            return TRUE;
        }
        if (dis->hwndItem == tabs->close_hwnd) {
            draw_button(tabs, dis, true);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLOREDIT:
        if ((HWND)lparam == tabs->rename_hwnd) {
            SetTextColor((HDC)wparam, tabs->active_fg);
            SetBkColor((HDC)wparam, tabs->active_bg);
            return (LRESULT)tabs->edit_bg_brush;
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, mt_native_tabs_parent_subclass_proc, 1);
        break;
    default:
        break;
    }

    return DefSubclassProc(hwnd, msg, wparam, lparam);
}

MtNativeTabs *mt_native_tabs_new(void *parent_handle)
{
    HWND parent = (HWND)parent_handle;
    if (!parent) return NULL;

    INITCOMMONCONTROLSEX icc;
    ZeroMemory(&icc, sizeof(icc));
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES;
    InitCommonControlsEx(&icc);

    MtNativeTabs *tabs = calloc(1, sizeof(MtNativeTabs));
    if (!tabs) return NULL;

    tabs->parent_hwnd = parent;
    tabs->selected_index = -1;
    tabs->rename_requested_index = -1;
    tabs->close_requested_index = -1;
    tabs->move_from_index = -1;
    tabs->move_to_index = -1;
    tabs->context_tab_index = -1;
    tabs->drag_index = -1;
    tabs->rename_index = -1;
    tabs->hot_tab_index = -1;
    tabs->hot_close_index = -1;
    tabs->pressed_close_index = -1;
    tabs->cached_width = -1;
    tabs->cached_count = -1;
    tabs->cached_active = -1;

    tabs->bar_bg = RGB(30, 30, 46);
    tabs->active_bg = RGB(49, 50, 68);
    tabs->inactive_bg = RGB(24, 24, 37);
    tabs->active_fg = RGB(205, 214, 244);
    tabs->inactive_fg = RGB(166, 173, 200);
    tabs->accent = RGB(137, 180, 250);
    tabs->border = RGB(69, 71, 90);
    tabs->activity = RGB(250, 179, 135);

    tabs->edit_bg_brush = CreateSolidBrush(tabs->active_bg);
    tabs->ui_font = CreateFontW(
        -14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI"
    );
    if (!tabs->ui_font) {
        tabs->ui_font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    }

    SetWindowSubclass(parent, mt_native_tabs_parent_subclass_proc, 1, (DWORD_PTR)tabs);

    tabs->tab_hwnd = CreateWindowExW(
        0,
        WC_TABCONTROLW,
        L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS |
            TCS_SINGLELINE | TCS_FOCUSNEVER | TCS_OWNERDRAWFIXED | TCS_FIXEDWIDTH |
            TCS_BUTTONS | TCS_FLATBUTTONS,
        0, 0, 100, (int)MT_TAB_BAR_HEIGHT,
        parent,
        NULL,
        GetModuleHandleW(NULL),
        NULL
    );
    tabs->add_hwnd = CreateWindowExW(
        0,
        L"BUTTON",
        L"+",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, MT_NATIVE_TAB_BUTTON_W, MT_NATIVE_TAB_BUTTON_H,
        parent,
        (HMENU)(INT_PTR)MT_NATIVE_TAB_ID_ADD,
        GetModuleHandleW(NULL),
        NULL
    );
    tabs->close_hwnd = CreateWindowExW(
        0,
        L"BUTTON",
        L"×",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        0, 0, MT_NATIVE_TAB_BUTTON_W, MT_NATIVE_TAB_BUTTON_H,
        parent,
        (HMENU)(INT_PTR)MT_NATIVE_TAB_ID_CLOSE,
        GetModuleHandleW(NULL),
        NULL
    );

    if (!tabs->tab_hwnd || !tabs->add_hwnd || !tabs->close_hwnd) {
        mt_native_tabs_destroy(tabs);
        return NULL;
    }

    SendMessageW(tabs->tab_hwnd, WM_SETFONT, (WPARAM)tabs->ui_font, TRUE);
    SendMessageW(tabs->add_hwnd, WM_SETFONT, (WPARAM)tabs->ui_font, TRUE);
    SendMessageW(tabs->close_hwnd, WM_SETFONT, (WPARAM)tabs->ui_font, TRUE);
    SendMessageW(tabs->tab_hwnd, WM_SETFONT, (WPARAM)tabs->ui_font, TRUE);
    TabCtrl_SetPadding(tabs->tab_hwnd, 18, 8);

    SetWindowSubclass(tabs->tab_hwnd, mt_native_tab_subclass_proc, 1, (DWORD_PTR)tabs);
    SetWindowSubclass(tabs->add_hwnd, mt_native_button_subclass_proc, 1, (DWORD_PTR)tabs);
    SetWindowSubclass(tabs->close_hwnd, mt_native_button_subclass_proc, 1, (DWORD_PTR)tabs);

    return tabs;
}

void mt_native_tabs_destroy(MtNativeTabs *tabs)
{
    if (!tabs) return;

    destroy_rename_editor(tabs);

    if (tabs->tab_hwnd) {
        RemoveWindowSubclass(tabs->tab_hwnd, mt_native_tab_subclass_proc, 1);
        DestroyWindow(tabs->tab_hwnd);
    }
    if (tabs->add_hwnd) {
        RemoveWindowSubclass(tabs->add_hwnd, mt_native_button_subclass_proc, 1);
        DestroyWindow(tabs->add_hwnd);
    }
    if (tabs->close_hwnd) {
        RemoveWindowSubclass(tabs->close_hwnd, mt_native_button_subclass_proc, 1);
        DestroyWindow(tabs->close_hwnd);
    }
    if (tabs->parent_hwnd) {
        RemoveWindowSubclass(tabs->parent_hwnd, mt_native_tabs_parent_subclass_proc, 1);
    }
    if (tabs->edit_bg_brush) DeleteObject(tabs->edit_bg_brush);
    if (tabs->ui_font && tabs->ui_font != (HFONT)GetStockObject(DEFAULT_GUI_FONT)) {
        DeleteObject(tabs->ui_font);
    }
    free(tabs);
}

void mt_native_tabs_sync(MtNativeTabs *tabs, MtTabManager *manager,
                         const MtTheme *theme, int width)
{
    if (!tabs || !manager) return;

    tabs->bound_tabs = manager;
    if (theme) {
        COLORREF new_bar_bg = rgba_to_colorref(theme->tab_bar_bg);
        COLORREF new_active_bg = rgba_to_colorref(theme->tab_active_bg);
        COLORREF new_inactive_bg = rgba_to_colorref(theme->tab_inactive_bg);
        COLORREF new_active_fg = rgba_to_colorref(theme->tab_active_fg);
        COLORREF new_inactive_fg = rgba_to_colorref(theme->tab_inactive_fg);
        COLORREF new_accent = rgba_to_colorref(theme->cursor);
        COLORREF new_border = rgba_to_colorref(theme->split_border);
        COLORREF new_activity = rgba_to_colorref(theme->tab_activity);

        tabs->bar_bg = new_bar_bg;
        tabs->inactive_bg = new_inactive_bg;
        tabs->active_fg = new_active_fg;
        tabs->inactive_fg = new_inactive_fg;
        tabs->accent = new_accent;
        tabs->border = new_border;
        tabs->activity = new_activity;

        if (tabs->active_bg != new_active_bg || !tabs->edit_bg_brush) {
            tabs->active_bg = new_active_bg;
            if (tabs->edit_bg_brush) DeleteObject(tabs->edit_bg_brush);
            tabs->edit_bg_brush = CreateSolidBrush(tabs->active_bg);
        } else {
            tabs->active_bg = new_active_bg;
        }
    }

    const int bar_h = (int)MT_TAB_BAR_HEIGHT;
    const int button_y = (bar_h - MT_NATIVE_TAB_BUTTON_H) / 2;
    const int right_pad = MT_NATIVE_TAB_SIDE_PAD;
    const int button_gap = 6;
    const int buttons_w = MT_NATIVE_TAB_BUTTON_W * 2 + button_gap;
    int tab_area_x = MT_NATIVE_TAB_SIDE_PAD;
    int tab_area_w = width - tab_area_x - right_pad - buttons_w - MT_NATIVE_TAB_GAP;
    if (tab_area_w < 120) tab_area_w = 120;

    MoveWindow(tabs->tab_hwnd, tab_area_x, MT_NATIVE_TAB_TOP_PAD,
               tab_area_w, bar_h - MT_NATIVE_TAB_TOP_PAD, TRUE);
    MoveWindow(tabs->add_hwnd, width - right_pad - buttons_w,
               button_y, MT_NATIVE_TAB_BUTTON_W, MT_NATIVE_TAB_BUTTON_H, TRUE);
    MoveWindow(tabs->close_hwnd, width - right_pad - MT_NATIVE_TAB_BUTTON_W,
               button_y, MT_NATIVE_TAB_BUTTON_W, MT_NATIVE_TAB_BUTTON_H, TRUE);

    int count = mt_tabs_count(manager);
    int item_w = count > 0 ? tab_area_w / count : tab_area_w;
    if (item_w < MT_NATIVE_TAB_MIN_W) item_w = MT_NATIVE_TAB_MIN_W;
    if (item_w > MT_NATIVE_TAB_MAX_W) item_w = MT_NATIVE_TAB_MAX_W;
    TabCtrl_SetItemSize(tabs->tab_hwnd, item_w, bar_h - 6);

    if (tab_cache_differs(tabs, manager, width)) {
        TabCtrl_DeleteAllItems(tabs->tab_hwnd);
        for (int i = 0; i < count; i++) {
            MtTab *tab = mt_tabs_get(manager, i);
            if (!tab) continue;
            wchar_t title[MT_TAB_MAX_TITLE];
            TCITEMW item;
            ZeroMemory(&item, sizeof(item));
            item.mask = TCIF_TEXT;
            utf8_to_utf16_lossy(tab->title, title, sizeof(title) / sizeof(title[0]));
            item.pszText = title;
            TabCtrl_InsertItem(tabs->tab_hwnd, i, &item);
        }
        snapshot_tabs(tabs, manager, width);
    }

    int active = mt_tabs_active_index(manager);
    if (active >= 0 && active < count) {
        TabCtrl_SetCurSel(tabs->tab_hwnd, active);
    }

    EnableWindow(tabs->close_hwnd, count > 1 ? TRUE : FALSE);
    ensure_rename_editor(tabs, manager);
    invalidate(tabs->tab_hwnd);
    invalidate(tabs->add_hwnd);
    invalidate(tabs->close_hwnd);
}

MtNativeTabsEvents mt_native_tabs_poll(MtNativeTabs *tabs)
{
    MtNativeTabsEvents events = { false, false, -1, -1, -1, -1, -1 };
    if (!tabs) return events;

    events.add_clicked = tabs->add_clicked;
    events.close_clicked = tabs->close_clicked;
    events.selected_index = tabs->selected_index;
    events.rename_requested_index = tabs->rename_requested_index;
    events.close_requested_index = tabs->close_requested_index;
    events.move_from_index = tabs->move_from_index;
    events.move_to_index = tabs->move_to_index;

    tabs->add_clicked = false;
    tabs->close_clicked = false;
    tabs->selected_index = -1;
    tabs->rename_requested_index = -1;
    tabs->close_requested_index = -1;
    tabs->move_from_index = -1;
    tabs->move_to_index = -1;
    return events;
}

#endif
