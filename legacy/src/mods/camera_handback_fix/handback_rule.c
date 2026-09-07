/* handback_rule.c: see handback_rule.h. */
#include "handback_rule.h"

bool handback_rule_owes_camera(bool took_it, int32_t flag, int32_t lock)
{
    /* All three, and the order is the order of certainty rather than of cost. Whether the dialogue
     * took the camera is the only one of the three that cannot be re-read from a cell later, so a
     * mistake there is the one that would touch somebody else's camera. */
    return took_it && flag != 0 && lock == 0;
}
