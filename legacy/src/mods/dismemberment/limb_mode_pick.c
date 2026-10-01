/* limb_mode_pick.c: the host's dismemberment mode or this machine's own. See the header. */
#include "limb_mode_pick.h"

#include "dismemberment.h"

#include <stdbool.h>

bool limb_mode_is_known(int mode)
{
    return mode >= (int)LIMB_MODE_OFF && mode <= (int)LIMB_MODE_ON_DEATH;
}

limb_mode_choice_t limb_mode_pick(bool host_named, float host_value, int ini_mode,
                                  int own_before)
{
    limb_mode_choice_t choice;

    choice.own       = limb_mode_is_known(ini_mode) ? ini_mode : own_before;
    choice.mode      = choice.own;
    choice.from_host = false;
    /* Compared as floats, so a fraction or anything past the three modes is not a mode at all.
     * The multiplayer's codec and the shared record refuse those already; this keeps a record of
     * another build from switching the flight constants to a mode nobody chose. */
    if (host_named && (host_value == (float)LIMB_MODE_OFF ||
                       host_value == (float)LIMB_MODE_NODE_ONLY ||
                       host_value == (float)LIMB_MODE_ON_DEATH)) {
        choice.mode      = (int)host_value;
        choice.from_host = true;
    }
    return choice;
}
