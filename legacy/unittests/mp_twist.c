/* mp_twist.c: what the puppet's node rotations are drawn as, pure, and the module's refusals in a
 * process with no game.
 *
 * The whole of the per-frame decision is arithmetic over four things: the value the substep
 * started from, the value it produced, the weight the engine hands out, and whether the pair was
 * collapsed. A collapsed pair is not a fifth input but a shape of the first two, both ends holding
 * one value, which is exactly what the collapse does to it; so it is driven here as such, and the
 * property that matters is that such a pair answers the same at every weight. That property is the
 * whole reason the collapse exists: without it the far body sweeps the same few degrees from end to
 * end thirty two times a second while the weight runs from zero to one.
 *
 * The angles are degrees on a circle, so the mixture has to take the shortest arc. A pair sitting
 * either side of the half turn would otherwise be mixed the long way round and the body would spin
 * through three hundred degrees to reach a neighbour two degrees away.
 *
 * The last section drives the engine-facing half in a process where nothing resolves. Every ask
 * has to come back as a refusal with nothing dereferenced, which is what says the module is safe
 * on a build it does not recognise rather than merely quiet on the one it does.
 */
#include "unittest.h"

#include "mp_twist.h"

#include <stdint.h>

/* Tighter than a degree would need, because these are exact linear mixtures of small numbers and
 * a float carries about seven digits: a tolerance loose enough to hide a wrong formula would hide
 * the very thing being pinned. */
#define ANGLE_TOLERANCE 0.0005

static void check_the_two_ends(void)
{
    ut_section("the two ends of the pair");

    ut_near(mp_twist_draw_angle(10.0f, 40.0f, 0.0f), 10.0, ANGLE_TOLERANCE,
            "weight zero draws the value the substep started from");
    ut_near(mp_twist_draw_angle(10.0f, 40.0f, 1.0f), 40.0, ANGLE_TOLERANCE,
            "weight one draws the value the substep produced");
    ut_near(mp_twist_draw_angle(10.0f, 40.0f, 0.5f), 25.0, ANGLE_TOLERANCE,
            "half way draws half way");
    ut_near(mp_twist_draw_angle(10.0f, 40.0f, 0.25f), 17.5, ANGLE_TOLERANCE,
            "a quarter of the way draws a quarter of the way");

    ut_near(mp_twist_draw_angle(-30.0f, -10.0f, 0.5f), -20.0, ANGLE_TOLERANCE,
            "negative angles mix the same way");

    /* The engine keeps its weight inside (0, 1], so anything outside is a reading that cannot be
     * believed rather than a value to extrapolate from. Both ends are held rather than followed. */
    ut_near(mp_twist_draw_angle(10.0f, 40.0f, -1.0f), 10.0, ANGLE_TOLERANCE,
            "a weight below zero is held at the start rather than extrapolated backwards");
    ut_near(mp_twist_draw_angle(10.0f, 40.0f, 2.0f), 40.0, ANGLE_TOLERANCE,
            "a weight above one is held at the end rather than extrapolated forwards");
}

static void check_a_collapsed_pair(void)
{
    ut_section("a collapsed pair");

    /* A collapsed pair is one whose two ends hold the same value, which is what the collapse
     * makes of it. Every weight has to answer that value, or a substep that produced nothing
     * would still move the body. */
    ut_near(mp_twist_draw_angle(33.0f, 33.0f, 0.0f), 33.0, ANGLE_TOLERANCE,
            "a collapsed pair answers its value at weight zero");
    ut_near(mp_twist_draw_angle(33.0f, 33.0f, 0.5f), 33.0, ANGLE_TOLERANCE,
            "and at half way");
    ut_near(mp_twist_draw_angle(33.0f, 33.0f, 1.0f), 33.0, ANGLE_TOLERANCE,
            "and at weight one, so the frames of such a substep are all one orientation");
    ut_near(mp_twist_draw_angle(-179.0f, -179.0f, 0.75f), -179.0, ANGLE_TOLERANCE,
            "including a pair sitting on the far side of the half turn");
}

static void check_the_shortest_arc(void)
{
    ut_section("the shortest arc across the half turn");

    /* 170 to -160 is thirty degrees the short way and three hundred and thirty the long way. Half
     * way round the short arc is 185, which folds back into the band the node array holds. */
    ut_near(mp_twist_draw_angle(170.0f, -160.0f, 0.5f), -175.0, ANGLE_TOLERANCE,
            "the mixture crosses the half turn rather than going the long way round");
    ut_near(mp_twist_draw_angle(170.0f, -160.0f, 0.0f), 170.0, ANGLE_TOLERANCE,
            "and its start is still its start");
    ut_near(mp_twist_draw_angle(170.0f, -160.0f, 1.0f), -160.0, ANGLE_TOLERANCE,
            "and its end still its end");

    ut_near(mp_twist_draw_angle(-170.0f, 160.0f, 0.5f), 175.0, ANGLE_TOLERANCE,
            "and the same the other way round");

    /* The far player turning through the whole circle is the case where a mixture the long way
     * round would be visible as the body spinning backwards for a frame. */
    ut_near(mp_twist_draw_angle(179.0f, -179.0f, 0.5f), 180.0, ANGLE_TOLERANCE,
            "two degrees apart across the boundary mix to the point between them");
}

static void check_a_rotation_that_ends(void)
{
    ut_section("a rotation that ends on the far side");

    /* A node absent from the new sample stands at zero in the pair's near end, so the frames of
     * that substep run it out to zero instead of dropping it there in one step. That is what the
     * substep half's own zeroing used to do on its own, and it is why the pair carries the
     * absent nodes rather than forgetting them. */
    ut_near(mp_twist_draw_angle(40.0f, 0.0f, 0.0f), 40.0, ANGLE_TOLERANCE,
            "the first frame after it ended still shows where it was");
    ut_near(mp_twist_draw_angle(40.0f, 0.0f, 0.5f), 20.0, ANGLE_TOLERANCE,
            "half way through the substep it is half way home");
    ut_near(mp_twist_draw_angle(40.0f, 0.0f, 1.0f), 0.0, ANGLE_TOLERANCE,
            "and by the end of it, home");

    /* And a rotation that begins runs up from zero rather than appearing at its full angle. */
    ut_near(mp_twist_draw_angle(0.0f, -60.0f, 0.5f), -30.0, ANGLE_TOLERANCE,
            "one that begins comes up from zero rather than appearing whole");
}

static void check_the_engine_half_without_a_game(void)
{
    mp_wire_twist_t     read[MP_WIRE_MAX_TWISTS];
    mp_wire_twist_t     write[2];
    mp_twist_counters_t counters;

    ut_section("the engine half in a process with no game");

    write[0].node  = 0u;
    write[0].pitch = 5.0f;
    write[0].yaw   = 12.0f;
    write[1].node  = 3u;
    write[1].pitch = 0.0f;
    write[1].yaw   = -4.0f;

    /* Nothing resolves here, so the site is zero, the cells are zero and no detour is placed.
     * What the module has to do about that is decline, not dereference. */
    mp_twist_resolve();
    mp_twist_get_counters(&counters);
    ut_check(!counters.installed,
             "with no world draw resolved the frame hook is reported as absent");

    ut_check(mp_twist_read(0u, read) == 0u, "a read of no object finds no rotations");
    ut_check(mp_twist_read(0x1000u, read) == 0u, "and neither does one of an address that is not");

    mp_twist_open_substep(1u);
    mp_twist_apply(1u, 0u, write, 2u);
    mp_twist_apply(1u, 0x1000u, write, 2u);
    mp_twist_apply(1u, 0u, NULL, 0u);
    mp_twist_get_counters(&counters);
    ut_check(counters.frames == 0u && counters.refusals == 0u && counters.given_up == 0u,
             "a substep applied to no object draws nothing and refuses nothing");

    mp_twist_withhold(1u, 0u);
    mp_twist_get_counters(&counters);
    ut_check(counters.withheld == 1u && counters.frames == 0u,
             "a substep whose rotations belong to another rig is counted and draws nothing");

    mp_twist_reset(1u);
    mp_twist_get_counters(&counters);
    ut_check(!counters.installed && counters.frames == 0u,
             "and the reset leaves the counters where a run with no game left them");

    mp_twist_get_counters(NULL);
    ut_check(1, "asking for the counters into nothing is not a crash");
}

int main(void)
{
    check_the_two_ends();
    check_a_collapsed_pair();
    check_the_shortest_arc();
    check_a_rotation_that_ends();
    check_the_engine_half_without_a_game();

    return ut_summary("puppet node rotations");
}
