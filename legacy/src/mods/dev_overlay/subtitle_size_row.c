/* subtitle_size_row.c: see subtitle_size_row.h. */
#include "subtitle_size_row.h"

#include "view_range_row.h"

#include "common/ini.h"
#include "common/text.h"

#include <stdio.h>

#define RESOLUTION_SECTION "enhanced_resolution"
#define SUBTITLE_SCALE_KEY "SubtitleScale"
#define SUBTITLE_DECIMALS  2

float subtitle_size_row_clamp(float scale)
{
    /* A NaN compares false against both bounds, so it is caught first and answered with the
     * default rather than falling through as though it were in range. */
    if (scale != scale) {
        return SUBTITLE_SIZE_DEFAULT;
    }
    if (scale < SUBTITLE_SIZE_MIN) {
        return SUBTITLE_SIZE_MIN;
    }
    if (scale > SUBTITLE_SIZE_MAX) {
        return SUBTITLE_SIZE_MAX;
    }
    return scale;
}

bool subtitle_size_row_parse(const char *text, float *out)
{
    /* The same hand written parser the other rows use, reached rather than repeated: it is the one
     * place that keeps reading a full stop as a decimal point on a machine whose language does not
     * agree, which strtof does not. */
    return view_range_row_parse(text, out);
}

void subtitle_size_row_format(float scale, char *out, size_t size)
{
    if (out == NULL || size == 0u) {
        return;
    }
    text_format(out, size, "%.2fx", (double)scale);
}

float subtitle_size_row_get(void)
{
    char  buffer[32];
    float value = SUBTITLE_SIZE_DEFAULT;

    if (!ini_read_string(RESOLUTION_SECTION, SUBTITLE_SCALE_KEY, "", buffer, sizeof(buffer))) {
        return SUBTITLE_SIZE_DEFAULT;
    }
    if (!subtitle_size_row_parse(buffer, &value)) {
        return SUBTITLE_SIZE_DEFAULT;
    }
    /* Zero is not shown as zero. It is enhanced_resolution's spelling for "leave the engine's own
     * shrinking size alone", and it sits outside the band this row offers, so there is no honest
     * place to put a handle for it. The row reports the default instead, and a player who wants the
     * engine's own behaviour back sets 0 in the file, where the comment explains it. */
    if (!(value > 0.0f)) {
        return SUBTITLE_SIZE_DEFAULT;
    }
    return subtitle_size_row_clamp(value);
}

bool subtitle_size_row_set(float scale)
{
    return ini_write_float(RESOLUTION_SECTION, SUBTITLE_SCALE_KEY,
                           subtitle_size_row_clamp(scale), SUBTITLE_DECIMALS);
}
