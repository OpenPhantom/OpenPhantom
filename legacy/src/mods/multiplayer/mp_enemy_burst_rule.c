/* mp_enemy_burst_rule.c: the decisions behind an enemy bursting into its pieces. See the header. */
#include "mp_enemy_burst_rule.h"

#include <stddef.h>

/* The largest speed the byte holds, 255 quarters. The engine's own calls pass 0.5, 1.0, 2.0 and
 * 2.75; an enemy's death arm only the middle two. */
#define MAX_SPEED 63.75f

/* Bit 0 of a body's flags: drawn. The burst clears it on the body, and only at its very end. */
#define BODY_DRAWN 0x01u

/* The actor's state flag that says it carries the player's body. */
#define ACTOR_CARRIES_PLAYER 0x2000u

bool mp_enemy_burst_quarters(float speed, uint8_t *quarters)
{
    uint32_t q;

    if (quarters != NULL) {
        *quarters = 0u;
    }
    /* Not a number fails the first comparison and an infinity the second. */
    if (!(speed > 0.0f) || speed > MAX_SPEED) {
        return false;
    }
    q = (uint32_t)(speed * 4.0f + 0.5f);
    if (q == 0u || q > 255u) {
        return false;
    }
    if (quarters != NULL) {
        *quarters = (uint8_t)q;
    }
    return true;
}

float mp_enemy_burst_speed(uint8_t quarters)
{
    return (float)quarters / 4.0f;
}

mp_enemy_burst_seen_t mp_enemy_burst_observe(bool read_before, uint32_t before, bool read_after,
                                             uint32_t after)
{
    if (!read_before || (before & BODY_DRAWN) == 0u) {
        return MP_ENEMY_BURST_SEEN_HIDDEN;
    }
    if (read_after && (after & BODY_DRAWN) == 0u) {
        return MP_ENEMY_BURST_SEEN_BURST;
    }
    return MP_ENEMY_BURST_SEEN_DRAWN;
}

bool mp_enemy_burst_carries_player(uint32_t actor_flags)
{
    return (actor_flags & ACTOR_CARRIES_PLAYER) != 0u;
}

/* Where the ring holds this body for this substep, or the ring's count when it does not. */
static uint32_t index_of(const mp_enemy_burst_ring_t *ring, uint32_t body, uint32_t substep)
{
    uint32_t i;

    for (i = 0; i < ring->count && i < MP_ENEMY_BURST_RING_SLOTS; ++i) {
        if (ring->entry[i].body == body && ring->entry[i].substep == substep) {
            return i;
        }
    }
    return ring->count;
}

bool mp_enemy_burst_ring_put(mp_enemy_burst_ring_t *ring, uint32_t body, float speed,
                             uint32_t substep, uint32_t caller)
{
    uint32_t at;

    if (ring == NULL) {
        return false;
    }
    at = index_of(ring, body, substep);
    if (at >= MP_ENEMY_BURST_RING_SLOTS) {
        return false;
    }
    if (at == ring->count) {
        ++ring->count;
    }
    ring->entry[at].body    = body;
    ring->entry[at].speed   = speed;
    ring->entry[at].substep = substep;
    ring->entry[at].caller  = caller;
    return true;
}

bool mp_enemy_burst_ring_find(const mp_enemy_burst_ring_t *ring, uint32_t body, uint32_t substep,
                              float *speed)
{
    uint32_t at;

    if (ring == NULL) {
        return false;
    }
    at = index_of(ring, body, substep);
    if (at >= ring->count) {
        return false;
    }
    if (speed != NULL) {
        *speed = ring->entry[at].speed;
    }
    return true;
}

bool mp_enemy_burst_ring_take(mp_enemy_burst_ring_t *ring, uint32_t body, uint32_t substep)
{
    uint32_t at;

    if (ring == NULL) {
        return false;
    }
    at = index_of(ring, body, substep);
    if (at >= ring->count) {
        return false;
    }
    ring->entry[at] = ring->entry[ring->count - 1u];
    --ring->count;
    return true;
}

static void tally(mp_enemy_burst_callers_t *callers, uint32_t address)
{
    uint32_t i;

    for (i = 0; i < MP_ENEMY_BURST_CALLERS; ++i) {
        if (callers->count[i] != 0u && callers->address[i] == address) {
            ++callers->count[i];
            return;
        }
        if (callers->count[i] == 0u) {
            callers->address[i] = address;
            callers->count[i]   = 1u;
            return;
        }
    }
    ++callers->others;
}

uint32_t mp_enemy_burst_ring_drop(mp_enemy_burst_ring_t *ring, mp_enemy_burst_callers_t *callers)
{
    uint32_t dropped;
    uint32_t i;

    if (ring == NULL) {
        return 0u;
    }
    dropped = ring->count;
    for (i = 0; callers != NULL && i < dropped && i < MP_ENEMY_BURST_RING_SLOTS; ++i) {
        tally(callers, ring->entry[i].caller);
    }
    ring->count = 0u;
    return dropped;
}

mp_enemy_burst_attach_t mp_enemy_burst_attach(bool found, float speed, uint32_t actor_flags,
                                              uint8_t *quarters)
{
    if (quarters != NULL) {
        *quarters = 0u;
    }
    if (!found) {
        return MP_ENEMY_BURST_ATTACH_NONE;
    }
    if (mp_enemy_burst_carries_player(actor_flags)) {
        return MP_ENEMY_BURST_ATTACH_PLAYER;
    }
    if (!mp_enemy_burst_quarters(speed, quarters)) {
        return MP_ENEMY_BURST_ATTACH_NO_SPEED;
    }
    return MP_ENEMY_BURST_ATTACH_CARRIED;
}

mp_enemy_burst_gate_t mp_enemy_burst_gate_name(const mp_enemy_burst_reading_t *r)
{
    if (r == NULL) {
        return MP_ENEMY_BURST_GATE_NONE;
    }
    if (r->player_body) {
        return MP_ENEMY_BURST_GATE_PLAYER;
    }
    if (r->radius_read && r->radius >= 1.0f) {
        return MP_ENEMY_BURST_GATE_WIDE;
    }
    if (r->flags_read && (r->flags & BODY_DRAWN) == 0u) {
        return MP_ENEMY_BURST_GATE_HIDDEN;
    }
    /* The engine doubles a signed count and compares it unsigned, so none free is too few for any
     * body at all. */
    if (r->nodes_read && r->free_read && r->nodes >= r->free_things * 2u) {
        return MP_ENEMY_BURST_GATE_CROWDED;
    }
    return MP_ENEMY_BURST_GATE_NONE;
}

mp_enemy_burst_verdict_t mp_enemy_burst_decide(bool actor_here, bool kept, bool alive, bool newer,
                                               uint8_t quarters)
{
    mp_enemy_burst_verdict_t v;

    v.burst = false;
    if (newer && !kept) {
        v.removal = MP_ENEMY_BURST_REMOVAL_STALE;
        return v;
    }
    if (!actor_here) {
        v.removal = MP_ENEMY_BURST_REMOVAL_GONE;
        return v;
    }
    v.removal = MP_ENEMY_BURST_REMOVAL_PERFORM;
    v.burst   = quarters != 0u && alive && !newer;
    return v;
}
