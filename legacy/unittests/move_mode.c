/* move_mode: which characters the engine does not collision test, and when that matters.
 *
 * The modes checked here are not invented. They were read out of a live level by the diagnostics
 * character census before this fix was written: the ordinary NPCs came back as mode 0, the seated
 * background characters as mode 3, one prop as mode 1, and one character as mode 5. The character
 * this fix exists for was mode 3, and the character standing next to her who never sinks was mode
 * 0, which is as close to a controlled experiment as a 1999 engine offers.
 */
#include "move_mode.h"

#include "unittest.h"

#include <math.h>

/* Modes observed in one level, with what the census saw for each. */
#define MODE_ORDINARY_NPC 0   /* most characters, and every one that behaves */
#define MODE_PROP         1   /* a fixed power unit */
#define MODE_SEATED       3   /* the seated background characters, including the one that sinks */
#define MODE_OBSERVED_5   5   /* one character, bit 0 set with another bit alongside it */

static void test_which_modes_skip_collision(void)
{
    ut_section("who the engine tests and who it does not");

    ut_check(!move_mode_skips_collision(MODE_ORDINARY_NPC),
             "mode 0 is collision tested, which is every ordinary NPC and the character standing "
             "beside the one that sinks");
    ut_check(move_mode_skips_collision(MODE_SEATED),
             "mode 3 is not, and that is the mode the sinking character is actually in");
    ut_check(move_mode_skips_collision(MODE_PROP), "mode 1 is not either, bit 0 alone");
    ut_check(move_mode_skips_collision(MODE_OBSERVED_5),
             "mode 5 is not, because only bit 0 is read and the other bits mean something else");
    ut_check(!move_mode_skips_collision(2),
             "mode 2 is tested: an even mode has bit 0 clear whatever else it carries");
    ut_check(!move_mode_skips_collision(6), "and so is 6");
}

static void test_when_a_move_is_uncontested(void)
{
    const float still[3]   = { 0.0f, 0.0f, 0.0f };
    const float sinking[3] = { 0.0f, 0.0f, -0.51f };
    const float walking[3] = { 1.2f, 0.4f, 0.0f };

    ut_section("when there is something to clear");

    ut_check(move_mode_move_is_uncontested(MODE_SEATED, sinking),
             "an untested character carrying a downward velocity is about to move with nothing "
             "able to stop it, which is the whole bug in one sentence");
    ut_check(!move_mode_move_is_uncontested(MODE_SEATED, still),
             "an untested character that is not moving needs nothing done to it, which is almost "
             "every character on almost every step");
    ut_check(!move_mode_move_is_uncontested(MODE_ORDINARY_NPC, walking),
             "a collision tested character is never touched no matter how fast it is going, "
             "because the engine will stop it properly");
    ut_check(!move_mode_move_is_uncontested(MODE_ORDINARY_NPC, sinking),
             "including one that is falling, which must keep its velocity to land");
    ut_check(move_mode_move_is_uncontested(MODE_PROP, walking),
             "and a prop shoved sideways is caught too, not just a downward push");
}

static void test_the_threshold(void)
{
    float velocity[3] = { 0.0f, 0.0f, 0.0f };

    ut_section("what counts as moving");

    velocity[2] = MOVE_MODE_VELOCITY_EPSILON * 0.5f;
    ut_check(!move_mode_move_is_uncontested(MODE_SEATED, velocity),
             "a velocity smaller than the epsilon is rounding, not motion, and is left alone");

    velocity[2] = -MOVE_MODE_VELOCITY_EPSILON * 0.5f;
    ut_check(!move_mode_move_is_uncontested(MODE_SEATED, velocity),
             "in either direction");

    velocity[2] = MOVE_MODE_VELOCITY_EPSILON * 2.0f;
    ut_check(move_mode_move_is_uncontested(MODE_SEATED, velocity),
             "past the epsilon it counts, so the threshold has no silent gap above it");

    velocity[0] = 0.0f;
    velocity[1] = MOVE_MODE_VELOCITY_EPSILON * 2.0f;
    velocity[2] = 0.0f;
    ut_check(move_mode_move_is_uncontested(MODE_SEATED, velocity),
             "any one axis is enough: a sideways shove needs stopping as much as a downward one");
}

static void test_numbers_that_cannot_be_compared(void)
{
    float velocity[3] = { 0.0f, 0.0f, 0.0f };

    ut_section("a velocity that is not a number");

    velocity[2] = (float)NAN;
    ut_check(move_mode_move_is_uncontested(MODE_SEATED, velocity),
             "a NaN velocity on an untested character counts as motion and is cleared, because a "
             "NaN reaching a move that nothing will test is the worst case available and clearing "
             "it is the safe answer");
    ut_check(!move_mode_move_is_uncontested(MODE_ORDINARY_NPC, velocity),
             "but a collision tested character is still left alone, since this fix has no business "
             "in a move the engine is going to resolve properly");

    ut_check(!move_mode_move_is_uncontested(MODE_SEATED, NULL),
             "a null velocity is not read through, it simply means there is nothing to do");
}

int main(void)
{
    test_which_modes_skip_collision();
    test_when_a_move_is_uncontested();
    test_the_threshold();
    test_numbers_that_cannot_be_compared();

    return ut_summary("move_mode");
}
