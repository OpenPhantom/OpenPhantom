/* creeping_mover: telling a mover that creeps from one that carries.
 *
 * The rates here are not invented. They were decoded out of every level file the executable names,
 * all eleven of them, by reading each animated-object record's proven open displacement and its
 * travel time and dividing by the simulation's 32 ticks a second. Every crusher in the game that
 * carries walkable faces and descends is represented below, which is what makes the gap between the
 * one at fault and the slowest genuine platform a measurement rather than a guess.
 */
#include "creeping_mover.h"

#include "unittest.h"

#include <math.h>

/* The fault: the final level's crusher, which lowers a floor panel five centimetres over about two
 * seconds and takes the two characters standing on it down through the floor drawn beneath. */
#define FINAL_87 0.00081f

/* The slowest genuine platforms in the game, three of them at the same rate. */
#define SLOWEST_REAL 0.03233f

/* The fastest, an articulated ninety unit lift, and the one it would be worst to break. */
#define FASTEST_REAL 0.58195f

/* Two stand-in movers. Only their addresses matter: the set pairs an id with the mover it was seen
 * on, and these are what tell one level's mover 87 from another's. */
static const int MOVER_A = 0;
static const int MOVER_B = 0;

int main(void)
{
    creeping_mover_set_t set = {{0}, 0};
    unsigned             i;

    ut_section("the rates, decoded out of every level the executable names");
    ut_check(creeping_mover_is_creep(FINAL_87),
             "the crusher that lowers a floor panel five centimetres is a creep");
    ut_check(!creeping_mover_is_creep(SLOWEST_REAL),
             "the slowest real platform in the game is not, and it is forty times faster");
    ut_check(!creeping_mover_is_creep(FASTEST_REAL),
             "nor is the ninety unit lift, which is the one it would be worst to freeze");

    ut_check(!creeping_mover_is_creep(CREEPING_MOVER_LIMIT),
             "the limit itself carries rather than creeps, so the boundary is the platforms'");
    ut_check(creeping_mover_is_creep(CREEPING_MOVER_LIMIT / 2.0f),
             "half the limit still creeps");


    ut_section("a mover that is not descending is never refused");
    ut_check(!creeping_mover_is_creep(0.0f),
             "a mover holding still is not creeping, so a standstill is never refused");
    ut_check(!creeping_mover_is_creep(-FINAL_87),
             "a mover carrying somebody UPWARD is not creeping, whatever its rate");
    ut_check(!creeping_mover_is_creep(-FASTEST_REAL),
             "and neither is a fast one, which is a lift doing its job");


    ut_section("numbers that cannot be compared");
    ut_check(!creeping_mover_is_creep((float)NAN),
             "a rate that is not a number is refused rather than compared, because a comparison "
             "against a bound is false for NaN and would answer 'carries' by accident");
    ut_check(!creeping_mover_is_creep((float)INFINITY),
             "and so is an infinite one");


    ut_section("remembering which mover was refused");
    ut_check(!creeping_mover_known(&set, 87u, &MOVER_A),
             "an empty set knows no mover");
    ut_check(creeping_mover_note(&set, 87u, &MOVER_A),
             "the first sighting of a mover is reported");
    ut_check(!creeping_mover_note(&set, 87u, &MOVER_A),
             "the second is not, so each mover is named once however many times it is refused");
    ut_check(creeping_mover_known(&set, 87u, &MOVER_A),
             "and it is remembered afterwards, which is how the ground snap recognises the mover "
             "the rider carry refused without being able to measure the rate itself");
    ut_check(!creeping_mover_known(&set, 49u, &MOVER_A),
             "a different mover is still unknown, so one level's fault does not exempt another's "
             "platform");

    /* An id is only unique inside its own level, and nothing here is told when a level opens. */
    ut_check(!creeping_mover_known(&set, 87u, &MOVER_B),
             "the same id on a different mover is not the mover that was refused, so a later "
             "level reusing the number keeps its ground snap");
    ut_check(creeping_mover_note(&set, 87u, &MOVER_B),
             "and noting it reports it as new, because on that level it is");
    ut_check(creeping_mover_known(&set, 87u, &MOVER_B),
             "after which it is the one remembered");
    ut_check(!creeping_mover_known(&set, 87u, &MOVER_A),
             "and the mover from the level that closed is not, since it cannot come back");

    ut_check(creeping_mover_note(&set, 0u, &MOVER_A),
             "mover id zero is a real id and is recorded like any other");
    ut_check(creeping_mover_known(&set, 0u, &MOVER_A),
             "and is remembered, rather than reading as an empty slot");

    for (i = (unsigned)set.count; i < CREEPING_MOVER_MAX; i++) {
        ut_check(creeping_mover_note(&set, 1000u + i, &MOVER_A),
                 "the table fills to its stated capacity");
    }
    ut_check(!creeping_mover_note(&set, 9999u, &MOVER_A),
             "a full table refuses a new mover rather than growing");
    ut_check(creeping_mover_known(&set, 87u, &MOVER_B),
             "and keeps what it already had, because evicting one would let a mover already being "
             "refused start carrying again halfway through its run");
    ut_check(creeping_mover_note(&set, 9999u, &MOVER_B) == false,
             "a full table refuses an unheard-of id whichever mover carries it");

    ut_check(!creeping_mover_note(NULL, 87u, &MOVER_A),
             "a null set is refused rather than written through");
    ut_check(!creeping_mover_known(NULL, 87u, &MOVER_A), "and answers that it knows nothing");

    return ut_summary("creeping_mover");
}
