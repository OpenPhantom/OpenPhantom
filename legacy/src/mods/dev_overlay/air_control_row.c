/* air_control_row.c: see air_control_row.h. */
#include "air_control_row.h"

#include "free_look_row.h"
#include "strafe_row.h"

#include "common/ini.h"

#define INPUT_SECTION     "enhanced_input"
#define AIR_CONTROL_KEY   "AirControl"

bool air_control_row_get(void)
{
    /* The default is enhanced_input's own shipped default, and the two have to stay in step: a row
     * reading OFF while the feature is ON would be worse than no row at all. */
    return ini_read_bool(INPUT_SECTION, AIR_CONTROL_KEY, false);
}

bool air_control_row_set(bool enabled)
{
    /* Both keys are written and neither feature is called, exactly as the passive camera's row
     * does it. enhanced_input re-reads these once a second and applies them in its own order with
     * its own refusals; asking free look directly from here cannot work, because it declines while
     * the player phases are stopped and that is the state the game is in while this menu is open. */
    if (enabled && !free_look_row_get() && !free_look_row_set(true)) {
        return false;
    }
    return ini_write_int(INPUT_SECTION, AIR_CONTROL_KEY, enabled ? 1 : 0);
}

bool air_control_row_available(void)
{
    /* Asked of the key the strafe row edits rather than of enhanced_input, so this answers
     * correctly with that DLL absent and answers immediately when that row is flipped. */
    return strafe_row_get();
}
