/* character_buoyancy.c: the half of the waterline correction that can be checked without the game.
 *
 * What is driven here is the lift and the plausibility test. Everything else in that module reads
 * engine memory: the two model measurements need a live model, and the correction itself needs the
 * swim tick to have run, so both are checked by the log line the swap writes rather than here.
 *
 * The numbers used as cases are the shipped assets, measured from the .baf files: the crown of the
 * head over the body's own origin, times the scale the actor is drawn at. The engine pins the body
 * 0.72 of a unit under the surface whoever is wearing it.
 */
#include "unittest.h"

#include "character_buoyancy.h"

/* The engine's own pin, read out of the swim tick's subtraction on the retail image. */
#define DEPTH        0.72f

/* Crown heights in world units: the model's own measurement times its authored scale. */
#define OBIWAN       0.9292f    /* 0.9292 at scale 1.0  */
#define QUIGON       1.0046f    /* 0.9133 at scale 1.1  */
#define PANAKA       0.7514f    /* 0.7514 at scale 1.0  */
#define QUEEN        0.8265f    /* 0.8265 at scale 1.0  */
#define ANAKIN       0.7454f    /* 0.6776 at scale 1.1  */
#define JAWA         0.4461f    /* 0.4461 at scale 1.0  */
#define BIGHEAD      1.3330f    /* the tallest offered  */

/* What share of himself a swimmer shows over the water once the lift has been applied. */
static float share_above(float own, float worn)
{
    float lift = character_buoyancy_lift_for(DEPTH, own, worn);

    return (worn - (DEPTH - lift)) / worn;
}

static void check_wearing_your_own_body_changes_nothing(void)
{
    ut_section("a body of the player's own height");

    ut_near(character_buoyancy_lift_for(DEPTH, OBIWAN, OBIWAN), 0.0f, 0.0f,
            "the same height gives a lift of exactly zero, so wearing your own model is untouched");
    ut_near(character_buoyancy_lift_for(DEPTH, QUEEN, QUEEN), 0.0f, 0.0f,
            "and that holds for every hero, not only the one the constant was chosen for");
}

static void check_a_shorter_body_is_lifted_and_a_taller_one_pushed_down(void)
{
    ut_section("the direction of the correction");

    ut_check(character_buoyancy_lift_for(DEPTH, OBIWAN, ANAKIN) > 0.0f,
             "a child's body is lifted, because the pin would otherwise close over its head");
    ut_check(character_buoyancy_lift_for(DEPTH, OBIWAN, BIGHEAD) < 0.0f,
             "a body taller than the player's own is pushed down by the same rule");
    ut_check(character_buoyancy_lift_for(DEPTH, OBIWAN, JAWA) >
             character_buoyancy_lift_for(DEPTH, OBIWAN, ANAKIN),
             "the shorter of two bodies is lifted the further");
}

static void check_every_body_shows_the_same_share_of_itself(void)
{
    const float hero = (OBIWAN - DEPTH) / OBIWAN;

    ut_section("the share above the water, which is the whole point");

    ut_near(share_above(OBIWAN, ANAKIN), hero, 0.0005f,
            "the child shows the same share of himself as the hero, not the 3 per cent he showed");
    ut_near(share_above(OBIWAN, JAWA), hero, 0.0005f,
            "and so does the shortest body offered, which was under the surface entirely");
    ut_near(share_above(OBIWAN, BIGHEAD), hero, 0.0005f,
            "and the tallest, which rode nearly half out of the water");
    ut_near(share_above(QUIGON, ANAKIN), (QUIGON - DEPTH) / QUIGON, 0.0005f,
            "the reference is the hero the level gave, so a taller player floats his body higher");
}

static void check_what_it_measured_before_the_correction(void)
{
    ut_section("what the engine's own pin does to a borrowed body");

    ut_check((ANAKIN - DEPTH) / ANAKIN < 0.05f,
             "the child showed under a twentieth of himself, the water at the top of the skull");
    ut_check(JAWA - DEPTH < 0.0f,
             "the shortest body offered did not reach the surface at all");
    ut_check((OBIWAN - DEPTH) / OBIWAN > 0.20f,
             "the hero the constant was chosen for shows a fifth of himself, water at the neck");
}

static void check_a_measurement_that_is_not_a_body_is_declined(void)
{
    ut_section("refusals, and every one of them leaves the engine's pin alone");

    ut_near(character_buoyancy_lift_for(DEPTH, 0.0f, ANAKIN), 0.0f, 0.0f,
            "a height of zero is a failed measurement, not a body of no height");
    ut_near(character_buoyancy_lift_for(DEPTH, OBIWAN, 0.0f), 0.0f, 0.0f,
            "and so is a worn height of zero");
    ut_near(character_buoyancy_lift_for(DEPTH, -OBIWAN, ANAKIN), 0.0f, 0.0f,
            "a negative height is refused rather than turned into a lift");
    ut_near(character_buoyancy_lift_for(0.0f, OBIWAN, ANAKIN), 0.0f, 0.0f,
            "a depth of zero means the site was never read, so there is nothing to correct");
    ut_near(character_buoyancy_lift_for(DEPTH, OBIWAN, OBIWAN * 100.0f), 0.0f, 0.0f,
            "a body a hundred times the player's own is a misread and is declined");
    ut_near(character_buoyancy_lift_for(DEPTH, OBIWAN, OBIWAN / 100.0f), 0.0f, 0.0f,
            "and so is one a hundredth of it");

    ut_check(!character_buoyancy_ratio_is_plausible(OBIWAN, 0.0f),
             "the plausibility test refuses a zero height on its own");
    ut_check(character_buoyancy_ratio_is_plausible(OBIWAN, JAWA),
             "the shortest and the tallest shipped bodies are both inside the band");
    ut_check(character_buoyancy_ratio_is_plausible(OBIWAN, BIGHEAD),
             "so the band never declines a body the roster actually offers");
}

/* The lift is added after the tick, so the value the next substep starts from is whatever this one
 * left. The pin is absolute, so it has to wash that out completely or the body would climb. */
static void check_the_correction_does_not_accumulate(void)
{
    const float surface = 10.0f;
    float       z = 3.0f;
    float       lift = character_buoyancy_lift_for(DEPTH, OBIWAN, ANAKIN);
    int         substep;

    ut_section("the pin is absolute, so the lift cannot accumulate");

    for (substep = 0; substep < 100; ++substep) {
        z += (surface - z) - DEPTH;      /* what the tick writes, whatever z was */
        z += lift;                       /* what this module adds afterwards */
    }
    ut_near(z, surface - DEPTH + lift, 0.0005f,
            "a hundred substeps end exactly one lift above the engine's pin, not a hundred");
}

int main(void)
{
    check_wearing_your_own_body_changes_nothing();
    check_a_shorter_body_is_lifted_and_a_taller_one_pushed_down();
    check_every_body_shows_the_same_share_of_itself();
    check_what_it_measured_before_the_correction();
    check_a_measurement_that_is_not_a_body_is_declined();
    check_the_correction_does_not_accumulate();

    return ut_summary("character buoyancy");
}
