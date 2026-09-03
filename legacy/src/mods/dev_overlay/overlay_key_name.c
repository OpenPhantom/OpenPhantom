/* overlay_key_name.c: see overlay_key_name.h. */
#include "overlay_key_name.h"

#include <windows.h>

#include <stdio.h>

void overlay_key_name(int32_t vk, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0u) {
        return;
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        /* Both ranges are their own virtual key codes, which is why this is a cast and not a
           lookup table. */
        _snprintf(out, out_size, "%c", (char)vk);
    } else if (vk >= VK_F1 && vk <= VK_F12) {
        _snprintf(out, out_size, "F%d", (int)(vk - VK_F1 + 1));
    } else {
        switch (vk) {
        case VK_SPACE:   _snprintf(out, out_size, "Space");  break;
        case VK_TAB:     _snprintf(out, out_size, "Tab");    break;
        case VK_RETURN:  _snprintf(out, out_size, "Enter");  break;
        case VK_ESCAPE:  _snprintf(out, out_size, "Esc");    break;
        case VK_CONTROL: _snprintf(out, out_size, "Ctrl");   break;
        case VK_SHIFT:   _snprintf(out, out_size, "Shift");  break;
        case VK_MENU:    _snprintf(out, out_size, "Alt");    break;
        default:         _snprintf(out, out_size, "%02X", (unsigned)vk); break;
        }
    }
    out[out_size - 1u] = '\0';
}
