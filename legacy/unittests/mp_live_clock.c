/* The clock the re-entry's waits are measured on: the wall clock, left standing while a level runs
 * and this machine does not simulate it.
 *
 * Pure arithmetic over two counts and a flag. What it has to get right is easy to get wrong in
 * three places. More frames are drawn than substeps run, so a look that finds no new substep is
 * the ordinary case and not a hold. Outside a running level nothing simulates by nature, and there
 * the clock has to run on, or a wish would outlive its level. And a hold of any length may neither
 * count nor wrap. A parked player holds it at once, with the world running around him, which is
 * what an open developer menu is inside a session.
 */
#include "unittest.h"

#include "mp_live_clock.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One drawn frame at 240 a second, and the substep that comes with every eighth of them. */
#define FRAME_MS        4u
#define FRAMES_PER_STEP 8u

typedef struct rig {
    mp_live_clock_t clock;
    uint32_t        steps;
    uint32_t        wall_ms;
    uint32_t        frame;
    uint32_t        reads;
    bool            parked;   /* the player module reads its suspended state */
} rig_t;

static void draw(rig_t *rig, uint32_t frames, bool simulates, bool level_runs)
{
    uint32_t i;

    for (i = 0u; i < frames; ++i) {
        ++rig->frame;
        rig->wall_ms += FRAME_MS;
        if (simulates && rig->frame % FRAMES_PER_STEP == 0u) {
            ++rig->steps;
        }
        rig->reads = mp_live_clock_look(&rig->clock, rig->steps, level_runs, rig->parked,
                                        rig->wall_ms);
    }
}

static void check_a_running_level(void)
{
    rig_t rig;

    memset(&rig, 0, sizeof rig);
    ut_section("a level that runs: the wall clock, although most frames bring no substep");
    ut_check(mp_live_clock_look(&rig.clock, 0u, true, false, 0u) == 0u,
             "the first look reads nought");
    draw(&rig, 2500u, true, true);
    ut_checkf(rig.reads == 2500u * FRAME_MS && rig.clock.held_ms == 0u && rig.clock.holds == 0u,
              "ten seconds at 240 frames and 30 substeps a second read ten seconds (%u ms, %u "
              "left out)", (unsigned)rig.reads, (unsigned)rig.clock.held_ms);
}

static void check_a_held_world(void)
{
    rig_t    rig;
    uint32_t before;

    memset(&rig, 0, sizeof rig);
    (void)mp_live_clock_look(&rig.clock, 0u, true, false, 0u);
    draw(&rig, 1000u, true, true);
    before = rig.reads;

    ut_section("a level whose world is held: the clock stands, however many frames are drawn");
    draw(&rig, 5000u, false, true);
    ut_checkf(rig.reads - before <= MP_LIVE_CLOCK_GAP_MS + FRAME_MS * FRAMES_PER_STEP,
              "twenty seconds of a held world add no more than the gap that tells a hold from a "
              "frame without a substep (%u ms)", (unsigned)(rig.reads - before));
    ut_checkf(rig.clock.holds == 1u && rig.clock.holding &&
                  rig.clock.held_ms + rig.clock.ms == rig.wall_ms,
              "it is one stretch, and what was left out and what was counted are the wall clock "
              "between them (%u + %u of %u)", (unsigned)rig.clock.held_ms, (unsigned)rig.clock.ms,
              (unsigned)rig.wall_ms);

    before = rig.reads;
    draw(&rig, 250u, true, true);
    ut_checkf(rig.reads - before > 250u * FRAME_MS - FRAME_MS * FRAMES_PER_STEP &&
                  !rig.clock.holding && rig.clock.holds == 1u,
              "the first substep ends the hold and the clock runs again (%u ms of the next "
              "second)", (unsigned)(rig.reads - before));

    ut_section("a second hold is a second stretch, and a very long look cannot wrap the count");
    before = rig.clock.ms;
    rig.wall_ms += 0xF0000000u;
    (void)mp_live_clock_look(&rig.clock, rig.steps, true, false, rig.wall_ms);
    rig.wall_ms += 0x20000000u;   /* and past the wrap of the wall clock */
    (void)mp_live_clock_look(&rig.clock, rig.steps, true, false, rig.wall_ms);
    ut_check(rig.clock.holds == 2u && rig.clock.holding && rig.clock.ms == before,
             "one look after weeks without a substep is a hold and adds nothing, and neither "
             "does the next one");
}

static void check_outside_a_level(void)
{
    rig_t rig;

    memset(&rig, 0, sizeof rig);
    (void)mp_live_clock_look(&rig.clock, 0u, false, false, 0u);

    ut_section("no level running: nothing simulates by nature, and the clock runs on");
    draw(&rig, 5000u, false, false);
    ut_checkf(rig.reads == 5000u * FRAME_MS && rig.clock.holds == 0u,
              "twenty seconds of a front end read twenty seconds, so a wish that outlived its "
              "level still runs out (%u ms)", (unsigned)rig.reads);

    ut_section("and a missing clock is answered rather than dereferenced");
    ut_check(mp_live_clock_look(NULL, 1u, true, false, 1000u) == 0u, "nought");
}

static void check_a_parked_player(void)
{
    rig_t    rig;
    uint32_t before;

    memset(&rig, 0, sizeof rig);
    (void)mp_live_clock_look(&rig.clock, 0u, true, false, 0u);
    draw(&rig, 1000u, true, true);
    before = rig.reads;

    ut_section("a parked player in a world that runs: the clock stands from the first look");
    rig.parked = true;
    draw(&rig, 5000u, true, true);
    ut_checkf(rig.reads == before && rig.clock.holds == 1u && rig.clock.holding,
              "twenty seconds behind an open menu, with every substep running, add nothing (%u "
              "ms)", (unsigned)(rig.reads - before));
    rig.parked = false;
    draw(&rig, 250u, true, true);
    ut_checkf(rig.reads - before == 250u * FRAME_MS && !rig.clock.holding,
              "and the clock runs from the look that finds him back (%u ms of the next second)",
              (unsigned)(rig.reads - before));

    ut_section("outside a level a parked player holds nothing either");
    before     = rig.reads;
    rig.parked = true;
    draw(&rig, 250u, false, false);
    ut_check(rig.reads - before == 250u * FRAME_MS && rig.clock.holds == 1u,
             "the front end parks the player for good, and a wish has to run out there");
}

int main(void)
{
    check_a_running_level();
    check_a_held_world();
    check_outside_a_level();
    check_a_parked_player();

    return ut_summary("mp_live_clock");
}
