/* free_look_row.c: see free_look_row.h. */
#include "free_look_row.h"

#include "common/ini.h"

#define INPUT_SECTION  "enhanced_input"
#define FREE_LOOK_KEY  "FreeLook"
#define CAMERA_FOLLOW_KEY "CameraFollow"

bool free_look_row_get(void)
{
    /* The default here is the shipped default in enhanced_input, and the two have to stay in step:
     * a row that reads OFF while the feature is ON would be worse than no row. */
    return ini_read_bool(INPUT_SECTION, FREE_LOOK_KEY, false);
}

bool free_look_row_set(bool enabled)
{
    if (!ini_write_int(INPUT_SECTION, FREE_LOOK_KEY, enabled ? 1 : 0)) {
        return false;
    }

    /* The passive camera is built on this and cannot outlive it, so it goes off here too rather
     * than being left reading ON with nothing under it. One way only: switching free look ON does
     * not switch the passive camera on. */
    if (!enabled) {
        (void)ini_write_int(INPUT_SECTION, CAMERA_FOLLOW_KEY, 0);
    }
    return true;
}
