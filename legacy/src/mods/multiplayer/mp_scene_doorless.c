/* mp_scene_doorless.c: a scene that runs on the host with no door heard, pure. See the header. */
#include "mp_scene_doorless.h"

#include "mp_scene_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

mp_scene_adopt_t mp_scene_adopt_step(uint32_t *seen, const mp_scene_adopt_look_t *look)
{
    bool hero;

    if (seen == NULL || look == NULL) {
        return MP_SCENE_ADOPT_NO;
    }
    /* A scene of the host's own, one given up that the engine may still play, or a host that
     * would not begin one at a door either: nothing is taken over, and the count starts over. */
    hero = look->module_parked && look->driven;
    if (!look->may_begin || look->phase != MP_SCENE_PHASE_NONE || look->given_up ||
        (look->lock_level < MP_SCENE_LOCK_LEVEL && !hero)) {
        *seen = 0u;
        return MP_SCENE_ADOPT_NO;
    }
    if (*seen < MP_SCENE_ADOPT_SUBSTEPS) {
        ++*seen;
    }
    if (*seen < MP_SCENE_ADOPT_SUBSTEPS) {
        return MP_SCENE_ADOPT_WAIT;
    }
    *seen = 0u;
    return hero ? MP_SCENE_ADOPT_HERO : MP_SCENE_ADOPT_LOCK;
}
