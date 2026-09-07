/* free_look_row.c: see free_look_row.h. */
#include "free_look_row.h"

#include "common/ini.h"

#define INPUT_SECTION  "enhanced_input"
#define FREE_LOOK_KEY  "FreeLook"
#define CAMERA_FOLLOW_KEY "CameraFollow"
#define AIR_CONTROL_KEY   "AirControl"

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

    /* Both of the features built on this go off with it, rather than being left reading ON with
     * nothing under them. One way only: switching free look ON switches neither of them on.
     *
     * The air steer was missing here and that is what was reported. enhanced_input turns it off
     * correctly when it sees free look go, but that is a poll a second later, so until it ran the
     * row read ON while doing nothing, and with that DLL absent it would have read ON forever.
     * Writing it here means the row tells the truth the instant free look is switched off, and it
     * means one click brings the air steer back rather than two. */
    if (!enabled) {
        (void)ini_write_int(INPUT_SECTION, CAMERA_FOLLOW_KEY, 0);
        (void)ini_write_int(INPUT_SECTION, AIR_CONTROL_KEY, 0);
    }
    return true;
}
