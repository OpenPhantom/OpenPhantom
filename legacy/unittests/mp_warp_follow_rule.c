/* mp_warp_follow_rule.c: when a client's player follows a warp of the host's.
 *
 * The rule is driven the way a session drives it: the entry arrives while the host still stands
 * beside this player, a fade before he lands, so the door of the teleport says "beside him
 * already" on the very looks on which a careless rule would call the warp done.
 */
#include "unittest.h"

#include "mp_player_help_rule.h"
#include "mp_warp_follow_rule.h"

#include "common/player_help_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static mp_player_help_verdict_t door(uint8_t outcome, uint8_t reason)
{
    mp_player_help_verdict_t v;

    v.outcome = outcome;
    v.reason  = reason;
    return v;
}

static mp_warp_follow_step_t step(uint32_t waited, bool landed, uint8_t outcome, uint8_t reason)
{
    mp_warp_follow_look_t look;

    look.waited = waited;
    look.landed = landed;
    look.door   = door(outcome, reason);
    return mp_warp_follow_step(&look);
}

static void check_the_landing(void)
{
    ut_section("the host has landed once his pose reads near the warp's target");
    ut_check(!mp_warp_follow_landed(false, false, 0.0f),
             "no pose of the host is no landing, whatever distance came with it");
    ut_check(!mp_warp_follow_landed(false, true, MP_WARP_FOLLOW_LANDED) &&
                 mp_warp_follow_landed(false, true, MP_WARP_FOLLOW_LANDED - 0.01f),
             "nearer than the seat search's outer ring is landed, the ring itself is not");
    ut_check(mp_warp_follow_landed(true, false, 0.0f) && mp_warp_follow_landed(true, true, 80.0f),
             "a host who landed and walked on, or whose pose is lost, stays landed");
    ut_check(!mp_warp_follow_landed(false, true, (float)NAN) &&
                 !mp_warp_follow_landed(false, true, (float)INFINITY),
             "a distance that is no finite number is not near");
}

static void check_the_wait_for_the_host(void)
{
    ut_section("the press waits for the host to stand at the target");
    ut_check(step(0u, false, PLAYER_HELP_OUTCOME_NOTHING, PLAYER_HELP_REASON_NEAR_ALREADY) ==
                 MP_WARP_FOLLOW_WAIT,
             "the entry arrives with the host still beside this player: not done, waited for");
    ut_check(step(40u, false, PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE) ==
                 MP_WARP_FOLLOW_WAIT,
             "an open door is not pressed either while he has not been seen at the target");
    ut_check(step(40u, true, PLAYER_HELP_OUTCOME_OPEN, PLAYER_HELP_REASON_NONE) ==
                 MP_WARP_FOLLOW_PRESS,
             "seen there, the open door is pressed");
    ut_check(step(40u, true, PLAYER_HELP_OUTCOME_NOTHING, PLAYER_HELP_REASON_NEAR_ALREADY) ==
                 MP_WARP_FOLLOW_BESIDE,
             "and a player the warp left beside him has nothing to follow");

    ut_section("a host never seen at the target is taken as landed after three seconds");
    ut_check(step(MP_WARP_FOLLOW_LANDING_SUBSTEPS - 1u, false, PLAYER_HELP_OUTCOME_OPEN,
                  PLAYER_HELP_REASON_NONE) == MP_WARP_FOLLOW_WAIT,
             "one substep before the bound the press still waits");
    ut_check(step(MP_WARP_FOLLOW_LANDING_SUBSTEPS, false, PLAYER_HELP_OUTCOME_OPEN,
                  PLAYER_HELP_REASON_NONE) == MP_WARP_FOLLOW_PRESS,
             "at the bound the door decides: open, so the player goes to where the host is");
    ut_check(step(MP_WARP_FOLLOW_LANDING_SUBSTEPS, false, PLAYER_HELP_OUTCOME_NOTHING,
                  PLAYER_HELP_REASON_NEAR_ALREADY) == MP_WARP_FOLLOW_BESIDE,
             "or beside him, when the engine dropped the warp and he never left");
}

static void check_the_door(void)
{
    static const uint8_t passes[] = {
        PLAYER_HELP_REASON_DEAD,          PLAYER_HELP_REASON_AT_A_GUN,
        PLAYER_HELP_REASON_BUSY,          PLAYER_HELP_REASON_HOST_HAS_NO_BODY,
        PLAYER_HELP_REASON_HOST_DEAD,     PLAYER_HELP_REASON_OVERLAY_HOLDS,
    };
    static const uint8_t ends[] = {
        PLAYER_HELP_REASON_NO_LEVEL,      PLAYER_HELP_REASON_HOST_ELSEWHERE,
        PLAYER_HELP_REASON_NOT_BOUND,     PLAYER_HELP_REASON_NO_SEAT,
        PLAYER_HELP_REASON_GAVE_UP,       PLAYER_HELP_REASON_WORLD_CHANGED,
        PLAYER_HELP_REASON_MENU_OPEN,     PLAYER_HELP_REASON_CONVERSATION,
        PLAYER_HELP_REASON_NONE,
    };
    unsigned waited_out = 0u;
    unsigned given_up   = 0u;
    unsigned early      = 0u;
    size_t   i;

    ut_section("a door shut for a reason that passes is waited out");
    for (i = 0u; i < sizeof passes / sizeof passes[0]; ++i) {
        waited_out += mp_warp_follow_passes(passes[i]) &&
                              step(200u, true, PLAYER_HELP_OUTCOME_REFUSED, passes[i]) ==
                                  MP_WARP_FOLLOW_WAIT
                          ? 1u : 0u;
    }
    ut_checkf(waited_out == sizeof passes / sizeof passes[0],
              "dead, at a gun, being moved, the host with no body or dead, held by the developer "
              "menu (%u of 6)", waited_out);

    ut_section("a door shut for any other reason ends the wait at once");
    for (i = 0u; i < sizeof ends / sizeof ends[0]; ++i) {
        given_up += !mp_warp_follow_passes(ends[i]) &&
                            step(200u, true, PLAYER_HELP_OUTCOME_REFUSED, ends[i]) ==
                                MP_WARP_FOLLOW_GIVE_UP
                        ? 1u : 0u;
        early += step(0u, false, PLAYER_HELP_OUTCOME_REFUSED, ends[i]) == MP_WARP_FOLLOW_GIVE_UP
                     ? 1u : 0u;
    }
    ut_checkf(given_up == sizeof ends / sizeof ends[0],
              "no level, the host in another one, nothing bound and every reason the door never "
              "gives (%u of 9)", given_up);
    ut_checkf(early == sizeof ends / sizeof ends[0],
              "before the host has landed as well: there is nothing to wait for (%u of 9)", early);
    ut_check(step(0u, false, PLAYER_HELP_OUTCOME_NOTHING, PLAYER_HELP_REASON_IS_HOST) ==
                 MP_WARP_FOLLOW_GIVE_UP,
             "and the host's own machine, which has nowhere to follow to");
    ut_check(mp_warp_follow_step(NULL) == MP_WARP_FOLLOW_GIVE_UP, "no look is no follow");
}

static void check_the_bound(void)
{
    ut_section("the whole wait is twenty seconds from the entry");
    ut_check(step(MP_WARP_FOLLOW_SUBSTEPS - 1u, true, PLAYER_HELP_OUTCOME_REFUSED,
                  PLAYER_HELP_REASON_DEAD) == MP_WARP_FOLLOW_WAIT,
             "a dead player is waited for on the last substep of it");
    ut_check(step(MP_WARP_FOLLOW_SUBSTEPS, true, PLAYER_HELP_OUTCOME_REFUSED,
                  PLAYER_HELP_REASON_DEAD) == MP_WARP_FOLLOW_GIVE_UP,
             "and given up on the next");
    ut_check(step(MP_WARP_FOLLOW_SUBSTEPS, true, PLAYER_HELP_OUTCOME_OPEN,
                  PLAYER_HELP_REASON_NONE) == MP_WARP_FOLLOW_GIVE_UP,
             "an open door past the bound is not pressed: the warp is old news by then");
    ut_check(MP_WARP_FOLLOW_LANDING_SUBSTEPS < MP_WARP_FOLLOW_SUBSTEPS,
             "the host is taken as landed well inside the bound");
}

static void check_the_end_of_a_teleport(void)
{
    ut_section("a teleport that ends without a landing is waited for again");
    ut_check(!mp_warp_follow_tries_again(door(PLAYER_HELP_OUTCOME_DONE, PLAYER_HELP_REASON_NONE)),
             "a landing is the end of the follow");
    ut_check(mp_warp_follow_tries_again(door(PLAYER_HELP_OUTCOME_REFUSED,
                                             PLAYER_HELP_REASON_NO_SEAT)) &&
                 mp_warp_follow_tries_again(door(PLAYER_HELP_OUTCOME_REFUSED,
                                                 PLAYER_HELP_REASON_GAVE_UP)) &&
                 mp_warp_follow_tries_again(door(PLAYER_HELP_OUTCOME_REFUSED,
                                                 PLAYER_HELP_REASON_HOST_DEAD)),
             "no free place beside the host, a body that would not move, a host who died "
             "meanwhile: tried again inside the bound");
    ut_check(!mp_warp_follow_tries_again(door(PLAYER_HELP_OUTCOME_REFUSED,
                                              PLAYER_HELP_REASON_WORLD_CHANGED)) &&
                 !mp_warp_follow_tries_again(door(PLAYER_HELP_OUTCOME_REFUSED,
                                                  PLAYER_HELP_REASON_NO_LEVEL)),
             "a level that changed or ended under it is not");
}

int main(void)
{
    check_the_landing();
    check_the_wait_for_the_host();
    check_the_door();
    check_the_bound();
    check_the_end_of_a_teleport();

    return ut_summary("mp_warp_follow_rule");
}
