/* fog_band_row.c: see fog_band_row.h. */
#include "fog_band_row.h"

#include "view_range_row.h"

#include "common/ini.h"

#include <stdio.h>

#define VIEW_DISTANCE_SECTION "view_distance_fix"
#define FOG_BAND_KEY          "FogBandScale"

/* Two decimal places, matching how the ini carries this key and how the chip shows it. */
#define FOG_BAND_DECIMALS 2

float fog_band_row_clamp(float scale)
{
    /* A NaN compares false against everything, so it would fall past both bounds below and out
       into a file the game reads on every start. Caught first, and answered with the default,
       because a value nobody can read should leave the fog alone rather than change it. A number
       that is merely out of range is a different case: it says which direction the player wanted,
       so it clamps to that end. */
    if (scale != scale) {
        return FOG_BAND_DEFAULT;
    }
    if (scale < FOG_BAND_MIN) {
        return FOG_BAND_MIN;
    }
    if (scale > FOG_BAND_MAX) {
        return FOG_BAND_MAX;
    }
    return scale;
}

bool fog_band_row_parse(const char *text, float *out)
{
    /* The same parser the draw distance row uses, reached rather than repeated: both accept a bare
       decimal number with an optional trailing "x", and both have to keep working on a German
       Windows, where strtof would read "0.8" as 0 and leave ".8" behind. One copy of that is
       enough, and this row's own range is enforced by the clamp above rather than by the parse. */
    return view_range_row_parse(text, out);
}

void fog_band_row_format(float scale, char *out, size_t size)
{
    if (out == NULL || size == 0u) {
        return;
    }
    (void)_snprintf(out, size - 1u, "%.2fx", (double)scale);
    out[size - 1u] = '\0';
}

float fog_band_row_get(void)
{
    /* Read as text and parsed here rather than through a float reader, so the ini and the row
       agree about what counts as a number, decimal point included. */
    char  buffer[32];
    float value = FOG_BAND_DEFAULT;

    if (!ini_read_string(VIEW_DISTANCE_SECTION, FOG_BAND_KEY, "", buffer, sizeof(buffer))) {
        return FOG_BAND_DEFAULT;
    }
    if (!fog_band_row_parse(buffer, &value)) {
        return FOG_BAND_DEFAULT;
    }
    return fog_band_row_clamp(value);
}

bool fog_band_row_set(float scale)
{
    return ini_write_float(VIEW_DISTANCE_SECTION, FOG_BAND_KEY, fog_band_row_clamp(scale),
                           FOG_BAND_DECIMALS);
}
