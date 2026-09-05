/* overlay_key_name.c: see overlay_key_name.h. */
#include "overlay_key_name.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

void overlay_key_name(int32_t vk, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0u) {
        return;
    }
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        /* Both ranges are their own virtual key codes, which is why this is a cast and not a
           lookup table. */
        _snprintf(out, out_size, "%c", (char)vk);
    } else if (vk >= VK_F1 && vk <= VK_F24) {
        _snprintf(out, out_size, "F%d", (int)(vk - VK_F1 + 1));
    } else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        _snprintf(out, out_size, "Num%d", (int)(vk - VK_NUMPAD0));
    } else {
        switch (vk) {
        case VK_SPACE:    _snprintf(out, out_size, "Space");  break;
        case VK_TAB:      _snprintf(out, out_size, "Tab");    break;
        case VK_RETURN:   _snprintf(out, out_size, "Enter");  break;
        case VK_ESCAPE:   _snprintf(out, out_size, "Esc");    break;
        case VK_CONTROL:  _snprintf(out, out_size, "Ctrl");   break;
        case VK_SHIFT:    _snprintf(out, out_size, "Shift");  break;
        case VK_MENU:     _snprintf(out, out_size, "Alt");    break;
        case VK_MULTIPLY: _snprintf(out, out_size, "Num*");   break;
        case VK_ADD:      _snprintf(out, out_size, "Num+");   break;
        case VK_SUBTRACT: _snprintf(out, out_size, "Num-");   break;
        case VK_DIVIDE:   _snprintf(out, out_size, "Num/");   break;
        case VK_DECIMAL:  _snprintf(out, out_size, "Num.");   break;
        case VK_INSERT:   _snprintf(out, out_size, "Insert"); break;
        case VK_DELETE:   _snprintf(out, out_size, "Delete"); break;
        case VK_HOME:     _snprintf(out, out_size, "Home");   break;
        case VK_END:      _snprintf(out, out_size, "End");    break;
        case VK_OEM_3:    _snprintf(out, out_size, "Backtick"); break;
        /* Decimal, not hex, because this is the form the reading direction accepts. A code
           with no name here is shown so it can be typed straight back into OpenKey, and a hex
           string would be refused by the parser and fall back to the default key. */
        default:          _snprintf(out, out_size, "%u", (unsigned)vk); break;
        }
    }
    out[out_size - 1u] = '\0';
}

/* ==============================================================================================
 * The other direction, for a settings file a person types into
 * ============================================================================================ */
/* Names are compared with the spaces, underscores and case taken out, so "numpad +", "Numpad+" and
 * "NUM_PLUS" are one answer. The table is the names this accepts, not every name a keyboard has:
 * anything missing can still be given as its number. */
typedef struct key_alias {
    const char *name;
    int32_t     vk;
} key_alias_t;

static const key_alias_t ALIASES[] = {
    { "default",     0 },            /* F6 or the key below Escape, which is the shipped answer */
    { "belowescape", 0 },
    { "backtick",    VK_OEM_3 },     /* the key below Escape on a British or American layout */
    { "grave",       VK_OEM_3 },
    { "tilde",       VK_OEM_3 },
    { "caret",       0xDC },         /* and on a German one, where it is OEM_5 */
    { "circumflex",  0xDC },
    { "ctrl",        VK_CONTROL },   /* named in the other direction, so nameable in this one */
    { "control",     VK_CONTROL },
    { "shift",       VK_SHIFT },
    { "alt",         VK_MENU },
    { "space",       VK_SPACE },
    { "tab",         VK_TAB },
    { "enter",       VK_RETURN },
    { "return",      VK_RETURN },
    { "escape",      VK_ESCAPE },
    { "esc",         VK_ESCAPE },
    { "backspace",   VK_BACK },
    { "insert",      VK_INSERT },
    { "delete",      VK_DELETE },
    { "home",        VK_HOME },
    { "end",         VK_END },
    { "pageup",      VK_PRIOR },
    { "pagedown",    VK_NEXT },
    { "up",          VK_UP },
    { "down",        VK_DOWN },
    { "left",        VK_LEFT },
    { "right",       VK_RIGHT },
    { "capslock",    VK_CAPITAL },
    { "scrolllock",  VK_SCROLL },
    { "pause",       VK_PAUSE },
    { "numpad+",     VK_ADD },
    { "numpadplus",  VK_ADD },
    { "numplus",     VK_ADD },      /* the underscore form above collapses to this, not to numpad+ */
    { "num+",        VK_ADD },
    { "numpad-",     VK_SUBTRACT },
    { "numpadminus", VK_SUBTRACT },
    { "numminus",    VK_SUBTRACT },
    { "num-",        VK_SUBTRACT },
    { "numpad*",     VK_MULTIPLY },
    { "numpadstar",  VK_MULTIPLY },
    { "numstar",     VK_MULTIPLY },
    { "num*",        VK_MULTIPLY },
    { "numpad/",     VK_DIVIDE },
    { "numpadslash", VK_DIVIDE },
    { "numslash",    VK_DIVIDE },
    { "num/",        VK_DIVIDE },
    { "numpad.",     VK_DECIMAL },
    { "num.",        VK_DECIMAL },
    { "semicolon",   VK_OEM_1 },
    { "apostrophe",  VK_OEM_7 },
    { "comma",       VK_OEM_COMMA },
    { "period",      VK_OEM_PERIOD },
    { "slash",       VK_OEM_2 },
    { "backslash",   VK_OEM_5 },
    { "minus",       VK_OEM_MINUS },
    { "equals",      VK_OEM_PLUS },
    { "leftbracket", VK_OEM_4 },
    { "rightbracket", VK_OEM_6 }
};

#define ALIAS_COUNT (sizeof ALIASES / sizeof ALIASES[0])

/* Lower case, with spaces and underscores dropped. Returns false if it does not fit, which for a
 * key name means the text was never one. */
static bool normalise(const char *text, char *out, size_t out_size)
{
    size_t n = 0;

    if (text == NULL || out == NULL || out_size == 0u) {
        return false;
    }
    for (; *text != '\0'; ++text) {
        char c = *text;

        if (c == ' ' || c == '\t' || c == '_') {
            continue;
        }
        if (c >= 'A' && c <= 'Z') {
            c = (char)(c - 'A' + 'a');
        }
        if (n + 1u >= out_size) {
            return false;
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return n > 0u;
}

bool overlay_key_from_name(const char *text, int32_t *out)
{
    char   name[32];
    size_t i;
    int32_t value = 0;

    if (out == NULL || !normalise(text, name, sizeof name)) {
        return false;
    }

    /* A bare number is a virtual key code, which is what this setting has always been and what
     * every ini written before this understood. It is also the only way to reach a key with no
     * name here, including the number row, whose keys are their own codes: 53 is the 5 key.
     *
     * THE NUMBER ROW THEREFORE DOES NOT READ BACK from what the panel shows. The panel prints the
     * 5 key as "5" and this reads "5" as code 5, and the two cannot be reconciled: 0 has to keep
     * meaning unset, 8 and 9 are Backspace and Tab in every ini written before names existed, and
     * changing any of that would move somebody's working hotkey. Naming the ten keys some other
     * way on screen would fix the round trip and cost every reader the obvious label, so the
     * ambiguity stays and is written down here instead. */
    if (name[0] >= '0' && name[0] <= '9') {
        for (i = 0; name[i] != '\0'; ++i) {
            if (name[i] < '0' || name[i] > '9') {
                return false;
            }
            value = (value * 10) + (name[i] - '0');
            if (value > 255) {
                return false;
            }
        }
        *out = value;
        return true;
    }

    /* A single letter is its own code, the same as the panel prints it. */
    if (name[0] >= 'a' && name[0] <= 'z' && name[1] == '\0') {
        *out = (int32_t)(name[0] - 'a' + 'A');
        return true;
    }

    /* F1 to F24, which is the range Windows defines and more than any keyboard has. */
    if (name[0] == 'f' && name[1] >= '1' && name[1] <= '9') {
        int32_t number = name[1] - '0';

        if (name[2] >= '0' && name[2] <= '9' && name[3] == '\0') {
            number = (number * 10) + (name[2] - '0');
        } else if (name[2] != '\0') {
            number = 0;
        }
        if (number >= 1 && number <= 24) {
            *out = VK_F1 + number - 1;
            return true;
        }
        return false;
    }

    /* Numpad digits, before the table, so numpad0 does not have to be ten more rows in it. */
    if ((strncmp(name, "numpad", 6) == 0 && name[6] >= '0' && name[6] <= '9' && name[7] == '\0') ||
        (strncmp(name, "num", 3) == 0 && name[3] >= '0' && name[3] <= '9' && name[4] == '\0')) {
        char digit = (name[3] >= '0' && name[3] <= '9') ? name[3] : name[6];

        *out = VK_NUMPAD0 + (digit - '0');
        return true;
    }

    for (i = 0; i < ALIAS_COUNT; ++i) {
        if (strcmp(name, ALIASES[i].name) == 0) {
            *out = ALIASES[i].vk;
            return true;
        }
    }
    return false;
}
