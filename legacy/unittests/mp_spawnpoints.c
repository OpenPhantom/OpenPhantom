/* Where a deathmatch puts a body that has just come back.
 *
 * Three decisions live in this module and none of them needs a game: which authored placement
 * qualifies as a spawn point, whether a candidate stands far enough from everything already kept,
 * and which of the kept points a particular re-entry gets. All three run here over the values the
 * eleven shipped levels actually author.
 *
 * The engine-side half is checked for refusing. In a test process no cell resolves, so the pass
 * over the placement table has to answer that rather than walk a null world.
 */
#include "unittest.h"

#include "mp_cells.h"
#include "mp_spawnpoints.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Three of the six flag words the levels author around the player-host bit. All six appear on a
 * shipped placement, and three of the records carrying one of them also carry an enemy class. */
#define HOSTS_PLAYER_PLAIN    0x2000u
#define HOSTS_PLAYER_AND_80   0x2080u
#define HOSTS_PLAYER_AND_8000 0xA000u

/* Bit 0 is the scan eligibility an ordinary enemy placement carries. It is deliberately NOT part
 * of the rule: a placement a script spawns is still an authored standing position, and in a
 * deathmatch nothing spawns from any of them anyway. */
#define SCAN_ELIGIBLE 0x0001u

/* The settings a new game runs at. Difficulty is authored as 0 on all 2250 shipped records, so
 * the difficulty gate never fires on shipped data; the detail gate does, on eleven of them. */
#define NEW_GAME_DIFFICULTY 4
#define DETAIL_LOWEST       1
#define DETAIL_HIGHEST      4

static void check_which_placements_qualify(void)
{
    ut_section("the class decides what a placement is");
    ut_check(mp_spawnpoints_accepts(SCAN_ELIGIBLE, 2, 0, 0, 0, NEW_GAME_DIFFICULTY,
                                    DETAIL_HIGHEST),
             "a rifleman stands on the floor, so his spot is a spawn point");
    ut_check(mp_spawnpoints_accepts(SCAN_ELIGIBLE, 8, 0, 0, 0, NEW_GAME_DIFFICULTY,
                                    DETAIL_HIGHEST),
             "and so does the tank, whose class the spawner maps onto the rifleman's");
    ut_check(!mp_spawnpoints_accepts(SCAN_ELIGIBLE, 0, 0, 0, 0, NEW_GAME_DIFFICULTY,
                                     DETAIL_HIGHEST),
             "the inert class is scenery and a script anchor, not a place to stand");
    ut_check(!mp_spawnpoints_accepts(SCAN_ELIGIBLE, 18, 0, 0, 0, NEW_GAME_DIFFICULTY,
                                     DETAIL_HIGHEST),
             "a pickup class names where an item lies, which is not where a body may appear");

    ut_section("a placement that is not put on the ground is not a floor");
    ut_check(mp_spawnpoints_accepts(SCAN_ELIGIBLE, 2, 1, 0, 0, NEW_GAME_DIFFICULTY,
                                    DETAIL_HIGHEST),
             "move mode 1 is still ground-snapped, so it qualifies");
    ut_check(!mp_spawnpoints_accepts(SCAN_ELIGIBLE, 2, 2, 0, 0, NEW_GAME_DIFFICULTY,
                                     DETAIL_HIGHEST),
             "move mode 2 keeps whatever height the editor gave it, so it is refused");
    ut_check(!mp_spawnpoints_accepts(SCAN_ELIGIBLE, 2, 5, 0, 0, NEW_GAME_DIFFICULTY,
                                     DETAIL_HIGHEST),
             "and so is every mode above it: 106 of the 1112 enemy placements are of that kind");

    ut_section("the player-host flag outranks the class");
    ut_check(!mp_spawnpoints_accepts(HOSTS_PLAYER_PLAIN, 2, 0, 0, 0, NEW_GAME_DIFFICULTY,
                                     DETAIL_HIGHEST),
             "a cutscene handover carrying an enemy class is refused");
    ut_check(!mp_spawnpoints_accepts(HOSTS_PLAYER_AND_80, 2, 0, 0, 0, NEW_GAME_DIFFICULTY,
                                     DETAIL_HIGHEST),
             "whatever else its flag word carries");
    ut_check(!mp_spawnpoints_accepts(HOSTS_PLAYER_AND_8000, 8, 0, 0, 0, NEW_GAME_DIFFICULTY,
                                     DETAIL_HIGHEST),
             "and whichever of the two enemy classes it names");
}

static void check_the_two_gates_read_in_opposite_directions(void)
{
    ut_section("the difficulty gate: the scan skips a record BELOW its minimum");
    ut_check(!mp_spawnpoints_accepts(SCAN_ELIGIBLE, 2, 0, 6, 0, 4, DETAIL_HIGHEST),
             "difficulty 4 refuses a record that asks for 6");
    ut_check(mp_spawnpoints_accepts(SCAN_ELIGIBLE, 2, 0, 4, 0, 4, DETAIL_HIGHEST),
             "and takes one that asks for exactly 4");
    ut_check(mp_spawnpoints_accepts(SCAN_ELIGIBLE, 2, 0, 0, 0, 0, DETAIL_HIGHEST),
             "every shipped record asks for 0, so the gate passes at any setting");

    ut_section("the detail gate: the scan skips a record ABOVE the detail level");
    ut_check(!mp_spawnpoints_accepts(SCAN_ELIGIBLE, 2, 0, 0, 2, NEW_GAME_DIFFICULTY,
                                     DETAIL_LOWEST),
             "a record gated at 2 is refused at detail level 1");
    ut_check(mp_spawnpoints_accepts(SCAN_ELIGIBLE, 2, 0, 0, 2, NEW_GAME_DIFFICULTY, 2),
             "and taken at detail level 2");
    ut_check(mp_spawnpoints_accepts(SCAN_ELIGIBLE, 2, 0, 0, 0, NEW_GAME_DIFFICULTY,
                                    DETAIL_LOWEST),
             "the 995 records gated at 0 are taken at every level");
}

static void check_the_thinning(void)
{
    static const mp_spawnpoint_t KEPT[] = {
        { { 0.0f, 0.0f, 0.0f }, 0.0f, 0u, false },
        { { 40.0f, 0.0f, 0.0f }, 0.0f, 0u, false }
    };
    static const float ON_TOP[3]   = { 0.0f, 0.0f, 0.0f };
    static const float NEAR_ONE[3] = { 10.0f, 0.0f, 0.0f };
    static const float BETWEEN[3]  = { 20.0f, 0.0f, 0.0f };
    static const float ABOVE[3]    = { 0.0f, 0.0f, 10.0f };

    ut_section("nothing is kept within the separation of something already kept");
    ut_check(!mp_spawnpoints_far_enough(ON_TOP, KEPT, 2u, 15.0f),
             "a candidate on top of a kept point is refused");
    ut_check(!mp_spawnpoints_far_enough(NEAR_ONE, KEPT, 2u, 15.0f),
             "ten units from one of them is still the same room");
    ut_check(mp_spawnpoints_far_enough(BETWEEN, KEPT, 2u, 15.0f),
             "twenty units from both is far enough from both");
    ut_check(!mp_spawnpoints_far_enough(ABOVE, KEPT, 2u, 15.0f),
             "the test is in three dimensions, so a point on the floor above is still too close");
    ut_check(mp_spawnpoints_far_enough(ON_TOP, KEPT, 0u, 15.0f),
             "with nothing kept yet everything is far enough, which is what seeds the pass");
    ut_check(mp_spawnpoints_far_enough(NEAR_ONE, KEPT, 2u, 5.0f),
             "a smaller separation keeps more, which is the one knob this has");

    ut_section("the squared distance is the whole of the arithmetic");
    ut_near((double)mp_spawnpoints_distance_sq(ON_TOP, BETWEEN), 400.0, 0.001,
            "twenty units apart is four hundred squared units");
    ut_near((double)mp_spawnpoints_distance_sq(ON_TOP, ON_TOP), 0.0, 0.001,
            "and a point is no distance from itself");
}

/* Four points in a row, twenty units apart, which is what a thinned level looks like. */
static void seed(mp_spawnpoint_t *points)
{
    size_t index;

    for (index = 0; index < 4u; ++index) {
        points[index].position[0] = (float)index * 20.0f;
        points[index].position[1] = 0.0f;
        points[index].position[2] = 0.0f;
        points[index].heading     = 0.0f;
        points[index].taken       = false;
        points[index].taken_at    = 0u;
    }
}

static void check_the_choice(void)
{
    mp_spawnpoint_t      points[4];
    float                living[1][3] = { { 0.0f, 0.0f, 0.0f } };
    mp_spawn_pick_tier_t tier = MP_SPAWN_PICK_NONE;
    size_t               chosen;

    ut_section("the point farthest from the nearest living player wins");
    seed(points);
    chosen = mp_spawnpoints_pick(points, 4u, living, 1u, NULL, 1000u, &tier);
    ut_check(chosen == 3u, "with one player at the origin the far end of the row is chosen");
    ut_check(tier == MP_SPAWN_PICK_BEST, "and it is the first tier that answered");

    living[0][0] = 60.0f;
    chosen = mp_spawnpoints_pick(points, 4u, living, 1u, NULL, 1000u, &tier);
    ut_check(chosen == 0u, "move him to the far end and the choice moves to the other one");

    ut_section("the spot somebody was just killed on is refused");
    living[0][0] = 0.0f;
    chosen = mp_spawnpoints_pick(points, 4u, living, 1u, points[3].position, 1000u, &tier);
    ut_check(chosen == 2u, "the farthest point is skipped because that is where he died");
    ut_check(tier == MP_SPAWN_PICK_BEST, "which is still an ordinary answer, not a fallback");

    ut_section("a point somebody has just come back through is refused for a while");
    seed(points);
    points[3].taken    = true;
    points[3].taken_at = 1000u;
    chosen = mp_spawnpoints_pick(points, 4u, living, 1u, NULL, 1000u, &tier);
    ut_check(chosen == 2u, "the locked point is passed over while the lock stands");
    chosen = mp_spawnpoints_pick(points, 4u, living, 1u, NULL,
                                 1000u + MP_SPAWNPOINTS_REUSE_LOCK, &tier);
    ut_check(chosen == 3u, "and taken again once it has run out");

    ut_section("every point locked: the locks are dropped rather than the re-entry");
    seed(points);
    {
        size_t index;

        for (index = 0; index < 4u; ++index) {
            points[index].taken    = true;
            points[index].taken_at = 1000u;
        }
    }
    chosen = mp_spawnpoints_pick(points, 4u, living, 1u, NULL, 1000u, &tier);
    ut_check(chosen == 3u, "the farthest point is taken anyway");
    ut_check(tier == MP_SPAWN_PICK_ANY, "and the tier says the lock was what gave way");

    ut_section("every point inside the death lock: the level start is what is left");
    seed(points);
    chosen = mp_spawnpoints_pick(points, 1u, living, 1u, points[0].position, 1000u, &tier);
    ut_check(chosen == 0u, "index 0 is the level start and is taken");
    ut_check(tier == MP_SPAWN_PICK_START, "and the tier says so, so a report can name it");

    ut_section("with nobody standing, the least recently used point wins");
    seed(points);
    points[0].taken    = true;
    points[0].taken_at = 900u;
    points[1].taken    = true;
    points[1].taken_at = 800u;
    chosen = mp_spawnpoints_pick(points, 4u, NULL, 0u, NULL,
                                 1000u + MP_SPAWNPOINTS_REUSE_LOCK, &tier);
    ut_check(chosen == 2u, "a point never used beats both that have been");

    ut_section("an empty table answers nothing rather than index zero");
    chosen = mp_spawnpoints_pick(points, 0u, living, 1u, NULL, 1000u, &tier);
    ut_check(chosen == MP_SPAWNPOINTS_NONE, "there is no point to give");
    ut_check(tier == MP_SPAWN_PICK_NONE, "and the tier says the table was empty");
}

static void check_the_engine_side_refuses_without_a_game(void)
{
    float position[3] = { 1.0f, 2.0f, 3.0f };
    float heading = 9.0f;
    float living[1][3] = { { 0.0f, 0.0f, 0.0f } };

    ut_section("no world, no cells: the pass has to say so rather than walk a null");
    ut_check(mp_spawnpoints_build() == 0u, "a build with no world open keeps no points");
    ut_check(mp_spawnpoints_count() == 0u, "and the table stays empty");
    ut_check(!mp_spawnpoints_get(0u, position, &heading), "so there is nothing to read out of it");
    ut_check(!mp_spawnpoints_take(living, 1u, NULL, 0u, position, &heading),
             "and a re-entry asking for a point is refused rather than given a zero");
    ut_check(mp_spawnpoints_now() == 0u,
             "the frame stamp answers zero while the clock cell has not resolved");

    ut_section("the separation is a distance or it is not taken");
    ut_check(!mp_spawnpoints_set_separation(0.0f), "zero is refused");
    ut_check(!mp_spawnpoints_set_separation(-1.0f), "so is a negative one");
    ut_check(mp_spawnpoints_set_separation(20.0f), "and a real one is taken");
    ut_check(mp_spawnpoints_set_separation(MP_SPAWNPOINTS_MIN_SEPARATION),
             "put back, so the order of these checks cannot matter");

    mp_spawnpoints_clear();
    mp_spawnpoints_report();
    ut_check(true, "the clear and the report run with nothing behind them");
}

/* The thinning that left a level with one point.
 *
 * Fifteen units is right for the levels it was measured on and wrong for a small one: a field run
 * played a deathmatch on bridge.b3d, whose eight placements yielded five that qualified, and the
 * thinning dropped all five as too close to the level start. One point is not a deathmatch: both
 * players come back on the same square, and it is the square the choice refuses for the player who
 * just died on it. So the pass is run again with less separation when it kept too few, and the rule
 * says when that is worth doing.
 */
static void check_the_thinning_is_relaxed_when_it_kept_too_few(void)
{
    float once;

    ut_section("a pass that kept too few and threw some away is run again with less");

    once = mp_spawnpoints_relaxed(MP_SPAWNPOINTS_MIN_SEPARATION, 1u, 5u);
    ut_checkf(once > 0.0f && once < MP_SPAWNPOINTS_MIN_SEPARATION,
              "one point kept of five that qualified asks for a smaller separation (%.2f)",
              (double)once);
    ut_check(mp_spawnpoints_relaxed(MP_SPAWNPOINTS_MIN_SEPARATION, MP_SPAWNPOINTS_WANTED, 30u) ==
                 0.0f,
             "a pass that kept what a round wants is the answer, however much it threw away");
    ut_check(mp_spawnpoints_relaxed(MP_SPAWNPOINTS_MIN_SEPARATION, 1u, 1u) == 0.0f,
             "and a level that simply has one qualifying placement is not thinned too hard: "
             "relaxing cannot invent a point that was never there");

    ut_section("it comes to a stop rather than halving for ever");
    {
        float separation = MP_SPAWNPOINTS_MIN_SEPARATION;
        unsigned passes  = 0u;

        while (passes < 100u) {
            float next = mp_spawnpoints_relaxed(separation, 1u, 20u);

            if (next == 0.0f) {
                break;
            }
            ut_checkf(next < separation, "every pass asks for less than the last (%.2f)",
                      (double)next);
            separation = next;
            ++passes;
        }
        ut_checkf(passes > 0u && passes < 100u,
                  "the relaxing ends after %u pass(es) even when nothing is ever kept", passes);
        ut_checkf(separation >= MP_SPAWNPOINTS_SEPARATION_FLOOR,
                  "and it stops at the floor rather than below it (%.2f)", (double)separation);
    }
}

int main(void)
{
    check_which_placements_qualify();
    check_the_two_gates_read_in_opposite_directions();
    check_the_thinning();
    check_the_thinning_is_relaxed_when_it_kept_too_few();
    check_the_choice();
    check_the_engine_side_refuses_without_a_game();
    return ut_summary("mp_spawnpoints");
}
