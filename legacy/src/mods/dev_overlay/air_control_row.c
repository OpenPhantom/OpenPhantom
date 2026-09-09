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
    /* Either scheme, because either one is enough to take the engine's own jump steering away.
     *
     * The engine steers a jump on its own, and that is the fact this row exists around. Both the
     * Jump and Fall descriptors carry the ordinary steer phase, so in the shipped game the turn
     * input turns the body while the player is off the ground, and always did. What removes it is
     * free look: outside Stand our own steering handles the substep and the engine's turn no
     * longer reaches the body, because the mouse is the camera there and a turn rate left standing
     * would move the heading underneath it. So this row is not an addition to the game, it is the
     * thing that gives back what our control scheme took.
     *
     * That is why the gate is either row rather than the sideways walk alone. Free look on with the
     * sideways walk off used to be the one configuration with no jump steering at all: the engine's
     * was suppressed and this row was greyed out. It still steers there, with fewer directions,
     * because the angle is built from the sideways AND forward input and a lone forward key is a
     * turn toward the camera rather than nothing.
     *
     * Asked of the keys the two rows edit rather than of enhanced_input, so this answers correctly
     * with that DLL absent and answers immediately when either row is flipped. */
    return strafe_row_get() || free_look_row_get();
}
