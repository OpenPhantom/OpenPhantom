/* Putting a dead player back into a level that is still running.
 *
 * Four decisions in this module are arithmetic with a known right answer and run here with no
 * game in the process: what one tick does with one wish, which of the engine's gates is shut,
 * whether a pose that arrived from another machine is a pose at all, and whether a body is a
 * corpse. What the ground under an anchor means and where the candidates are is the seat's, and
 * its own tests pin it.
 *
 * The engine-side half is checked for refusing. Nothing resolves in a test process, and the two
 * entry points have to answer that rather than call into an address that is not there.
 */
#include "unittest.h"

#include "mp_respawn.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A delay a deathmatch might configure: five seconds at the pump's own rate. */
#define A_DELAY 300u

static void check_the_tick_of_one_wish(void)
{
    ut_section("nothing wanted, nothing done");
    ut_check(mp_respawn_step(false, true, 0u, 0u) == MP_RESPAWN_STEP_IDLE,
             "an idle module answers idle even with both gates open");
    ut_check(mp_respawn_step(false, false, MP_RESPAWN_DEADLINE_FRAMES + 1u, 0u) ==
                 MP_RESPAWN_STEP_IDLE,
             "and a deadline that passed while nothing was wanted drops nothing");

    ut_section("the delay the caller asked for is a rule of the round");
    ut_check(mp_respawn_step(true, true, 0u, A_DELAY) == MP_RESPAWN_STEP_HOLD,
             "an open gate does not shorten a configured re-entry time");
    ut_check(mp_respawn_step(true, true, A_DELAY - 1u, A_DELAY) == MP_RESPAWN_STEP_HOLD,
             "not on the frame before it either");
    ut_check(mp_respawn_step(true, true, A_DELAY, A_DELAY) == MP_RESPAWN_STEP_RUN,
             "and on the frame it runs out, the re-entry happens");
    ut_check(mp_respawn_step(true, false, A_DELAY, A_DELAY) == MP_RESPAWN_STEP_WAIT,
             "unless the engine is not ready, in which case it waits");

    ut_section("cooperative play asks for no delay at all");
    ut_check(mp_respawn_step(true, true, 0u, 0u) == MP_RESPAWN_STEP_RUN,
             "so the very first tick after the request carries it out");

    ut_section("an open gate beats an expired deadline");
    ut_check(mp_respawn_step(true, true, MP_RESPAWN_DEADLINE_FRAMES, 0u) == MP_RESPAWN_STEP_RUN,
             "a wish that becomes possible on the frame it runs out is carried out");
    ut_check(mp_respawn_step(true, false, MP_RESPAWN_DEADLINE_FRAMES, 0u) == MP_RESPAWN_STEP_DROP,
             "and only a closed gate on that frame throws it away");
    ut_check(mp_respawn_step(true, false, MP_RESPAWN_DEADLINE_FRAMES - 1u, 0u) ==
                 MP_RESPAWN_STEP_WAIT,
             "one frame earlier it is still waiting");
}

static void check_a_pose_off_the_wire(void)
{
    static const float GOOD[3] = { 1.0f, -2.0f, 3.5f };
    float              bad[3] = { 1.0f, 2.0f, 3.0f };

    ut_section("a pose another machine computed is not this machine's arithmetic");
    ut_check(mp_respawn_pose_is_usable(GOOD, 90.0f), "a finite point and heading is usable");
    ut_check(!mp_respawn_pose_is_usable(GOOD, (float)NAN), "a heading that is not a number is not");
    ut_check(!mp_respawn_pose_is_usable(GOOD, (float)INFINITY), "nor an infinite one");
    ut_check(!mp_respawn_pose_is_usable(NULL, 0.0f), "and no point at all is not either");

    bad[1] = (float)INFINITY;
    ut_check(!mp_respawn_pose_is_usable(bad, 0.0f),
             "one bad axis is enough, because the re-entry stores what it is handed");
    bad[1] = (float)NAN;
    ut_check(!mp_respawn_pose_is_usable(bad, 0.0f), "whichever way it is bad");
}

static void check_the_engine_side_refuses_without_a_game(void)
{
    static const float SOMEWHERE[3] = { 10.0f, 20.0f, 30.0f };

    ut_section("no sites, no cells: every entry point has to say so rather than call a null");
    ut_check(!mp_respawn_install(),
             "the install declines because the re-entry and the health writer did not resolve");
    ut_check(!mp_respawn_installed(), "so the module stays off");
    ut_check(!mp_respawn_at(SOMEWHERE, 0.0f, 1u, 0u), "a request at a point is refused");
    ut_check(!mp_respawn_beside(SOMEWHERE, 1u, 0u), "and so is one beside the players");
    ut_check(!mp_respawn_beside(NULL, 1u, 0u), "with the place of the death unknown as well");
    ut_check(!mp_respawn_pending(), "nothing is held, so nothing can be carried out later");

    mp_respawn_tick(100u);
    mp_respawn_cancel();
    mp_respawn_set_landed_listener(NULL);
    mp_respawn_report();
    ut_check(true, "the tick, the cancel and the report run with nothing behind them");
}

/* Which gate is shut, as a word rather than as three conditions in one sentence.
 *
 * The drop warning used to name all three at once ("neither a running level with a living body
 * nor a seat has come up"), and the field run of 2026-09-17 left exactly the question it cannot
 * answer: a deathmatch wish was held for 3186 frames and then dropped, and the log does not say
 * whether the level, the module state or the body was what stood in the way.
 */
static void check_which_gate_is_shut(void)
{
    ut_section("the three engine gates are told apart, in the order they are read");

    ut_check(mp_respawn_gate_shut(true, true, true) == MP_RESPAWN_SHUT_NOTHING,
             "a running level, a running module and a body is the open door");
    ut_check(mp_respawn_gate_shut(false, true, true) == MP_RESPAWN_SHUT_LEVEL,
             "no level is read first: the other two mean nothing without one");
    ut_check(mp_respawn_gate_shut(true, false, true) == MP_RESPAWN_SHUT_MODULE,
             "a module that is not in its running state is named as itself");
    ut_check(mp_respawn_gate_shut(true, true, false) == MP_RESPAWN_SHUT_BODY,
             "and a module that is running with no body is the other half of the old sentence");
    ut_check(mp_respawn_gate_shut(false, false, false) == MP_RESPAWN_SHUT_LEVEL,
             "with everything shut the first one is the one worth saying");

    ut_section("and each has a word for the log");
    ut_check(mp_respawn_shut_word(MP_RESPAWN_SHUT_NOTHING) != NULL &&
                 mp_respawn_shut_word(MP_RESPAWN_SHUT_LEVEL) != NULL &&
                 mp_respawn_shut_word(MP_RESPAWN_SHUT_MODULE) != NULL &&
                 mp_respawn_shut_word(MP_RESPAWN_SHUT_BODY) != NULL,
             "every one of the four reads as a sentence a player's report can be matched to");
}

/* The one answer the death hull, the landing and the corpse watch ask. */
static void check_what_a_corpse_is(void)
{
    const uint32_t DEATH = 0x004B5448u;   /* the death descriptor's address in the retail image */
    const uint32_t STAND = 0x004B5410u;
    uint8_t        record[0x400];

    ut_section("a corpse is the dead flag with the death descriptor up");
    ut_check(mp_respawn_is_a_corpse(1u, DEATH, DEATH), "the flag and the descriptor: a corpse");
    ut_check(!mp_respawn_is_a_corpse(1u, STAND, DEATH),
             "the flag without the descriptor is a death still being entered, not a corpse");
    ut_check(!mp_respawn_is_a_corpse(0u, DEATH, DEATH), "the descriptor without the flag is not");
    ut_check(!mp_respawn_is_a_corpse(0u, STAND, DEATH), "a living body is not");
    ut_check(mp_respawn_is_a_corpse(1u, STAND, 0u),
             "with the descriptor's cell unresolved the flag answers alone");

    memset(record, 0, sizeof record);
    ut_check(!mp_respawn_record_is_a_corpse(0u), "no record is no corpse");
    ut_check(!mp_respawn_record_is_a_corpse((uintptr_t)record),
             "a record with its flag down is not one, whatever the descriptor cell reads");
    ut_check(!mp_respawn_player_is_a_corpse(),
             "and with no game in the process this player is not one either");
}

int main(void)
{
    check_what_a_corpse_is();
    check_the_tick_of_one_wish();
    check_which_gate_is_shut();
    check_a_pose_off_the_wire();
    check_the_engine_side_refuses_without_a_game();
    return ut_summary("mp_respawn");
}
