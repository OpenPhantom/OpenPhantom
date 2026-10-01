/* mp_knockback_rule.c: what a contact with code 0x22 does to an actor, and what a throw looks like
 * on a client. The model is in the header. */
#include "mp_knockback_rule.h"

#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The clip field is the base channel's clip ordinal in its low byte, as the pose writer reads
 * it. */
#define CLIP_MASK 0xFFu

mp_knockback_touch_t mp_knockback_touch_of(int32_t health, int32_t state, uint32_t state_flags)
{
    /* The handler's own order: its two gates, then the arm's two tests. Standing by is tested
     * after health, so a dead actor standing by is dead; the state test comes before the flag,
     * so an actor thrown with the flag set is thrown. */
    if (health <= 0 || state == MP_KNOCKBACK_STATE_CORPSE) {
        return MP_KNOCKBACK_DOWN;
    }
    if (state == MP_KNOCKBACK_STATE_STANDBY) {
        return MP_KNOCKBACK_DOWN;
    }
    if (state == MP_KNOCKBACK_STATE_THROWN || state == MP_KNOCKBACK_STATE_LANDING) {
        return MP_KNOCKBACK_ALREADY;
    }
    if ((state_flags & MP_KNOCKBACK_FLAG_NO_THROW) != 0u) {
        return MP_KNOCKBACK_NO_THROW;
    }
    return MP_KNOCKBACK_THROWN;
}

bool mp_knockback_touch_agrees(mp_knockback_touch_t touch, int32_t state_before,
                               int32_t state_after)
{
    if (touch == MP_KNOCKBACK_THROWN) {
        return state_after == MP_KNOCKBACK_STATE_THROWN;
    }
    return state_after == state_before;
}

/* Unsigned, so a counter that passed its top still gives an age. */
static bool within(uint32_t earlier, uint32_t later, uint32_t window)
{
    return later - earlier <= window;
}

/* Every refused report whose window has passed is counted as met by none and let go. */
static void let_go_of_the_old(mp_knockback_memory_t *memory, uint32_t substep)
{
    size_t i;

    for (i = 0; i < MP_KNOCKBACK_WAITING; ++i) {
        mp_knockback_waiting_t *w = &memory->waiting[i];

        if (w->actor != 0u && !within(w->substep, substep, MP_KNOCKBACK_WINDOW)) {
            ++memory->unmet;
            w->actor = 0u;
        }
    }
}

/* A far wave's touch settles the reports of the same player on the same actor still waiting. */
static void meet_the_waiting(mp_knockback_memory_t *memory, uint32_t actor, uint8_t bank,
                             uint32_t substep)
{
    size_t i;

    for (i = 0; i < MP_KNOCKBACK_WAITING; ++i) {
        mp_knockback_waiting_t *w = &memory->waiting[i];

        if (w->actor == actor && w->bank == bank &&
            within(w->substep, substep, MP_KNOCKBACK_WINDOW)) {
            ++memory->met;
            w->actor = 0u;
        }
    }
}

/* The pair's own entry, else an empty one, else the one that touched longest ago. */
static mp_knockback_pair_t *pair_for(mp_knockback_memory_t *memory, uint32_t object,
                                     uint32_t actor, uint32_t substep, bool *held)
{
    mp_knockback_pair_t *empty  = NULL;
    mp_knockback_pair_t *oldest = &memory->pair[0];
    size_t               i;

    *held = false;
    for (i = 0; i < MP_KNOCKBACK_PAIRS; ++i) {
        mp_knockback_pair_t *p = &memory->pair[i];

        if (p->object == object && p->actor == actor && object != 0u) {
            *held = true;
            return p;
        }
        if (p->object == 0u) {
            if (empty == NULL) {
                empty = p;
            }
        } else if (substep - p->substep > substep - oldest->substep) {
            oldest = p;
        }
    }
    return empty != NULL ? empty : oldest;
}

bool mp_knockback_touch(mp_knockback_memory_t *memory, uint32_t object, uint32_t actor,
                        uint8_t bank, uint32_t substep)
{
    mp_knockback_pair_t *p;
    bool                 held;
    bool                 first;

    let_go_of_the_old(memory, substep);
    if (bank != 0u) {
        meet_the_waiting(memory, actor, bank, substep);
    }
    p     = pair_for(memory, object, actor, substep, &held);
    first = !held || !within(p->substep, substep, 1u);
    p->object  = object;
    p->actor   = actor;
    p->substep = substep;
    p->bank    = bank;
    return first;
}

void mp_knockback_refused(mp_knockback_memory_t *memory, uint32_t actor, uint8_t bank,
                          uint32_t substep)
{
    size_t i;

    ++memory->refused;
    let_go_of_the_old(memory, substep);
    if (actor == 0u || bank == 0u) {
        ++memory->unwatched;
        return;
    }
    for (i = 0; i < MP_KNOCKBACK_PAIRS; ++i) {
        const mp_knockback_pair_t *p = &memory->pair[i];

        if (p->object != 0u && p->actor == actor && p->bank == bank &&
            within(p->substep, substep, MP_KNOCKBACK_WINDOW)) {
            ++memory->met;
            return;
        }
    }
    for (i = 0; i < MP_KNOCKBACK_WAITING; ++i) {
        mp_knockback_waiting_t *w = &memory->waiting[i];

        if (w->actor == 0u) {
            w->actor   = actor;
            w->bank    = bank;
            w->substep = substep;
            return;
        }
    }
    ++memory->unwatched;
}

uint32_t mp_knockback_waiting_count(mp_knockback_memory_t *memory, uint32_t substep)
{
    uint32_t count = 0u;
    size_t   i;

    let_go_of_the_old(memory, substep);
    for (i = 0; i < MP_KNOCKBACK_WAITING; ++i) {
        count += memory->waiting[i].actor != 0u ? 1u : 0u;
    }
    return count;
}

void mp_knockback_edges_count(mp_knockback_edges_t *edges, bool known, uint32_t state_before,
                              uint32_t clip_before, uint32_t state, uint32_t clip, bool wrote)
{
    uint32_t now    = state & MP_ENEMY_STATE_MASK;
    uint32_t before = state_before & MP_ENEMY_STATE_MASK;

    if (!wrote) {
        return;
    }
    if (now >= (uint32_t)MP_KNOCKBACK_STATE_THROWN && now <= (uint32_t)MP_KNOCKBACK_STATE_GET_UP) {
        ++edges->in_throw;
    }
    if (now != (uint32_t)MP_KNOCKBACK_STATE_THROWN) {
        return;
    }
    if (!known) {
        ++edges->no_before;
        return;
    }
    if ((clip & CLIP_MASK) != (clip_before & CLIP_MASK)) {
        ++edges->clip_moved;
    }
    if (before != (uint32_t)MP_KNOCKBACK_STATE_THROWN) {
        ++edges->throws;
    }
}

void mp_knockback_forget(mp_knockback_memory_t *memory)
{
    size_t i;

    for (i = 0; i < MP_KNOCKBACK_WAITING; ++i) {
        memory->cut += memory->waiting[i].actor != 0u ? 1u : 0u;
    }
    memset(memory->pair, 0, sizeof memory->pair);
    memset(memory->waiting, 0, sizeof memory->waiting);
}
