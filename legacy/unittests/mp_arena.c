/* Which placements a team deathmatch buries.
 *
 * The rule is one function over two plain values, so the whole of it runs here with no game in the
 * process. The cases are the authored data: the two enemy classes, the classes that carry the
 * scenery and the pickups, and the flag word that outranks all of them.
 *
 * The engine-side half is checked for refusing. In a test process no cell and no site resolves,
 * and the two entry points have to answer that rather than walk a null world.
 */
#include "unittest.h"

#include "mp_arena.h"
#include "mp_cells.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The flag word as the levels actually author it around the player-host bit. Every one of these
 * appears on a shipped placement, which is why they are here rather than a single clean 0x2000. */
#define HOSTS_PLAYER_PLAIN   0x2000u
#define HOSTS_PLAYER_AND_80  0x2080u
#define HOSTS_PLAYER_AND_20  0x2020u
#define HOSTS_PLAYER_AND_800 0x2800u
#define HOSTS_PLAYER_AND_1000 0x3000u
#define HOSTS_PLAYER_AND_8000 0xA000u

/* Bit 0 is the scan eligibility the ordinary enemy placements carry. */
#define SCAN_ELIGIBLE 0x0001u

/* No mover slot set: this placement drives no level geometry. */
#define DRIVES_NOTHING false
#define DRIVES_A_MOVER true

static void check_every_actor_class_goes_under(void)
{
    /* The editor's own table: 0 None, 1 Player, 2 Enemy, 3 Civilian, 4 Tripod, 5 Turret,
     * 8 AI Tank, 9 NPC Ally. Class -1 appears on shipped records too. */
    static const int32_t ACTORS[] = { -1, 0, 1, 2, 3, 4, 5, 8, 9 };
    size_t i;

    ut_section("EVERY actor class goes under, not only the two that shoot");

    /* The rule used to name the riflemen and the tanks, on the reading that the class says what a
     * placement builds. It does not: the engine's spawner puts the class into the body's
     * perception and collision fields and nowhere else. Burying by class left the civilians, the
     * allies, the scripted Player-class actors and all the traffic standing, which is 590 visible
     * placements across the eleven levels and 49 in Theed alone. */
    for (i = 0; i < sizeof ACTORS / sizeof ACTORS[0]; ++i) {
        ut_checkf(mp_arena_buries(SCAN_ELIGIBLE, ACTORS[i], DRIVES_NOTHING),
                  "class %d is buried when it drives nothing", (int)ACTORS[i]);
        ut_checkf(mp_arena_buries(0u, ACTORS[i], DRIVES_NOTHING),
                  "class %d is buried even unseen by the scan, because a reveal would hand it "
                  "over", (int)ACTORS[i]);
    }
}

static void check_what_stays_standing(void)
{
    static const int32_t ACTORS[] = { -1, 0, 1, 2, 3, 4, 5, 8, 9 };
    static const int32_t PICKUPS[] = { 10, 13, 14, 17, 18, 19, 20, 21, 22, 26 };
    size_t i;

    ut_section("every pickup class stays, or a deathmatch level has nothing to pick up");
    for (i = 0; i < sizeof PICKUPS / sizeof PICKUPS[0]; ++i) {
        ut_checkf(!mp_arena_buries(SCAN_ELIGIBLE, PICKUPS[i], DRIVES_NOTHING),
                  "class %d stays", (int)PICKUPS[i]);
    }

    ut_section("and so does anything that drives a mover, UNLESS it shoots");

    /* This is the arm that keeps the level playable. A lift with a civilian's class is still the
     * only way to the upper floor, and a grate opened by an invisible hit body is still the way
     * through the wall.
     *
     * But it does NOT rescue somebody who shoots, and that ordering is a defect this test was
     * written after: with the mover arm ahead of the class arm, eighty one armed placements
     * across the eleven levels survived an emptied arena purely because their scripts may open a
     * door, thirteen of them in Theed. The lifts those thirteen drive were dead under the old
     * rule too, so keeping them buried loses nothing that ever worked. */
    for (i = 0; i < sizeof ACTORS / sizeof ACTORS[0]; ++i) {
        bool shoots = ACTORS[i] == MP_PLACEMENT_CLASS_ENEMY || ACTORS[i] == MP_PLACEMENT_CLASS_TANK;

        ut_checkf(mp_arena_buries(SCAN_ELIGIBLE, ACTORS[i], DRIVES_A_MOVER) == shoots,
                  "class %d %s when it drives a mover", (int)ACTORS[i],
                  shoots ? "still goes under" : "stays");
    }

    ut_section("the pickup band is closed at BOTH ends");

    /* Above the band the engine's own contact handler runs damage arms rather than pickups: the
     * burning death and ordinary contact damage. An open ended test would have called one of
     * those a health pack and left it standing in the arena. */
    ut_check(!mp_arena_buries(SCAN_ELIGIBLE, MP_PLACEMENT_CLASS_LAST_PICKUP, DRIVES_NOTHING),
             "the last pickup class stays");
    ut_check(mp_arena_buries(SCAN_ELIGIBLE, MP_PLACEMENT_CLASS_LAST_PICKUP + 1, DRIVES_NOTHING),
             "and the one above it does not");
    ut_check(mp_arena_buries(SCAN_ELIGIBLE, 0x20, DRIVES_NOTHING),
             "nor does a contact damage class");
}

static void check_the_flag_outranks_the_class(void)
{
    /* This is the case the module was first built the other way round on. The bit was read as
     * "the player's own body stands up in this one", so it protected the placement. It is the
     * cutscene handover: waking one calls player_suspend and parks the player module at zero,
     * and every respawn is gated on that state reading one. In an arena it has to go under. */
    ut_section("a placement that hosts the player is buried too, whatever its class says");
    ut_check(mp_arena_buries(HOSTS_PLAYER_PLAIN, MP_PLACEMENT_CLASS_ENEMY, DRIVES_NOTHING),
             "an enemy class carrying the handover bit goes under on either ground");
    ut_check(mp_arena_buries(HOSTS_PLAYER_PLAIN, 0, DRIVES_NOTHING),
             "and so does an inert class carrying it, which the class rule alone would keep");
    ut_check(mp_arena_buries(HOSTS_PLAYER_PLAIN, MP_PLACEMENT_CLASS_TANK, DRIVES_NOTHING),
             "and a tank class carrying it");
    ut_check(mp_arena_buries(HOSTS_PLAYER_AND_80, 0, DRIVES_NOTHING),
             "the bit is tested on its own, not against the whole word");
    ut_check(mp_arena_buries(HOSTS_PLAYER_AND_20, 0, DRIVES_NOTHING) &&
                 mp_arena_buries(HOSTS_PLAYER_AND_800, 0, DRIVES_NOTHING) &&
                 mp_arena_buries(HOSTS_PLAYER_AND_1000, 0, DRIVES_NOTHING) &&
                 mp_arena_buries(HOSTS_PLAYER_AND_8000, 0, DRIVES_NOTHING),
             "and every other flag word the eleven levels pair it with buries as well");
    ut_check(mp_arena_buries(HOSTS_PLAYER_PLAIN | SCAN_ELIGIBLE, 0, DRIVES_NOTHING),
             "scan eligibility changes nothing either way");

    ut_section("and the bits beside it do not stand in for it");
    /* The flag is tested against a placement that would otherwise STAY, because under this rule
     * an actor goes under anyway and a bury would prove nothing about the bit. A pickup is the
     * clean subject: it stays on every other ground. */
    ut_check(!mp_arena_buries(0x1000u, 13, DRIVES_NOTHING),
             "the bit below the handover bit does not bury a pickup");
    ut_check(!mp_arena_buries(0x4000u, 13, DRIVES_NOTHING), "and neither does the bit above it");
    ut_check(mp_arena_buries(HOSTS_PLAYER_PLAIN, 13, DRIVES_NOTHING),
             "but the handover bit buries even a pickup, because a woken handover parks the "
             "player module and no arm of this feature works after that");
    ut_check(mp_arena_buries(HOSTS_PLAYER_PLAIN, 13, DRIVES_A_MOVER),
             "and it outranks the mover slot too");
    ut_check(!mp_arena_buries(0xFFFFFFFFu & ~(uint32_t)HOSTS_PLAYER_PLAIN, 13, DRIVES_NOTHING),
             "every bit but that one set leaves a pickup standing");
}

static void check_the_engine_side_refuses_without_a_game(void)
{
    ut_section("an arena that is off empties nothing");

    /* The gate was missing from the day the module was written: the pass ran at the beginning of
     * every level whatever the game was, and only the spawner was ever asked. While the rule
     * buried two classes that was a co-op level short of its riflemen. When the rule grew to bury
     * everything that neither is a pickup nor drives a mover, the same ungated pass took four
     * hundred placements out of a campaign level and left no game behind. */
    mp_arena_set_active(false);
    ut_check(mp_arena_clear_enemies() == 0u,
             "a pass with the arena off buries nothing, whatever the level holds");

    ut_section("no world, no sites: both entry points have to say so rather than walk a null");
    mp_arena_set_active(true);
    ut_check(mp_arena_clear_enemies() == 0u,
             "a pass with no world open buries nothing");
    ut_check(!mp_arena_install(),
             "and the gate declines rather than detouring an address that did not resolve");

    mp_arena_set_active(true);
    mp_arena_set_active(false);
    mp_arena_report();
    ut_check(true, "the switch and the report run with nothing behind them");
}

int main(void)
{
    check_every_actor_class_goes_under();
    check_what_stays_standing();
    check_the_flag_outranks_the_class();
    check_the_engine_side_refuses_without_a_game();
    return ut_summary("mp_arena");
}
