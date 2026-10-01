/* panel_cage.c: see panel_cage.h.
 *
 * Not ClipCursor. That was the first cut, and enhanced_resolution's focus guard holds a
 * ClipCursor of its own round the whole window and puts it back the moment the cage it reads
 * differs from the window's rectangle, so the panel's cage was undone every frame; the two
 * DLLs share nothing and may not, so neither can ask the other to stand aside. The engine's own
 * confinement of the pointer to its play area is a warp on every move, not a cage, and this
 * does the same: a cursor found outside the panel is put on the nearest point inside it.
 */
#include "panel_cage.h"

#include "overlay_draw.h"
#include "overlay_input.h"
#include "overlay_layout.h"

#include <windows.h>

/* The panel's rectangle in desktop pixels: the layout is in the picture's own units, the
 * picture fills the window's client area, and the client area sits somewhere on the desktop.
 * The far edges are a pixel in, so a warped cursor reads back inside the panel. */
static bool panel_on_desktop(RECT *out)
{
    HWND            window = (HWND)overlay_input_window();
    const layout_t *lay = overlay_layout();
    RECT            client;
    POINT           origin;
    float           screen_w = 0.0f;
    float           screen_h = 0.0f;
    float           sx;
    float           sy;

    if (window == NULL || !GetClientRect(window, &client) ||
        !overlay_draw_screen(&screen_w, &screen_h) || screen_w <= 0.0f || screen_h <= 0.0f ||
        !(lay->width > 0.0f) || !(lay->height > 0.0f)) {
        return false;
    }
    origin.x = client.left;
    origin.y = client.top;
    if (!ClientToScreen(window, &origin)) {
        return false;
    }
    sx = (float)(client.right - client.left) / screen_w;
    sy = (float)(client.bottom - client.top) / screen_h;
    out->left   = origin.x + (LONG)(lay->left * sx);
    out->top    = origin.y + (LONG)(lay->top * sy);
    out->right  = origin.x + (LONG)((lay->left + lay->width) * sx) - 1;
    out->bottom = origin.y + (LONG)((lay->top + lay->height) * sy) - 1;
    return out->right > out->left && out->bottom > out->top;
}

static bool game_has_focus(void)
{
    HWND  foreground = GetForegroundWindow();
    DWORD owner = 0;

    if (foreground == NULL) {
        return false;
    }
    GetWindowThreadProcessId(foreground, &owner);
    return owner == GetCurrentProcessId();
}

void panel_cage_tick(void)
{
    RECT  rect;
    POINT cursor;
    POINT inside;

    if (!overlay_input_is_open() || overlay_input_is_hidden() || !game_has_focus() ||
        !panel_on_desktop(&rect) || !GetCursorPos(&cursor)) {
        return;
    }
    inside = cursor;
    if (inside.x < rect.left)   { inside.x = rect.left; }
    if (inside.x > rect.right)  { inside.x = rect.right; }
    if (inside.y < rect.top)    { inside.y = rect.top; }
    if (inside.y > rect.bottom) { inside.y = rect.bottom; }
    if (inside.x != cursor.x || inside.y != cursor.y) {
        SetCursorPos(inside.x, inside.y);
        overlay_input_update_pointer();   /* the pointer drawn this frame is the one put back */
    }
}
