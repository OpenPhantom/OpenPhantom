/* The render timeline, driven over its edges with no game and no clock.
 *
 * What has to hold is about sequences: the warm-up ends at the target lag and not before, a
 * steady stream advances the render by exactly one tick per substep, a lost sample changes
 * nothing visible, a rate difference between the two ladders is absorbed by slews alone and never
 * by a jump, a sender that stops halts the render at its last sample and is resumed with one
 * counted resync, a receiver that stops is rebased once, a reordered arrival is ignored, and a
 * streak that starts inside the cooldown does not count.
 *
 * The measured target lag is the same kind of claim about sequences, and it is the reason the
 * arithmetic is worth having in one place with no game around it: an even stream reaches the
 * floor, and only after the window; a stream with gaps holds the target above it; a stream that
 * turns rough is given room in the substep that measures the gap; a stream that calms down waits
 * seconds for each tick back; and a stream whose gaps recur inside the window holds its target
 * instead of pumping between two values, which is the check the hysteresis exists for.
 */
#include "unittest.h"

#include "mp_timeline.h"

#include <stdbool.h>
#include <stdint.h>

/* One substep: the tick arrives, then the advance. */
static bool step(mp_timeline_t *t, uint32_t tick)
{
    mp_timeline_note_received(t, tick);
    return mp_timeline_advance(t);
}

static void check_warm_up(void)
{
    mp_timeline_t t;

    ut_section("the warm-up");
    mp_timeline_init(&t, 3u);
    ut_check(!mp_timeline_advance(&t), "with nothing received there is nothing to advance");
    ut_check(!mp_timeline_render_known(&t), "and no render tick");
    ut_check(!step(&t, 100u), "the first tick alone is not a render tick");
    ut_check(!step(&t, 101u) && !step(&t, 102u), "nor are the second and third");
    ut_check(step(&t, 103u), "the fourth puts the render a target lag behind the newest");
    ut_check(mp_timeline_render_known(&t) && mp_timeline_render_tick(&t) == 100u &&
                 mp_timeline_render_phase8(&t) == 0u,
             "at tick 100, phase zero");
    ut_check(mp_timeline_lag(&t) == 3, "with a lag of exactly three");
    ut_check(step(&t, 104u) && mp_timeline_render_tick(&t) == 101u,
             "and the next substep advances it by one");

    mp_timeline_init(&t, 0u);
    ut_check(t.target_lag == MP_TIMELINE_TARGET_MIN, "a target below the floor is clamped to it");
    mp_timeline_init(&t, 9u);
    ut_check(t.target_lag == MP_TIMELINE_TARGET_MAX, "and one above the ceiling to that");

    ut_section("the warm-up across the counter's wrap");
    mp_timeline_init(&t, 3u);
    (void)step(&t, 0xFFFFFFFEu);
    (void)step(&t, 0xFFFFFFFFu);
    (void)step(&t, 0u);
    ut_check(step(&t, 1u) && mp_timeline_render_tick(&t) == 0xFFFFFFFEu,
             "the render tick lands before the wrap while the newest is past it");
    ut_check(step(&t, 2u) && mp_timeline_render_tick(&t) == 0xFFFFFFFFu, "and advances into it");
    ut_check(step(&t, 3u) && mp_timeline_render_tick(&t) == 0u && mp_timeline_lag(&t) == 3,
             "and across it, the lag read as three the whole way");
}

static void check_steady_stream(void)
{
    mp_timeline_t t;
    uint32_t      tick;
    uint32_t      steps_of_one = 0u;

    ut_section("a steady stream");
    mp_timeline_init(&t, 3u);
    for (tick = 1u; tick <= 4u; ++tick) {
        (void)step(&t, tick);
    }
    for (tick = 5u; tick <= 1004u; ++tick) {
        uint32_t before = mp_timeline_render_tick(&t);

        if (step(&t, tick) && mp_timeline_render_tick(&t) == before + 1u &&
            mp_timeline_render_phase8(&t) == 0u) {
            ++steps_of_one;
        }
    }
    ut_check(steps_of_one == 1000u,
             "a thousand substeps with one arrival each advance the render by exactly one each");
    ut_check(mp_timeline_lag(&t) == 3, "and the lag never leaves the target");
    ut_check(mp_timeline_inserted(&t) == 0u && mp_timeline_skipped(&t) == 0u &&
                 mp_timeline_resyncs(&t) == 0u && mp_timeline_halts(&t) == 0u,
             "with no slew, no resync and no halt");
}

static void check_lost_sample(void)
{
    mp_timeline_t t;
    uint32_t      tick;
    uint32_t      before;

    ut_section("one lost sample");
    mp_timeline_init(&t, 3u);
    for (tick = 1u; tick <= 20u; ++tick) {
        (void)step(&t, tick);
    }
    before = mp_timeline_render_tick(&t);
    ut_check(mp_timeline_advance(&t) && mp_timeline_render_tick(&t) == before + 1u,
             "the substep that receives nothing still advances the render by one");
    ut_check(mp_timeline_lag(&t) == 2, "the lag dips by one");
    ut_check(step(&t, 22u) && mp_timeline_render_tick(&t) == before + 2u &&
                 mp_timeline_lag(&t) == 3,
             "and the next arrival brings it back");
    for (tick = 23u; tick <= 60u; ++tick) {
        (void)step(&t, tick);
    }
    ut_check(mp_timeline_inserted(&t) == 0u && mp_timeline_skipped(&t) == 0u &&
                 mp_timeline_resyncs(&t) == 0u && mp_timeline_halts(&t) == 0u,
             "and nothing else happens because of it");
}

/* Ten minutes of the two ladders at different rates: the sender at one rate producing ticks,
 * the receiver at the other running substeps, every sender tick delivered in the first receiver
 * substep at or after its time. */
static void run_drift(double sender_hz, double receiver_hz, mp_timeline_t *t)
{
    double   sender_period = 1.0 / sender_hz;
    double   receiver_period = 1.0 / receiver_hz;
    double   now = 0.0;
    double   next_tick_at = sender_period;
    uint32_t tick = 0u;
    uint32_t substep;
    uint32_t substeps = (uint32_t)(600.0 * receiver_hz);

    mp_timeline_init(t, 3u);
    for (substep = 0u; substep < substeps; ++substep) {
        now += receiver_period;
        while (next_tick_at <= now) {
            ++tick;
            mp_timeline_note_received(t, tick);
            next_tick_at += sender_period;
        }
        (void)mp_timeline_advance(t);
    }
}

static void check_drift(void)
{
    mp_timeline_t t;

    ut_section("drift: the sender at 32.1 Hz against a receiver at 31.9 Hz for ten minutes");
    run_drift(32.1, 31.9, &t);
    ut_checkf(mp_timeline_skipped(&t) > 100u && mp_timeline_skipped(&t) < 140u,
              "the surplus of about 120 ticks is absorbed by %u skip slews",
              (unsigned)mp_timeline_skipped(&t));
    ut_check(mp_timeline_inserted(&t) == 0u, "and no insert");
    ut_check(mp_timeline_resyncs(&t) == 0u && mp_timeline_halts(&t) == 0u,
             "never by a resync or a halt");
    ut_checkf(mp_timeline_lag(&t) >= 2 && mp_timeline_lag(&t) <= 5,
              "and the lag stays near the target, %d at the end", (int)mp_timeline_lag(&t));

    ut_section("drift the other way: the sender at 31.9 Hz against a receiver at 32.1 Hz");
    run_drift(31.9, 32.1, &t);
    ut_checkf(mp_timeline_inserted(&t) > 100u && mp_timeline_inserted(&t) < 140u,
              "the shortfall of about 120 ticks is absorbed by %u insert slews",
              (unsigned)mp_timeline_inserted(&t));
    ut_check(mp_timeline_skipped(&t) == 0u, "and no skip");
    ut_check(mp_timeline_resyncs(&t) == 0u && mp_timeline_halts(&t) == 0u,
             "never by a resync or a halt");
    ut_checkf(mp_timeline_lag(&t) >= 1 && mp_timeline_lag(&t) <= 4,
              "and the lag stays near the target, %d at the end", (int)mp_timeline_lag(&t));
}

static void check_sender_stall(void)
{
    mp_timeline_t t;
    uint32_t      tick;
    uint32_t      substep;
    uint32_t      last;

    ut_section("a three second stall of the sender");
    mp_timeline_init(&t, 3u);
    for (tick = 1u; tick <= 50u; ++tick) {
        (void)step(&t, tick);
    }
    last = mp_timeline_render_tick(&t);
    ut_check(mp_timeline_advance(&t) && mp_timeline_advance(&t) && mp_timeline_advance(&t),
             "three substeps without arrivals still advance, eating the lag");
    ut_check(mp_timeline_render_tick(&t) == 50u && mp_timeline_lag(&t) == 0,
             "and the render reaches the newest sample");
    ut_check(!mp_timeline_advance(&t), "the next substep halts rather than pass it");
    ut_check(mp_timeline_halted(&t) && mp_timeline_halts(&t) == 1u, "counted once");
    for (substep = 0u; substep < 92u; ++substep) {
        (void)mp_timeline_advance(&t);
    }
    ut_check(mp_timeline_render_tick(&t) == 50u && mp_timeline_halts(&t) == 1u,
             "and stays halted at that sample for the rest of the stall, still counted once");
    ut_check(last == 47u, "(the render was at 47 before the stall)");

    ut_check(!step(&t, 51u) && !step(&t, 52u),
             "when the sender resumes the render waits until a whole target lag is back");
    ut_check(step(&t, 53u) && mp_timeline_render_tick(&t) == 50u,
             "at a lag of three it resumes without moving");
    ut_check(!mp_timeline_halted(&t) && mp_timeline_resyncs(&t) == 1u,
             "and the resumption is the one resync");
    ut_check(step(&t, 54u) && mp_timeline_render_tick(&t) == 51u,
             "then it advances one per substep again");

    ut_section("the sender resumes with a burst");
    mp_timeline_init(&t, 3u);
    for (tick = 1u; tick <= 50u; ++tick) {
        (void)step(&t, tick);
    }
    for (substep = 0u; substep < 96u; ++substep) {
        (void)mp_timeline_advance(&t);
    }
    ut_check(mp_timeline_halted(&t), "halted at the newest sample");
    mp_timeline_note_received(&t, 51u);
    mp_timeline_note_received(&t, 52u);
    mp_timeline_note_received(&t, 53u);
    mp_timeline_note_received(&t, 54u);
    ut_check(mp_timeline_advance(&t) && mp_timeline_render_tick(&t) == 50u &&
                 mp_timeline_lag(&t) == 4,
             "four ticks in one substep resume the render where it stood, a lag of four");
    ut_check(mp_timeline_resyncs(&t) == 1u, "counted as the one resync");

    ut_section("a three second stall of the receiver");
    mp_timeline_init(&t, 3u);
    for (tick = 1u; tick <= 50u; ++tick) {
        (void)step(&t, tick);
    }
    for (tick = 51u; tick <= 146u; ++tick) {
        mp_timeline_note_received(&t, tick);   /* the ticks arrive, no substep runs */
    }
    ut_check(mp_timeline_advance(&t) && mp_timeline_render_tick(&t) == 143u,
             "the first substep after it rebases the render a target lag behind the newest");
    ut_check(mp_timeline_resyncs(&t) == 1u && mp_timeline_halts(&t) == 0u,
             "one resync, no halt");
    ut_check(step(&t, 147u) && mp_timeline_render_tick(&t) == 144u, "and it runs on from there");
}

static void check_reorder(void)
{
    mp_timeline_t t;
    uint32_t      tick;

    ut_section("a reordered arrival");
    mp_timeline_init(&t, 3u);
    for (tick = 1u; tick <= 10u; ++tick) {
        (void)step(&t, tick);
    }
    mp_timeline_note_received(&t, 12u);
    mp_timeline_note_received(&t, 11u);
    ut_check(mp_timeline_newest_tick(&t) == 12u, "an older tick after a newer one is ignored");
    mp_timeline_note_received(&t, 12u);
    ut_check(mp_timeline_newest_tick(&t) == 12u, "and so is a duplicate");
    mp_timeline_note_received(&t, 5u);
    ut_check(mp_timeline_newest_tick(&t) == 12u, "and a straggler from long ago");
    ut_check(mp_timeline_advance(&t) && mp_timeline_render_tick(&t) == 8u,
             "the render advances by one as always");
}

/* The stream both halves of this file are built from: one sender tick per own substep, which is
 * what the two ladders produce when neither of them slips. */
static void even_stream(mp_timeline_t *t, uint32_t *tick, uint32_t substeps)
{
    uint32_t substep;

    for (substep = 0u; substep < substeps; ++substep) {
        ++*tick;
        (void)step(t, *tick);
    }
}

static void check_slew_and_cooldown(void)
{
    mp_timeline_t t;
    uint32_t      tick = 0u;
    uint32_t      substep;
    uint32_t      phases_seen = 0u;

    ut_section("a slew, and the cooldown in which streaks do not count");
    mp_timeline_init(&t, 3u);
    even_stream(&t, &tick, 10u);
    ut_check(mp_timeline_lag(&t) == 3, "steady at the target");
    mp_timeline_note_received(&t, ++tick);
    mp_timeline_note_received(&t, ++tick);
    mp_timeline_note_received(&t, ++tick);
    ut_check(mp_timeline_advance(&t) && mp_timeline_lag(&t) == 5,
             "three ticks in one substep: a lag of five, the first substep of a streak");
    even_stream(&t, &tick, 6u);
    ut_check(mp_timeline_skipped(&t) == 0u, "six more substeps at five earn nothing yet");
    even_stream(&t, &tick, 1u);
    ut_check(mp_timeline_skipped(&t) == 1u && mp_timeline_render_phase8(&t) == 1u,
             "the eighth starts a skip slew, whose first step is nine eighths");
    for (substep = 0u; substep < 7u; ++substep) {
        even_stream(&t, &tick, 1u);
        if (mp_timeline_render_phase8(&t) == (substep + 2u) % MP_TIMELINE_EIGHTHS) {
            ++phases_seen;
        }
    }
    ut_check(phases_seen == 7u, "and the phase walks 2, 3, 4, 5, 6, 7, 0 over the next seven");
    ut_check(mp_timeline_lag(&t) == 4, "which brought the lag down by one");

    mp_timeline_note_received(&t, ++tick);
    even_stream(&t, &tick, 1u);
    ut_check(mp_timeline_lag(&t) == 5, "the lag is pushed back to five inside the cooldown");
    even_stream(&t, &tick, 30u);
    ut_check(mp_timeline_skipped(&t) == 1u,
             "thirty substeps at five inside the cooldown earn no second slew");
    even_stream(&t, &tick, 8u);
    ut_check(mp_timeline_skipped(&t) == 1u,
             "nor do the first substeps after it, which the cooldown's tail overlaps");
    even_stream(&t, &tick, 2u);
    ut_check(mp_timeline_skipped(&t) == 2u,
             "eight fresh substeps after the cooldown do");
    ut_check(mp_timeline_inserted(&t) == 0u && mp_timeline_resyncs(&t) == 0u,
             "with nothing else counted");

    ut_section("a lag below the band earns an insert slew");
    mp_timeline_init(&t, 3u);
    tick = 0u;
    even_stream(&t, &tick, 10u);
    (void)mp_timeline_advance(&t);
    (void)mp_timeline_advance(&t);
    ut_check(mp_timeline_lag(&t) == 1,
             "two substeps without arrivals: a lag of one, the first substep of a streak");
    even_stream(&t, &tick, 6u);
    ut_check(mp_timeline_inserted(&t) == 0u, "six more substeps at one earn nothing yet");
    even_stream(&t, &tick, 1u);
    ut_check(mp_timeline_inserted(&t) == 1u && mp_timeline_render_phase8(&t) == 7u,
             "the eighth starts an insert slew, whose first step is seven eighths");
    even_stream(&t, &tick, 7u);
    ut_check(mp_timeline_lag(&t) == 2 && mp_timeline_render_phase8(&t) == 0u,
             "eight substeps later the lag is back up by one");
    ut_check(mp_timeline_halts(&t) == 0u, "and nothing halted on the way");
}

static void check_measured_even_stream(void)
{
    mp_timeline_t t;
    uint32_t      tick = 0u;

    ut_section("the measured target on an even stream");
    mp_timeline_init(&t, 3u);
    ut_check(!mp_timeline_auto_lag(&t) && mp_timeline_target_lag(&t) == 3u,
             "init leaves the regulation off, at the target it was given");

    mp_timeline_set_auto_lag(&t, true);
    even_stream(&t, &tick, MP_TIMELINE_LAG_WINDOW);
    ut_check(mp_timeline_dip(&t) == 0u, "an even stream never dips below its target");
    ut_check(mp_timeline_target_lag(&t) == 3u,
             "and the first window of it gives nothing back, eight seconds of evidence first");
    /* The window is counted from the first MEASURED substep, and the measurement starts once the
     * warm-up has produced a render tick, which costs the target's own worth of substeps first. */
    even_stream(&t, &tick, 4u);
    ut_check(mp_timeline_target_lag(&t) == 3u, "nor do the substeps the warm-up ate");
    even_stream(&t, &tick, 1u);
    ut_check(mp_timeline_target_lag(&t) == 2u, "the substep after the window gives back one tick");
    even_stream(&t, &tick, MP_TIMELINE_LAG_LOCK);
    ut_check(mp_timeline_target_lag(&t) == 2u,
             "and the bar holds it there for a second and a half");
    even_stream(&t, &tick, 1u);
    ut_check(mp_timeline_target_lag(&t) == 1u,
             "then the second comes back, and it is at the floor");
    even_stream(&t, &tick, 600u);
    ut_check(mp_timeline_target_lag(&t) == MP_TIMELINE_TARGET_MIN &&
                 mp_timeline_target_changes(&t) == 2u,
             "and it stops there: two moves in half a minute of an even stream");
    ut_checkf(mp_timeline_lag(&t) >= 1 && mp_timeline_lag(&t) <= 2,
              "the render follows it down to a lag of %d", (int)mp_timeline_lag(&t));
    ut_check(mp_timeline_halts(&t) == 0u && mp_timeline_resyncs(&t) == 0u,
             "with nothing halted and nothing rebased on the way");

    mp_timeline_set_auto_lag(&t, false);
    ut_check(!mp_timeline_auto_lag(&t) && mp_timeline_target_lag(&t) == 3u,
             "switching the regulation off puts the target back to the value init was given");
    even_stream(&t, &tick, 600u);
    ut_check(mp_timeline_target_lag(&t) == 3u && mp_timeline_target_changes(&t) == 2u,
             "and it stays there however even the stream is, with no further move counted");
}

/* The engine's substep ladder runs one to four substeps in a frame, so a sender that hitches
 * delivers its ticks in clumps. This is what that looks like: `n` substeps in which nothing
 * arrives, then one that delivers the whole arrears. The mean rate is unchanged. */
static void clumped_stream(mp_timeline_t *t, uint32_t *tick, uint32_t clump, uint32_t rounds)
{
    uint32_t round;
    uint32_t index;

    for (round = 0u; round < rounds; ++round) {
        for (index = 0u; index + 1u < clump; ++index) {
            (void)mp_timeline_advance(t);
        }
        for (index = 0u; index < clump; ++index) {
            ++*tick;
            mp_timeline_note_received(t, *tick);
        }
        (void)mp_timeline_advance(t);
    }
}

/* The sender genuinely stops for `substeps` and then resumes with the arrears. Unlike a clump,
 * the render really does run out of samples here. */
static void real_stall(mp_timeline_t *t, uint32_t *tick, uint32_t substeps)
{
    uint32_t index;

    for (index = 0u; index < substeps; ++index) {
        (void)mp_timeline_advance(t);
    }
    for (index = 0u; index < substeps; ++index) {
        ++*tick;
        mp_timeline_note_received(t, *tick);
    }
    (void)mp_timeline_advance(t);
}

static void check_measured_clumped_stream(void)
{
    mp_timeline_t t;
    uint32_t      tick = 0u;

    /* This is the check the whole measurement was rebuilt for. Counting arrivals, a clump of four
     * reads as a gap of three and buys three ticks of buffer, which is ninety four milliseconds of
     * delay for jitter that was never felt. Counting the DIP, the same stream asks for nothing:
     * the render is fed the whole way and never once runs out. */
    ut_section("a sender that clumps four substeps at a time costs no buffer at all");
    mp_timeline_init(&t, 3u);
    mp_timeline_set_auto_lag(&t, true);
    even_stream(&t, &tick, 200u);
    clumped_stream(&t, &tick, 4u, 300u);

    ut_checkf(mp_timeline_target_lag(&t) == MP_TIMELINE_TARGET_MIN,
              "the target sits at the floor (%u), not one above the clump",
              (unsigned)mp_timeline_target_lag(&t));
    ut_check(mp_timeline_dip(&t) == 0u, "because the lag never fell below what it was given");
    ut_check(mp_timeline_halts(&t) == 0u,
             "and the render never ran out, which is the proof that the floor was enough");

    ut_section("a sender that clumps two at a time is no different");
    mp_timeline_init(&t, 3u);
    mp_timeline_set_auto_lag(&t, true);
    even_stream(&t, &tick, 200u);
    clumped_stream(&t, &tick, 2u, 600u);
    ut_check(mp_timeline_target_lag(&t) == MP_TIMELINE_TARGET_MIN &&
             mp_timeline_halts(&t) == 0u, "floor, and nothing ran out");
}

static void check_measured_rise_and_decay(void)
{
    mp_timeline_t t;
    uint32_t      tick = 0u;
    uint32_t      raised;

    ut_section("a real stall raises the target, and then gives it back");
    mp_timeline_init(&t, 3u);
    mp_timeline_set_auto_lag(&t, true);
    even_stream(&t, &tick, 400u);
    ut_check(mp_timeline_target_lag(&t) == MP_TIMELINE_TARGET_MIN,
             "twelve seconds of an even stream have taken the target to the floor");

    real_stall(&t, &tick, 4u);
    raised = mp_timeline_target_lag(&t);
    ut_checkf(raised > MP_TIMELINE_TARGET_MIN, "a four substep stall raises it to %u",
              (unsigned)raised);
    ut_checkf(raised <= 4u, "but not to the ceiling: a stall is not worth six ticks (%u)",
              (unsigned)raised);

    even_stream(&t, &tick, MP_TIMELINE_LAG_WINDOW + MP_TIMELINE_LAG_LOCK);
    ut_check(mp_timeline_target_lag(&t) < raised,
             "a window and a lock of calm give some of it back");
    even_stream(&t, &tick, 600u);
    ut_check(mp_timeline_target_lag(&t) == MP_TIMELINE_TARGET_MIN,
             "and twenty seconds of calm are back at the floor");
}

static void check_measured_threshold(void)
{
    mp_timeline_t t;
    uint32_t      tick = 0u;
    uint32_t      round;

    ut_section("a stall that recurs holds the target up rather than pumping");
    mp_timeline_init(&t, 2u);
    mp_timeline_set_auto_lag(&t, true);
    for (round = 0u; round < 20u; ++round) {
        even_stream(&t, &tick, 157u);
        real_stall(&t, &tick, 3u);
    }
    ut_checkf(mp_timeline_target_lag(&t) > MP_TIMELINE_TARGET_MIN,
              "a stall every five seconds keeps the target off the floor (%u)",
              (unsigned)mp_timeline_target_lag(&t));
    ut_checkf(mp_timeline_target_changes(&t) <= 8u,
              "and it settles rather than pumping between two values (%u moves in a hundred "
              "seconds)", (unsigned)mp_timeline_target_changes(&t));
}

static void check_switch_leaves_the_machinery(void)
{
    mp_timeline_t t;
    uint32_t      tick = 0u;
    uint32_t      substep;
    uint32_t      phases_seen = 0u;

    ut_section("switching the regulation during a slew");
    mp_timeline_init(&t, 3u);
    even_stream(&t, &tick, 10u);
    mp_timeline_note_received(&t, ++tick);
    mp_timeline_note_received(&t, ++tick);
    mp_timeline_note_received(&t, ++tick);
    (void)mp_timeline_advance(&t);
    even_stream(&t, &tick, 7u);
    ut_check(mp_timeline_skipped(&t) == 1u && mp_timeline_render_phase8(&t) == 1u,
             "a skip slew is running after eight substeps at a lag of five");

    mp_timeline_set_auto_lag(&t, true);
    mp_timeline_set_auto_lag(&t, false);
    for (substep = 0u; substep < 7u; ++substep) {
        even_stream(&t, &tick, 1u);
        if (mp_timeline_render_phase8(&t) == (substep + 2u) % MP_TIMELINE_EIGHTHS) {
            ++phases_seen;
        }
    }
    ut_check(phases_seen == 7u,
             "switching it on and off again inside the slew leaves the phase walking 2 to 0");
    ut_check(mp_timeline_skipped(&t) == 1u && mp_timeline_lag(&t) == 4,
             "it is the slew it was, and it brought the lag down by one");

    ut_section("switching the regulation while the render is halted");
    mp_timeline_init(&t, 3u);
    tick = 0u;
    even_stream(&t, &tick, 50u);
    for (substep = 0u; substep < 96u; ++substep) {
        (void)mp_timeline_advance(&t);
    }
    ut_check(mp_timeline_halted(&t) && mp_timeline_halts(&t) == 1u,
             "a three second stall of the sender has halted the render");
    mp_timeline_set_auto_lag(&t, true);
    ut_check(mp_timeline_halted(&t) && mp_timeline_target_lag(&t) == 3u,
             "switching the regulation on there changes neither the halt nor the target");
    for (substep = 0u; substep < 64u; ++substep) {
        (void)mp_timeline_advance(&t);
    }
    ut_check(mp_timeline_dip(&t) == 0u && mp_timeline_target_lag(&t) == 3u,
             "two more seconds of silence measure no gap: a halt is a stall, not jitter");
    ut_check(!step(&t, 51u) && !step(&t, 52u), "the resumption still waits for a whole target");
    ut_check(step(&t, 53u) && mp_timeline_resyncs(&t) == 1u,
             "and arrives with the one resync, exactly as with a fixed target");
}

int main(void)
{
    check_warm_up();
    check_steady_stream();
    check_lost_sample();
    check_drift();
    check_sender_stall();
    check_reorder();
    check_slew_and_cooldown();
    check_measured_even_stream();
    check_measured_clumped_stream();
    check_measured_rise_and_decay();
    check_measured_threshold();
    check_switch_leaves_the_machinery();

    return ut_summary("mp_timeline");
}
