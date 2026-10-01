/* The phase controller for the map's free runners, driven with no engine and no map.
 *
 * What has to hold is arithmetic and sequences. On the ring, 0.99 and 0.01 are a hair apart and
 * the sign of that hair says who is ahead. A difference smaller than the grain the two machines
 * can sample to is not a difference. A debt is paid off in bites that are strictly smaller than a
 * substep, in a bounded number of them, and never in a direction that would run a mover backwards.
 * A mover nobody is integrating collects nothing, because a bias that stacks would be taken all at
 * once by whoever integrates next. And a mover that jumps further than the engine's own frame time
 * cap allows has been rewritten by somebody else, which makes the debt a claim about a world that
 * is gone.
 */
#include "unittest.h"

#include "mp_world_phase.h"

#include <stdbool.h>
#include <stdint.h>

/* A middling free runner: travel 29, as every shipped record has, at 30 pose units per second.
 * One lap in 0.967 s, one substep of travel is 32 thousandths of the ring. */
#define TEST_LENGTH 29.0f
#define TEST_SPEED  30.0f

static bool near(float a, float b, float tolerance)
{
    float d = a - b;

    return (d < 0.0f ? -d : d) <= tolerance;
}

/* One substep of an undisturbed mover, so a test can walk it without an engine. */
static float advanced(float pose, float bias)
{
    float pose_next = pose + TEST_SPEED * (MP_WORLD_PHASE_SUBSTEP - bias);

    while (pose_next > TEST_LENGTH) {
        pose_next -= TEST_LENGTH;
    }
    if (pose_next < 0.0f) {
        pose_next = 0.0f;
    }
    return pose_next;
}

static void check_ring(void)
{
    ut_section("the short way round the ring");

    ut_check(near(mp_world_phase_delta(0.6f, 0.4f), 0.2f, 1e-6f),
             "a fifth ahead reads as a fifth ahead");
    ut_check(near(mp_world_phase_delta(0.4f, 0.6f), -0.2f, 1e-6f), "and a fifth behind as behind");
    ut_check(near(mp_world_phase_delta(0.99f, 0.01f), -0.02f, 1e-6f),
             "0.99 against 0.01 is two hundredths BEHIND, not ninety eight ahead");
    ut_check(near(mp_world_phase_delta(0.01f, 0.99f), 0.02f, 1e-6f), "and the other way round");
    ut_check(mp_world_phase_delta(0.5f, 0.0f) <= 0.5f && mp_world_phase_delta(0.5f, 0.0f) > -0.5f,
             "half a ring stays inside the half open interval");
    ut_check(near(mp_world_phase_delta(0.25f, 0.25f), 0.0f, 1e-6f), "and agreement is zero");
}

static void check_dead_band(void)
{
    float band = mp_world_phase_dead_band(TEST_SPEED, TEST_LENGTH);

    ut_section("the dead band is two substeps of the mover's own motion");

    ut_check(near(band, 2.0f * MP_WORLD_PHASE_SUBSTEP * TEST_SPEED / TEST_LENGTH, 1e-6f),
             "two substeps of travel, as a fraction of the travel");
    ut_check(mp_world_phase_dead_band(1.0f, TEST_LENGTH) < band,
             "a slow mover can be measured more finely than a fast one");
    ut_check(mp_world_phase_dead_band(0.0f, TEST_LENGTH) >= 1.0f &&
             mp_world_phase_dead_band(TEST_SPEED, 0.0f) >= 1.0f,
             "a record with no speed or no travel admits no difference at all");
}

static void check_measure(void)
{
    mp_world_phase_t phase;

    ut_section("what a measurement books");

    mp_world_phase_reset(&phase);
    ut_check(!mp_world_phase_measure(&phase, 7u, 0.01f, TEST_SPEED, TEST_LENGTH),
             "a difference inside the dead band books nothing");
    ut_check(mp_world_phase_owing(&phase) == 0u, "and leaves nothing owing");

    ut_check(mp_world_phase_measure(&phase, 7u, 0.25f, TEST_SPEED, TEST_LENGTH),
             "a quarter of the ring books a debt");
    ut_check(mp_world_phase_owing(&phase) == 1u, "one mover owes");
    ut_checkf(phase.worst_milli == 250u, "and the worst is recorded as %u thousandths",
              (unsigned)phase.worst_milli);

    ut_check(!mp_world_phase_measure(&phase, 7u, 0.005f, TEST_SPEED, TEST_LENGTH),
             "a later measurement inside the band settles the mover");
    ut_check(mp_world_phase_owing(&phase) == 0u && phase.settled == 1u,
             "the debt is gone rather than left standing");

    ut_check(mp_world_phase_measure(&phase, 7u, 0.30f, TEST_SPEED, TEST_LENGTH) &&
             mp_world_phase_measure(&phase, 7u, 0.10f, TEST_SPEED, TEST_LENGTH),
             "a second measurement is accepted while a debt stands");
    {
        /* Replaced, not added to: 0.10 of the ring at this speed is 0.0967 s. */
        float expected = 0.10f * TEST_LENGTH / TEST_SPEED;
        float bite;

        (void)mp_world_phase_bite(&phase, 7u, 1.0f, TEST_SPEED, TEST_LENGTH);   /* seeds */
        bite = mp_world_phase_bite(&phase, 7u, advanced(1.0f, 0.0f), TEST_SPEED, TEST_LENGTH);
        ut_check(bite > 0.0f && bite <= MP_WORLD_PHASE_BITE * MP_WORLD_PHASE_SUBSTEP,
                 "the first bite is positive and capped");
        ut_checkf(expected > bite, "and the debt of %d thousandths of a second is larger than it",
                  (int)(expected * 1000.0f));
    }
}

static void check_convergence(void)
{
    mp_world_phase_t phase;
    float            pose = 3.0f;
    float            total = 0.0f;
    int              substep;
    int              nudged = 0;

    ut_section("a debt is paid off, in bounded bites, in a bounded time");

    mp_world_phase_reset(&phase);
    (void)mp_world_phase_measure(&phase, 3u, 0.5f, TEST_SPEED, TEST_LENGTH);

    for (substep = 0; substep < 32 * 20; ++substep) {
        float bite = mp_world_phase_bite(&phase, 3u, pose, TEST_SPEED, TEST_LENGTH);

        if (bite != 0.0f) {
            ++nudged;
            total += bite;
        }
        ut_check(bite < MP_WORLD_PHASE_SUBSTEP && bite > -MP_WORLD_PHASE_SUBSTEP,
                 "every bite is strictly smaller than a substep, so it can never leave a bias "
                 "standing on the integrator's equality test");
        pose = advanced(pose, bite);
        if (mp_world_phase_owing(&phase) == 0u) {
            break;
        }
    }
    ut_check(mp_world_phase_owing(&phase) == 0u, "the debt is settled");
    ut_checkf(near(total, 0.5f * TEST_LENGTH / TEST_SPEED, 1e-3f),
              "and what was paid equals what was owed (%d against %d thousandths of a second)",
              (int)(total * 1000.0f), (int)(0.5f * TEST_LENGTH / TEST_SPEED * 1000.0f));
    ut_checkf(nudged <= 32 * 8, "in %d integrations, well inside a level's patience", nudged);
    ut_checkf(phase.nudges == (uint32_t)nudged, "and the counter agrees (%u)",
              (unsigned)phase.nudges);
}

static void check_still_mover(void)
{
    mp_world_phase_t phase;
    int              substep;

    ut_section("a mover nobody integrates collects nothing");

    mp_world_phase_reset(&phase);
    (void)mp_world_phase_measure(&phase, 5u, 0.4f, TEST_SPEED, TEST_LENGTH);

    (void)mp_world_phase_bite(&phase, 5u, 6.0f, TEST_SPEED, TEST_LENGTH);   /* seeds */
    for (substep = 0; substep < 64; ++substep) {
        ut_check(mp_world_phase_bite(&phase, 5u, 6.0f, TEST_SPEED, TEST_LENGTH) == 0.0f,
                 "the pose did not move, so nothing is written");
    }
    ut_check(mp_world_phase_owing(&phase) == 1u, "and the debt is still there, unpaid");
    ut_check(mp_world_phase_bite(&phase, 5u, advanced(6.0f, 0.0f), TEST_SPEED, TEST_LENGTH) > 0.0f,
             "the first integration after that pays exactly one bite");
}

static void check_foreign_writer(void)
{
    mp_world_phase_t phase;

    ut_section("a pose that jumps further than the engine can step drops the debt");

    mp_world_phase_reset(&phase);
    (void)mp_world_phase_measure(&phase, 9u, 0.4f, TEST_SPEED, TEST_LENGTH);
    (void)mp_world_phase_bite(&phase, 9u, 2.0f, TEST_SPEED, TEST_LENGTH);   /* seeds at 2.0 */

    /* The frame time cap is a tenth of a second, so 30 * 0.1 = 3 pose units is the most one
     * integration can produce. Six is somebody else having rewritten timeBase. */
    ut_check(mp_world_phase_bite(&phase, 9u, 8.0f, TEST_SPEED, TEST_LENGTH) == 0.0f,
             "no bite is written into a mover that was just moved by somebody else");
    ut_check(mp_world_phase_owing(&phase) == 0u && phase.discarded == 1u,
             "and the debt is discarded rather than paid against the new world");
}

static void check_slots(void)
{
    mp_world_phase_t phase;
    uint32_t         id;

    ut_section("the slots are counted rather than overrun");

    mp_world_phase_reset(&phase);
    for (id = 0; id < MP_WORLD_PHASE_SLOTS; ++id) {
        ut_check(mp_world_phase_measure(&phase, (uint16_t)id, 0.3f, TEST_SPEED, TEST_LENGTH),
                 "every slot takes its mover");
    }
    ut_check(!mp_world_phase_measure(&phase, 999u, 0.3f, TEST_SPEED, TEST_LENGTH) &&
             phase.full == 1u,
             "one more is refused and counted, not written past the end");
    ut_check(mp_world_phase_owing(&phase) == MP_WORLD_PHASE_SLOTS, "and the table stays full");

    ut_check(mp_world_phase_bite(&phase, 999u, 1.0f, TEST_SPEED, TEST_LENGTH) == 0.0f,
             "a mover with no slot is never nudged");
}

static void check_refusals(void)
{
    mp_world_phase_t phase;

    ut_section("what is refused outright");

    mp_world_phase_reset(&phase);
    ut_check(!mp_world_phase_measure(NULL, 1u, 0.3f, TEST_SPEED, TEST_LENGTH) &&
             mp_world_phase_bite(NULL, 1u, 1.0f, TEST_SPEED, TEST_LENGTH) == 0.0f,
             "no controller, no work");
    ut_check(!mp_world_phase_measure(&phase, 1u, 0.3f, 0.0f, TEST_LENGTH) &&
             !mp_world_phase_measure(&phase, 1u, 0.3f, TEST_SPEED, 0.0f),
             "a record with no speed or no travel books nothing: both would divide by zero");
    ut_check(mp_world_phase_owing(&phase) == 0u, "so nothing owes");
}

int main(void)
{
    check_ring();
    check_dead_band();
    check_measure();
    check_convergence();
    check_still_mover();
    check_foreign_writer();
    check_slots();
    check_refusals();

    return ut_summary("mp_world_phase");
}
