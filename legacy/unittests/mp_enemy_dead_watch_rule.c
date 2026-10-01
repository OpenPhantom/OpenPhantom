/* mp_enemy_dead_watch_rule.c: when a dead enemy stood too long, and when a corpse was moved.
 *
 * The watch is a measurement, and a measurement that is wrong in the quiet direction is worse
 * than none: a limit off by a substep, a run that closes on the wrong sample or a corpse's own
 * last Face taken for another hand would each report a clean run over a broken one, or the other
 * way round. So every exit of a run is driven here, the limit at its boundary, a life that bursts,
 * a corpse dialogue_anim_fix stood up as it did in the field, and the lives that must never count.
 */
#include "unittest.h"

#include "mp_enemy_dead_watch_rule.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define SUBSTEP (1.0f / 32.0f)

/* A body standing and alive, at world time `now`, census `census`. */
static mp_dead_watch_sample_t alive(float now, uint32_t census, uint8_t state)
{
    mp_dead_watch_sample_t s;

    memset(&s, 0, sizeof s);
    s.now    = now;
    s.census = census;
    s.health = 40;
    s.state  = state;
    s.clip   = 9;
    s.drawn  = true;
    s.solid  = true;
    return s;
}

/* The same body dead, still drawn and solid. */
static mp_dead_watch_sample_t dead(float now, uint32_t census, uint8_t state)
{
    mp_dead_watch_sample_t s = alive(now, census, state);

    s.health = -3;
    return s;
}

static void only_a_fall_from_above_zero(void)
{
    mp_dead_watch_life_t   life;
    mp_dead_watch_sample_t s;
    uint32_t               events = 0;
    uint32_t               census;

    ut_section("a fall is only one from above zero");
    mp_dead_watch_begin(&life, 1);
    s = dead(1.0f, 1, 1);
    ut_check(mp_dead_watch_step(&life, &s) == 0u,
             "a life first seen with no health left has not fallen here");
    s = dead(20.0f, 600, 1);
    ut_check(mp_dead_watch_step(&life, &s) == 0u && !life.fell,
             "and it never falls, however long it stands");

    mp_dead_watch_begin(&life, 1);
    for (census = 1; census <= 1000u; ++census) {
        s = alive((float)census * SUBSTEP, census, 1);
        s.health = 0;
        events |= mp_dead_watch_step(&life, &s);
    }
    ut_check(events == 0u && !life.was_up,
             "a placement authored with no hit points, standing for 31 s, never counts");
    ut_check(!mp_dead_watch_needs_body(&life, 0),
             "and its body is never read, because it cannot fall");
}

static void the_deaths_that_close_at_once(void)
{
    mp_dead_watch_life_t   life;
    mp_dead_watch_sample_t s;
    uint32_t               events;

    ut_section("the engine's own deaths");
    mp_dead_watch_begin(&life, 1);
    s = alive(0.0f, 1, 1);
    (void)mp_dead_watch_step(&life, &s);
    ut_check(!mp_dead_watch_needs_body(&life, 40), "a live body is not read");
    ut_check(mp_dead_watch_needs_body(&life, -3), "the body is read on the sample of the fall");
    s = dead(SUBSTEP, 2, 11);
    events = mp_dead_watch_step(&life, &s);
    ut_check(events == (MP_DEAD_WATCH_FELL | MP_DEAD_WATCH_CLOSED),
             "a death the epilogue sends to state 11 falls and closes on the same sample");
    ut_check(life.end == MP_DEAD_WATCH_DEATH_STATE, "and it ended in a death state");
    ut_near(mp_dead_watch_stood_seconds(&life), 0.0, 1e-6, "having stood for no time at all");
    ut_check(mp_dead_watch_bucket(mp_dead_watch_stood_seconds(&life)) == 0u,
             "which is the first bucket, within 2 s");
    ut_check(!mp_dead_watch_needs_body(&life, -3), "a closed run's body is not read again");

    mp_dead_watch_begin(&life, 2);
    s = alive(0.0f, 1, 10);
    (void)mp_dead_watch_step(&life, &s);
    s = dead(SUBSTEP, 2, 1);
    s.script_death = true;
    ut_check(mp_dead_watch_step(&life, &s) == MP_DEAD_WATCH_FELL,
             "a sabre death the guard owns falls into state 1 and stays open");
    s = dead(3.0f, 97, 1);
    s.script_death = true;
    s.guard_fired  = true;
    ut_check(mp_dead_watch_step(&life, &s) == 0u, "its script plays the death clip for 3 s");
    s = dead(3.0f + SUBSTEP, 98, 1);
    s.solid = false;
    ut_check(mp_dead_watch_step(&life, &s) == MP_DEAD_WATCH_CLOSED &&
                 life.end == MP_DEAD_WATCH_UNSOLID,
             "the death clip's own event takes the collision, and the run closes unsolid");
    ut_near(mp_dead_watch_stood_seconds(&life), 3.0 - SUBSTEP, 1e-4,
            "it stood to its last sample standing, 3 s after the fall");
    ut_check(mp_dead_watch_stood_censuses(&life) == 95u, "which is 95 censuses after the fall");
    ut_check(mp_dead_watch_bucket(mp_dead_watch_stood_seconds(&life)) == 1u,
             "the second bucket, within 5 s");
}

/* A run open from world time 0 to `until`, sampled every substep. */
static uint32_t stand_until(mp_dead_watch_life_t *life, float until, uint8_t state,
                            uint32_t *census, bool *passed_once)
{
    uint32_t events = 0;
    uint32_t passed = 0;
    float    now;

    for (now = SUBSTEP * 2.0f; now <= until + 1e-4f; now += SUBSTEP) {
        mp_dead_watch_sample_t s = dead(now, ++*census, state);
        uint32_t               e = mp_dead_watch_step(life, &s);

        passed += (e & MP_DEAD_WATCH_PASSED_LIMIT) != 0u ? 1u : 0u;
        events |= e;
    }
    *passed_once = passed == 1u;
    return events;
}

static void the_limit(void)
{
    mp_dead_watch_life_t   life;
    mp_dead_watch_sample_t s;
    uint32_t               census = 1;
    bool                   once = false;

    ut_section("the limit, 12 s of world time");
    mp_dead_watch_begin(&life, 1);
    s = alive(0.0f, census, 1);
    (void)mp_dead_watch_step(&life, &s);
    s = dead(SUBSTEP, ++census, 1);
    (void)mp_dead_watch_step(&life, &s);
    (void)stand_until(&life, SUBSTEP + 12.0f, 1, &census, &once);
    ut_check(!life.over, "a run that has stood exactly 12 s has not passed the limit");
    s = dead(SUBSTEP + 12.0f + SUBSTEP, ++census, 1);
    ut_check(mp_dead_watch_step(&life, &s) == MP_DEAD_WATCH_PASSED_LIMIT && life.over,
             "one substep more passes it, on that sample");
    s = dead(40.0f, ++census, 1);
    ut_check(mp_dead_watch_step(&life, &s) == 0u, "and it passes it once, not on every sample");
    ut_check(mp_dead_watch_phase(life.state, false) == MP_DEAD_WATCH_PHASE_UNDEAD,
             "dead in the script's state with no script death is the undead of the wall bounce");
    s = dead(40.0f + SUBSTEP, ++census, 13);
    ut_check(mp_dead_watch_step(&life, &s) == MP_DEAD_WATCH_CLOSED &&
                 mp_dead_watch_bucket(mp_dead_watch_stood_seconds(&life)) == 3u,
             "closing after 40 s puts it in the last bucket, later than 12 s");

    ut_section("where a run stood when it passed");
    ut_check(mp_dead_watch_phase(7, true) == MP_DEAD_WATCH_PHASE_LANDING,
             "state 7 is the landing, which tests no health");
    ut_check(mp_dead_watch_phase(1, true) == MP_DEAD_WATCH_PHASE_SCRIPTED,
             "state 1 with the script's death is a death begun and not finished");
    ut_check(mp_dead_watch_phase(1, false) == MP_DEAD_WATCH_PHASE_UNDEAD,
             "state 1 without it is an undead nobody will end");
    ut_check(mp_dead_watch_phase(9, false) == MP_DEAD_WATCH_PHASE_OTHER &&
                 mp_dead_watch_phase(15, true) == MP_DEAD_WATCH_PHASE_OTHER,
             "a get-up or the zapped state is another state");

    ut_section("the buckets at their edges");
    ut_check(mp_dead_watch_bucket(0.0f) == 0u && mp_dead_watch_bucket(2.0f) == 0u,
             "0 and 2 s are within 2 s");
    ut_check(mp_dead_watch_bucket(2.001f) == 1u && mp_dead_watch_bucket(5.0f) == 1u,
             "past 2 s up to 5 s is the second");
    ut_check(mp_dead_watch_bucket(5.001f) == 2u && mp_dead_watch_bucket(12.0f) == 2u,
             "past 5 s up to 12 s is the third");
    ut_check(mp_dead_watch_bucket(12.001f) == 3u, "past 12 s is later");
    ut_check(mp_dead_watch_bucket(-1.0f) == 0u && mp_dead_watch_bucket(nanf("")) == 0u,
             "a negative or unreadable span is the first, never the one that means a failure");
}

static void the_exits(void)
{
    mp_dead_watch_life_t   life;
    mp_dead_watch_sample_t s;

    ut_section("every way a run ends");
    s = dead(1.0f, 1, 1);
    s.drawn = false;
    ut_check(mp_dead_watch_down(&s) == MP_DEAD_WATCH_HIDDEN, "a body no longer drawn is down");
    s = dead(1.0f, 1, 1);
    s.solid = false;
    ut_check(mp_dead_watch_down(&s) == MP_DEAD_WATCH_UNSOLID, "a body no longer solid is down");
    s = dead(1.0f, 1, 14);
    s.drawn = false;
    ut_check(mp_dead_watch_down(&s) == MP_DEAD_WATCH_DEATH_STATE,
             "a death state names the end before the body's bits do");
    s = dead(1.0f, 1, 10);
    ut_check(mp_dead_watch_down(&s) == MP_DEAD_WATCH_STILL_STANDING,
             "state 10, drawn and solid, still stands");

    mp_dead_watch_begin(&life, 1);
    s = alive(0.0f, 1, 6);
    (void)mp_dead_watch_step(&life, &s);
    s = dead(SUBSTEP, 2, 7);
    (void)mp_dead_watch_step(&life, &s);
    s = alive(2.0f, 66, 1);
    ut_check(mp_dead_watch_step(&life, &s) == MP_DEAD_WATCH_CLOSED &&
                 life.end == MP_DEAD_WATCH_REVIVED,
             "health rising above zero again closes the run as revived");
    s = dead(3.0f, 98, 1);
    ut_check(mp_dead_watch_step(&life, &s) == 0u && !life.standing,
             "a second fall of the same life opens no second run");

    mp_dead_watch_begin(&life, 1);
    s = alive(0.0f, 1, 1);
    (void)mp_dead_watch_step(&life, &s);
    s = dead(SUBSTEP, 2, 1);
    (void)mp_dead_watch_step(&life, &s);
    s = dead(5.0f, 161, 1);
    (void)mp_dead_watch_step(&life, &s);
    ut_check(mp_dead_watch_gone(&life) == MP_DEAD_WATCH_CLOSED &&
                 life.end == MP_DEAD_WATCH_REMOVED,
             "a standing corpse that leaves the census closes as removed");
    ut_near(mp_dead_watch_stood_seconds(&life), 5.0 - SUBSTEP, 1e-4,
            "at its last sample standing, not at the moment the removal was noticed");
    ut_check(mp_dead_watch_gone(&life) == 0u, "and a second notice of it closes nothing");
    mp_dead_watch_begin(&life, 1);
    ut_check(mp_dead_watch_gone(&life) == 0u,
             "a life that never fell leaves with nothing to close");

    mp_dead_watch_begin(&life, 1);
    s = alive(100.0f, 1, 1);
    (void)mp_dead_watch_step(&life, &s);
    s = dead(100.0f + SUBSTEP, 2, 1);
    (void)mp_dead_watch_step(&life, &s);
    s = dead(50.0f, 3, 1);
    ut_check(mp_dead_watch_step(&life, &s) == 0u && mp_dead_watch_stood_seconds(&life) == 0.0f,
             "a clock that went back, a restored game, stands for no time and passes nothing");
}

static void a_life_that_bursts(void)
{
    mp_dead_watch_life_t   life;
    mp_dead_watch_sample_t s;

    ut_section("a life that bursts");
    mp_dead_watch_begin(&life, 3);
    s = alive(0.0f, 1, 1);
    (void)mp_dead_watch_step(&life, &s);
    s = dead(SUBSTEP, 2, 1);
    s.script_death = true;
    (void)mp_dead_watch_step(&life, &s);
    s = dead(0.5f, 17, 13);
    ut_check(mp_dead_watch_step(&life, &s) == MP_DEAD_WATCH_CLOSED &&
                 life.end == MP_DEAD_WATCH_DEATH_STATE,
             "a script's burst enters state 13 and closes the run there");
    s = dead(0.5f + SUBSTEP, 18, 13);
    s.drawn = false;
    ut_check(mp_dead_watch_step(&life, &s) == 0u,
             "the pieces flying hide the body, which closes nothing twice");
    ut_check(mp_dead_watch_gone(&life) == 0u, "and its removal closes nothing either");

    mp_dead_watch_begin(&life, 3);
    s = alive(0.0f, 1, 1);
    (void)mp_dead_watch_step(&life, &s);
    s = dead(SUBSTEP, 2, 4);
    (void)mp_dead_watch_step(&life, &s);
    s = dead(2.0f * SUBSTEP, 3, 4);
    s.drawn = false;
    ut_check(mp_dead_watch_step(&life, &s) == MP_DEAD_WATCH_CLOSED &&
                 life.end == MP_DEAD_WATCH_HIDDEN,
             "a body shattered straight off its hit loses bit 0 and closes as hidden");
}

static void a_corpse_another_hand_stood_up(void)
{
    mp_dead_watch_life_t   life;
    mp_dead_watch_sample_t s;
    uint32_t               census = 1;
    uint32_t               events;

    ut_section("a corpse another hand stood up");
    /* Placement 15 as a field run logged it: thrown, killed by the sabre, laid down on clip 21,
     * and half a second later put on clip 0, its stand, by dialogue_anim_fix. */
    mp_dead_watch_begin(&life, 1);
    s = alive(0.0f, census, 7);
    s.clip = 13;
    (void)mp_dead_watch_step(&life, &s);
    s = dead(SUBSTEP, ++census, 14);
    s.clip = 13;
    events = mp_dead_watch_step(&life, &s);
    ut_check((events & (MP_DEAD_WATCH_CORPSE_HELD | MP_DEAD_WATCH_CORPSE_MOVED)) == 0u,
             "the first substep in state 14 fixes nothing: the script may still play its Face");
    s = dead(2.0f * SUBSTEP, ++census, 14);
    s.clip = 21;
    events = mp_dead_watch_step(&life, &s);
    ut_check(events == MP_DEAD_WATCH_CORPSE_HELD && life.corpse_clip == 21u,
             "the second holds the corpse's clip, 21, even though it changed from 13");
    s = dead(1.0f, ++census, 14);
    s.clip = 21;
    ut_check(mp_dead_watch_step(&life, &s) == 0u, "the corpse lying on 21 is nothing to report");
    s = dead(1.5f, ++census, 14);
    s.clip = 0;
    ut_check(mp_dead_watch_step(&life, &s) == MP_DEAD_WATCH_CORPSE_MOVED &&
                 life.corpse_moved_to == 0u,
             "clip 0 put on the corpse is another hand's, from 21 to 0");
    ut_near(life.corpse_moved_at - life.corpse_at, 1.5 - SUBSTEP, 1e-4,
            "1.5 s after it lay down");
    s = dead(2.0f, ++census, 14);
    s.clip = 21;
    ut_check(mp_dead_watch_step(&life, &s) == 0u, "a corpse is counted once, however often after");

    mp_dead_watch_begin(&life, 2);
    s = dead(10.0f, ++census, 14);
    s.clip = 5;
    (void)mp_dead_watch_step(&life, &s);
    s = alive(10.0f + SUBSTEP, ++census, 1);
    s.clip = 0;
    (void)mp_dead_watch_step(&life, &s);
    ut_check(!life.corpse_moved && life.corpse_samples == 1u,
             "a body that leaves state 14 after one substep was never a corpse to compare");

    ut_section("a second life of the same key");
    mp_dead_watch_begin(&life, 3);
    ut_check(life.known && life.generation == 3u && !life.fell && !life.corpse_moved &&
                 life.corpse_samples == 0u,
             "begins with nothing of the last one");
}

/* A client's replica: the reaction state is the host's word and ends nothing; the body does. */
static mp_dead_watch_sample_t replica(float now, uint32_t census, uint8_t state, int32_t health)
{
    mp_dead_watch_sample_t s = alive(now, census, state);

    s.health  = health;
    s.by_body = true;
    return s;
}

static void a_replica_judged_by_its_body(void)
{
    mp_dead_watch_life_t   life;
    mp_dead_watch_sample_t s;
    uint32_t               census = 1;
    uint32_t               events = 0;
    float                  now;

    ut_section("a client's replica is judged by its body alone");
    mp_dead_watch_begin(&life, 1);
    s = replica(0.0f, census, 1, 30);
    (void)mp_dead_watch_step(&life, &s);
    s = replica(SUBSTEP, ++census, 13, -4);
    ut_check(mp_dead_watch_step(&life, &s) == MP_DEAD_WATCH_FELL && life.standing,
             "the host reports state 13 and the replica stands drawn and solid: the run opens "
             "and the death state does not close it");
    for (now = 2.0f * SUBSTEP; now < 12.0f + 4.0f * SUBSTEP; now += SUBSTEP) {
        s = replica(now, ++census, 14, -4);
        s.clip = (census & 1u) != 0u ? 21u : 0u;
        events |= mp_dead_watch_step(&life, &s);
    }
    ut_check((events & MP_DEAD_WATCH_PASSED_LIMIT) != 0u && life.standing,
             "a replica standing solid in the corpse state past 12 s passes the limit, the "
             "divergence the host's watch cannot see");
    ut_check((events & (MP_DEAD_WATCH_CORPSE_HELD | MP_DEAD_WATCH_CORPSE_MOVED)) == 0u &&
                 life.corpse_samples == 0u,
             "and the corpse's clip is left to the host's watch");
    s = replica(now, ++census, 14, -4);
    s.solid = false;
    ut_check(mp_dead_watch_step(&life, &s) == MP_DEAD_WATCH_CLOSED &&
                 life.end == MP_DEAD_WATCH_UNSOLID,
             "the class written away by the host's body closes it as no longer solid");

    mp_dead_watch_begin(&life, 2);
    s = replica(0.0f, 1, 1, 30);
    (void)mp_dead_watch_step(&life, &s);
    s = replica(0.1f, 2, 1, -1);
    (void)mp_dead_watch_step(&life, &s);
    s = replica(0.5f, 3, 1, -1);
    (void)mp_dead_watch_step(&life, &s);
    s = replica(0.5f + SUBSTEP, 4, 1, -1);
    s.drawn = false;
    ut_check(mp_dead_watch_step(&life, &s) == MP_DEAD_WATCH_CLOSED &&
                 life.end == MP_DEAD_WATCH_HIDDEN,
             "a replica that bursts here loses bit 0 and closes as no longer drawn");
    ut_near(mp_dead_watch_stood_seconds(&life), 0.4, 1e-4,
            "after the 0.4 s a droid stands before its burst, which the host's body had too");
    ut_check(mp_dead_watch_bucket(mp_dead_watch_stood_seconds(&life)) == 0u,
             "and that falls in the first bucket of the distribution");
}

int main(void)
{
    only_a_fall_from_above_zero();
    the_deaths_that_close_at_once();
    the_limit();
    the_exits();
    a_life_that_bursts();
    a_corpse_another_hand_stood_up();
    a_replica_judged_by_its_body();
    return ut_summary("mp_enemy_dead_watch_rule");
}
