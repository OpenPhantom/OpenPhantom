/* Putting a dead player back into a level that is still running.
 *
 * Six decisions in this module are arithmetic with a known right answer and run here with no
 * game in the process: what one tick does with one wish, how long a wait has lasted, whether the
 * engine's re-entry has lost its fade, which of the engine's gates is shut, whether a pose that
 * arrived from another machine is a pose at all, and whether a body is a corpse. What the
 * ground under an anchor means and where the candidates are is the seat's, and its own tests
 * pin it.
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
    const uint32_t FRAMES = MP_RESPAWN_DEADLINE_FRAMES;
    const uint32_t MS     = MP_RESPAWN_DEADLINE_MS;

    ut_section("nothing wanted, nothing done");
    ut_check(mp_respawn_step(false, true, 0u, 0u, 0u) == MP_RESPAWN_STEP_IDLE,
             "an idle module answers idle even with both gates open");
    ut_check(mp_respawn_step(false, false, FRAMES + 1u, MS + 1u, 0u) == MP_RESPAWN_STEP_IDLE,
             "and a deadline that passed while nothing was wanted drops nothing");

    ut_section("the delay the caller asked for is a rule of the round");
    ut_check(mp_respawn_step(true, true, 0u, 0u, A_DELAY) == MP_RESPAWN_STEP_HOLD,
             "an open gate does not shorten a configured re-entry time");
    ut_check(mp_respawn_step(true, true, A_DELAY - 1u, MS, A_DELAY) == MP_RESPAWN_STEP_HOLD,
             "not on the frame before it either, however long that took");
    ut_check(mp_respawn_step(true, true, A_DELAY, 0u, A_DELAY) == MP_RESPAWN_STEP_RUN,
             "and on the frame it runs out, the re-entry happens");
    ut_check(mp_respawn_step(true, false, A_DELAY, 0u, A_DELAY) == MP_RESPAWN_STEP_WAIT,
             "unless the engine is not ready, in which case it waits");

    ut_section("cooperative play asks for no delay at all");
    ut_check(mp_respawn_step(true, true, 0u, 0u, 0u) == MP_RESPAWN_STEP_RUN,
             "so the very first tick after the request carries it out");

    ut_section("an open gate beats an expired deadline");
    ut_check(mp_respawn_step(true, true, FRAMES, MS, 0u) == MP_RESPAWN_STEP_RUN,
             "a wish that becomes possible on the frame it runs out is carried out");
    ut_check(mp_respawn_step(true, false, FRAMES, MS, 0u) == MP_RESPAWN_STEP_DROP,
             "and only a closed gate on that frame throws it away");
    ut_check(mp_respawn_step(true, false, FRAMES - 1u, MS, 0u) == MP_RESPAWN_STEP_WAIT,
             "one frame earlier it is still waiting");

    ut_section("the deadline is the frames and the time, both");
    ut_check(mp_respawn_step(true, false, FRAMES, MS - 1u, 0u) == MP_RESPAWN_STEP_WAIT,
             "the frames of a fast machine run out in under four seconds: still waiting");
    ut_check(mp_respawn_step(true, false, FRAMES * 4u, MS - 1u, 0u) == MP_RESPAWN_STEP_WAIT,
             "however many of them there are");
    ut_check(mp_respawn_step(true, false, FRAMES - 1u, MS * 4u, 0u) == MP_RESPAWN_STEP_WAIT,
             "and the time alone, behind a loading screen that draws no frame, drops nothing");
    ut_check(mp_respawn_deadline_passed(FRAMES, MS), "both reached is passed");
    ut_check(!mp_respawn_deadline_passed(FRAMES, MS - 1u), "the frames alone are not");
    ut_check(!mp_respawn_deadline_passed(FRAMES - 1u, MS), "nor the time alone");
    ut_check(!mp_respawn_deadline_passed(0u, 0u), "and a wait that has just begun is not");
}

/* The time a wait has lasted, from the stamp its first look sets. */
static void check_the_time_a_wait_has_lasted(void)
{
    bool     stamped = false;
    uint32_t since   = 0u;
    uint32_t look;
    uint32_t waited  = 0u;

    ut_section("the first look of a wait stamps it, a clock that reads nought included");
    ut_check(mp_respawn_waited_ms(&stamped, &since, 0u) == 0u && stamped && since == 0u,
             "a wait that begins while the clock reads nought is stamped, and has lasted nothing");
    ut_check(mp_respawn_waited_ms(&stamped, &since, 20000u) == 20000u && since == 0u,
             "and twenty seconds on it has lasted twenty seconds, with the stamp left alone");

    ut_section("a wait that begins late has lasted nothing, whatever the clock reads");
    stamped = false;
    ut_check(mp_respawn_waited_ms(&stamped, &since, 600000u) == 0u && since == 600000u,
             "ten minutes into the session the first look answers nought, not ten minutes");
    ut_check(mp_respawn_waited_ms(&stamped, &since, 600250u) == 250u, "and then it counts");

    ut_section("nine hundred frames at two hundred and forty a second are not fifteen seconds");
    stamped = false;
    for (look = 0u; look <= MP_RESPAWN_DEADLINE_FRAMES; ++look) {
        waited = mp_respawn_waited_ms(&stamped, &since, 1000u + look * 4u);
    }
    ut_check(waited == MP_RESPAWN_DEADLINE_FRAMES * 4u, "they are three seconds and six tenths");
    ut_check(!mp_respawn_deadline_passed(MP_RESPAWN_DEADLINE_FRAMES, waited),
             "so the wait that used to end there has not run out");
    ut_check(mp_respawn_step(true, false, MP_RESPAWN_DEADLINE_FRAMES, waited, 0u) ==
                 MP_RESPAWN_STEP_WAIT,
             "and the wish is still waiting");

    ut_section("the clock running past its top is still a difference");
    stamped = false;
    (void)mp_respawn_waited_ms(&stamped, &since, 0xFFFFFF00u);
    ut_check(mp_respawn_waited_ms(&stamped, &since, 0x00000100u) == 0x200u,
             "a stamp just under the top and a look just over it are 512 ms apart");

    ut_section("and nothing to stamp is nothing waited");
    ut_check(mp_respawn_waited_ms(NULL, &since, 5u) == 0u, "no bit");
    ut_check(mp_respawn_waited_ms(&stamped, NULL, 5u) == 0u, "no time");
}

/* The engine's re-entry waits on a fade a closing menu can take away. */
static void check_when_the_fade_is_lost(void)
{
    const uint32_t RUNNING = 1u;
    const uint32_t FADING  = MP_RESPAWN_MODULE_FADING;
    const uint32_t ASKED   = 4u;   /* the re-entry was asked for and its fade not started yet */

    ut_section("lost is: waiting on the fade, and the tint neither running nor run out");
    ut_check(mp_respawn_fade_is_lost(FADING, false, false),
             "the wait with a tint that was stopped under it: lost");
    ut_check(!mp_respawn_fade_is_lost(FADING, false, true),
             "the wait with the tint still going to black is the engine's own second");
    ut_check(!mp_respawn_fade_is_lost(FADING, true, true),
             "a tint that holds stays active past its end, and the spawn is a tick away");
    ut_check(!mp_respawn_fade_is_lost(FADING, true, false),
             "a backdrop that ran out at once lets the re-entry go on by itself");

    ut_section("and no other state of the module is a wait on a fade");
    ut_check(!mp_respawn_fade_is_lost(RUNNING, false, false),
             "a running module with no tint is an ordinary frame of the game");
    ut_check(!mp_respawn_fade_is_lost(ASKED, false, false),
             "the state that starts the fade has not started it yet, and will");
    ut_check(!mp_respawn_fade_is_lost(0u, false, false), "nor a module that is off");
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

    mp_respawn_tick(100u, 3200u);
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
    check_the_time_a_wait_has_lasted();
    check_when_the_fade_is_lost();
    check_which_gate_is_shut();
    check_a_pose_off_the_wire();
    check_the_engine_side_refuses_without_a_game();
    return ut_summary("mp_respawn");
}
