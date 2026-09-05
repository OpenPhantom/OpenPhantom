/* sensitivity_row.c: see sensitivity_row.h. */
#include "sensitivity_row.h"

#include "view_range_row.h"

#include "common/ini.h"

#include <stdio.h>

#define INPUT_SECTION   "enhanced_input"
#define SENSITIVITY_KEY "MouseDegreesPerCount"

/* Three, because the band starts at 0.001 and two decimals would collapse its bottom third to
 * zero. The ini carries it the same way. */
#define SENSITIVITY_DECIMALS 3

float sensitivity_row_clamp(float degrees)
{
    /* A NaN compares false against both bounds, so it is caught first and answered with the value
     * a fresh installation runs at, rather than falling through into a file the game reads on
     * every start. */
    if (degrees != degrees) {
        return SENSITIVITY_DEFAULT;
    }
    if (degrees < SENSITIVITY_MIN) {
        return SENSITIVITY_MIN;
    }
    if (degrees > SENSITIVITY_MAX) {
        return SENSITIVITY_MAX;
    }
    return degrees;
}

bool sensitivity_row_parse(const char *text, float *out)
{
    /* The same hand written parser the other rows use, reached rather than repeated: it is the one
     * place that keeps reading a full stop as a decimal point on a machine whose language does not
     * agree, which strtof does not. */
    return view_range_row_parse(text, out);
}

void sensitivity_row_format(float degrees, char *out, size_t size)
{
    if (out == NULL || size == 0u) {
        return;
    }
    (void)_snprintf(out, size - 1u, "%.3f", (double)degrees);
    out[size - 1u] = '\0';
}

float sensitivity_row_get(void)
{
    char  buffer[32];
    float value = SENSITIVITY_DEFAULT;

    if (!ini_read_string(INPUT_SECTION, SENSITIVITY_KEY, "", buffer, sizeof(buffer))) {
        return SENSITIVITY_DEFAULT;
    }
    if (!sensitivity_row_parse(buffer, &value)) {
        return SENSITIVITY_DEFAULT;
    }
    return sensitivity_row_clamp(value);
}

bool sensitivity_row_set(float degrees)
{
    return ini_write_float(INPUT_SECTION, SENSITIVITY_KEY, sensitivity_row_clamp(degrees),
                           SENSITIVITY_DECIMALS);
}
