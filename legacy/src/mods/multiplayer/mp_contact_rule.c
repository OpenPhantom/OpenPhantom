/* mp_contact_rule.c: what each delivery path does with a verdict. See the header. */
#include "mp_contact_rule.h"

#include <stdbool.h>
#include <stdint.h>

bool mp_contact_rule_puppet_reports(mp_contact_verdict_t verdict)
{
    return verdict == MP_CONTACT_ALLOWED;
}

bool mp_contact_rule_carries_out(mp_contact_verdict_t verdict)
{
    return verdict != MP_CONTACT_REFUSED;
}

mp_contact_collision_t mp_contact_rule_collision(bool passable, bool alive, bool revived,
                                                 uint32_t marked, uint32_t object)
{
    bool applied = marked != 0u && marked == object;
    bool stale   = marked != 0u && marked != object;

    if (revived) {
        if (!passable) {
            return MP_COLLISION_RESTORE;
        }
        return applied ? MP_COLLISION_KEEP : MP_COLLISION_PASSABLE;
    }
    if (passable && alive) {
        return applied ? MP_COLLISION_KEEP : MP_COLLISION_PASSABLE;
    }
    if (applied) {
        /* Died while passable: the mark waits for the revival or for the end of the scene. */
        if (passable) {
            return MP_COLLISION_KEEP;
        }
        return alive ? MP_COLLISION_RESTORE : MP_COLLISION_LEFT_DEAD;
    }
    return stale ? MP_COLLISION_UNMARK : MP_COLLISION_KEEP;
}
