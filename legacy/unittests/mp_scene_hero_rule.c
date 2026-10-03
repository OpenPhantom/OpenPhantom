/* mp_scene_hero_rule.c: when the hero a scene drives is stuck on its walk, and where it is put.
 *
 * The walk opcode stores its goal in the actor's target, except while it goes round an obstacle:
 * then the target holds the detour point, and it still holds it in the tick the walk arrives there
 * with the detour flag already cleared. Every case below is a sequence of readings as the binding
 * takes them after the enemy tick, with the position standing still unless a case moves it.
 */
#include "unittest.h"

#include "mp_scene_hero_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ACTOR       0x100u
#define OTHER_ACTOR 0x200u

static const float GOAL[3]   = { 99.40f, 32.00f, 29.00f };
static const float START[3]  = { 99.59f, 35.68f, 29.00f };
static const float DETOUR[3] = { 98.00f, 34.00f, 29.00f };

static mp_scene_hero_reading_t walking_to(const float target[3], const float position[3],
                                          bool detour)
{
    mp_scene_hero_reading_t r;

    memset(&r, 0, sizeof r);
    r.actor          = ACTOR;
    r.move_requested = true;
    r.move_speed     = 1.0f;
    r.move_mode      = 0;
    r.detour         = detour;
    r.waypoint       = 1;
    memcpy(r.target, target, sizeof r.target);
    memcpy(r.position, position, sizeof r.position);
    return r;
}

/* Feeds `count` copies of one reading from substep `*now` on; answers the first PUT's substep
 * relative to the start, or 0 when none came. */
static uint32_t feed(mp_scene_hero_clock_t *clock, uint32_t *now,
                     const mp_scene_hero_reading_t *r, uint32_t count, float goal[3])
{
    uint32_t i;

    for (i = 1u; i <= count; ++i) {
        if (mp_scene_hero_rule_step(clock, (*now)++, r, goal) == MP_SCENE_HERO_PUT) {
            return i;
        }
    }
    return 0u;
}

static bool same(const float a[3], const float b[3])
{
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2];
}

static void check_what_a_walking_wish_is(void)
{
    mp_scene_hero_reading_t r = walking_to(GOAL, START, false);

    ut_section("a walking wish: the walk ran, a speed that is not nought, an even mode");
    ut_check(mp_scene_hero_rule_wants_to_walk(&r), "the walk at speed 1 in mode 0 is one");
    r.move_speed = -1.0f;
    ut_check(mp_scene_hero_rule_wants_to_walk(&r), "a negative speed walks backwards and is one");
    r.move_speed = 0.0f;
    ut_check(!mp_scene_hero_rule_wants_to_walk(&r), "a speed of nought only turns and is none");
    r.move_speed = 1.0f;
    r.move_mode  = 3;
    ut_check(!mp_scene_hero_rule_wants_to_walk(&r),
             "an odd mode walks through bodies and geometry and is none");
    r.move_mode      = 2;
    r.move_requested = false;
    ut_check(!mp_scene_hero_rule_wants_to_walk(&r), "nor is a tick in which the walk did not run");
    ut_check(!mp_scene_hero_rule_wants_to_walk(NULL), "nor no reading at all");
}

static void check_progress_and_standing_still(void)
{
    mp_scene_hero_clock_t   clock;
    mp_scene_hero_reading_t r;
    float                   goal[3] = { 0.0f, 0.0f, 0.0f };
    float                   at[3];
    uint32_t                now = 1000u;
    uint32_t                i;
    bool                    put = false;

    ut_section("a walk that gets somewhere is left alone; one that stands still is put");
    mp_scene_hero_rule_reset(&clock);
    memcpy(at, START, sizeof at);
    for (i = 0u; i < 300u; ++i) {
        at[1] -= 0.01f;   /* a tenth of a unit every ten substeps */
        r = walking_to(GOAL, at, false);
        put = put || mp_scene_hero_rule_step(&clock, now++, &r, goal) == MP_SCENE_HERO_PUT;
    }
    ut_check(!put, "a body closing in by a tenth of a unit every ten substeps is never put");

    mp_scene_hero_rule_reset(&clock);
    r = walking_to(GOAL, START, false);
    i = feed(&clock, &now, &r, 200u, goal);
    ut_checkf(i == MP_SCENE_HERO_STILL_SUBSTEPS + 1u,
              "a body standing still is put after 96 substeps past the first look: %u", i);
    ut_check(same(goal, GOAL), "and onto the goal itself");
    ut_check(clock.longest_still == MP_SCENE_HERO_STILL_SUBSTEPS,
             "the longest stand-still is the 96 that led to it");

    r.move_speed = 0.0f;
    ut_check(feed(&clock, &now, &r, 300u, goal) == 0u,
             "a turn on the spot at speed nought is never put, however long it lasts");
    r.move_speed = 1.0f;
    r.move_mode  = 1;
    ut_check(feed(&clock, &now, &r, 300u, goal) == 0u, "nor a walk in an odd mode");
}

/* A run of readings that counts its walking substeps and remembers where the first put came. */
typedef struct run {
    mp_scene_hero_clock_t clock;
    uint32_t              now;
    uint32_t              walked;
    uint32_t              put_at;
    float                 goal[3];
} run_t;

static void step_in(run_t *run, const mp_scene_hero_reading_t *r)
{
    if (run->put_at != 0u) {
        return;
    }
    ++run->walked;
    if (mp_scene_hero_rule_step(&run->clock, run->now++, r, run->goal) == MP_SCENE_HERO_PUT) {
        run->put_at = run->walked;
    }
}

/* The case a detour flag made the clock start over again and again: the walk goes round an
 * obstacle, arrives at the detour point, reads the goal again, and goes round once more, every 20
 * substeps, and the body gets no closer. */
static void check_detours_coming_and_going(void)
{
    run_t                   run;
    mp_scene_hero_reading_t r;
    float                   detour[3];
    uint32_t                round;
    uint32_t                i;

    ut_section("detours every 20 substeps with no progress are put at 96");
    memset(&run, 0, sizeof run);
    run.now = 5000u;
    r = walking_to(GOAL, START, false);
    step_in(&run, &r);                          /* the real goal, seen once */
    for (round = 0u; round < 10u; ++round) {
        memcpy(detour, DETOUR, sizeof detour);
        detour[0] += (float)round * 0.5f;       /* each round a detour point of its own */
        r = walking_to(detour, START, true);
        for (i = 0u; i < 18u; ++i) {
            step_in(&run, &r);
        }
        r = walking_to(detour, START, false);   /* arrived: the flag gone, the target not */
        step_in(&run, &r);
        r = walking_to(GOAL, START, false);     /* the goal read again */
        step_in(&run, &r);
    }
    ut_checkf(run.put_at == MP_SCENE_HERO_STILL_SUBSTEPS + 1u,
              "the clock ran on through four rounds of detours into a fifth and put at 96: %u",
              run.put_at);
    ut_check(same(run.goal, GOAL), "onto the real goal, not onto a detour point");
    ut_check(run.clock.detour_ran, "and it knows a detour ran, for its line");
}

static void check_the_arrival_at_a_detour_point(void)
{
    mp_scene_hero_clock_t   clock;
    mp_scene_hero_reading_t r;
    float                   goal[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t                now = 9000u;

    ut_section("the tick a walk arrives at a detour point is no real goal");
    mp_scene_hero_rule_reset(&clock);
    r = walking_to(GOAL, START, false);
    (void)feed(&clock, &now, &r, 50u, goal);
    r = walking_to(DETOUR, START, true);
    (void)feed(&clock, &now, &r, 10u, goal);
    r = walking_to(DETOUR, START, false);
    (void)mp_scene_hero_rule_step(&clock, now++, &r, goal);
    ut_check(same(clock.goal, GOAL) && clock.still == 60u,
             "the flag gone and the detour point still in the target: the goal and the clock stay");
    (void)mp_scene_hero_rule_step(&clock, now++, &r, goal);
    ut_check(same(clock.goal, GOAL) && clock.still == 61u,
             "and a second such tick, the flag gone twice, is the detour point seen last: no goal");
}

static void check_a_put_while_a_detour_is_flagged(void)
{
    mp_scene_hero_clock_t   clock;
    mp_scene_hero_reading_t r;
    float                   goal[3] = { 0.0f, 0.0f, 0.0f };
    uint32_t                now = 12000u;

    ut_section("a put while the walk is on a detour goes onto the real goal");
    mp_scene_hero_rule_reset(&clock);
    r = walking_to(GOAL, START, false);
    (void)mp_scene_hero_rule_step(&clock, now++, &r, goal);
    r = walking_to(DETOUR, START, true);
    ut_check(feed(&clock, &now, &r, 200u, goal) == MP_SCENE_HERO_STILL_SUBSTEPS,
             "a walk stuck on its way round is put after 96 substeps");
    ut_check(same(goal, GOAL),
             "onto the real goal, which the next walk reaches with the flag set and clears");
}

static void check_what_starts_the_clock_again(void)
{
    mp_scene_hero_clock_t   clock;
    mp_scene_hero_reading_t r;
    float                   goal[3] = { 0.0f, 0.0f, 0.0f };
    float                   moved[3];
    uint32_t                now = 20000u;

    ut_section("a new waypoint, a real goal that moved, another actor");
    mp_scene_hero_rule_reset(&clock);
    r = walking_to(GOAL, START, false);
    (void)feed(&clock, &now, &r, 90u, goal);
    r          = walking_to(DETOUR, START, true);
    r.waypoint = 2;
    (void)mp_scene_hero_rule_step(&clock, now++, &r, goal);
    ut_check(!clock.goal_known && clock.still == 0u,
             "a new waypoint drops the goal it passed, and a detour after it brings none back");
    r          = walking_to(GOAL, START, false);
    r.waypoint = 2;
    (void)mp_scene_hero_rule_step(&clock, now++, &r, goal);
    ut_check(!clock.goal_known, "nor does the tick after it, the flag gone only once");
    ut_check(feed(&clock, &now, &r, 200u, goal) == MP_SCENE_HERO_STILL_SUBSTEPS + 1u,
             "the next real goal starts the clock from nothing");

    mp_scene_hero_rule_reset(&clock);
    r = walking_to(GOAL, START, false);
    (void)feed(&clock, &now, &r, 90u, goal);
    ut_check(clock.still == 89u, "ninety looks at one goal: 89 substeps standing still");
    memcpy(moved, GOAL, sizeof moved);
    moved[0] += 0.2f;
    r = walking_to(moved, START, false);
    (void)mp_scene_hero_rule_step(&clock, now++, &r, goal);
    ut_check(clock.still == 90u, "a goal that crept by 0.2 u is the same goal, the clock runs on");
    moved[0] += 0.2f;
    r = walking_to(moved, START, false);
    (void)mp_scene_hero_rule_step(&clock, now++, &r, goal);
    ut_check(clock.still == 0u,
             "and once it stands 0.4 u from where the clock began it is a new one");

    (void)mp_scene_hero_rule_step(&clock, now++, &r, goal);
    r.actor = OTHER_ACTOR;
    (void)mp_scene_hero_rule_step(&clock, now++, &r, goal);
    ut_check(clock.actor == OTHER_ACTOR && clock.still == 0u,
             "another actor starts a clock of its own");
    ut_check(mp_scene_hero_rule_step(&clock, now++, NULL, goal) == MP_SCENE_HERO_IDLE &&
                 clock.actor == 0u && !clock.walked,
             "and no actor at all forgets everything");
}

/* A waypoint whose way is blocked from its first tick: the walk goes round from the start, arrives
 * at a detour point, goes round again, and no reading is ever its own goal. Nothing is put; the
 * walking substeps are counted and every 96 of them in a row are named. */
static void check_detours_from_the_start_of_a_waypoint(void)
{
    mp_scene_hero_clock_t   clock;
    mp_scene_hero_reading_t r;
    mp_scene_hero_verdict_t v;
    float                   goal[3] = { 0.0f, 0.0f, 0.0f };
    float                   detour[3];
    uint32_t                now = 30000u;
    uint32_t                walked = 0u;
    uint32_t                blind = 0u;
    uint32_t                lost_at = 0u;
    uint32_t                lost = 0u;
    uint32_t                puts = 0u;
    uint32_t                i;

    ut_section("detour after detour from the start of a waypoint: counted, named, never put");
    mp_scene_hero_rule_reset(&clock);
    for (i = 0u; i < 200u; ++i) {
        memcpy(detour, DETOUR, sizeof detour);
        detour[1] += (float)(i / 20u) * 0.5f;            /* a detour point of its own a round */
        r = walking_to(detour, START, i % 20u != 19u);   /* 19 ticks on the way, one arrived */
        v = mp_scene_hero_rule_step(&clock, now++, &r, goal);
        ++walked;
        blind += v == MP_SCENE_HERO_BLIND || v == MP_SCENE_HERO_LOST ? 1u : 0u;
        puts += v == MP_SCENE_HERO_PUT ? 1u : 0u;
        if (v == MP_SCENE_HERO_LOST) {
            lost_at = lost_at == 0u ? walked : lost_at;
            ++lost;
        }
    }
    ut_check(puts == 0u && !clock.goal_known,
             "200 walking substeps with no goal of its own read: nothing is put anywhere");
    ut_checkf(blind == 200u, "every one of them is counted as walking with no goal read: %u",
              blind);
    ut_checkf(lost_at == MP_SCENE_HERO_STILL_SUBSTEPS && lost == 2u,
              "and the stretch is named at 96 and again at 192: first %u, %u time(s)", lost_at,
              lost);
    ut_check(clock.longest_blind == MP_SCENE_HERO_STILL_SUBSTEPS,
             "the longest stretch is the 96 it was named at");
    r = walking_to(GOAL, START, false);
    v = mp_scene_hero_rule_step(&clock, now++, &r, goal);
    ut_check(v == MP_SCENE_HERO_WALKING && clock.goal_known && clock.blind == 0u,
             "the first reading of its own goal ends the stretch and starts the clock");
}

static void check_walked_lately(void)
{
    mp_scene_hero_clock_t   clock;
    mp_scene_hero_reading_t r = walking_to(GOAL, START, false);
    float                   goal[3] = { 0.0f, 0.0f, 0.0f };

    ut_section("a walking wish is remembered for 32 substeps");
    mp_scene_hero_rule_reset(&clock);
    ut_check(!mp_scene_hero_rule_walked_lately(&clock, 100u), "a reset clock walked never");
    (void)mp_scene_hero_rule_step(&clock, 100u, &r, goal);
    ut_check(mp_scene_hero_rule_walked_lately(&clock, 100u) &&
                 mp_scene_hero_rule_walked_lately(&clock, 131u),
             "a walk at 100 is remembered through 131");
    ut_check(!mp_scene_hero_rule_walked_lately(&clock, 132u), "and forgotten at 132");
    r.move_speed = 0.0f;
    (void)mp_scene_hero_rule_step(&clock, 140u, &r, goal);
    ut_check(!mp_scene_hero_rule_walked_lately(&clock, 140u), "a turn on the spot is no walk");
}

int main(void)
{
    check_what_a_walking_wish_is();
    check_progress_and_standing_still();
    check_detours_coming_and_going();
    check_the_arrival_at_a_detour_point();
    check_a_put_while_a_detour_is_flagged();
    check_what_starts_the_clock_again();
    check_detours_from_the_start_of_a_waypoint();
    check_walked_lately();

    return ut_summary("mp_scene_hero_rule");
}
