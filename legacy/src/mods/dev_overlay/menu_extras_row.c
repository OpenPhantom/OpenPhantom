/* menu_extras_row.c: see menu_extras_row.h. */
#include "menu_extras_row.h"

#include "common/ini.h"

#define FOV_SECTION      "variable_fov"
#define FOV_KEY          "MenuSlider"
#define INPUT_SECTION    "enhanced_input"
#define WIDGETS_KEY      "MenuWidgets"

bool menu_extras_row_get(void)
{
    /* Both defaults here are the shipped defaults in the DLLs that read them, and the three have to
     * stay in step: a row that reads OFF while a screen shows the widgets would be worse than no
     * row. */
    return ini_read_bool(FOV_SECTION, FOV_KEY, false) &&
           ini_read_bool(INPUT_SECTION, WIDGETS_KEY, false);
}

bool menu_extras_row_set(bool enabled)
{
    /* Both are attempted even when the first fails, so the two keys cannot be left disagreeing
     * because a write was abandoned halfway. The answer is still false, and the caller leaves the
     * row where it was. */
    bool wrote_fov   = ini_write_int(FOV_SECTION, FOV_KEY, enabled ? 1 : 0);
    bool wrote_boxes = ini_write_int(INPUT_SECTION, WIDGETS_KEY, enabled ? 1 : 0);

    return wrote_fov && wrote_boxes;
}
