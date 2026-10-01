/* mp_spawnpoints.c: the buried enemy placements, read back as places to come back to.
 *
 * The rules and the reasoning are in the header. What is here is the pass over the level's
 * placement table, the greedy thinning, and the per re-entry choice, in that order.
 *
 * One property of the pass is worth having in front of a maintainer: it reads and never writes.
 * The arena writes the buried state over these records and this module runs afterwards, so the
 * two must not both be editing the table; nothing here would notice if they were.
 *
 * SIZE NOTE: over 600 lines. Four fifths of it is the reading of one placement record and the three
 * refusals around it, and the rest is the choice; a seam between the pass and the choice would put
 * the thinning on one side of it and the reuse lock on the other, and those two are the same
 * decision about the same table.
 */
#include "mp_spawnpoints.h"

#include "mp_cells.h"
#include "mp_placements.h"
#include "mp_seat_rule.h"

#include "common/logging.h"
#include "common/memory.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* How far into a placement record THIS pass reads. It wants two fields the shared reader does
 * not, the spawn gates and the position, and it stops SHORT of the spawn state that the shared
 * one ends on: the position at 0xAC plus three floats is 0xB8, against 0xCC there. The two
 * extents are different because the two readers want different fields, not because one is a
 * subset of the other, so neither can be derived from the other. */
#define PLACEMENT_READ_BYTES (MP_PLACEMENT_POSITION + 12u)

/* The two classes that stand on the ground and fight. The tank is mapped onto the ordinary enemy
 * class by the spawner before it builds anything, so both have to be named or the levels that use
 * tanks lose those points. */
#define SPAWN_CLASS_ENEMY 2
#define SPAWN_CLASS_TANK  8

typedef struct spawnpoints_state {
    mp_spawnpoint_t points[MP_SPAWNPOINTS_MAX];
    size_t          count;
    float           separation;   /* what a caller asked for, the default until it does */
    float           used;         /* what the last pass actually thinned by, 0 before one ran */
    uint32_t        relaxed;      /* passes the thinning was relaxed for on this level */
    bool            built;

    /* The pass. */
    uint32_t placements_seen;
    uint32_t placements_unreadable;
    uint32_t passed_rule;        /* qualified before thinning */
    uint32_t dropped_thinning;   /* qualified and stood too close to something already kept */
    uint32_t dropped_capacity;   /* qualified, far enough, and the table was full */
    bool     have_start;
    bool     gates_read;         /* whether the difficulty and detail cells could be read */

    /* The choice. */
    uint32_t picks_best;
    uint32_t picks_any;
    uint32_t picks_start;
    uint32_t picks_refused;
} spawnpoints_state_t;

static spawnpoints_state_t sp = { { { { 0.0f, 0.0f, 0.0f }, 0.0f, 0u, false } }, 0u,
                                  MP_SPAWNPOINTS_MIN_SEPARATION, 0.0f, 0u, false,
                                  0u, 0u, 0u, 0u, 0u, false, false, 0u, 0u, 0u, 0u };

/* ==============================================================================================
 * The pure decisions.
 * ============================================================================================ */

bool mp_spawnpoints_accepts(uint32_t flags, int32_t class_id, int32_t move_mode,
                            int32_t min_difficulty, int32_t detail_gate,
                            int32_t difficulty, int32_t detail_level)
{
    /* The cutscene handover, refused first and whatever the class says. Such a placement takes
     * the player rather than standing somewhere, and three of the twenty five that carry the bit
     * also carry an enemy class, so a filter reading the class alone would take them. */
    if ((flags & MP_PLACEMENT_F_HOSTS_PLAYER) != 0u) {
        return false;
    }
    /* Bit 0 of the flag word, scan eligibility, is deliberately not tested: a placement a script
     * spawns rather than the proximity scan is still an authored standing position, and in a
     * deathmatch the arena has buried every one of them either way. Over the eleven shipped
     * levels 1112 of the 2250 placements carry class 2 or 8, 1107 and 5. */
    if (class_id != SPAWN_CLASS_ENEMY && class_id != SPAWN_CLASS_TANK) {
        return false;
    }
    /* From move mode 2 up the engine never puts the record on the ground, so its authored height
     * is whatever the editor left it at. A point like that is not a floor. This is the filter
     * that earns its place: 106 of the 1112 enemy class records have mode 2 or more (mode 3 fifty
     * four, mode 5 thirty three, mode 6 ten, mode 2 seven, mode 4 two). */
    if (move_mode >= 2) {
        return false;
    }
    /* The engine's own two gates, in the engine's own directions: it skips a record while the
     * difficulty is below the record's minimum, and while the record's detail gate is above the
     * detail level. Read out of the activation scan at 0x00437161:
     *
     *     if (g_difficulty < rec->minDifficulty) continue;
     *     if (rec->detailGate > g_detailLevel)   continue;
     *
     * On shipped data the difficulty gate never fires, the minimum is 0 in 2250 of 2250 records;
     * it is here because the engine has it and an edited level could carry a value. The detail
     * gate does fire: eleven of the 1006 records left by the tests above carry a gate of 2 (three
     * on FEDSHIP, five on RACE, three on SWAMP), so at detail level 1 they are refused, and
     * after thinning it changes no level's count because each stands near a point kept anyway. */
    if (difficulty < min_difficulty) {
        return false;
    }
    return detail_gate <= detail_level;
}

float mp_spawnpoints_distance_sq(const float a[3], const float b[3])
{
    float dx;
    float dy;
    float dz;

    if (a == NULL || b == NULL) {
        return 0.0f;
    }
    dx = a[0] - b[0];
    dy = a[1] - b[1];
    dz = a[2] - b[2];
    return dx * dx + dy * dy + dz * dz;
}

bool mp_spawnpoints_far_enough(const float candidate[3], const mp_spawnpoint_t *kept,
                               size_t kept_count, float min_separation)
{
    size_t index;
    float  limit;

    if (candidate == NULL) {
        return false;
    }
    if (kept == NULL || kept_count == 0u) {
        return true;
    }
    limit = min_separation * min_separation;
    for (index = 0; index < kept_count; ++index) {
        if (mp_spawnpoints_distance_sq(candidate, kept[index].position) < limit) {
            return false;
        }
    }
    return true;
}

/* How far this point is from the nearest player still standing. With nobody standing every point
 * scores the same and the age below decides, which is the right answer rather than a degenerate
 * one: in a round of one there is no distance to maximise. */
static float distance_to_nearest_living(const float point[3], const float (*living)[3],
                                        size_t living_count)
{
    float  nearest = 0.0f;
    size_t index;

    for (index = 0; index < living_count; ++index) {
        float distance = mp_spawnpoints_distance_sq(point, living[index]);

        if (index == 0u || distance < nearest) {
            nearest = distance;
        }
    }
    return nearest;
}

/* How long ago this point was last used. A point never used answers the largest value there is,
 * so it wins every tie against one that has been. */
static uint32_t age_of(const mp_spawnpoint_t *point, uint32_t now)
{
    if (!point->taken) {
        return UINT32_MAX;
    }
    /* Unsigned subtraction, so the frame counter wrapping past its top gives an age rather than a
     * number close to four billion. */
    return now - point->taken_at;
}

/* The two locks, one reading each for both choices. Coming back on the square that just killed
 * you is the same death again; coming back where a team mate did a moment ago is two bodies in one
 * doorway. */
static bool inside_death_lock(const mp_spawnpoint_t *point, const float *died_at)
{
    return died_at != NULL &&
           mp_spawnpoints_distance_sq(point->position, died_at) <
               MP_SPAWNPOINTS_DEATH_LOCK * MP_SPAWNPOINTS_DEATH_LOCK;
}

static bool inside_reuse_lock(const mp_spawnpoint_t *point, uint32_t now)
{
    return age_of(point, now) < MP_SPAWNPOINTS_REUSE_LOCK;
}

static bool beats(float score, uint32_t age, float best_score, uint32_t best_age, bool have_best)
{
    if (!have_best) {
        return true;
    }
    if (score > best_score) {
        return true;
    }
    if (score < best_score) {
        return false;
    }
    return age > best_age;
}

size_t mp_spawnpoints_pick(const mp_spawnpoint_t *points, size_t count,
                           const float (*living)[3], size_t living_count,
                           const float *died_at, uint32_t now, mp_spawn_pick_tier_t *tier)
{
    size_t   best = MP_SPAWNPOINTS_NONE;
    float    best_score = 0.0f;
    uint32_t best_age = 0u;
    int      pass;

    if (tier != NULL) {
        *tier = MP_SPAWN_PICK_NONE;
    }
    if (points == NULL || count == 0u) {
        return MP_SPAWNPOINTS_NONE;
    }

    /* Pass 0 applies every rule. Pass 1 drops the reuse lock and keeps the death lock, because
     * coming back somewhere a team mate used a moment ago is a nuisance and coming back on the
     * spot that just killed you is the same death again. */
    for (pass = 0; pass < 2; ++pass) {
        size_t index;

        for (index = 0; index < count; ++index) {
            float    score;
            uint32_t age;

            if (inside_death_lock(&points[index], died_at)) {
                continue;
            }
            if (pass == 0 && inside_reuse_lock(&points[index], now)) {
                continue;
            }
            score = distance_to_nearest_living(points[index].position, living, living_count);
            age   = age_of(&points[index], now);
            if (beats(score, age, best_score, best_age, best != MP_SPAWNPOINTS_NONE)) {
                best       = index;
                best_score = score;
                best_age   = age;
            }
        }
        if (best != MP_SPAWNPOINTS_NONE) {
            if (tier != NULL) {
                *tier = (pass == 0) ? MP_SPAWN_PICK_BEST : MP_SPAWN_PICK_ANY;
            }
            return best;
        }
    }

    /* Everything was inside the death lock, which a very small level can do. The level start is
     * index 0 by construction and is taken rather than answering with nothing. */
    if (tier != NULL) {
        *tier = MP_SPAWN_PICK_START;
    }
    return 0u;
}

float mp_spawnpoints_relaxed(float separation, size_t kept, size_t qualified)
{
    float next;

    if (kept >= (size_t)MP_SPAWNPOINTS_WANTED || kept >= qualified) {
        return 0.0f;
    }
    if (!isfinite(separation) || separation <= MP_SPAWNPOINTS_SEPARATION_FLOOR) {
        return 0.0f;
    }
    next = separation * 0.5f;
    return next < MP_SPAWNPOINTS_SEPARATION_FLOOR ? MP_SPAWNPOINTS_SEPARATION_FLOOR : next;
}

/* ==============================================================================================
 * The pass over the level.
 * ============================================================================================ */

static bool pose_is_finite(const float position[3], float heading)
{
    int axis;

    if (!isfinite(heading)) {
        return false;
    }
    for (axis = 0; axis < 3; ++axis) {
        if (!isfinite(position[axis])) {
            return false;
        }
    }
    return true;
}

/* Appends when the point is finite, far enough from everything kept, and there is room. The three
 * refusals are counted apart, because "the level has few points" and "the table filled up" are
 * different findings and only one of them is a reason to raise the capacity. */
static bool append(const float position[3], float heading)
{
    if (!pose_is_finite(position, heading)) {
        return false;
    }
    if (!mp_spawnpoints_far_enough(position, sp.points, sp.count, sp.used)) {
        ++sp.dropped_thinning;
        return false;
    }
    if (sp.count >= MP_SPAWNPOINTS_MAX) {
        ++sp.dropped_capacity;
        return false;
    }
    memcpy(sp.points[sp.count].position, position, sizeof sp.points[sp.count].position);
    sp.points[sp.count].heading  = heading;
    sp.points[sp.count].taken    = false;
    sp.points[sp.count].taken_at = 0u;
    ++sp.count;
    return true;
}

/* The two settings the engine's own activation scan gates on. When either cell did not resolve
 * the gates are not applied at all and the caller says so once: a made up difficulty would refuse
 * points a level really offers, or accept points the engine would never spawn.
 *
 * The difficulty is [0x872FA0], the same operand `A1 A0 2F 87 00` the impact lookup reads five
 * bytes in; the detail level is [0x4AC538], which the scan's own gate at 0x0043721C compares
 * with `3B 05 38 C5 4A 00`. */
static bool read_gates(int32_t *difficulty, int32_t *detail_level)
{
    uintptr_t difficulty_cell = mp_cells_address(MP_CELL_IMPACT_DIFFICULTY);
    uintptr_t detail_cell     = mp_cells_address(MP_CELL_DETAIL_LEVEL);
    uint32_t  raw = 0;

    if (difficulty_cell == 0u || detail_cell == 0u) {
        return false;
    }
    if (!memory_read_u32(difficulty_cell, &raw)) {
        return false;
    }
    *difficulty = (int32_t)raw;
    if (!memory_read_u32(detail_cell, &raw)) {
        return false;
    }
    *detail_level = (int32_t)raw;
    return true;
}

/* One record, read whole. False means it could not be read at all, which is counted and skipped
 * rather than guessed at.
 *
 * The position at 0xAC and the yaw at 0x24, in degrees, have a witness independent of the
 * activation scan: script opcode 0x607, "Change Playr", warps the player with exactly those two
 * fields and hands them to player_respawnAt at 0x00447C90 as its second and third arguments. So
 * what this module reads as a place and a facing a body may take is what the engine hands to its
 * own re-entry. */
static bool read_placement(uintptr_t placement, uint32_t *flags, int32_t *class_id,
                           int32_t *move_mode, int32_t *min_difficulty, int32_t *detail_gate,
                           float position[3], float *heading)
{
    return placement != 0u &&
           memory_try_readable(placement, PLACEMENT_READ_BYTES) &&
           memory_try_read(placement + MP_PLACEMENT_FLAGS, flags, sizeof *flags) &&
           memory_try_read(placement + MP_PLACEMENT_CLASS_ID, class_id, sizeof *class_id) &&
           memory_try_read(placement + MP_PLACEMENT_MOVE_MODE, move_mode, sizeof *move_mode) &&
           memory_try_read(placement + MP_PLACEMENT_MIN_DIFF, min_difficulty,
                           sizeof *min_difficulty) &&
           memory_try_read(placement + MP_PLACEMENT_DETAIL_GATE, detail_gate,
                           sizeof *detail_gate) &&
           memory_try_read(placement + MP_PLACEMENT_POSITION, position, 12u) &&
           memory_try_read(placement + MP_PLACEMENT_START_YAW, heading, sizeof *heading);
}

/* The level's own start, appended first so that it is index 0 and the last fallback of the choice
 * needs no search. Seeding it can lower a level's count as well as raise it, because greedy
 * thinning is order dependent and a point kept early can exclude a candidate that would itself
 * have admitted two more: at the default separation RACE keeps eighteen with the start seeded
 * against nineteen without, BIGCITY twelve against thirteen. Over the eleven shipped levels the
 * pass keeps 156 points of 1003 qualified, from six on MAUL, the smallest level at sixty five
 * placements, to twenty on ESPA. */
static void append_level_start(uintptr_t world)
{
    float position[3];
    float heading = 0.0f;

    if (!memory_try_read(world + MP_LEVEL_PLAYER_START, position, sizeof position) ||
        !memory_try_read(world + MP_LEVEL_PLAYER_START_YAW, &heading, sizeof heading)) {
        return;
    }
    sp.have_start = append(position, heading);
}

/* One pass over the table at the separation the state holds. The level start goes in first, so
 * it is index 0 of every pass and of the set the last pass leaves behind. */
static void one_pass(uintptr_t world, uint32_t count, uint32_t table, int32_t difficulty,
                     int32_t detail_level)
{
    uint32_t index;

    memset(sp.points, 0, sizeof sp.points);
    sp.count                 = 0u;
    sp.have_start            = false;
    sp.placements_seen       = 0u;
    sp.placements_unreadable = 0u;
    sp.passed_rule           = 0u;
    sp.dropped_thinning      = 0u;
    sp.dropped_capacity      = 0u;

    append_level_start(world);
    for (index = 0; index < count; ++index) {
        uintptr_t placement = 0u;
        uint32_t  raw = 0u;
        uint32_t  flags = 0u;
        int32_t   class_id = 0;
        int32_t   move_mode = 0;
        int32_t   min_difficulty = 0;
        int32_t   detail_gate = 0;
        float     position[3];
        float     heading = 0.0f;

        ++sp.placements_seen;
        if (!memory_try_read((uintptr_t)table + (uintptr_t)index * 4u, &raw, sizeof raw)) {
            ++sp.placements_unreadable;
            continue;
        }
        placement = (uintptr_t)raw;
        if (!read_placement(placement, &flags, &class_id, &move_mode, &min_difficulty,
                            &detail_gate, position, &heading)) {
            ++sp.placements_unreadable;
            continue;
        }
        if (sp.gates_read) {
            if (!mp_spawnpoints_accepts(flags, class_id, move_mode, min_difficulty, detail_gate,
                                        difficulty, detail_level)) {
                continue;
            }
            /* Without the two settings the record's own gates are passed zeros on both sides,
             * which lets every record through them and leaves the class, the flag and the move
             * mode to decide. The warning above already said that this is what happened. */
        } else if (!mp_spawnpoints_accepts(flags, class_id, move_mode, 0, 0, 0, 0)) {
            continue;
        }
        ++sp.passed_rule;
        (void)append(position, heading);
    }
}

size_t mp_spawnpoints_build(void)
{
    uintptr_t world = 0u;
    uint32_t  count = 0u;
    uint32_t  table = 0u;
    int32_t   difficulty = 0;
    int32_t   detail_level = 0;

    mp_spawnpoints_clear();

    if (!mp_placements_table(&world, &count, &table)) {
        log_warning("no spawn points can be built: no world stands, or its placement table did "
                    "not read. A team deathmatch on this level has the level start and nothing "
                    "else to come back to");
        return 0u;
    }

    sp.gates_read = read_gates(&difficulty, &detail_level);
    if (!sp.gates_read) {
        log_warning("the difficulty and detail cells did not resolve, so the two gates the "
                    "engine's own spawn scan applies are NOT applied here. Some points may be "
                    "ones the engine would never have put a body on");
    }

    /* The pass is run again, closer, when it kept too few. A small level thins to nothing at
     * fifteen units, and one point is a deathmatch in which every player comes back on the same
     * square (mp_spawnpoints_relaxed says when another pass is worth making). */
    sp.used = sp.separation;
    for (;;) {
        float next;

        one_pass(world, count, table, difficulty, detail_level);
        next = mp_spawnpoints_relaxed(sp.used, sp.count, sp.passed_rule);
        if (next == 0.0f) {
            break;
        }
        ++sp.relaxed;
        sp.used = next;
    }

    sp.built = true;
    log_info("the level offers %u spawn point(s): %u placement(s) walked, %u qualified, %u "
             "dropped as too close to one already kept, %u dropped for want of room, %u "
             "unreadable; the level start is %s",
             (unsigned)sp.count, (unsigned)sp.placements_seen, (unsigned)sp.passed_rule,
             (unsigned)sp.dropped_thinning, (unsigned)sp.dropped_capacity,
             (unsigned)sp.placements_unreadable, sp.have_start ? "among them" : "NOT among them");
    if (sp.relaxed != 0u) {
        log_info("the thinning was relaxed from %.2f units to %.2f in %u further pass(es), "
                 "because a round wants %u point(s) and this level is small",
                 (double)sp.separation, (double)sp.used, (unsigned)sp.relaxed,
                 (unsigned)MP_SPAWNPOINTS_WANTED);
    }
    return sp.count;
}

void mp_spawnpoints_clear(void)
{
    memset(sp.points, 0, sizeof sp.points);
    sp.count                 = 0u;
    sp.built                 = false;
    sp.have_start            = false;
    sp.gates_read            = false;
    sp.placements_seen       = 0u;
    sp.placements_unreadable = 0u;
    sp.passed_rule           = 0u;
    sp.dropped_thinning      = 0u;
    sp.dropped_capacity      = 0u;
    sp.used                  = 0.0f;
    sp.relaxed               = 0u;
}

size_t mp_spawnpoints_count(void)
{
    return sp.count;
}

bool mp_spawnpoints_get(size_t index, float position[3], float *heading)
{
    if (index >= sp.count || position == NULL || heading == NULL) {
        return false;
    }
    memcpy(position, sp.points[index].position, sizeof sp.points[index].position);
    *heading = sp.points[index].heading;
    return true;
}

bool mp_spawnpoints_take(const float (*living)[3], size_t living_count,
                         const float *died_at, uint32_t now,
                         float position[3], float *heading)
{
    mp_spawn_pick_tier_t tier = MP_SPAWN_PICK_NONE;
    size_t               chosen;

    if (position == NULL || heading == NULL) {
        return false;
    }
    chosen = mp_spawnpoints_pick(sp.points, sp.count, living, living_count, died_at, now, &tier);
    if (chosen == MP_SPAWNPOINTS_NONE) {
        ++sp.picks_refused;
        log_warning("no spawn point could be chosen: this level built none, so a re-entry has "
                    "nowhere authored to go");
        return false;
    }

    switch (tier) {
    case MP_SPAWN_PICK_ANY:
        ++sp.picks_any;
        log_info("every spawn point was still locked from a recent re-entry, so point %u was "
                 "taken anyway", (unsigned)chosen);
        break;
    case MP_SPAWN_PICK_START:
        ++sp.picks_start;
        log_info("every spawn point stood where the player was killed, so the level's own start "
                 "was taken");
        break;
    case MP_SPAWN_PICK_BEST:
    case MP_SPAWN_PICK_NONE:
    default:
        ++sp.picks_best;
        break;
    }

    sp.points[chosen].taken    = true;
    sp.points[chosen].taken_at = now;
    memcpy(position, sp.points[chosen].position, sizeof sp.points[chosen].position);
    *heading = sp.points[chosen].heading;
    return true;
}

bool mp_spawnpoints_take_nearest(const float *anchor, const float *died_at, uint32_t now,
                                 float position[3], float *heading, bool *level_start)
{
    float  points[MP_SPAWNPOINTS_MAX][3];
    bool   unlocked[MP_SPAWNPOINTS_MAX];
    bool   start = true;
    size_t chosen = 0u;
    size_t index;

    if (position == NULL || heading == NULL || sp.count == 0u) {
        return false;
    }
    for (index = 0; index < sp.count; ++index) {
        memcpy(points[index], sp.points[index].position, sizeof points[index]);
        unlocked[index] = !inside_death_lock(&sp.points[index], died_at) &&
                          !inside_reuse_lock(&sp.points[index], now);
    }
    /* No anchor at all, a co-op player alone with nobody standing and nowhere known to have died:
     * the level start, which is where the engine itself puts a player. */
    if (anchor != NULL) {
        chosen = mp_seat_rule_nearest_free(points, unlocked, sp.count, anchor, &start);
    }
    if (chosen == MP_SEAT_NO_POINT) {
        return false;
    }
    if (level_start != NULL) {
        *level_start = start;
    }
    sp.points[chosen].taken    = true;
    sp.points[chosen].taken_at = now;
    memcpy(position, sp.points[chosen].position, sizeof sp.points[chosen].position);
    *heading = sp.points[chosen].heading;
    return true;
}

uint32_t mp_spawnpoints_now(void)
{
    uintptr_t cell = mp_cells_address(MP_CELL_CLOCK_TICKS);
    uint32_t  ticks = 0u;

    if (cell == 0u || !memory_read_u32(cell, &ticks)) {
        return 0u;
    }
    return ticks;
}

bool mp_spawnpoints_set_separation(float units)
{
    if (!isfinite(units) || units <= 0.0f) {
        log_warning("a spawn point separation of %.2f is not a distance, so the current %.2f is "
                    "kept", (double)units, (double)sp.separation);
        return false;
    }
    sp.separation = units;
    return true;
}

void mp_spawnpoints_report(void)
{
    if (!sp.built) {
        /* Said rather than skipped: a silent report is how a module ships built and never
         * called, and this line is the one that would have been missing. */
        log_warning("  the spawn points: no level was ever walked for them, so every re-entry "
                    "that asked for one was refused");
        return;
    }
    log_info("  the spawn points: %u kept of %u placement(s) walked, %u qualified, %u too close, "
             "%u for want of room, %u unreadable; the two engine gates were %s",
             (unsigned)sp.count, (unsigned)sp.placements_seen, (unsigned)sp.passed_rule,
             (unsigned)sp.dropped_thinning, (unsigned)sp.dropped_capacity,
             (unsigned)sp.placements_unreadable, sp.gates_read ? "applied" : "NOT applied");
    log_info("  the thinning: %.2f units asked for, %.2f used, %u relaxing pass(es) because the "
             "level is small", (double)sp.separation, (double)sp.used, (unsigned)sp.relaxed);
    log_info("  the spawn choice: %u by distance, %u with the reuse lock dropped, %u fell back "
             "to the level start, %u refused because the level had no points",
             (unsigned)sp.picks_best, (unsigned)sp.picks_any, (unsigned)sp.picks_start,
             (unsigned)sp.picks_refused);
}
