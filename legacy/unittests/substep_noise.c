/* substep_noise.c: the numbers effect_clock answers the engine with in place of a random draw.
 *
 * Two of the three effects effect_clock repairs keep their engine side untouched. The redirected
 * call still lands in the engine's own `fild` and its multiply by 1/32767, and the comparison
 * against the 0.2 at [0x004A8130] and the multiply by the 0.4 at [0x004A852C] that follow are the
 * engine's as well. So a replacement has to behave like a draw from the generator that used to be
 * there, and each way of failing that is quiet on screen or worse than the defect being repaired:
 *
 *   outside 0 to 32767      the engine's scaling runs off the end, giving a halo brighter than
 *                           white or a flicker test that can never fire
 *   alike across objects    every flickering object in a room goes dark on the same step
 *   alike across substeps   the effect is frozen rather than paced
 *   unalike within one      an object changes its mind between two frames of one simulation step,
 *   substep                 which is the defect the feature exists to remove
 *
 * The last of those is why the answer is a function of the substep number and the draw order
 * ordinal alone, with nothing else in it, and most of what follows checks that it is.
 *
 * Where a claim is about spread rather than about one value, it is checked over a run with a wide
 * margin. A mixer that had lost its spread would fail these by a mile, and a healthy one passes
 * with room measured in orders of magnitude, so the bounds are deliberately loose rather than tuned
 * to the numbers that came out.
 */
#include "unittest.h"

#include "substep_noise.h"

#include <stdint.h>
#include <string.h>

/* The engine skips a flickering object when the answer divided by 32767 is under 0.2, so every
 * answer up to and including 6553 is a dark one. */
#define DARK_BELOW 6554u

/* One quarter of the range, for the coverage check. */
#define QUARTER ((SUBSTEP_NOISE_MAX + 1u) / 4u)

typedef uint32_t (*noise_fn_t)(uint32_t tick, uint32_t ordinal);

static unsigned char seen[SUBSTEP_NOISE_MAX + 1u];

static unsigned distinct_over_ticks(noise_fn_t noise, uint32_t ordinal, unsigned count)
{
    unsigned distinct = 0;
    unsigned i;

    memset(seen, 0, sizeof seen);
    for (i = 0; i < count; ++i) {
        uint32_t value = noise((uint32_t)i, ordinal);

        if (seen[value] == 0) {
            seen[value] = 1;
            ++distinct;
        }
    }
    return distinct;
}

static unsigned distinct_over_ordinals(noise_fn_t noise, uint32_t tick, unsigned count)
{
    unsigned distinct = 0;
    unsigned i;

    memset(seen, 0, sizeof seen);
    for (i = 0; i < count; ++i) {
        uint32_t value = noise(tick, (uint32_t)i);

        if (seen[value] == 0) {
            seen[value] = 1;
            ++distinct;
        }
    }
    return distinct;
}

static unsigned quarters_covered_over_ticks(noise_fn_t noise, uint32_t ordinal, unsigned count)
{
    unsigned quarters = 0;
    unsigned bits = 0;
    unsigned i;

    for (i = 0; i < count; ++i) {
        bits |= 1u << (unsigned)(noise((uint32_t)i, ordinal) / QUARTER);
    }
    for (i = 0; i < 4u; ++i) {
        if ((bits & (1u << i)) != 0u) {
            ++quarters;
        }
    }
    return quarters;
}

static void the_tick_is_the_counter_divided(void)
{
    ut_section("the substep tick");

    /* The shipped setting is one roll per simulation step, so the tick is the counter itself and an
     * effect changes 32 times a second whatever the display is doing. */
    ut_check(substep_noise_tick(0u, 1u) == 0u, "at the default the tick is the counter itself");
    ut_check(substep_noise_tick(31u, 1u) == 31u, "and it follows the counter step for step");

    /* SubstepsPerRoll=4. The answer has to hold for four steps and turn over on the fourth, because
     * that is the whole meaning of the setting. */
    ut_check(substep_noise_tick(0u, 4u) == 0u, "four steps a roll: the first step opens the roll");
    ut_check(substep_noise_tick(3u, 4u) == 0u, "three steps in, the roll still holds");
    ut_check(substep_noise_tick(4u, 4u) == 1u, "the fourth step is where it turns over");
    ut_check(substep_noise_tick(7u, 4u) == 1u, "and the new roll holds for its own four");
    ut_check(substep_noise_tick(8u, 4u) == 2u, "then turns over again");

    /* The far end of the accepted range: one roll a second, and no slower. */
    ut_check(substep_noise_tick(31u, 32u) == 0u, "at the maximum a roll lasts a whole second");
    ut_check(substep_noise_tick(32u, 32u) == 1u, "and turns over on the thirty-second step");

    /* The installer clamps the ini value into 1 to 32 before it gets here, so this is a caller that
     * should not exist. It still must not divide by zero on the drawing path. */
    ut_check(substep_noise_tick(7u, 0u) == 7u, "a divisor of zero is read as one rather than used");
    ut_check(substep_noise_tick(0u, 0u) == 0u, "and a zero counter with it stays at zero");

    /* The counter is the engine's own word and nothing here narrows it. */
    ut_check(substep_noise_tick(0xFFFFFFFFu, 1u) == 0xFFFFFFFFu,
             "the counter is taken as it stands, right to the top of its word");
    ut_check(substep_noise_tick(0xFFFFFFFFu, 2u) == 0x7FFFFFFFu,
             "and halved at two steps a roll, without overflowing on the way");
}

static void every_answer_is_inside_the_engines_range(void)
{
    uint32_t tick;
    uint32_t ordinal;
    unsigned outside = 0;
    uint32_t lowest = SUBSTEP_NOISE_MAX;
    uint32_t highest = 0;

    ut_section("the range the engine scales by");

    for (tick = 0u; tick < 128u; ++tick) {
        for (ordinal = 0u; ordinal < 64u; ++ordinal) {
            uint32_t flicker = substep_noise_flicker(tick, ordinal);
            uint32_t halo = substep_noise_halo(tick, ordinal);

            if (flicker > SUBSTEP_NOISE_MAX || halo > SUBSTEP_NOISE_MAX) {
                ++outside;
            }
            if (flicker < lowest) {
                lowest = flicker;
            }
            if (flicker > highest) {
                highest = flicker;
            }
            if (halo < lowest) {
                lowest = halo;
            }
            if (halo > highest) {
                highest = halo;
            }
        }
    }

    ut_checkf(outside == 0u,
              "none of 8192 answers from either replacement leaves 0 to 32767 (%u did)", outside);

    /* Being inside the range is not enough. An answer stuck in a narrow band would pass the check
     * above while leaving the halo permanently half lit and the flicker permanently on. */
    ut_checkf(highest > 32000u, "the answers reach the top of the range (highest was %u)",
              (unsigned)highest);
    ut_checkf(lowest < 700u, "and the bottom of it (lowest was %u)", (unsigned)lowest);
}

static void an_object_holds_its_answer_through_one_step(void)
{
    uint32_t first[8];
    unsigned frame;
    unsigned object;
    unsigned changed = 0;

    ut_section("one simulation step, several drawn frames");

    /* What the caller does with these: every drawn frame walks the object list in the same order
     * and the ordinal restarts at zero with the frame. Within one substep each frame therefore asks
     * the same questions, and it must get the same answers back, because that is what holds an
     * object visible or dark for a whole simulation step instead of for a display refresh. */
    for (object = 0u; object < 8u; ++object) {
        first[object] = substep_noise_flicker(500u, object);
    }

    for (frame = 1u; frame < 8u; ++frame) {
        int identical = 1;

        for (object = 0u; object < 8u; ++object) {
            if (substep_noise_flicker(500u, object) != first[object]) {
                identical = 0;
            }
        }
        ut_checkf(identical, "frame %u of the same simulation step answers exactly as the first",
                  frame);
    }

    /* The other half of the same claim, and the half that is not free: when the simulation steps,
     * the answers have to move. */
    for (object = 0u; object < 8u; ++object) {
        if (substep_noise_flicker(501u, object) != first[object]) {
            ++changed;
        }
    }
    ut_checkf(changed >= 7u, "and the next simulation step re-rolls them: %u of 8 objects changed",
              changed);
}

static void the_objects_of_one_frame_do_not_answer_alike(void)
{
    unsigned distinct;

    ut_section("one simulation step, several objects");

    distinct = distinct_over_ordinals(substep_noise_flicker, 12345u, 256u);
    ut_checkf(distinct >= 240u,
              "256 objects drawn in one step get %u different answers, so a room full of them does "
              "not go dark together", distinct);

    distinct = distinct_over_ordinals(substep_noise_halo, 12345u, 256u);
    ut_checkf(distinct >= 240u, "and 256 halos in one step get %u different answers", distinct);

    /* The two replacements must not agree with each other either: an object that both flickers and
     * carries a halo would otherwise take both decisions from the same number. */
    {
        unsigned agreed = 0;
        unsigned i;

        for (i = 0u; i < 256u; ++i) {
            if (substep_noise_flicker((uint32_t)i, (uint32_t)i) ==
                substep_noise_halo((uint32_t)i, (uint32_t)i)) {
                ++agreed;
            }
        }
        ut_checkf(agreed <= 4u,
                  "the flicker and the halo answer the same question differently (%u of 256 agreed "
                  "by chance)", agreed);
    }
}

static void one_object_is_re_rolled_as_the_simulation_steps(void)
{
    unsigned distinct;
    unsigned quarters;
    unsigned dark = 0;
    uint32_t tick;
    uint32_t ordinal;

    ut_section("one object, one simulation step at a time");

    distinct = distinct_over_ticks(substep_noise_flicker, 7u, 256u);
    ut_checkf(distinct >= 240u,
              "over 256 consecutive steps one object gets %u different answers, so it is paced "
              "rather than cycling through a handful of them", distinct);

    distinct = distinct_over_ticks(substep_noise_halo, 7u, 256u);
    ut_checkf(distinct >= 240u, "and one halo gets %u different answers over the same run",
              distinct);

    quarters = quarters_covered_over_ticks(substep_noise_flicker, 7u, 256u);
    ut_checkf(quarters == 4u,
              "those answers reach all four quarters of the range (%u of 4), so consecutive steps "
              "are unrelated rather than walking it in order", quarters);

    quarters = quarters_covered_over_ticks(substep_noise_halo, 7u, 256u);
    ut_checkf(quarters == 4u, "the halo covers all four quarters as well (%u of 4)", quarters);

    /* The authored look, and the reason the range matters. The engine calls an object dark when the
     * answer is under a fifth of full scale, so about a fifth of the answers have to be. The band
     * is wide because what is being ruled out is a replacement that is dark almost never or almost
     * always, not a few per cent either way. */
    for (tick = 0u; tick < 64u; ++tick) {
        for (ordinal = 0u; ordinal < 64u; ++ordinal) {
            if (substep_noise_flicker(tick, ordinal) < DARK_BELOW) {
                ++dark;
            }
        }
    }
    ut_checkf(dark >= 410u && dark <= 1228u,
              "roughly one answer in five falls under the engine's threshold, which is the "
              "authored rate (%u of 4096, one in five would be 819)", dark);
}

static void the_flicker_cancels_at_one_ordinal_per_step(void)
{
    uint32_t tick;
    unsigned cancelled = 0;
    unsigned zeros = 0;
    unsigned halo_zeros = 0;

    ut_section("the cancellation");

    /* Both of the flicker's terms are built with the same constant, so when the ordinal plus one
     * equals the substep number they are the same number, they cancel, and the mixer maps zero to
     * zero. Zero is under the engine's threshold, so that one object is dark for that one step. */
    ut_check(substep_noise_flicker(1u, 0u) == 0u,
             "the object at ordinal zero answers zero on substep one, so it is dark for that step");

    for (tick = 1u; tick <= 256u; ++tick) {
        if (substep_noise_flicker(tick, tick - 1u) == 0u) {
            ++cancelled;
        }
    }
    ut_checkf(cancelled == 256u,
              "the object whose ordinal is one below the step number cancels every time (%u of "
              "256), which is the one artefact this replacement introduces", cancelled);

    ut_check(substep_noise_flicker(0u, 0xFFFFFFFFu) == 0u,
             "the same cancellation where the ordinal plus one wraps back to zero");

    /* What bounds the cost: for one object it happens at one substep and never again, because the
     * substep number only grows. A frame with a handful of flickering objects therefore meets all
     * of its cancellations in the first moments of a level. */
    for (tick = 0u; tick < 256u; ++tick) {
        if (substep_noise_flicker(tick, 7u) == 0u) {
            ++zeros;
        }
    }
    ut_checkf(zeros >= 1u && zeros <= 4u,
              "over 256 steps the eighth object answers zero %u time(s), so the cancellation costs "
              "one dark step and not a run of them", zeros);

    /* The halo is built with two different constants, so its cancelling ordinal is a large and
     * effectively arbitrary number rather than the substep minus one, and no frame holds enough
     * objects to reach it. */
    for (tick = 1u; tick <= 256u; ++tick) {
        if (substep_noise_halo(tick, tick - 1u) == 0u) {
            ++halo_zeros;
        }
    }
    ut_checkf(halo_zeros <= 2u,
              "the halo does not cancel where the flicker does (%u zeros in 256), because its two "
              "constants differ", halo_zeros);
}

static void no_two_steps_pin_the_same_arc_seed(void)
{
    uint32_t a;
    uint32_t b;
    unsigned collisions = 0;

    ut_section("the arc seed");

    /* The arcs are rebuilt from scratch out of the generator's own numbers, so a repeated seed is a
     * repeated bolt. An odd multiplier is invertible over a 32 bit word, which is the whole reason
     * this is enough. */
    for (a = 0u; a < 128u; ++a) {
        for (b = a + 1u; b < 128u; ++b) {
            if (substep_noise_arc_seed(a) == substep_noise_arc_seed(b)) {
                ++collisions;
            }
        }
    }
    ut_checkf(collisions == 0u,
              "no two of 128 consecutive steps pin the same seed, so no two rebuild the same bolt "
              "(%u collisions)", collisions);

    ut_check(substep_noise_arc_seed(0u) == 0u, "step zero pins the generator to zero");
    ut_check(substep_noise_arc_seed(1u) != substep_noise_arc_seed(0u),
             "and the step after it pins something else");
}

int main(void)
{
    the_tick_is_the_counter_divided();
    every_answer_is_inside_the_engines_range();
    an_object_holds_its_answer_through_one_step();
    the_objects_of_one_frame_do_not_answer_alike();
    one_object_is_re_rolled_as_the_simulation_steps();
    the_flicker_cancels_at_one_ordinal_per_step();
    no_two_steps_pin_the_same_arc_seed();

    return ut_summary("substep_noise");
}
