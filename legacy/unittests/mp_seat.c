/* The one search for a seat, on the engine side.
 *
 * Two things run here with no game in the process. What a ground reading means is arithmetic with
 * a known answer, and it decides whether a search waits. And the wish's stages have to end even
 * where nothing can be probed: a wish on a point the caller named reaches that point unprobed after
 * three seconds of simulation, and a wish beside the players with no authored point to fall back
 * to never seats anybody anywhere, because in a test process no level stands.
 *
 * The rest of the engine side is checked for refusing, because nothing resolves here.
 */
#include "unittest.h"

#include "mp_seat.h"
#include "mp_signatures.h"
#include "mp_signatures_world.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void check_what_the_ground_under_a_point_means(void)
{
    ut_section("standing on the floor is the only reading a seat can be searched around");
    ut_check(mp_seat_floor_state(0.0f, false) == MP_SEAT_FLOOR_OK,
             "a body on its own floor reads no distance at all");
    ut_check(mp_seat_floor_state(0.25f, false) == MP_SEAT_FLOOR_OK,
             "a step under the feet is inside the band");
    ut_check(mp_seat_floor_state(-0.5f, false) == MP_SEAT_FLOOR_OK,
             "and so is a floor a little below, which is what a walk down a slope reads");
    ut_check(mp_seat_floor_state(MP_SEAT_FLOOR_BAND, false) == MP_SEAT_FLOOR_OK,
             "the band edge itself is still usable");

    ut_section("falling: the probe found no floor at all");
    ut_check(mp_seat_floor_state(MP_PROBE_NO_FLOOR, false) == MP_SEAT_FLOOR_NONE,
             "the engine's own no-floor value is compared for, not tested for magnitude");
    ut_check(mp_seat_floor_state((float)NAN, false) == MP_SEAT_FLOOR_NONE,
             "and a distance that is not a number lands in the same answer");

    ut_section("on a mover: the walkable probe cannot see the platform");
    ut_check(mp_seat_floor_state(0.0f, true) == MP_SEAT_FLOOR_MOVER,
             "a body standing on a lift reads an ordinary distance, so the flag has to decide");
    ut_check(mp_seat_floor_state(MP_PROBE_NO_FLOOR, true) == MP_SEAT_FLOOR_NONE,
             "and a body falling off one is answered as falling, which is the nearer truth");

    ut_section("swimming, or over a drop: the floor is not under the feet");
    ut_check(mp_seat_floor_state(-6.0f, false) == MP_SEAT_FLOOR_FAR,
             "the riverbed six units down is not something to snap a team mate to");
    ut_check(mp_seat_floor_state(6.0f, false) == MP_SEAT_FLOOR_FAR,
             "and a floor six units above is a ceiling from here");
}

static void check_the_world_probe_table(void)
{
    ut_section("the three probes are one table and the enumerator indexes it");
    ut_check((size_t)MP_WORLD_SITE_COUNT == 3u,
             "three sites: the ground probe, the head clearance and the walkable line");
    ut_check((size_t)MP_WORLD_SITE_PROBE_FLOOR == 0u &&
                 (size_t)MP_WORLD_SITE_HEAD_CLEARANCE == 1u &&
                 (size_t)MP_WORLD_SITE_WALKABLE_DISTANCE == 2u,
             "in the order the table is written, because the table is positional and a row one "
             "place out would resolve under its neighbour's name in silence");
    ut_check(MP_SURF_LOW_CEILING == 0x0800u,
             "the only mask the shipped game hands the head clearance probe");
}

static void check_a_wish_on_a_point_always_ends(void)
{
    static const float POINT[3] = { 12.0f, -4.0f, 30.0f };
    mp_seat_wish_t     wish;
    mp_seat_counts_t   counts;
    float              seat[3] = { 0.0f, 0.0f, 0.0f };
    float              heading = 0.0f;
    uint32_t           now;
    bool               seated = false;

    memset(&counts, 0, sizeof counts);
    mp_seat_wish_at_point(&wish, "the test", POINT, 45.0f, 1u);

    ut_section("nothing can be probed here, and the wish still ends on the point it was given");
    (void)mp_seat_wish_step(&wish, 0u, &counts, seat, &heading);
    /* Then five thousand substeps with the engine's gates shut, a cutscene say: no look. */
    mp_seat_wish_pass(&wish, 5000u);
    for (now = 5001u; now <= 5001u + MP_SEAT_GIVE_UP_SUBSTEPS + 2u && !seated; ++now) {
        seated = mp_seat_wish_step(&wish, now, &counts, seat, &heading);
    }
    ut_checkf(seated && now - 1u == 5001u + MP_SEAT_GIVE_UP_SUBSTEPS,
              "seated on substep %u: three seconds of empty looks after the gates opened, the "
              "time they were shut not counted", (unsigned)(now - 1u));
    ut_check(wish.stage == MP_SEAT_STAGE_AS_AUTHORED,
             "through the last stage, the point as the level authored it");
    ut_check(seat[0] == POINT[0] && seat[1] == POINT[1] && seat[2] == POINT[2] &&
                 heading == 45.0f,
             "on exactly that point, facing the way it was asked for");
    ut_checkf(counts.as_authored == 1u && counts.searches == counts.found_nothing &&
                  counts.searches >= MP_SEAT_GIVE_UP_SUBSTEPS,
              "and the counters say so: %u search(es), all empty, one put on the point as it is",
              (unsigned)counts.searches);
}

static void check_a_wish_beside_the_players_seats_nobody_nowhere(void)
{
    static const float DIED_AT[3] = { 1.0f, 2.0f, 3.0f };
    static const float MATE[3]    = { 5.0f, 2.0f, 3.0f };
    mp_seat_wish_t     wish;
    mp_seat_counts_t   counts;
    float              seat[3];
    float              heading = 0.0f;
    uint32_t           now;
    bool               seated = false;

    memset(&counts, 0, sizeof counts);
    mp_seat_note_body(0u, MATE, 90.0f, true);
    mp_seat_note_no_body(1u);
    mp_seat_note_no_body(2u);
    mp_seat_wish_beside_players(&wish, "the test", DIED_AT, 0u);

    ut_section("with no level there is no authored point, so a beside wish keeps looking");
    for (now = 0u; now < 4u * MP_SEAT_GIVE_UP_SUBSTEPS && !seated; ++now) {
        seated = mp_seat_wish_step(&wish, now, &counts, seat, &heading);
    }
    ut_check(!seated, "nobody is seated on a point nothing vouched for");
    ut_check(wish.stage == MP_SEAT_STAGE_ANCHOR && wish.said_no_point,
             "the wish stays beside the players and has said once why");
    ut_check(counts.to_point == 0u && counts.to_start == 0u && counts.as_authored == 0u,
             "and no fallback is counted that did not happen");

    ut_section("the far bodies the pump hands in");
    mp_seat_note_body(3u, MATE, 0.0f, true);
    mp_seat_note_body(0u, NULL, 0.0f, true);
    mp_seat_note_no_body(7u);
    ut_check(true, "an index past the far banks and a missing pose are refused, not written");
}

static void check_the_engine_side_refuses_without_a_game(void)
{
    static const float SOMEWHERE[3] = { 10.0f, 20.0f, 30.0f };
    mp_seat_counts_t   counts;
    float              seat[3] = { 0.0f, 0.0f, 0.0f };

    memset(&counts, 0, sizeof counts);
    ut_section("nothing resolves in a test process");
    ut_check(mp_seat_probe(SOMEWHERE, true, 1u, NULL, 0u, &counts, seat) == MP_SEAT_NO_PROBES,
             "a search before the install refuses rather than calling a null");
    ut_check(seat[0] == 0.0f && seat[1] == 0.0f && seat[2] == 0.0f,
             "and writes no seat, so a caller cannot use one it never got");
    ut_check(!mp_seat_probes_resolved(), "the probes are absent");
    ut_check(!mp_seat_level_running(), "and no level runs");
    mp_seat_install();
    ut_check(!mp_seat_probes_resolved(), "the install names what did not resolve and stays off");
    ut_check(mp_seat_probe(SOMEWHERE, false, 0u, NULL, 0u, &counts, seat) == MP_SEAT_NO_LEVEL,
             "after it, with no level, a search says so rather than probing the last level");
    mp_seat_report("the test's seat:", "the test's fallback:", &counts);
    ut_check(true, "and the report runs with nothing behind it");
}

int main(void)
{
    check_what_the_ground_under_a_point_means();
    check_the_world_probe_table();
    check_a_wish_on_a_point_always_ends();
    check_a_wish_beside_the_players_seats_nobody_nowhere();
    check_the_engine_side_refuses_without_a_game();
    return ut_summary("mp_seat");
}
