/* fov_row.c: see fov_row.h. */
#include "fov_row.h"

#include "view_range_row.h"

#include "common/ini.h"

#include <stdio.h>
#include <string.h>

#define FOV_SECTION       "variable_fov"
#define BASE_KEY          "BaseFov"
#define EXTRA_KEY         "ExtraDegrees"
#define SLIDER_MIN_KEY    "SliderMinFovDegrees"
#define SLIDER_MAX_KEY    "SliderMaxFovDegrees"

/* One decimal, matching how variable_fov writes both of the keys this reads. */
#define FOV_DECIMALS 1

/* Nothing has been published while this is what EffectiveFov reads back as. A field of view of
 * zero is not a value the engine can be in, so it doubles as "absent" without a second key. */
#define FOV_NONE 0.0f

float fov_row_min(void)
{
    return (float)ini_read_int(FOV_SECTION, SLIDER_MIN_KEY, (int)FOV_ROW_MIN_DEFAULT);
}

float fov_row_max(void)
{
    return (float)ini_read_int(FOV_SECTION, SLIDER_MAX_KEY, (int)FOV_ROW_MAX_DEFAULT);
}

float fov_row_clamp(float degrees)
{
    float low  = fov_row_min();
    float high = fov_row_max();

    /* The ends can be edited in the file, so they are not assumed to be the right way round. */
    if (low > high) {
        float swap = low;

        low  = high;
        high = swap;
    }
    /* A NaN compares false against both bounds, so it is caught first rather than falling out into
       a file the game reads on every start. */
    if (degrees != degrees || degrees < low) {
        return low;
    }
    if (degrees > high) {
        return high;
    }
    return degrees;
}

bool fov_row_parse(const char *text, float *out)
{
    char  copy[32];
    char *tail;
    size_t length;

    if (text == NULL || out == NULL) {
        return false;
    }
    length = strlen(text);
    if (length == 0u || length >= sizeof(copy)) {
        return false;
    }
    memcpy(copy, text, length + 1u);

    /* "deg" is stripped so the chip can be typed back in exactly as it is shown. The draw distance
       row's parser does the same job for its own "x", and it is reached rather than repeated
       because it is also the one place that keeps working on a German Windows, where strtof reads
       "97.5" as 97 and leaves ".5" behind. */
    tail = copy + length;
    while (tail > copy && (tail[-1] == ' ' || tail[-1] == 'd' || tail[-1] == 'e' ||
                           tail[-1] == 'g' || tail[-1] == 'D' || tail[-1] == 'E' ||
                           tail[-1] == 'G')) {
        --tail;
    }
    *tail = '\0';

    /* The shared parser accepts a trailing "x", because that is the draw distance row's own
     * unit. Here it must not: "90x" is a number somebody typed for a different row, and taking
     * it would quietly set the field of view from it. A valid number ends in a digit once its
     * own unit has been stripped, so that is the whole test, and it runs before delegating so
     * that nothing has been written through `out` when it fails. */
    if (tail == copy || tail[-1] < '0' || tail[-1] > '9') {
        return false;
    }
    return view_range_row_parse(copy, out);
}

void fov_row_format(float degrees, char *out, size_t size)
{
    if (out == NULL || size == 0u) {
        return;
    }
    (void)_snprintf(out, size - 1u, "%.0f deg", (double)degrees);
    out[size - 1u] = '\0';
}

bool fov_row_get(float *degrees)
{
    float base = ini_read_float(FOV_SECTION, BASE_KEY, FOV_NONE);

    if (degrees == NULL || base <= FOV_NONE) {
        return false;
    }
    /* The width of the picture is the base plus the offset, and both come out of the file. The
     * offset is what a drag writes, so this reflects a drag on the very next read rather than
     * waiting for the game to publish anything back. */
    *degrees = base + ini_read_float(FOV_SECTION, EXTRA_KEY, 0.0f);
    return true;
}

bool fov_row_set(float degrees)
{
    float base = ini_read_float(FOV_SECTION, BASE_KEY, FOV_NONE);

    if (base <= FOV_NONE) {
        return false;                          /* no base to measure an offset against */
    }
    return ini_write_float(FOV_SECTION, EXTRA_KEY, fov_row_clamp(degrees) - base, FOV_DECIMALS);
}
