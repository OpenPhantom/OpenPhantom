/* mp_seat_rule.c: the seat rules, pure. See the header.
 *
 * Nothing here touches the engine. The search that asks the engine's probes is mp_seat; what it
 * does with their answers, and when it gives up asking, is decided here.
 */
#include "mp_seat_rule.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* How many directions apart two neighbouring slots start their rings. Two of eight is a quarter
 * turn, so the four players a session holds start in four different quarters. */
#define DIRECTIONS_PER_SLOT 2u

size_t mp_seat_rule_ring_start(uint8_t slot)
{
    return ((size_t)slot * DIRECTIONS_PER_SLOT) % (size_t)MP_SEAT_RING_STEPS;
}

void mp_seat_rule_ring_offset(size_t start, size_t step, float radius, float offset[2])
{
    /* The wrap is here rather than at the caller, so a loop with the wrong bound cannot ask for a
     * direction this function never meant to produce. */
    double angle;

    if (offset == NULL) {
        return;
    }
    angle = (double)((start + step) % (size_t)MP_SEAT_RING_STEPS) *
            (6.283185307179586 / (double)MP_SEAT_RING_STEPS);
    offset[0] = (float)(cos(angle) * (double)radius);
    offset[1] = (float)(sin(angle) * (double)radius);
}

mp_seat_verdict_t mp_seat_rule_floor_verdict(mp_seat_floor_t floor)
{
    switch (floor) {
    case MP_SEAT_FLOOR_NONE:  return MP_SEAT_NO_FLOOR;
    case MP_SEAT_FLOOR_MOVER: return MP_SEAT_ON_MOVER;
    case MP_SEAT_FLOOR_FAR:   return MP_SEAT_A_DROP;
    case MP_SEAT_FLOOR_OK:
    default:                  return MP_SEAT_FREE;
    }
}

/* How far inside a radius a seat has to lie to count as within it. A ring candidate is placed with
 * a cosine and a sine, and one at exactly the radius comes out a few millionths either side. */
#define RING_SLACK 0.01f

mp_seat_verdict_t mp_seat_rule_surface_verdict(uint16_t surface)
{
    if ((surface & MP_SEAT_SURFACE_HURTS) != 0u) {
        return MP_SEAT_HURTS;
    }
    if ((surface & MP_SEAT_SURFACE_WATER) != 0u) {
        return MP_SEAT_WATER;
    }
    return MP_SEAT_FREE;
}

static float distance_sq(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];
    float dz = a[2] - b[2];

    return dx * dx + dy * dy + dz * dz;
}

bool mp_seat_rule_within(const float seat[3], const float point[3], float radius)
{
    float inside = radius - RING_SLACK;

    if (seat == NULL || point == NULL || !(inside > 0.0f)) {
        return false;
    }
    return distance_sq(seat, point) < inside * inside;
}

bool mp_seat_rule_ended_a_life(bool landed, float landed_at, float ended_at)
{
    /* A body that died before it was seen standing never had a life anywhere else. */
    if (!landed) {
        return true;
    }
    if (!(ended_at >= landed_at)) {
        return false;
    }
    return ended_at - landed_at < MP_SEAT_KILL_WINDOW_SECONDS;
}

bool mp_seat_rule_near_a_body(const float seat[3], const mp_seat_body_t *bodies, size_t count,
                              float clearance)
{
    /* Standing or not: a body lying on the floor is still a body the next one would be put
     * inside. The distance is in three dimensions, so a player on the floor above is not in the
     * way of a seat on the floor below. */
    float  limit = clearance * clearance;
    size_t i;

    if (seat == NULL || bodies == NULL) {
        return false;
    }
    for (i = 0; i < count; ++i) {
        if (bodies[i].known && distance_sq(seat, bodies[i].position) < limit) {
            return true;
        }
    }
    return false;
}

size_t mp_seat_rule_lower_slots(const uint8_t *roster_slots, size_t count, uint8_t host_slot,
                                uint8_t my_slot, uint8_t *lower, size_t lower_max)
{
    size_t  taken = 0u;
    uint8_t slot;

    if (roster_slots == NULL || lower == NULL) {
        return 0u;
    }
    /* By value rather than by the roster's order, so the list comes out lowest first and each slot
     * once however the roster lists them. */
    for (slot = 0u; slot < my_slot && taken < lower_max; ++slot) {
        size_t i;
        bool   listed = false;

        if (slot == host_slot) {
            continue;
        }
        for (i = 0; i < count && !listed; ++i) {
            listed = roster_slots[i] == slot;
        }
        if (listed) {
            lower[taken++] = slot;
        }
    }
    return taken;
}

size_t mp_seat_rule_anchor_order(const mp_seat_body_t *bodies, size_t count, const float *died_at,
                                 size_t *order, size_t order_max)
{
    size_t taken = 0u;
    size_t i;

    if (bodies == NULL || order == NULL) {
        return 0u;
    }
    /* An insertion sort over at most a handful of bodies. Strictly nearer moves ahead, so bodies
     * at the same distance keep their index order. */
    for (i = 0; i < count; ++i) {
        float  distance;
        size_t at;

        if (!bodies[i].known || !bodies[i].stands) {
            continue;
        }
        distance = died_at != NULL ? distance_sq(bodies[i].position, died_at) : 0.0f;
        at = taken;
        while (at > 0u && died_at != NULL &&
               distance < distance_sq(bodies[order[at - 1u]].position, died_at)) {
            if (at < order_max) {
                order[at] = order[at - 1u];
            }
            --at;
        }
        if (at < order_max) {
            order[at] = i;
        }
        if (taken < order_max) {
            ++taken;
        }
    }
    return taken;
}

void mp_seat_rule_wait_start(mp_seat_wait_t *wait)
{
    if (wait != NULL) {
        wait->started = false;
        wait->last    = 0u;
        wait->good    = 0u;
        wait->total   = 0u;
    }
}

bool mp_seat_rule_wait_look(mp_seat_wait_t *wait, uint32_t now, mp_seat_look_t look)
{
    uint32_t passed;

    if (wait == NULL) {
        return false;
    }
    /* The substeps since the last look belong to what this look found. The first look has nothing
     * behind it; a look with the gates shut only moves the mark, so the time a cutscene or a load
     * held the player is not time the search spent. Unsigned subtraction reads a wrapped counter
     * right. */
    passed = wait->started ? now - wait->last : 0u;
    wait->started = true;
    wait->last    = now;
    if (look == MP_SEAT_LOOK_NONE) {
        return false;
    }
    wait->total += passed;
    if (look == MP_SEAT_LOOK_EMPTY) {
        wait->good += passed;
    }
    return wait->good >= MP_SEAT_GIVE_UP_SUBSTEPS || wait->total >= MP_SEAT_WAIT_CAP_SUBSTEPS;
}

mp_seat_stage_t mp_seat_rule_next_stage(mp_seat_stage_t stage, bool beside)
{
    if (stage == MP_SEAT_STAGE_ANCHOR && beside) {
        return MP_SEAT_STAGE_FALLBACK;
    }
    return MP_SEAT_STAGE_AS_AUTHORED;
}

size_t mp_seat_rule_nearest_free(const float (*points)[3], const bool *unlocked, size_t count,
                                 const float anchor[3], bool *level_start)
{
    size_t best = MP_SEAT_NO_POINT;
    float  best_distance = 0.0f;
    size_t i;

    if (level_start != NULL) {
        *level_start = false;
    }
    if (points == NULL || anchor == NULL || count == 0u) {
        return MP_SEAT_NO_POINT;
    }
    for (i = 0; i < count; ++i) {
        float distance;

        if (unlocked != NULL && !unlocked[i]) {
            continue;
        }
        distance = distance_sq(points[i], anchor);
        if (best == MP_SEAT_NO_POINT || distance < best_distance) {
            best          = i;
            best_distance = distance;
        }
    }
    /* Everything locked: the level start, which is where the engine itself puts a player and so
     * the one point that is never nothing. */
    if (best == MP_SEAT_NO_POINT) {
        best = 0u;
    }
    if (level_start != NULL) {
        *level_start = best == 0u;
    }
    return best;
}
