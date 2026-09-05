/* strafe_row.c: see strafe_row.h. */
#include "strafe_row.h"

#include "common/ini.h"

#define INPUT_SECTION "enhanced_input"
#define STRAFE_KEY    "Strafe"

bool strafe_row_get(void)
{
    /* The default here is the shipped default in enhanced_input, and the two have to stay in step:
     * a row that reads OFF while the feature is ON would be worse than no row. */
    return ini_read_bool(INPUT_SECTION, STRAFE_KEY, false);
}

bool strafe_row_set(bool enabled)
{
    return ini_write_int(INPUT_SECTION, STRAFE_KEY, enabled ? 1 : 0);
}
