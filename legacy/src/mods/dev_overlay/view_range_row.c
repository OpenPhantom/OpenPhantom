/* view_range_row.c: see view_range_row.h. */
#include "view_range_row.h"

#include "common/ini.h"

#include <stdio.h>

#define VIEW_DISTANCE_SECTION "view_distance_fix"
#define VIEW_RANGE_KEY        "ViewRangeScale"

/* Two decimal places, matching how the ini already carries this key and how the chip shows it. */
#define VIEW_RANGE_DECIMALS 2

float view_range_row_clamp(float scale)
{
    /* Written as a positive test so a NaN falls through to the minimum rather than past both
       comparisons and out into the file. */
    if (!(scale > VIEW_RANGE_MIN)) {
        return VIEW_RANGE_MIN;
    }
    if (scale > VIEW_RANGE_MAX) {
        return VIEW_RANGE_MAX;
    }
    return scale;
}

bool view_range_row_parse(const char *text, float *out)
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

void view_range_row_format(float scale, char *out, size_t size)
{
    if (out == NULL || size == 0u) {
        return;
    }
    (void)_snprintf(out, size - 1u, "%.2fx", (double)scale);
    out[size - 1u] = '\0';
}

float view_range_row_get(void)
{
    /* Read as text and parsed here rather than through a float reader, so the ini and the row
       agree about what counts as a number, decimal point included. */
    char buffer[32];
    float value = VIEW_RANGE_MIN;

    if (!ini_read_string(VIEW_DISTANCE_SECTION, VIEW_RANGE_KEY, "", buffer, sizeof(buffer))) {
        return VIEW_RANGE_MIN;
    }
    if (!view_range_row_parse(buffer, &value)) {
        return VIEW_RANGE_MIN;
    }
    return view_range_row_clamp(value);
}

bool view_range_row_set(float scale)
{
    return ini_write_float(VIEW_DISTANCE_SECTION, VIEW_RANGE_KEY, view_range_row_clamp(scale),
                           VIEW_RANGE_DECIMALS);
}
