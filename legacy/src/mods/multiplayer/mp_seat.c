/* mp_seat.c: the engine's three probes asked for a seat. See the header. The wish that waits for
 * one is mp_seat_wish.c.
 *
 * Two things about the code are worth having in front of a maintainer, because neither is visible
 * in a type:
 *
 * The ground contact block is an in and out parameter. The ground probe reads the mover fields of
 * the block it is handed BEFORE it clears the rest, so that it can re-test the platform the last
 * probe stood on. Every block this file hands it is zeroed first, which switches that fast path off
 * and makes each probe an independent reading, which is what a search needs.
 *
 * Everything is measured from the anchor ON ITS FLOOR, not from where it was reported. The walkable
 * probe runs its own probes at the destination's height, so two points at a height nobody stands
 * at would ask about a line nobody walks.
 *
 * This is the one search every seat goes through, the re-entry's, the arrival's and the host's
 * place for a scene: the probes, the one ring loop with its order, the verdict per candidate, the
 * places a wish keeps away from and the report. The wish itself is mp_seat_wish.c. Should the
 * file pass 600 lines again, the seam is the seats that ended a life, mp_seat_note_seated to
 * mp_seat_world_ended, which only judge reads.
 */
#include "mp_seat.h"

#include "mp_cells.h"
#include "mp_seat_internal.h"
#include "mp_signatures.h"
#include "mp_signatures_world.h"

#include "common/logging.h"
#include "common/memory.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The game mode cell: 2 is a level running. */
#define GAME_MODE_RUNNING 2u

/* The ground contact block, in dwords. The probe clears exactly 0x22 of them and its last field,
 * the floor polygon's four world vertices, ends on that boundary. A block shorter than this is one
 * the probe writes past. */
#define GROUND_CONTACT_WORDS 0x22u

/* The three fields of that block this module reads, as dword indices: the signed height of the
 * floor over the probed point, the face that floor is, and whether it belongs to a mover. */
#define GROUND_DIST_WORD      0u
#define GROUND_POLY_WORD      5u   /* byte offset 0x14 */
#define GROUND_ON_MOVER_WORD  6u   /* byte offset 0x18 */

/* The face's surface word, sixteen bits, the one the player's own ground probe tests for the floor
 * that hurts and the death entry tests for water. */
#define POLY_SURFACE 0x3Cu

/* The world's own clock, in seconds since the level began, as the substep loop sets it. */
#define WORLD_CLOCK_SECONDS 0x54u

/* The probes take pointers and plain values and the caller cleans up, so cdecl throughout; the two
 * that answer a distance return it on the x87 stack, which is what a float return is here. */
typedef void(__cdecl *probe_floor_fn_t)(const float position[3], void *ground);
typedef float(__cdecl *head_clearance_fn_t)(const float position[3], uint16_t mask);
typedef float(__cdecl *walkable_distance_fn_t)(uintptr_t world, const float from[3],
                                              const float to[3]);

typedef union ground_contact {
    float   f[GROUND_CONTACT_WORDS];
    int32_t i[GROUND_CONTACT_WORDS];
} ground_contact_t;

_Static_assert(sizeof(ground_contact_t) == 0x88u,
               "the ground contact block the engine's probe fills is 0x88 bytes");

/* Every verdict has a counter, the free one included, so the table indexes by the enum. */
_Static_assert(MP_SEAT_VERDICTS == 11u, "one refusal counter per verdict");

/* A place this player is kept away from, in the world it was noted in and at the world time it was
 * noted. The level's end clears every one of them through mp_seat_world_ended; the world and the
 * time are only asked on top, so that a lock can never apply to a world it was not noted in, nor
 * to a clock that has started over behind it. */
typedef struct seat_place {
    bool     known;
    uint32_t world;
    float    time;
    float    at[3];
} seat_place_t;

typedef struct seat_state {
    bool installed;

    probe_floor_fn_t       probe_floor;
    head_clearance_fn_t    head_clearance;
    walkable_distance_fn_t walkable_distance;
    uintptr_t              level_cell;
    uintptr_t              mode_cell;

    mp_seat_body_t far_bodies[MP_SEAT_FAR_BODIES];

    /* The last seat handed to the engine, and whether and when a living body stood on it. */
    seat_place_t seated;
    bool         seated_landed;
    seat_place_t locks[MP_SEAT_LOCKS];
    size_t       next_lock;
} seat_state_t;

static seat_state_t ss;

/* ==============================================================================================
 * Installation and the two readings every search starts from.
 * ============================================================================================ */

void mp_seat_install(void)
{
    if (ss.installed) {
        return;
    }
    /* The three probes are this module's own table and nothing else resolves it. */
    (void)mp_signatures_world_resolve();
    ss.probe_floor =
        (probe_floor_fn_t)mp_signatures_world_address(MP_WORLD_SITE_PROBE_FLOOR);
    ss.head_clearance =
        (head_clearance_fn_t)mp_signatures_world_address(MP_WORLD_SITE_HEAD_CLEARANCE);
    ss.walkable_distance =
        (walkable_distance_fn_t)mp_signatures_world_address(MP_WORLD_SITE_WALKABLE_DISTANCE);
    ss.level_cell = mp_cells_address(MP_CELL_LEVEL);
    ss.mode_cell  = mp_cells_address(MP_CELL_GAME_MODE);

    /* Optional, and the loss is named, because a seat that puts a body inside a wall reads exactly
     * like a working feature until somebody plays it. */
    if (!mp_seat_probes_resolved()) {
        log_warning("the world probes did not all resolve, so no seat beside a living player can "
                    "be probed: the ground probe is %s, the head clearance is %s, the walkable "
                    "line is %s and the world pointer is %s",
                    ss.probe_floor != NULL ? "resolved" : "NOT resolved",
                    ss.head_clearance != NULL ? "resolved" : "NOT resolved",
                    ss.walkable_distance != NULL ? "resolved" : "NOT resolved",
                    ss.level_cell != 0u ? "resolved" : "NOT resolved");
    }
    ss.installed = true;
}

bool mp_seat_probes_resolved(void)
{
    return ss.probe_floor != NULL && ss.head_clearance != NULL && ss.walkable_distance != NULL &&
           ss.level_cell != 0u;
}

bool mp_seat_level_running(void)
{
    uint32_t mode = 0u;

    return ss.mode_cell != 0u && memory_try_read_u32(ss.mode_cell, &mode) &&
           mode == GAME_MODE_RUNNING;
}

bool mp_seat_world_now(uint32_t *world, float *seconds)
{
    *world   = 0u;
    *seconds = 0.0f;
    return ss.level_cell != 0u && memory_try_read_u32(ss.level_cell, world) && *world != 0u &&
           memory_try_read((uintptr_t)*world + WORLD_CLOCK_SECONDS, seconds, sizeof *seconds);
}

mp_seat_floor_t mp_seat_floor_state(float floor_distance, bool on_mover)
{
    /* The engine writes its float maximum for "no floor under this point" and its own readers
     * compare against that value, so this does too. A distance that is not a number lands in the
     * same answer, because the only thing a caller can do with either is wait. Swimming is the
     * case the band exists for: the probe does not fail on it, it answers the riverbed under the
     * water, because the surface is a separate mechanism, a polygon whose footstep material nibble
     * is 14, read by the water height routine at 0x0040C2BE and never consulted by the ground
     * probe. */
    if (!isfinite(floor_distance) || floor_distance >= MP_PROBE_NO_FLOOR) {
        return MP_SEAT_FLOOR_NONE;
    }
    /* Asked before the band, because a body on a lift reads an ordinary distance: it stands on the
     * platform. What makes it unusable is that the walkable probe clears the mover flag before
     * each of its own probes and therefore never sees the platform at all. */
    if (on_mover) {
        return MP_SEAT_FLOOR_MOVER;
    }
    if (floor_distance > MP_SEAT_FLOOR_BAND || floor_distance < -MP_SEAT_FLOOR_BAND) {
        return MP_SEAT_FLOOR_FAR;
    }
    return MP_SEAT_FLOOR_OK;
}

/* The pump hands every far bank in, bank 1 first, on every frame of a session: once the last one
 * is in, the far players are one frame's reading, and that is when a seat handed out is watched. */
static void note_the_bank(size_t index)
{
    if (index == MP_SEAT_FAR_BODIES - 1u) {
        mp_seat_wish_far_bodies_noted();
    }
}

void mp_seat_note_body(size_t index, const float position[3], float heading, bool stands)
{
    if (index >= MP_SEAT_FAR_BODIES || position == NULL) {
        return;
    }
    ss.far_bodies[index].known  = true;
    ss.far_bodies[index].stands = stands;
    memcpy(ss.far_bodies[index].position, position, sizeof ss.far_bodies[index].position);
    ss.far_bodies[index].heading = heading;
    note_the_bank(index);
}

void mp_seat_note_no_body(size_t index)
{
    if (index < MP_SEAT_FAR_BODIES) {
        ss.far_bodies[index].known  = false;
        ss.far_bodies[index].stands = false;
        note_the_bank(index);
    }
}

const mp_seat_body_t *mp_seat_far_bodies(void)
{
    return ss.far_bodies;
}

/* ==============================================================================================
 * One candidate.
 * ============================================================================================ */

/* One reading of the ground under one point, on a zeroed block: see the file comment. `surface`
 * gets the floor face's surface word, nought when there is no face or it does not read. */
static mp_seat_floor_t read_floor(const float position[3], float *distance, uint16_t *surface)
{
    ground_contact_t ground;
    uint32_t         face;

    memset(&ground, 0, sizeof ground);
    ss.probe_floor(position, &ground);
    *distance = ground.f[GROUND_DIST_WORD];
    *surface  = 0u;
    face      = (uint32_t)ground.i[GROUND_POLY_WORD];
    if (face != 0u && !memory_try_read((uintptr_t)face + POLY_SURFACE, surface, sizeof *surface)) {
        *surface = 0u;
    }
    return mp_seat_floor_state(*distance, ground.i[GROUND_ON_MOVER_WORD] != 0);
}

/* Whether a candidate lies near a seat that ended a life of this player in the world running
 * now. */
static bool near_a_locked_seat(const float seat[3])
{
    uint32_t world = 0u;
    float    now = 0.0f;
    size_t   i;

    if (!mp_seat_world_now(&world, &now)) {
        return false;
    }
    for (i = 0; i < MP_SEAT_LOCKS; ++i) {
        const seat_place_t *lock = &ss.locks[i];

        if (lock->known && lock->world == world && lock->time <= now &&
            mp_seat_rule_within(seat, lock->at, MP_SEAT_LOCK_RADIUS)) {
            return true;
        }
    }
    return false;
}

/* Zero means the whole line is walkable; any other value is the distance to the cell that stopped
 * it. */
static bool line_is_walkable(const float from[3], const float to[3])
{
    uint32_t world = 0u;

    if (!memory_try_read_u32(ss.level_cell, &world) || world == 0u) {
        return false;
    }
    return ss.walkable_distance((uintptr_t)world, from, to) == 0.0f;
}

/* The verdict on one candidate, in the order the header of mp_seat_rule gives: the walkable line
 * first because it is the cheapest probe to reject a candidate behind a wall, the floor next
 * because its answer is also the snap, what that floor is, the places this player is kept away
 * from, the bodies on the snapped seat, and the head clearance last because it has to run at the
 * snapped height.
 * `on_a_ring` asks for the walkable line from the anchor, which every ring candidate needs, around
 * a body and around a point alike: a floor of its own within the band is also what a room behind
 * a thin wall has. `seat` carries the snapped point. */
static mp_seat_verdict_t judge(const float anchor[3], const float candidate[3], bool on_a_ring,
                               const mp_seat_body_t *bodies, size_t body_count,
                               const mp_seat_avoid_t *avoid, float seat[3])
{
    float             distance = 0.0f;
    uint16_t          surface = 0u;
    mp_seat_floor_t   floor;
    mp_seat_verdict_t verdict;

    if (on_a_ring && !line_is_walkable(anchor, candidate)) {
        return MP_SEAT_NOT_WALKABLE;
    }
    floor = read_floor(candidate, &distance, &surface);
    if (floor != MP_SEAT_FLOOR_OK) {
        return mp_seat_rule_floor_verdict(floor);
    }
    verdict = mp_seat_rule_surface_verdict(surface);
    if (verdict != MP_SEAT_FREE) {
        return verdict;
    }
    seat[0] = candidate[0];
    seat[1] = candidate[1];
    seat[2] = candidate[2] + distance;   /* the probe's distance IS the snap */
    if (avoid != NULL && avoid->death != NULL &&
        mp_seat_rule_within(seat, avoid->death, MP_SEAT_DEATH_CLEARANCE)) {
        return MP_SEAT_NEAR_DEATH;
    }
    if (avoid != NULL && avoid->locks && near_a_locked_seat(seat)) {
        return MP_SEAT_ENDED_A_LIFE;
    }
    if (mp_seat_rule_near_a_body(seat, bodies, body_count, MP_SEAT_BODY_CLEARANCE)) {
        return MP_SEAT_TAKEN;
    }
    /* Zero means clear. The probe counts only faces carrying the mask it is handed, and the low
     * ceiling flag is the only mask the shipped game passes it, so this asks whether an authored
     * crawl space lies over the seat rather than whether any geometry does. */
    return ss.head_clearance(seat, (uint16_t)MP_SURF_LOW_CEILING) == 0.0f ? MP_SEAT_FREE
                                                                          : MP_SEAT_LOW_CEILING;
}

/* The reason an anchor was refused, so the report can say which of the waiting cases the field
 * actually meets. */
static void count_anchor(mp_seat_counts_t *counts, mp_seat_floor_t floor)
{
    switch (floor) {
    case MP_SEAT_FLOOR_NONE:
        ++counts->anchor_falling;
        break;
    case MP_SEAT_FLOOR_MOVER:
        ++counts->anchor_on_mover;
        break;
    case MP_SEAT_FLOOR_FAR:
        ++counts->anchor_far_floor;
        break;
    case MP_SEAT_FLOOR_OK:
    default:
        break;
    }
}

/* A refused candidate, by its reason. One taken by nothing standing in the world, only by a seat a
 * lower slot of the session is foreseen to take, is counted as held for it as well. */
static void count_refused(mp_seat_counts_t *counts, mp_seat_verdict_t verdict,
                          const float seat[3], const mp_seat_body_t *bodies, size_t body_count,
                          const mp_seat_avoid_t *avoid)
{
    ++counts->refused[verdict];
    if (verdict == MP_SEAT_TAKEN && avoid != NULL && avoid->in_the_world < body_count &&
        !mp_seat_rule_near_a_body(seat, bodies, avoid->in_the_world, MP_SEAT_BODY_CLEARANCE)) {
        ++counts->order_held;
    }
}

/* The one ring loop. Every search runs it, whatever its order of directions and whatever it
 * refuses on top of the probes: the rings outside, the near one first, and on each ring the
 * directions in the order handed in. `anchor_floor` gets the reading of the floor under the
 * target. */
static mp_seat_outcome_t search_the_rings(const float target[3], bool beside,
                                          const mp_seat_search_t *search,
                                          const mp_seat_body_t *bodies, size_t body_count,
                                          const mp_seat_avoid_t *avoid,
                                          mp_seat_counts_t *counts, float seat[3],
                                          mp_seat_floor_t *anchor_floor)
{
    static const float RINGS[2] = { MP_SEAT_RING_NEAR, MP_SEAT_RING_FAR };
    float              anchor[3];
    float              distance = 0.0f;
    uint16_t           surface = 0u;
    mp_seat_floor_t    floor;
    mp_seat_verdict_t  verdict;
    size_t             ring;
    size_t             step;
    bool               rings_walkable;

    if (!ss.installed || target == NULL || seat == NULL || counts == NULL) {
        return MP_SEAT_NO_PROBES;
    }
    /* The probes read the world the level built. With none standing there is nothing to probe,
     * and the pointer they would follow belongs to the level before. */
    if (!mp_seat_level_running()) {
        return MP_SEAT_NO_LEVEL;
    }
    rings_walkable = ss.walkable_distance != NULL && ss.level_cell != 0u;
    if (ss.probe_floor == NULL || ss.head_clearance == NULL || (beside && !rings_walkable)) {
        return MP_SEAT_NO_PROBES;
    }
    ++counts->anchors_tried;
    floor = read_floor(target, &distance, &surface);
    *anchor_floor = floor;
    if (floor != MP_SEAT_FLOOR_OK) {
        count_anchor(counts, floor);
        return MP_SEAT_ANCHOR_MOVING;
    }
    anchor[0] = target[0];
    anchor[1] = target[1];
    anchor[2] = target[2] + distance;

    if (!beside) {
        verdict = judge(anchor, anchor, false, bodies, body_count, avoid, seat);
        if (verdict == MP_SEAT_FREE) {
            return MP_SEAT_FOUND;
        }
        count_refused(counts, verdict, seat, bodies, body_count, avoid);
    }
    /* No ring candidate is vouched for without the walkable line, so a point is then taken as
     * itself or not at all. */
    if (!rings_walkable) {
        return MP_SEAT_NONE_FREE;
    }
    for (ring = 0; ring < sizeof RINGS / sizeof RINGS[0]; ++ring) {
        for (step = 0; step < (size_t)MP_SEAT_RING_STEPS; ++step) {
            float offset[2];
            float candidate[3];

            mp_seat_rule_ring_offset(search->order[step], 0u, RINGS[ring], offset);
            candidate[0] = anchor[0] + offset[0];
            candidate[1] = anchor[1] + offset[1];
            candidate[2] = anchor[2];
            verdict = judge(anchor, candidate, true, bodies, body_count, avoid, seat);
            if (verdict == MP_SEAT_FREE) {
                return MP_SEAT_FOUND;
            }
            count_refused(counts, verdict, seat, bodies, body_count, avoid);
        }
    }
    return MP_SEAT_NONE_FREE;
}

mp_seat_outcome_t mp_seat_probe_avoiding(const float target[3], bool beside, uint8_t slot,
                                         const mp_seat_body_t *bodies, size_t body_count,
                                         const mp_seat_avoid_t *avoid, mp_seat_counts_t *counts,
                                         float seat[3])
{
    mp_seat_search_t search;
    mp_seat_floor_t  floor = MP_SEAT_FLOOR_OK;

    memset(&search, 0, sizeof search);
    mp_seat_rule_slot_order(slot, search.order);
    return search_the_rings(target, beside, &search, bodies, body_count, avoid, counts, seat,
                            &floor);
}

mp_seat_outcome_t mp_seat_probe_ordered(const float target[3], bool beside,
                                        const mp_seat_search_t *search,
                                        const mp_seat_body_t *bodies, size_t body_count,
                                        mp_seat_counts_t *counts, float seat[3],
                                        mp_seat_floor_t *anchor_floor)
{
    mp_seat_floor_t floor = MP_SEAT_FLOOR_OK;

    if (search == NULL) {
        return MP_SEAT_NO_PROBES;
    }
    if (anchor_floor != NULL) {
        *anchor_floor = MP_SEAT_FLOOR_OK;
    }
    return search_the_rings(target, beside, search, bodies, body_count, NULL, counts, seat,
                            anchor_floor != NULL ? anchor_floor : &floor);
}

/* ==============================================================================================
 * The seats that ended a life.
 * ============================================================================================ */

void mp_seat_note_seated(const float seat[3])
{
    uint32_t world = 0u;
    float    now = 0.0f;

    ss.seated.known = seat != NULL && mp_seat_world_now(&world, &now);
    ss.seated_landed = false;
    if (ss.seated.known) {
        ss.seated.world = world;
        ss.seated.time  = now;
        memcpy(ss.seated.at, seat, sizeof ss.seated.at);
    }
}

void mp_seat_note_landed(void)
{
    uint32_t world = 0u;
    float    now = 0.0f;

    if (ss.seated.known && mp_seat_world_now(&world, &now) && world == ss.seated.world) {
        ss.seated_landed = true;
        ss.seated.time   = now;
    }
}

void mp_seat_note_life_ended(void)
{
    uint32_t      world = 0u;
    float         now = 0.0f;
    seat_place_t *lock;

    if (!ss.seated.known || !mp_seat_world_now(&world, &now) || world != ss.seated.world) {
        ss.seated.known = false;
        return;
    }
    ss.seated.known = false;
    if (!mp_seat_rule_ended_a_life(ss.seated_landed, ss.seated.time, now)) {
        return;
    }
    lock        = &ss.locks[ss.next_lock];
    *lock       = ss.seated;
    lock->known = true;
    lock->time  = now;
    ss.next_lock = (ss.next_lock + 1u) % (size_t)MP_SEAT_LOCKS;
    log_info("the seat at %.2f %.2f %.2f ended a life of this player %s, so it and everything "
             "within %.1f unit(s) of it are refused to him for the rest of this level",
             (double)lock->at[0], (double)lock->at[1], (double)lock->at[2],
             ss.seated_landed ? "within seconds of the landing" : "before the body stood on it",
             (double)MP_SEAT_LOCK_RADIUS);
}

/* The level's end is the locks' end: the next level may be loaded into the same world record, and
 * a lock kept past it would come back to life at the old level's coordinates as soon as the new
 * clock passed the time it was taken at. The seat last handed to the engine goes as well, so a
 * death in the next level cannot lock a seat of this one. The wish's watch on a seat it handed out
 * ends here too. */
void mp_seat_world_ended(void)
{
    memset(&ss.seated, 0, sizeof ss.seated);
    ss.seated_landed = false;
    memset(ss.locks, 0, sizeof ss.locks);
    ss.next_lock = 0u;
    mp_seat_wish_world_ended();
}

/* ==============================================================================================
 * The report.
 * ============================================================================================ */

/* The two lines of a caller whose wishes are seated after lower slots, under its seat line: what
 * the order did to the search, and whether the seat handed out stayed clear of the other players.
 * A caller with no such wish has neither. */
static void report_the_order(const mp_seat_counts_t *counts)
{
    if (counts->order_who == NULL) {
        return;
    }
    log_info("  %s's order: this player is seated after %u lower slot(s) of the session; %u "
             "candidate(s) held for the seat a lower slot takes, %u search(es) that found none for "
             "a lower slot, %u wish(es) begun with no roster to read the session from",
             counts->order_who, (unsigned)counts->order_lower, (unsigned)counts->order_held,
             (unsigned)counts->order_none, (unsigned)counts->order_no_roster);
    log_info("  %s's neighbours: the nearest far player %.2f u from the seat as it was handed over "
             "and %.2f u two seconds later; %u hand-over(s) within a unit of a far player (must be "
             "0)", counts->order_who, (double)counts->handed_nearest,
             (double)counts->later_nearest, (unsigned)counts->handed_close);
}

void mp_seat_report(const char *seat_label, const char *fallback_label,
                    const mp_seat_counts_t *counts)
{
    if (seat_label == NULL || fallback_label == NULL || counts == NULL) {
        return;
    }
    mp_seat_report_searches(seat_label, counts);
    report_the_order(counts);
    log_info("  %s %u time(s) to a spawn point near the anchor, %u to the level start, %u on the "
             "point as the level authored it; the longest wait on a good anchor %u substep(s)",
             fallback_label, (unsigned)counts->to_point, (unsigned)counts->to_start,
             (unsigned)counts->as_authored, (unsigned)counts->longest_wait);
}

void mp_seat_report_searches(const char *seat_label, const mp_seat_counts_t *counts)
{
    if (seat_label == NULL || counts == NULL) {
        return;
    }
    log_info("  %s %u search(es), %u of them found nothing; the anchor was falling %u time(s), "
             "on a mover %u and over water or a drop %u; candidates refused: %u not walkable, %u "
             "no floor, %u a drop, %u a low ceiling, %u taken by a body, %u on a mover; anchors "
             "tried: %u (%u live re-reads), %u search(es) with nobody standing, %u seat(s) on "
             "the anchor named by the host's scene; also refused: %u a hurting floor, "
             "%u water, %u too near the death, %u a seat that ended a life",
             seat_label, (unsigned)counts->searches, (unsigned)counts->found_nothing,
             (unsigned)counts->anchor_falling, (unsigned)counts->anchor_on_mover,
             (unsigned)counts->anchor_far_floor,
             (unsigned)counts->refused[MP_SEAT_NOT_WALKABLE],
             (unsigned)counts->refused[MP_SEAT_NO_FLOOR],
             (unsigned)counts->refused[MP_SEAT_A_DROP],
             (unsigned)counts->refused[MP_SEAT_LOW_CEILING],
             (unsigned)counts->refused[MP_SEAT_TAKEN],
             (unsigned)counts->refused[MP_SEAT_ON_MOVER],
             (unsigned)counts->anchors_tried, (unsigned)counts->live_reads,
             (unsigned)counts->nobody_standing, (unsigned)counts->on_named,
             (unsigned)counts->refused[MP_SEAT_HURTS],
             (unsigned)counts->refused[MP_SEAT_WATER],
             (unsigned)counts->refused[MP_SEAT_NEAR_DEATH],
             (unsigned)counts->refused[MP_SEAT_ENDED_A_LIFE]);
}
