/* mp_warp_follow_rule.c: when a client's player follows a warp of the host's. See the header. */
#include "mp_warp_follow_rule.h"

#include "common/player_help_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool mp_warp_follow_landed(bool landed_before, bool host_known, float host_off)
{
    if (landed_before) {
        return true;
    }
    return host_known && isfinite(host_off) && host_off < MP_WARP_FOLLOW_LANDED;
}

bool mp_warp_follow_passes(uint8_t reason)
{
    switch (reason) {
    case PLAYER_HELP_REASON_DEAD:
    case PLAYER_HELP_REASON_AT_A_GUN:
    case PLAYER_HELP_REASON_BUSY:
    case PLAYER_HELP_REASON_HOST_HAS_NO_BODY:
    case PLAYER_HELP_REASON_HOST_DEAD:
    case PLAYER_HELP_REASON_OVERLAY_HOLDS:
        return true;
    default:
        return false;
    }
}

mp_warp_follow_step_t mp_warp_follow_step(const mp_warp_follow_look_t *look)
{
    bool beside;

    if (look == NULL) {
        return MP_WARP_FOLLOW_GIVE_UP;
    }
    beside = look->door.outcome == (uint8_t)PLAYER_HELP_OUTCOME_NOTHING &&
             look->door.reason == (uint8_t)PLAYER_HELP_REASON_NEAR_ALREADY;
    /* Nothing to do for a reason other than standing beside him is the host's own machine, which
     * hears no entry; it is given up like a refusal that does not pass. */
    if (look->door.outcome == (uint8_t)PLAYER_HELP_OUTCOME_NOTHING && !beside) {
        return MP_WARP_FOLLOW_GIVE_UP;
    }
    if (look->door.outcome == (uint8_t)PLAYER_HELP_OUTCOME_REFUSED &&
        !mp_warp_follow_passes(look->door.reason)) {
        return MP_WARP_FOLLOW_GIVE_UP;
    }
    if (look->waited >= MP_WARP_FOLLOW_SUBSTEPS) {
        return MP_WARP_FOLLOW_GIVE_UP;
    }
    if (!look->landed && look->waited < MP_WARP_FOLLOW_LANDING_SUBSTEPS) {
        return MP_WARP_FOLLOW_WAIT;
    }
    if (look->door.outcome == (uint8_t)PLAYER_HELP_OUTCOME_OPEN) {
        return MP_WARP_FOLLOW_PRESS;
    }
    return beside ? MP_WARP_FOLLOW_BESIDE : MP_WARP_FOLLOW_WAIT;
}

bool mp_warp_follow_tries_again(mp_player_help_verdict_t ended)
{
    if (ended.outcome == (uint8_t)PLAYER_HELP_OUTCOME_DONE) {
        return false;
    }
    return ended.reason != (uint8_t)PLAYER_HELP_REASON_WORLD_CHANGED &&
           ended.reason != (uint8_t)PLAYER_HELP_REASON_NO_LEVEL;
}
