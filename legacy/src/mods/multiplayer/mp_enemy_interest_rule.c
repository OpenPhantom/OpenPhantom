/* mp_enemy_interest_rule.c: which enemies a host describes to one peer. See the header. */
#include "mp_enemy_interest_rule.h"

#include "mp_range_gate_rule.h"

#include <stddef.h>
#include <stdlib.h>

/* The share of the keep radius a scaled wake radius may reach: the activation site's own limit,
 * so that an actor is not made and removed again on every substep. */
#define WAKE_KEEP_SHARE 0.89f

/* The reaction states a watcher sees as a change: a throw from the knock back to getting up, and
 * the death from its clip to the corpse. Every other state reads as the ordinary one. */
#define STATE_THROW_FIRST 6u
#define STATE_THROW_LAST  9u

static bool radius_holds(const float at[3], const float player[3], float radius, bool widened)
{
    return mp_range_gate_within(at, player, widened ? radius * MP_ENEMY_INTEREST_HYSTERESIS
                                                    : radius);
}

mp_enemy_reach_t mp_enemy_interest_reach(const mp_enemy_viewer_t *viewer,
                                         const mp_enemy_subject_t *subject,
                                         mp_enemy_reach_t previous)
{
    bool was_near;
    bool was_kept;

    if (viewer == NULL || subject == NULL || !viewer->placed || !subject->read) {
        return MP_ENEMY_REACH_NONE;
    }
    if (!subject->has_placement) {
        return MP_ENEMY_REACH_MIDDLE;
    }
    was_near = previous == MP_ENEMY_REACH_NEAR;
    was_kept = was_near || previous == MP_ENEMY_REACH_MIDDLE;
    /* Exactly zero is the engine's "no distance test", and only zero: the scan compares the
     * radius against 0.0 before it measures anything. */
    if (subject->wake == 0.0f ||
        radius_holds(subject->placement, viewer->position, subject->wake, was_near)) {
        return MP_ENEMY_REACH_NEAR;
    }
    if (subject->keep == 0.0f ||
        radius_holds(subject->position, viewer->position, subject->keep, was_kept)) {
        return MP_ENEMY_REACH_MIDDLE;
    }
    return MP_ENEMY_REACH_FAR;
}

float mp_enemy_interest_wake(float authored, float keep, float scale)
{
    float scaled;
    float share;
    float result;

    if (!(authored > 0.0f)) {
        return authored;
    }
    scaled = authored * scale;
    if (!(keep > 0.0f)) {
        return scaled;
    }
    share = keep * WAKE_KEEP_SHARE;
    if (scaled <= share) {
        return scaled;
    }
    result = (share > authored) ? share : authored;
    return (result > scaled) ? scaled : result;
}

/* The reaction state as a watcher tells it apart: itself inside a throw or a death, 0 otherwise. */
static uint32_t watched_state(const mp_enemy_record_t *record)
{
    uint32_t state = record->value[MP_ENEMY_F_STATE] & MP_ENEMY_STATE_MASK;

    if ((state >= STATE_THROW_FIRST && state <= STATE_THROW_LAST) ||
        (state >= MP_ENEMY_STATE_DEATH && state <= MP_ENEMY_STATE_CORPSE)) {
        return state;
    }
    return 0u;
}

/* The four mask words in one byte. Each word is turned by its own number of bits, less than a
 * byte, and the bytes of all four are folded together by exclusive or, so the same node hidden in
 * the node mask and in the mesh mask, or node n and node n + 32, does not cancel out. */
static uint32_t fold_masks(const mp_enemy_record_t *record)
{
    uint32_t x = record->value[MP_ENEMY_F_NODES_LO];
    uint32_t y = record->value[MP_ENEMY_F_NODES_HI];
    uint32_t z = record->value[MP_ENEMY_F_MESHES_LO];
    uint32_t w = record->value[MP_ENEMY_F_MESHES_HI];

    x ^= (y << 1) | (y >> 31);
    x ^= (z << 2) | (z >> 30);
    x ^= (w << 3) | (w >> 29);
    x ^= x >> 16;
    x ^= x >> 8;
    return x & 0xFFu;
}

_Static_assert(MP_ENEMY_STATE_MASK <= 0x1Fu, "the watched state has five bits");

uint32_t mp_enemy_interest_watched(const mp_enemy_record_t *record)
{
    uint32_t watched;
    uint32_t body;

    if (record == NULL) {
        return 0u;
    }
    /* Bits 0 to 4 the state, 5 drawn, 6 solid, 7 the force throw, 8 to 23 the shield, 24 to 31
     * the fold of the masks. */
    body     = record->value[MP_ENEMY_F_BODY];
    watched  = watched_state(record);
    watched |= (body & MP_ENEMY_BODY_IS_DRAWN) != 0u ? 0x20u : 0u;
    watched |= (body & MP_ENEMY_BODY_IS_SOLID) != 0u ? 0x40u : 0u;
    watched |= (record->value[MP_ENEMY_F_STATE] & MP_ENEMY_FLAG_FORCE_THROWN) != 0u ? 0x80u : 0u;
    watched |= (record->value[MP_ENEMY_F_SHIELD] & 0xFFFFu) << 8;
    watched |= fold_masks(record) << 24;
    return watched;
}

bool mp_enemy_interest_holds(const mp_enemy_interest_row_t *row, uint8_t generation)
{
    return row != NULL && row->seen && row->life == generation;
}

/* A far key this peer has never been told about. It gets no record at all: the peer's own engine
 * would never have had it. */
static bool unknown_far(const mp_enemy_interest_row_t *row, const mp_enemy_interest_ask_t *ask)
{
    return ask->reach == MP_ENEMY_REACH_FAR && !row->seen;
}

uint32_t mp_enemy_interest_must(const mp_enemy_interest_row_t *row,
                                const mp_enemy_interest_ask_t *ask)
{
    uint32_t must = 0u;

    if (row == NULL || ask == NULL) {
        return 0u;
    }
    if (row->seen && row->life != ask->generation) {
        must |= MP_ENEMY_MUST_LIFE;
    }
    if (mp_enemy_interest_holds(row, ask->generation) && ask->watched != row->watched) {
        must |= MP_ENEMY_MUST_STATE;
    }
    return must;
}

uint32_t mp_enemy_interest_priority(const mp_enemy_interest_ask_t *ask)
{
    uint32_t base;
    uint32_t boost = 1u;

    if (ask == NULL) {
        return 0u;
    }
    switch (ask->reach) {
    case MP_ENEMY_REACH_NEAR:
        base = MP_ENEMY_INTEREST_BASE_NEAR;
        break;
    case MP_ENEMY_REACH_FAR:
        base = MP_ENEMY_INTEREST_BASE_FAR;
        break;
    default:
        base = MP_ENEMY_INTEREST_BASE_MIDDLE;
        break;
    }
    boost *= (ask->engaged || ask->struck) ? 2u : 1u;
    boost *= ask->shooting ? 2u : 1u;
    if (boost > MP_ENEMY_INTEREST_BOOST_CAP) {
        boost = MP_ENEMY_INTEREST_BOOST_CAP;
    }
    return base * boost;
}

void mp_enemy_interest_step(mp_enemy_interest_row_t *row, const mp_enemy_interest_ask_t *ask)
{
    uint32_t sum;

    if (row == NULL || ask == NULL) {
        return;
    }
    row->reach = (uint8_t)ask->reach;
    /* The ask of this step read the hit before it runs out, so a second is this many steps. */
    if (row->struck != 0u) {
        --row->struck;
    }
    if (unknown_far(row, ask)) {
        row->accumulator = 0u;
        row->age         = 0u;
        return;
    }
    if (row->age < 0xFFu) {
        ++row->age;
    }
    sum              = (uint32_t)row->accumulator + mp_enemy_interest_priority(ask);
    row->accumulator = (uint16_t)(sum > 0xFFFFu ? 0xFFFFu : sum);
}

void mp_enemy_interest_idle(mp_enemy_interest_row_t *row)
{
    if (row == NULL) {
        return;
    }
    row->accumulator = 0u;
    row->age         = 0u;
    row->reach       = (uint8_t)MP_ENEMY_REACH_NONE;
    row->struck      = 0u;
}

_Static_assert(MP_ENEMY_INTEREST_STRUCK_SUBSTEPS <= 0xFFu, "the row keeps the second in a byte");

void mp_enemy_interest_note_struck(mp_enemy_interest_row_t *row)
{
    if (row != NULL) {
        row->struck = (uint8_t)MP_ENEMY_INTEREST_STRUCK_SUBSTEPS;
    }
}

bool mp_enemy_interest_struck(const mp_enemy_interest_row_t *row)
{
    return row != NULL && row->struck != 0u;
}

static uint32_t limit_of(mp_enemy_reach_t reach)
{
    switch (reach) {
    case MP_ENEMY_REACH_NEAR: return MP_ENEMY_INTEREST_LIMIT_NEAR;
    case MP_ENEMY_REACH_FAR:  return MP_ENEMY_INTEREST_LIMIT_FAR;
    default:                  return MP_ENEMY_INTEREST_LIMIT_MIDDLE;
    }
}

mp_enemy_tier_t mp_enemy_interest_tier(const mp_enemy_interest_row_t *row,
                                       const mp_enemy_interest_ask_t *ask, uint32_t must)
{
    if (row == NULL || ask == NULL) {
        return MP_ENEMY_TIER_SKIP;
    }
    if (must != 0u) {
        return MP_ENEMY_TIER_MUST;
    }
    if (unknown_far(row, ask)) {
        return MP_ENEMY_TIER_SKIP;
    }
    if ((ask->reach == MP_ENEMY_REACH_NEAR || ask->event_waiting) &&
        !mp_enemy_interest_holds(row, ask->generation)) {
        return MP_ENEMY_TIER_FIRST;
    }
    if (row->age >= limit_of(ask->reach)) {
        return MP_ENEMY_TIER_STARVED;
    }
    if (ask->reach == MP_ENEMY_REACH_FAR && row->age < MP_ENEMY_INTEREST_FAR_CADENCE) {
        return MP_ENEMY_TIER_SKIP;
    }
    return MP_ENEMY_TIER_RANKED;
}

/* The group a pick sorts in: those that may not wait, those that may overflow but not wait behind
 * the rest, the rest. */
static int group_of(uint8_t tier)
{
    switch (tier) {
    case MP_ENEMY_TIER_MUST:    return 0;
    case MP_ENEMY_TIER_FIRST:
    case MP_ENEMY_TIER_STARVED: return 1;
    default:                    return 2;
    }
}

static int compare_numbers(uint32_t lhs, uint32_t rhs)
{
    return (lhs > rhs) - (lhs < rhs);
}

static int compare_picks(const mp_enemy_interest_pick_t *a, const mp_enemy_interest_pick_t *b,
                         bool silent)
{
    int group_a = group_of(a->tier);
    int group_b = group_of(b->tier);
    int order;

    if (silent) {
        group_a = group_a == 0 ? 0 : 1;
        group_b = group_b == 0 ? 0 : 1;
    }
    if (group_a != group_b) {
        return group_a - group_b;
    }
    if (group_a != 0) {
        if (silent || group_a == 1) {
            order = compare_numbers(b->age, a->age);          /* the older first */
            if (order != 0) {
                return order;
            }
            if (!silent && a->tier != b->tier) {
                return a->tier == MP_ENEMY_TIER_FIRST ? -1 : 1;
            }
        } else {
            order = compare_numbers(b->accumulator, a->accumulator);
            if (order == 0) {
                order = compare_numbers(b->age, a->age);
            }
            if (order != 0) {
                return order;
            }
        }
    }
    return compare_numbers(a->turn, b->turn);
}

static int compare_loud(const void *a, const void *b)
{
    return compare_picks((const mp_enemy_interest_pick_t *)a,
                         (const mp_enemy_interest_pick_t *)b, false);
}

static int compare_silent(const void *a, const void *b)
{
    return compare_picks((const mp_enemy_interest_pick_t *)a,
                         (const mp_enemy_interest_pick_t *)b, true);
}

void mp_enemy_interest_order(mp_enemy_interest_pick_t *picks, size_t count, bool silent)
{
    if (picks == NULL || count < 2u) {
        return;
    }
    qsort(picks, count, sizeof picks[0], silent ? &compare_silent : &compare_loud);
}

bool mp_enemy_interest_admits(const mp_enemy_interest_pick_t *pick, bool silent, size_t extras)
{
    if (pick == NULL || pick->tier == MP_ENEMY_TIER_SKIP) {
        return false;
    }
    return !silent || pick->tier == MP_ENEMY_TIER_MUST ||
           extras < MP_ENEMY_INTEREST_SILENT_EXTRAS;
}

void mp_enemy_interest_written(mp_enemy_interest_row_t *row, uint8_t generation, uint32_t watched)
{
    if (row == NULL) {
        return;
    }
    row->accumulator = 0u;
    row->age         = 0u;
    row->life        = generation;
    row->seen        = true;
    row->watched     = watched;
}
