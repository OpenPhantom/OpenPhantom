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

static void test_a_ship_keeps_flying(void)
{
    const float flying[3] = { 4.0f, 0.0f, 0.5f };
    const float pushed[3] = { 4.0f, 0.0f, -0.21f };

    ut_section("the regression this design exists to avoid");

    /* A ship, a bird or a droid on a flying platform is exempt from collision PRECISELY because it
       flies, and it moves by velocity like anything else. The first version of this fix cleared
       the velocity of every exempt character and froze all of them, which is why the test for it
       comes before the test for the bug. */
    ut_check(!move_mode_contact_pushed(MODE_SEATED, flying, flying),
             "a character whose velocity is the same before and after the contact handler was not "
             "pushed by it, so a ship holding its course is left alone even though nothing will "
             "collision test it");
    ut_check(!move_mode_contact_pushed(MODE_PROP, flying, flying),
             "and the same for any other exempt mode");
    ut_check(move_mode_contact_pushed(MODE_SEATED, flying, pushed),
             "but a ship whose velocity the handler CHANGED was pushed, and that push is undone: "
             "it keeps the course it arrived with rather than being stopped dead");
}

static void test_the_push_this_fix_exists_for(void)
{
    const float still[3]   = { 0.0f, 0.0f, 0.0f };
    const float shoved[3]  = { -0.01f, 0.0f, -0.71f };
    const float walking[3] = { 1.2f, 0.4f, 0.0f };

    ut_section("the seated character being walked through the floor");

    ut_check(move_mode_contact_pushed(MODE_SEATED, still, shoved),
             "a character that was stationary and now carries a downward velocity was pushed by "
             "the contact, and nothing will collision test the move that follows");
    ut_check(!move_mode_contact_pushed(MODE_ORDINARY_NPC, still, shoved),
             "the identical push on a collision tested character is left alone, because the engine "
             "will stop it properly and this fix has no business there");
    ut_check(!move_mode_contact_pushed(MODE_ORDINARY_NPC, still, walking),
             "and a tested character being shoved sideways keeps it too");
    ut_check(!move_mode_contact_pushed(MODE_SEATED, still, still),
             "a contact that changed nothing needs nothing undone, which is almost every contact");
}

static void test_the_threshold(void)
{
    const float zero[3] = { 0.0f, 0.0f, 0.0f };
    float       velocity[3] = { 0.0f, 0.0f, 0.0f };

    ut_section("what counts as moving");

    velocity[2] = MOVE_MODE_VELOCITY_EPSILON * 0.5f;
    ut_check(!move_mode_contact_pushed(MODE_SEATED, zero, velocity),
             "a velocity smaller than the epsilon is rounding, not motion, and is left alone");

    velocity[2] = -MOVE_MODE_VELOCITY_EPSILON * 0.5f;
    ut_check(!move_mode_contact_pushed(MODE_SEATED, zero, velocity),
             "in either direction");

    velocity[2] = MOVE_MODE_VELOCITY_EPSILON * 2.0f;
    ut_check(move_mode_contact_pushed(MODE_SEATED, zero, velocity),
             "past the epsilon it counts, so the threshold has no silent gap above it");

    velocity[0] = 0.0f;
    velocity[1] = MOVE_MODE_VELOCITY_EPSILON * 2.0f;
    velocity[2] = 0.0f;
    ut_check(move_mode_contact_pushed(MODE_SEATED, zero, velocity),
             "any one axis is enough: a sideways shove needs stopping as much as a downward one");
}

static void test_numbers_that_cannot_be_compared(void)
{
    const float zero[3] = { 0.0f, 0.0f, 0.0f };
    float       velocity[3] = { 0.0f, 0.0f, 0.0f };

    ut_section("a velocity that is not a number");

    velocity[2] = (float)NAN;
    ut_check(move_mode_contact_pushed(MODE_SEATED, zero, velocity),
             "a NaN velocity on an untested character counts as motion and is cleared, because a "
             "NaN reaching a move that nothing will test is the worst case available and clearing "
             "it is the safe answer");
    ut_check(!move_mode_contact_pushed(MODE_ORDINARY_NPC, zero, velocity),
             "but a collision tested character is still left alone, since this fix has no business "
             "in a move the engine is going to resolve properly");

    ut_check(!move_mode_contact_pushed(MODE_SEATED, zero, NULL),
             "a null velocity is not read through, it simply means there is nothing to do");
}

int main(void)
{
    test_which_modes_skip_collision();
    test_a_ship_keeps_flying();
    test_the_push_this_fix_exists_for();
    test_the_threshold();
    test_numbers_that_cannot_be_compared();

    return ut_summary("move_mode");
}
