/* dev_menu_size_row.c: see dev_menu_size_row.h.
 *
 * The clamp, the parser and the formatter are the same three functions view_range_row.c has, for
 * the same reasons, and the parser in particular is hand written rather than handed to strtof for
 * the reason recorded there: it runs on text somebody is still typing, and strtof asks the locale
 * whether a full stop is a decimal point.
 */
#include "dev_menu_size_row.h"


#include "common/ini.h"

#include <stdio.h>

float dev_menu_size_row_clamp(float scale)
{
    /* Written as a positive test so a NaN falls through to the minimum rather than past both
       comparisons and out into the file. */
    if (!(scale > DEV_MENU_SIZE_MIN)) {
        return DEV_MENU_SIZE_MIN;
    }
    if (scale > DEV_MENU_SIZE_MAX) {
        return DEV_MENU_SIZE_MAX;
    }
    return scale;
}

bool dev_menu_size_row_parse(const char *text, float *out)
{
    float  value = 0.0f;
    float  fraction = 0.1f;
    bool   seen_digit = false;
    bool   seen_point = false;
    size_t index = 0;

    if (text == NULL || out == NULL) {
        return false;
    }

    /* Hand parsed rather than handed to strtof, for one reason worth the lines: this runs on text
       the player is still typing, and strtof's locale decides whether '.' is a decimal point at
       all. A German Windows would parse "2.5" as 2 and leave ".5" as trailing rubbish, which is
       the kind of fault nobody finds until somebody else's machine. */
    for (index = 0; text[index] != '\0'; ++index) {
        char c = text[index];

        if (c >= '0' && c <= '9') {
            seen_digit = true;
            if (seen_point) {
                value += (float)(c - '0') * fraction;
                fraction *= 0.1f;
            } else {
                value = (value * 10.0f) + (float)(c - '0');
            }
            continue;
        }
        if (c == '.' && !seen_point) {
            seen_point = true;
            continue;
        }
        /* The trailing "x" the chip displays, so the value can be typed back exactly as read. */
        if ((c == 'x' || c == 'X') && text[index + 1] == '\0' && seen_digit) {
            break;
        }
        return false;
    }

    if (!seen_digit) {
        return false;
    }
    *out = value;
    return true;
}

void dev_menu_size_row_format(float scale, char *out, size_t size)
{
    if (out == NULL || size == 0u) {
        return;
    }
    (void)_snprintf(out, size - 1u, "%.2fx", (double)scale);
    out[size - 1u] = '\0';
}

#define DEV_MENU_SIZE_SECTION  "dev_overlay"
#define DEV_MENU_SIZE_KEY      "DevMenuSize"
#define DEV_MENU_SIZE_DECIMALS 2

float dev_menu_size_row_get(void)
{
    /* Read as text and parsed here rather than through a float reader, so the ini and the row agree
       about what counts as a number, decimal point included. */
    char  buffer[32];
    float value = 1.0f;

    if (!ini_read_string(DEV_MENU_SIZE_SECTION, DEV_MENU_SIZE_KEY, "", buffer, sizeof(buffer))) {
        return 1.0f;
    }
    if (!dev_menu_size_row_parse(buffer, &value)) {
        return 1.0f;                 /* an absent or unreadable key means the authored size */
    }
    return dev_menu_size_row_clamp(value);
}

/* The value the panel is being drawn at right now. Negative means it has not been read yet.
 *
 * This is the one owner of the number, and the drawing layer asks for it rather than being told.
 * That direction matters: it lets this file be linked into a test on its own, with no engine and no
 * renderer behind it, which is what makes the parser and the clamp testable at all. */
static float applied = -1.0f;

float dev_menu_size_row_current(void)
{
    if (!(applied > 0.0f)) {
        applied = dev_menu_size_row_get();      /* first ask of the session, take it from the ini */
    }
    return applied;
}

bool dev_menu_size_row_set(float scale)
{
    const float wanted = dev_menu_size_row_clamp(scale);

    /* Applied before it is written, and applied whether or not the write succeeds. A read only ini
       is a reason to lose the setting next launch, not a reason to refuse to resize the panel the
       player is looking at right now. */
    applied = wanted;
    return ini_write_float(DEV_MENU_SIZE_SECTION, DEV_MENU_SIZE_KEY, wanted, DEV_MENU_SIZE_DECIMALS);
}
