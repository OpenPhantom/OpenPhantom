/* The one seat search and the rule set above it, driven through the real modules against a small
 * world this test builds.
 *
 * SIZE NOTE: over 600 lines. Half of it is the engine and the wire the test plays, which every
 * section shares; the seam, when it grows, is that half into a file of its own, as mp_peers_net is
 * for the session tests.
 *
 * The re-entry's rule set, the re-entry, the seat search and the spawn points are the real ones.
 * What is replaced is the engine under them: the three world probes answer from a flat floor with
 * a pocket, a wall and crawl spaces the test places, the cells are fields of this file, and the
 * engine's own re-entry only records where it was asked to put the body. A frame is driven in the
 * order the frame pump drives it: the far players handed to the rule set and to the seat, the rule
 * set's tick, then the re-entry's.
 */
#include "unittest.h"

#include "mp_arrival.h"
#include "mp_body_gate.h"
#include "mp_bridge_drain.h"
#include "mp_bridge_lobby.h"
#include "mp_cells.h"
#include "mp_damage.h"
#include "mp_death.h"
#include "mp_lobby.h"
#include "mp_placements.h"
#include "mp_reentry.h"
#include "mp_respawn.h"
#include "mp_rules.h"
#include "mp_seat.h"
#include "mp_signatures.h"
#include "mp_signatures_world.h"
#include "mp_spawnpoints.h"
#include "mp_start.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The engine, as this test plays it.
 * ============================================================================================ */

#define HERO_BLOCK_BYTES   0x400u
#define WORLD_RECORD_BYTES 0x60u

/* The world's own clock inside that record, in seconds, as the substep loop sets it. */
#define WORLD_CLOCK_SECONDS 0x54u

/* The wall clock of this rig: one frame is drawn a substep, a thirty second of a second, so
 * the frames and the milliseconds of a deadline are reached in the order they were before the
 * milliseconds were asked, the frames last. */
#define RIG_MS_PER_SUBSTEP 32u

#define PLACEMENT_BYTES    0xD0u
#define PLACEMENTS         2u
#define CRAWL_SPACES       4u

/* What the ground probe answers inside the pocket: the floor six units down, a drop. */
#define POCKET_DROP (-6.0f)

typedef struct fake_world {
    bool  pocket_on;         /* every point inside the radius but off the centre is a drop */
    float pocket[3];
    float pocket_radius;
    bool  wall_on;           /* a line that crosses x = wall_x is not walkable */
    float wall_x;
    float crawl[CRAWL_SPACES][3];
    size_t crawl_count;      /* a crawl space over each of these points */
} fake_world_t;

typedef struct fake_engine {
    uint32_t game_mode;      /* 2 while a level runs; the re-entry raises it to 4 */
    uint32_t world;          /* the world pointer the walkable probe is handed */
    uint32_t clock;
    uint32_t difficulty;
    uint32_t detail;
    uint8_t  hero_block[HERO_BLOCK_BYTES];
    float    died_at[3];
    bool     survives;       /* what the re-entry last told the damage module */

    uint32_t respawns;       /* calls of the engine's own re-entry */
    float    respawned_at[3];
    int32_t  health;

    uint8_t  world_record[WORLD_RECORD_BYTES];
    uint8_t  placement[PLACEMENTS][PLACEMENT_BYTES];
    uint32_t table[PLACEMENTS];
} fake_engine_t;

static fake_engine_t eng;
static fake_world_t  wld;

static void put_u32(uint8_t *block, size_t at, uint32_t value)
{
    memcpy(block + at, &value, sizeof value);
}

static void put_floats(uint8_t *block, size_t at, const float *values, size_t count)
{
    memcpy(block + at, values, count * sizeof values[0]);
}

static float distance_2d(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];

    return sqrtf(dx * dx + dy * dy);
}

static void __cdecl fake_probe_floor(const float position[3], void *ground)
{
    float   distance = -position[2];   /* the floor is at height 0 */
    float   words[1];
    int32_t on_mover = 0;

    if (wld.pocket_on) {
        float from_centre = distance_2d(position, wld.pocket);

        if (from_centre > 0.5f && from_centre < wld.pocket_radius) {
            distance = POCKET_DROP;
        }
    }
    words[0] = distance;
    memcpy(ground, words, sizeof words);
    memcpy((uint8_t *)ground + 0x18u, &on_mover, sizeof on_mover);
}

static float __cdecl fake_head_clearance(const float position[3], uint16_t mask)
{
    size_t i;

    (void)mask;
    for (i = 0; i < wld.crawl_count; ++i) {
        if (distance_2d(position, wld.crawl[i]) < 0.5f) {
            return 1.0f;
        }
    }
    return 0.0f;
}

static float __cdecl fake_walkable_distance(uintptr_t world, const float from[3],
                                            const float to[3])
{
    (void)world;
    if (wld.wall_on && (from[0] - wld.wall_x) * (to[0] - wld.wall_x) < 0.0f) {
        return 1.0f;
    }
    return 0.0f;
}

static void __cdecl fake_respawn_at(int32_t hero, const float position[3], float heading)
{
    (void)hero;
    (void)heading;
    ++eng.respawns;
    memcpy(eng.respawned_at, position, sizeof eng.respawned_at);
    put_u32(eng.hero_block, MP_HERO_BLOCK_MODULE_STATE, 4u);   /* the engine's fade begins */
}

static void __cdecl fake_set_health(int32_t health)
{
    eng.health = health;
}

/* ---- what the modules ask of the engine ------------------------------------------------------ */

uintptr_t mp_cells_address(mp_cell_t cell)
{
    switch (cell) {
    case MP_CELL_GAME_MODE:         return (uintptr_t)&eng.game_mode;
    case MP_CELL_LEVEL:             return (uintptr_t)&eng.world;
    case MP_CELL_CLOCK_TICKS:       return (uintptr_t)&eng.clock;
    case MP_CELL_IMPACT_DIFFICULTY: return (uintptr_t)&eng.difficulty;
    case MP_CELL_DETAIL_LEVEL:      return (uintptr_t)&eng.detail;
    case MP_CELL_HERO_BLOCK:        return (uintptr_t)eng.hero_block;
    default:                        return 0u;
    }
}

bool mp_cells_hero_position(float out[3])
{
    memcpy(out, eng.died_at, sizeof eng.died_at);
    return true;
}

uintptr_t mp_signatures_address(mp_site_t site)
{
    switch (site) {
    case MP_SITE_PLAYER_RESPAWN_AT: return (uintptr_t)&fake_respawn_at;
    case MP_SITE_STATUS_SET_HEALTH: return (uintptr_t)&fake_set_health;
    default:                        return 0u;
    }
}

size_t mp_signatures_world_resolve(void)
{
    return (size_t)MP_WORLD_SITE_COUNT;
}

uintptr_t mp_signatures_world_address(mp_world_site_t site)
{
    switch (site) {
    case MP_WORLD_SITE_PROBE_FLOOR:       return (uintptr_t)&fake_probe_floor;
    case MP_WORLD_SITE_HEAD_CLEARANCE:    return (uintptr_t)&fake_head_clearance;
    case MP_WORLD_SITE_WALKABLE_DISTANCE: return (uintptr_t)&fake_walkable_distance;
    default:                              return 0u;
    }
}

bool mp_placements_table(uintptr_t *world, uint32_t *count, uint32_t *table)
{
    *world = (uintptr_t)eng.world_record;
    *count = PLACEMENTS;
    *table = (uint32_t)(uintptr_t)eng.table;
    return true;
}

bool mp_damage_installed(void)
{
    return true;
}

void mp_damage_set_survives_death(bool survives)
{
    eng.survives = survives;
}

bool mp_damage_survives_death(void)
{
    return eng.survives;
}

/* Every death this test plays is a life of its own, counted for the whole run the way the death
 * hull counts them for the whole process. */
static uint32_t lives_ended;

bool mp_damage_entry_lives_ended(uint32_t *lives)
{
    *lives = lives_ended;
    return true;
}

bool mp_body_death_is_reported(void)
{
    return true;
}

void mp_body_gate_set_life_test(mp_body_gate_life_fn_t lives)
{
    (void)lives;
}

uint32_t mp_rules_respawn_substeps(const mp_rules_t *rules)
{
    (void)rules;
    return 0u;
}

/* ---- what the arrival asks of the wire and of the placement ---------------------------------- */

typedef struct fake_wire {
    bool                 is_client;
    uint8_t              my_slot;
    bool                 setup_known;
    mp_lobby_setup_t     setup;
    bool                 host_known;
    mp_bridge_far_pose_t host;
    uint32_t             placed;       /* points handed to the placement */
    float                placed_at[3];

    /* The host's history as the bridge keeps it: its newest sample, which the bridge resolves
     * into the pose in every substep whatever a level begin did, and how often it began. */
    bool                 sample_known;
    float                sample_at[3];
    uint32_t             sample_tick;
    uint32_t             starts;
} fake_wire_t;

static fake_wire_t wire;

/* What the bridge does in every substep: the host's pose resolved again out of its history. */
static void the_host_is_resolved(void)
{
    if (!wire.sample_known) {
        return;
    }
    memset(&wire.host, 0, sizeof wire.host);
    wire.host.slot = (uint8_t)MP_BRIDGE_HOST_SLOT;
    memcpy(wire.host.position, wire.sample_at, sizeof wire.host.position);
    wire.host.alive      = true;
    wire.host.state_tick = wire.sample_tick;
    wire.host_known      = true;
}

static void the_host_is_resolved_at(const float position[3], uint32_t state_tick)
{
    wire.sample_known = true;
    memcpy(wire.sample_at, position, sizeof wire.sample_at);
    wire.sample_tick = state_tick;
    the_host_is_resolved();
}

bool mp_bridge_drain_host_history_mark(uint32_t *newest_tick, uint32_t *starts)
{
    *newest_tick = wire.sample_tick;
    *starts      = wire.starts;
    return wire.sample_known;
}

bool mp_bridge_drain_host_elsewhere(void)
{
    return false;
}

bool mp_bridge_drain_is_client(void)
{
    return wire.is_client;
}

uint8_t mp_bridge_drain_my_slot(void)
{
    return wire.my_slot;
}

bool mp_bridge_drain_far_pose(mp_bridge_far_pose_t *out)
{
    if (!wire.host_known) {
        return false;
    }
    *out = wire.host;
    return true;
}

bool mp_bridge_lobby_setup(mp_lobby_setup_t *out)
{
    if (!wire.setup_known) {
        return false;
    }
    *out = wire.setup;
    return true;
}

bool mp_start_place_at(const float position[3], float heading)
{
    (void)heading;
    ++wire.placed;
    memcpy(wire.placed_at, position, sizeof wire.placed_at);
    return true;
}

void mp_start_cancel_pose(void)
{
}

/* ==============================================================================================
 * The level: its own start, two authored points, a running level and a living player.
 * ============================================================================================ */

static const float LEVEL_START[3] = { 100.0f, 0.0f, 0.0f };
static const float POINT_NEAR[3]  = { 40.0f, 0.0f, 0.0f };
static const float POINT_FAR[3]   = { -60.0f, 0.0f, 0.0f };

static void author_a_placement(size_t index, const float position[3])
{
    uint8_t *record = eng.placement[index];
    float    yaw    = 0.0f;

    memset(record, 0, PLACEMENT_BYTES);
    put_u32(record, MP_PLACEMENT_CLASS_ID, 2u);   /* an enemy standing on the ground */
    put_floats(record, MP_PLACEMENT_POSITION, position, 3u);
    put_floats(record, MP_PLACEMENT_START_YAW, &yaw, 1u);
    eng.table[index] = (uint32_t)(uintptr_t)record;
}

static void open_the_level(void)
{
    float yaw = 0.0f;
    bool  survives = eng.survives;   /* the re-entry tells the switch only when it changes */

    memset(&eng, 0, sizeof eng);
    memset(&wld, 0, sizeof wld);
    eng.survives  = survives;
    eng.game_mode = 2u;
    eng.world     = (uint32_t)(uintptr_t)eng.world_record;
    eng.detail    = 4u;
    put_u32(eng.hero_block, MP_HERO_BLOCK_MODULE_STATE, 1u);
    put_u32(eng.hero_block, MP_HERO_BLOCK_HACTOR, 0x00C0FFEEu);
    put_floats(eng.world_record, MP_LEVEL_PLAYER_START, LEVEL_START, 3u);
    put_floats(eng.world_record, MP_LEVEL_PLAYER_START_YAW, &yaw, 1u);
    author_a_placement(0u, POINT_NEAR);
    author_a_placement(1u, POINT_FAR);
    (void)mp_spawnpoints_build();
}

/* ==============================================================================================
 * The session, as the frame pump hands it in.
 * ============================================================================================ */

typedef struct far_player {
    bool  known;
    bool  stands;
    float position[3];
} far_player_t;

static far_player_t far_players[MP_REENTRY_MAX_PEERS];
static uint32_t     substeps;

static void join_a_session(bool is_client, uint8_t my_slot)
{
    mp_rules_t rules;

    memset(&rules, 0, sizeof rules);
    memset(far_players, 0, sizeof far_players);
    mp_reentry_note_session((uint8_t)MP_LOBBY_MODE_COOP, is_client, &rules);
    mp_reentry_note_my_slot(my_slot);
}

static void place_far_player(size_t bank_less_one, const float position[3], bool stands)
{
    far_players[bank_less_one].known  = true;
    far_players[bank_less_one].stands = stands;
    memcpy(far_players[bank_less_one].position, position, sizeof far_players[0].position);
}

/* One drawn frame and one substep, in the frame pump's order. */
static void one_frame(void)
{
    size_t i;

    ++substeps;
    ++eng.clock;
    {
        float seconds = (float)eng.clock / 32.0f;

        put_floats(eng.world_record, WORLD_CLOCK_SECONDS, &seconds, 1u);
    }
    for (i = 0; i < MP_REENTRY_MAX_PEERS; ++i) {
        if (far_players[i].known) {
            mp_reentry_note_peer(i, far_players[i].position, 0.0f, far_players[i].stands);
            mp_seat_note_body(i, far_players[i].position, 0.0f, far_players[i].stands);
        } else {
            mp_reentry_note_no_peer(i);
            mp_seat_note_no_body(i);
        }
    }
    mp_reentry_tick(substeps, substeps * RIG_MS_PER_SUBSTEP);
    mp_respawn_tick(substeps, substeps * RIG_MS_PER_SUBSTEP);
    the_host_is_resolved();
    mp_arrival_tick(substeps);
}

static void frames(uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; ++i) {
        one_frame();
    }
}

/* This machine's player dies where `at` says. */
static void die_at(uint8_t my_slot, const float at[3])
{
    mp_death_note_t note;

    memcpy(eng.died_at, at, sizeof eng.died_at);
    put_u32(eng.hero_block, MP_HERO_BLOCK_DEAD, 1u);
    ++lives_ended;
    note.victim_slot = my_slot;
    note.killer_slot = (uint8_t)MP_DEATH_NO_KILLER;
    note.reason      = (uint8_t)MP_DEATH_BY_HIT;
    mp_reentry_note_death(&note);
}

/* The engine's fade runs a frame, ends, and the body stands again. The re-entry watches the module
 * state leave its running value before it believes a landing, so the fade has to be seen. */
static void land_the_body(void)
{
    frames(1u);
    put_u32(eng.hero_block, MP_HERO_BLOCK_MODULE_STATE, 1u);
    put_u32(eng.hero_block, MP_HERO_BLOCK_DEAD, 0u);
    frames(2u);
}

/* ==============================================================================================
 * The wipe: nobody standing has one answer, and it is the rule set's.
 * ============================================================================================ */

/* Where the far player stands, a pocket whose every candidate reads a drop, so the seat search
 * finds nothing for the three seconds that would otherwise end in a fallback. */
static const float ANCHOR_IN_A_POCKET[3] = { 0.0f, 0.0f, 0.0f };
static const float DIED_HERE[3]          = { 10.0f, 0.0f, 0.0f };

static void dig_the_pocket(void)
{
    wld.pocket_on = true;
    memcpy(wld.pocket, ANCHOR_IN_A_POCKET, sizeof wld.pocket);
    wld.pocket_radius = 5.0f;
}

/* The host dies beside a client who stands, the re-entry is handed to the seat search, and the
 * client dies before a seat is found. Nobody stands, so the host's rule ends the level, as it does
 * when nobody stands at the moment of the death. The fallback used to take over instead: three
 * seconds later the host stood alone at an authored point, and no wipe took place. */
static void check_a_host_whose_last_mate_dies_ends_the_level(void)
{
    ut_section("the host: the last mate dies while the seat is searched, and the level ends");
    open_the_level();
    dig_the_pocket();
    join_a_session(false, 0u);
    place_far_player(0u, ANCHOR_IN_A_POCKET, true);
    frames(4u);
    die_at(0u, DIED_HERE);
    frames(10u);
    ut_check(mp_respawn_pending() && eng.respawns == 0u,
             "with the mate standing the wish is handed to the seat search, and the pocket "
             "holds it");

    far_players[0].stands = false;
    frames(4u * MP_SEAT_GIVE_UP_SUBSTEPS);
    ut_checkf(eng.respawns == 0u,
              "nobody stands, and nothing brought this player back (%u re-entries asked of the "
              "engine)", (unsigned)eng.respawns);
    ut_checkf(eng.game_mode == MP_REENTRY_OUTCOME_DEATH,
              "the level is ended the way a death ends it (the outcome reads %u)",
              (unsigned)eng.game_mode);
    ut_check(!mp_respawn_pending(), "and no wish is left in the seat search");
}

/* The same on a client: its rule is to wait as a corpse for the host's screen. When the host
 * comes back through his own re-entry the rule turns into BESIDE again, and the client stands up
 * beside him. */
static void check_a_client_whose_host_dies_waits_for_him(void)
{
    static const float HOST_STANDS_AGAIN[3] = { 60.0f, 0.0f, 0.0f };

    ut_section("a client: the host dies while the seat is searched, and the client waits for him");
    open_the_level();
    dig_the_pocket();
    join_a_session(true, 1u);
    place_far_player(0u, ANCHOR_IN_A_POCKET, true);
    frames(4u);
    die_at(1u, DIED_HERE);
    frames(10u);
    ut_check(mp_respawn_pending() && eng.respawns == 0u,
             "the host stands, so the wish is handed to the seat search");

    far_players[0].stands = false;
    frames(4u * MP_SEAT_GIVE_UP_SUBSTEPS);
    ut_checkf(eng.respawns == 0u,
              "nobody stands, and the fallback did not bring this client back alone (%u "
              "re-entries asked of the engine)", (unsigned)eng.respawns);
    ut_check(mp_reentry_waiting_for_host(),
             "it waits as a corpse for the host's screen, and the band says so");
    ut_check(eng.game_mode == 2u, "a client ends no level of its own");
    ut_check(!mp_respawn_pending(), "the wish is the rule set's again, not the seat search's");

    place_far_player(0u, HOST_STANDS_AGAIN, true);
    frames(3u);
    ut_checkf(eng.respawns == 1u && distance_2d(eng.respawned_at, HOST_STANDS_AGAIN) < 2.5f,
              "the host stands again, and the client comes back beside him (%.2f units away)",
              (double)distance_2d(eng.respawned_at, HOST_STANDS_AGAIN));
    ut_check(!mp_reentry_waiting_for_host(), "and waits no more");
    land_the_body();
}

/* The host dies while a scene's gathering holds, beside a mate whose every seat the pocket refuses.
 * The gathering names its seat, and the waiting wish takes it before any seat beside the mate. */
static void check_a_named_seat_is_asked_first(void)
{
    static const float NAMED_SEAT[3] = { 30.0f, 0.0f, 0.0f };

    ut_section("the host: a scene named a seat while the mate stands, and the "
               "host comes back there");
    open_the_level();
    dig_the_pocket();
    join_a_session(false, 0u);
    place_far_player(0u, ANCHOR_IN_A_POCKET, true);
    frames(4u);
    die_at(0u, DIED_HERE);
    frames(10u);
    ut_check(mp_respawn_pending() && eng.respawns == 0u,
             "the pocket holds the wish in the seat search");

    mp_seat_name_anchor(NAMED_SEAT, 0.0f);
    frames(3u);
    mp_seat_name_anchor(NULL, 0.0f);
    ut_checkf(eng.respawns == 1u && distance_2d(eng.respawned_at, NAMED_SEAT) < 2.5f,
              "the host comes back on the seat the gathering named (%u re-entries, %.2f units "
              "away)", (unsigned)eng.respawns, (double)distance_2d(eng.respawned_at, NAMED_SEAT));
    land_the_body();
}

/* A scene's gathering names its seat for a host who lies dead while it holds. The seat is one more
 * anchor of the same search, never an answer to nobody standing: when the last mate dies, the rule
 * set's wipe ends the level although the named seat is free. The named seat used to be asked
 * before anybody standing was, and brought the host back alone. */
static void check_a_named_seat_does_not_outlive_the_last_mate(void)
{
    static const float NAMED_SEAT[3] = { 30.0f, 0.0f, 0.0f };

    ut_section("the host: a scene named a seat, the last mate dies, and the level still ends");
    open_the_level();
    dig_the_pocket();
    join_a_session(false, 0u);
    place_far_player(0u, ANCHOR_IN_A_POCKET, true);
    frames(4u);
    die_at(0u, DIED_HERE);
    frames(10u);
    ut_check(mp_respawn_pending() && eng.respawns == 0u,
             "with the mate standing in the pocket the wish waits in the seat search");

    mp_seat_name_anchor(NAMED_SEAT, 0.0f);
    far_players[0].stands = false;
    frames(4u * MP_SEAT_GIVE_UP_SUBSTEPS);
    mp_seat_name_anchor(NULL, 0.0f);
    ut_checkf(eng.respawns == 0u,
              "nobody stands, and the named seat did not bring this player back (%u re-entries "
              "asked of the engine)", (unsigned)eng.respawns);
    ut_checkf(eng.game_mode == MP_REENTRY_OUTCOME_DEATH,
              "the level is ended the way a death ends it (the outcome reads %u)",
              (unsigned)eng.game_mode);
}

/* Only the wish the rule set handed over is its to take back. A wish on a point that something
 * else asked of the re-entry, in a co-op session with nobody standing, is carried out. */
static void check_only_its_own_wish_is_taken_back(void)
{
    static const float SOMEBODY_ELSES_POINT[3] = { -30.0f, 10.0f, 0.0f };

    ut_section("the rule set takes back its own wish and nobody else's");
    open_the_level();
    join_a_session(false, 0u);
    frames(2u);
    ut_check(mp_respawn_at(SOMEBODY_ELSES_POINT, 0.0f, 0u, 0u),
             "a wish on a point is held, with nobody standing in a co-op session");
    frames(2u);
    ut_checkf(eng.respawns == 1u && distance_2d(eng.respawned_at, SOMEBODY_ELSES_POINT) < 0.01f,
              "and carried out on that point (%u re-entries asked of the engine)",
              (unsigned)eng.respawns);
    land_the_body();
}

/* ==============================================================================================
 * The field run with four players: a good anchor, every candidate refused, and the fallback.
 * ============================================================================================ */

/* In a field run the host died, a client stood on his floor in a pocket, and every candidate of
 * both rings was refused, so the search found nothing for minutes. Three seconds of empty looks
 * on a good anchor take the free authored point nearest the anchor, the point itself first. */
static void check_the_field_run_ends_on_the_nearest_point(void)
{
    uint32_t died_on;
    uint32_t waited = 0u;

    ut_section("a good anchor, every candidate refused, and the fallback seats the host");
    open_the_level();
    dig_the_pocket();
    join_a_session(false, 0u);
    place_far_player(0u, ANCHOR_IN_A_POCKET, true);
    frames(4u);
    die_at(0u, DIED_HERE);
    died_on = substeps;
    while (eng.respawns == 0u && waited < 4u * MP_SEAT_GIVE_UP_SUBSTEPS) {
        one_frame();
        ++waited;
    }
    ut_checkf(eng.respawns == 1u && substeps - died_on > MP_SEAT_GIVE_UP_SUBSTEPS,
              "the host is asked back after %u substeps, past the %u on a good anchor",
              (unsigned)(substeps - died_on), (unsigned)MP_SEAT_GIVE_UP_SUBSTEPS);
    ut_checkf(distance_2d(eng.respawned_at, POINT_NEAR) < 0.01f,
              "on the authored point nearest the anchor, itself free (%.2f %.2f)",
              (double)eng.respawned_at[0], (double)eng.respawned_at[1]);
    ut_check(eng.health == MP_RESPAWN_HEALTH, "with his health written before the re-entry");
    land_the_body();
}

/* ==============================================================================================
 * A ring around a point: reachable from the point on foot, as a ring beside a player is.
 * ============================================================================================ */

/* The point itself has a crawl space over it, so the rings are tried, and a wall runs one unit to
 * the side the ring of slot 0 starts on. The first candidates stand behind it on a floor of their
 * own, which used to be enough: only a ring beside a player asked for the walkable line. */
static void check_a_ring_around_a_point_keeps_to_its_side_of_a_wall(void)
{
    static const float POINT[3] = { -30.0f, 0.0f, 0.0f };

    ut_section("a ring around an authored point takes no seat behind a wall");
    open_the_level();
    join_a_session(false, 0u);
    wld.wall_on    = true;
    wld.wall_x     = POINT[0] + 1.0f;
    memcpy(wld.crawl[0], POINT, sizeof wld.crawl[0]);
    wld.crawl_count = 1u;
    frames(2u);
    ut_check(mp_respawn_at(POINT, 0.0f, 0u, 0u), "a wish on the point is held");
    frames(2u);
    ut_checkf(eng.respawns == 1u && eng.respawned_at[0] < wld.wall_x,
              "the seat is on the point's side of the wall (x %.2f, the wall at %.2f)",
              (double)eng.respawned_at[0], (double)wld.wall_x);
    ut_checkf(distance_2d(eng.respawned_at, POINT) < 2.5f,
              "and on the near ring around the point (%.2f units from it)",
              (double)distance_2d(eng.respawned_at, POINT));
    land_the_body();
}

/* ==============================================================================================
 * The seats that ended a life belong to their level.
 * ============================================================================================ */

/* A seat beside the mate is found and handed to the engine, and the life that began there ends
 * before the body stood: the seat and its near ring are refused for the rest of that level. The
 * next level is loaded into the same world record and its clock starts over, so a lock kept past
 * the level's end would come back to life as soon as the new clock passed the time it was taken
 * at, at the old level's coordinates. The level's end is the lock's end, and the same search in
 * the next level finds the same seat again. */
static void check_a_locked_seat_belongs_to_its_level(void)
{
    static const float MATE_STANDS[3] = { -40.0f, 0.0f, 0.0f };
    mp_seat_wish_t     wish;
    mp_seat_counts_t   counts;
    float              first[3];
    float              seat[3];
    float              heading = 0.0f;

    ut_section("a seat that ended a life is refused for the rest of its level, and no longer");
    open_the_level();
    join_a_session(true, 1u);
    place_far_player(0u, MATE_STANDS, true);
    frames(4u);
    memset(&counts, 0, sizeof counts);
    mp_seat_wish_beside_players(&wish, "the test's wish", NULL, 1u);
    ut_check(mp_seat_wish_step(&wish, substeps, &counts, first, &heading),
             "a seat beside the mate is found");
    mp_seat_note_seated(first);
    frames(4u);
    mp_seat_note_life_ended();
    mp_seat_wish_beside_players(&wish, "the test's wish", NULL, 1u);
    ut_checkf(mp_seat_wish_step(&wish, substeps, &counts, seat, &heading) &&
                  distance_2d(seat, first) >= MP_SEAT_LOCK_RADIUS,
              "in the same level that seat and everything within %.1f unit(s) of it are refused "
              "(the next seat %.2f units away)", (double)MP_SEAT_LOCK_RADIUS,
              (double)distance_2d(seat, first));

    mp_seat_world_ended();
    open_the_level();
    frames(64u);
    mp_seat_wish_beside_players(&wish, "the test's wish", NULL, 1u);
    ut_checkf(mp_seat_wish_step(&wish, substeps, &counts, seat, &heading) &&
                  distance_2d(seat, first) < 0.01f,
              "two seconds into the next level, in the same world record, the same search finds "
              "the same seat: the lock went with its level (%.2f units from it)",
              (double)distance_2d(seat, first));
}

/* ==============================================================================================
 * The arrival: its wait for the host is simulation, not drawn frames.
 * ============================================================================================ */

/* Thirty seconds of simulation at thirty two substeps a second. */
#define THIRTY_SECONDS_OF_SUBSTEPS (30u * 32u)

/* A client of a fresh co-op level, with the host's history holding a sample of tick `tick` at
 * `at` as the level begins, or nothing for `at` NULL: the host still loading his. */
static void arm_behind_a_sample(const float at[3], uint32_t tick)
{
    memset(&wire, 0, sizeof wire);
    wire.is_client   = true;
    wire.my_slot     = (uint8_t)MP_BRIDGE_LISTEN_HOST_CLIENT_SLOT;
    wire.setup_known = true;
    wire.setup.mode  = (uint8_t)MP_LOBBY_MODE_COOP;
    wire.setup.flags = (uint8_t)MP_LOBBY_F_STARTED;
    wire.starts      = 1u;
    if (at != NULL) {
        the_host_is_resolved_at(at, tick);
        place_far_player(0u, at, true);
    }
    mp_arrival_note_level_begin();
}

static void arm_a_fresh_arrival(void)
{
    arm_behind_a_sample(NULL, 0u);
}

/* The host's next sample, and his body where it stands. */
static void the_host_arrives_at(const float position[3])
{
    the_host_is_resolved_at(position, wire.sample_tick + 1u);
    place_far_player(0u, position, true);
}

static void check_the_arrival_takes_no_pose_from_before_its_level(void)
{
    static const float OLD_AT[3] = { 70.0f, 0.0f, 0.0f };
    static const float NEW_AT[3] = { -20.0f, 0.0f, 0.0f };

    ut_section("the arrival takes no pose sampled before its level began, the first level too");
    open_the_level();
    memset(far_players, 0, sizeof far_players);
    arm_behind_a_sample(OLD_AT, 100u);
    frames(8u);
    ut_checkf(wire.placed == 0u,
              "the host's pose of tick 100 is resolved again in every substep and says it lives, "
              "and nothing is handed over beside it (%u handed over)", (unsigned)wire.placed);
    the_host_arrives_at(NEW_AT);
    frames(2u);
    ut_checkf(wire.placed == 1u && distance_2d(wire.placed_at, NEW_AT) < 4.5f,
              "tick 101, somewhere else, is the first sample after the level began, and the client "
              "is seated beside it (%.2f u away)", (double)distance_2d(wire.placed_at, NEW_AT));

    ut_section("a history begun again after the level began is new, whatever tick it carries");
    open_the_level();
    memset(far_players, 0, sizeof far_players);
    arm_behind_a_sample(OLD_AT, 100u);
    frames(4u);
    wire.starts = 2u;
    the_host_is_resolved_at(NEW_AT, 5u);
    place_far_player(0u, NEW_AT, true);
    frames(2u);
    ut_checkf(wire.placed == 1u && distance_2d(wire.placed_at, NEW_AT) < 4.5f,
              "tick 5 of a new history is taken, though it is older than 100 (%u handed over)",
              (unsigned)wire.placed);
}

/* `count` drawn frames at `hz` with nobody to arrive beside, the substeps running at thirty two a
 * second under them. */
static void draw_frames_at(uint32_t count, uint32_t hz)
{
    uint32_t base = substeps;
    uint32_t frame;

    for (frame = 0; frame < count; ++frame) {
        mp_arrival_tick(base + frame * 32u / hz);
    }
    substeps = base + count * 32u / hz;
}

static void check_the_arrival_waits_the_same_at_any_frame_rate(void)
{
    static const float HOST_AT[3] = { 70.0f, 0.0f, 0.0f };

    ut_section("the arrival waits thirty seconds of simulation, at 240 frames a second as at 60");
    open_the_level();
    memset(far_players, 0, sizeof far_players);
    arm_a_fresh_arrival();
    /* 2000 drawn frames at 240 a second, past the 1800 the deadline used to count: eight and a
     * third seconds, 266 substeps. */
    draw_frames_at(2000u, 240u);
    the_host_arrives_at(HOST_AT);
    frames(2u);
    ut_checkf(wire.placed == 1u && distance_2d(wire.placed_at, HOST_AT) < 2.5f,
              "a host who loads 2000 frames longer at 240 Hz is still waited for, and the client "
              "is handed a point beside him (%u handed over)", (unsigned)wire.placed);

    ut_section("and a host who never turns up is still given up");
    ut_check(MP_ARRIVAL_DEADLINE_SUBSTEPS == THIRTY_SECONDS_OF_SUBSTEPS,
             "the deadline is thirty seconds of simulation");
    open_the_level();
    memset(far_players, 0, sizeof far_players);
    arm_a_fresh_arrival();
    draw_frames_at(THIRTY_SECONDS_OF_SUBSTEPS + 32u, 32u);
    the_host_arrives_at(HOST_AT);
    frames(2u);
    ut_checkf(wire.placed == 0u,
              "past thirty seconds of simulation the offset was dropped (%u handed over)",
              (unsigned)wire.placed);
}

int main(void)
{
    ut_check(mp_respawn_install(), "the re-entry installs over the engine this test plays");

    check_a_host_whose_last_mate_dies_ends_the_level();
    check_a_client_whose_host_dies_waits_for_him();
    check_a_named_seat_is_asked_first();
    check_a_named_seat_does_not_outlive_the_last_mate();
    check_only_its_own_wish_is_taken_back();
    check_the_field_run_ends_on_the_nearest_point();
    check_a_ring_around_a_point_keeps_to_its_side_of_a_wall();
    check_the_arrival_waits_the_same_at_any_frame_rate();
    check_the_arrival_takes_no_pose_from_before_its_level();
    check_a_locked_seat_belongs_to_its_level();

    mp_reentry_report();
    mp_respawn_report();
    return ut_summary("mp_seat_world");
}
