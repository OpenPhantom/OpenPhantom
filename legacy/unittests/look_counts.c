/* look_counts.c: whether a small push on the right stick moves the view at all.
 *
 * This is the one property of the pad look that a field test cannot report on honestly. A player
 * can say the look feels right at half deflection and say nothing about a tenth of it. That is the
 * push nobody makes on purpose, the one the stick passes through on its way somewhere else, and a
 * dead band there reads as a bigger deadzone than the stick has.
 *
 * The numbers below are the ones the shipped defaults produce. LookSensitivity is 4000 counts a
 * second and the poll interval is 8 ms, so one poll at full deflection is worth 32 counts and a
 * deflection of 0.01 is worth 0.32 of one. Every check that drives a run of polls drives it at
 * that scale rather than at a round number invented here.
 *
 * The other half is the arithmetic refusing what it cannot convert. A sensitivity that is not a
 * number reaches a cast to long, which is undefined, and on this compiler and this architecture
 * lands on the most negative value a long can hold: the pointer leaves for the corner of the
 * screen on every poll for the rest of the session. The settings loader already corrects that key
 * on the way in, and these checks hold the same line at the other end, where a second caller
 * would arrive.
 */
#include "unittest.h"

#include "look_counts.h"

#include <math.h>

#define SENSITIVITY 4000.0f
#define POLL        0.008

int main(void)
{
    look_carry_t carry;
    long         dx;
    long         dy;
    long         total;
    bool         sent;
    bool         every_step_small;
    bool         carry_stayed_under_one;
    int          i;

    ut_section("a push too small to be worth a whole count");

    look_counts_reset(&carry);
    sent = look_counts_step(&carry, 0.01f, 0.0f, SENSITIVITY, POLL, &dx, &dy);
    ut_check(!sent && dx == 0 && dy == 0, "one poll on its own has nothing whole to send");
    ut_check(carry.x > 0.3 && carry.x < 0.4, "and what it could not send is owed, not discarded");

    look_counts_reset(&carry);
    total = 0;
    every_step_small = true;
    carry_stayed_under_one = true;
    for (i = 0; i < 100; ++i) {
        (void)look_counts_step(&carry, 0.01f, 0.0f, SENSITIVITY, POLL, &dx, &dy);
        total += dx;
        if (dx < 0 || dx > 1) {
            every_step_small = false;
        }
        if (carry.x <= -1.0 || carry.x >= 1.0) {
            carry_stayed_under_one = false;
        }
    }
    ut_check(total >= 31 && total <= 32,
             "a hundred of those polls turn the view by the 32 counts they are worth");
    ut_check(every_step_small, "and they arrive one at a time rather than as an occasional lurch");
    ut_check(carry_stayed_under_one, "what is owed never reaches a whole count");

    ut_section("uneven polls");

    /* A background thread at 125 Hz does not get an even 8 ms; the machine gives it what it has.
     * The sum is what must survive that, not any individual poll. */
    look_counts_reset(&carry);
    total = 0;
    for (i = 0; i < 200; ++i) {
        (void)look_counts_step(&carry, 0.05f, 0.0f, SENSITIVITY, (i % 2) ? 0.012 : 0.004, &dx, &dy);
        total += dx;
    }
    ut_check(total >= 319 && total <= 320,
             "polls of uneven length still add up to the turn they were worth together");

    ut_section("the directions");

    look_counts_reset(&carry);
    (void)look_counts_step(&carry, 1.0f, 0.0f, SENSITIVITY, POLL, &dx, &dy);
    ut_check(dx == 32 && dy == 0, "full right is 32 counts right at the shipped sensitivity");

    look_counts_reset(&carry);
    (void)look_counts_step(&carry, -1.0f, 0.0f, SENSITIVITY, POLL, &dx, &dy);
    ut_check(dx == -32, "full left is the same the other way");

    look_counts_reset(&carry);
    (void)look_counts_step(&carry, 0.0f, 1.0f, SENSITIVITY, POLL, &dx, &dy);
    ut_check(dy == -32, "the stick pushed up looks up, which is a negative mouse movement");

    look_counts_reset(&carry);
    (void)look_counts_step(&carry, 0.0f, -1.0f, SENSITIVITY, POLL, &dx, &dy);
    ut_check(dy == 32, "and pushed down, down");

    ut_section("a poll with no time in it");

    look_counts_reset(&carry);
    carry.x = 0.5;
    sent = look_counts_step(&carry, 1.0f, 1.0f, SENSITIVITY, 0.0, &dx, &dy);
    ut_check(!sent && dx == 0 && dy == 0, "no elapsed time sends nothing");
    ut_near(carry.x, 0.5, 1.0e-12, "and leaves what was owed exactly where it was");

    sent = look_counts_step(&carry, 1.0f, 1.0f, SENSITIVITY, -1.0, &dx, &dy);
    ut_check(!sent && dx == 0 && dy == 0, "a gap that ran backwards sends nothing either");
    ut_near(carry.x, 0.5, 1.0e-12, "and also leaves it alone");

    sent = look_counts_step(&carry, 1.0f, 1.0f, SENSITIVITY, (double)NAN, &dx, &dy);
    ut_check(!sent && dx == 0 && dy == 0, "a gap that is not a number sends nothing");
    ut_near(carry.x, 0.5, 1.0e-12, "and is not allowed to poison what was owed");

    ut_section("values a cast could not survive");

    look_counts_reset(&carry);
    carry.x = 0.5;
    sent = look_counts_step(&carry, 1.0f, 1.0f, (float)NAN, POLL, &dx, &dy);
    ut_check(!sent && dx == 0 && dy == 0, "a sensitivity that is not a number sends nothing");
    ut_check(carry.x == 0.0 && carry.y == 0.0,
             "and what was owed is cleared rather than left as a value nothing can undo");

    look_counts_reset(&carry);
    sent = look_counts_step(&carry, 1.0f, 1.0f, (float)INFINITY, POLL, &dx, &dy);
    ut_check(!sent && dx == 0 && dy == 0, "an endless sensitivity sends nothing");

    look_counts_reset(&carry);
    sent = look_counts_step(&carry, (float)NAN, 0.0f, SENSITIVITY, POLL, &dx, &dy);
    ut_check(!sent && dx == 0 && dy == 0, "a stick reading that is not a number sends nothing");

    sent = look_counts_step(&carry, 1.0f, 0.0f, SENSITIVITY, POLL, &dx, &dy);
    ut_check(sent && dx == 32, "and the next ordinary poll works again");

    ut_section("a sensitivity nothing clamped on the way in");

    look_counts_reset(&carry);
    sent = look_counts_step(&carry, 1.0f, 0.0f, 1.0e30f, POLL, &dx, &dy);
    ut_check(sent && dx == (long)LOOK_COUNTS_MAX,
             "a figure no cast could hold turns a long way rather than undefined");
    ut_check(carry.x == 0.0,
             "and the difference is dropped, not owed forward where it could never be paid");

    ut_section("losing the foreground");

    carry.x = 0.9;
    carry.y = -0.9;
    look_counts_reset(&carry);
    ut_check(carry.x == 0.0 && carry.y == 0.0, "the reset forgets both axes");

    return ut_summary("look counts");
}
