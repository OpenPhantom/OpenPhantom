/* mp_enemy_burst_rule.h: the decisions behind an enemy bursting into its pieces on both machines.
 *
 * Layer 1, pure.
 *
 * The engine bursts a body in one function: it throws a piece for every visible node with a mesh,
 * each a harmless shot of its own, and hides the body. A script's death arm calls it and asks for
 * the actor's removal in the same tick, so by the time the host describes its enemies the actor
 * is gone and no record can carry the burst. What carries it is the removal note itself: its last
 * byte is the speed, and a client bursts its replica before it performs the removal, in one call,
 * so the order is the code's and not the network's. The pieces never travel; each machine throws
 * its own.
 *
 * The rules here: the speed in quarters of a unit; whether a call burst the body, from bit 0 of
 * its flags before and after; the ring a burst waits in until the removal of the same body takes
 * it; what the host attaches; which of the engine's gates a burst that did not happen met; and the
 * client's whole decision for a removal, with the removal decided exactly as it was before this
 * byte existed.
 */
#ifndef MULTIPLAYER_MP_ENEMY_BURST_RULE_H
#define MULTIPLAYER_MP_ENEMY_BURST_RULE_H

#include <stdbool.h>
#include <stdint.h>

/* The speed as the note carries it: quarters, rounded, 1 to 255. False, and 0, for a speed the
 * byte cannot hold: none, a negative, not a number, one that rounds to no quarter, or one above
 * 63.75. Refused rather than cut down, and the removal goes regardless. */
bool  mp_enemy_burst_quarters(float speed, uint8_t *quarters);
float mp_enemy_burst_speed(uint8_t quarters);

/* What one call of the engine's burst did, from the body's flags: bit 0 is whether it is drawn. */
typedef enum mp_enemy_burst_seen {
    MP_ENEMY_BURST_SEEN_HIDDEN = 0,   /* hidden before the call, or unreadable: the engine's gate */
    MP_ENEMY_BURST_SEEN_DRAWN,        /* drawn after it as well: a gate, or it gave up inside */
    MP_ENEMY_BURST_SEEN_BURST         /* drawn before and hidden after: it burst */
} mp_enemy_burst_seen_t;

mp_enemy_burst_seen_t mp_enemy_burst_observe(bool read_before, uint32_t before, bool read_after,
                                             uint32_t after);

/* The actor carries the player's body. Neither end bursts one: the host attaches no speed for it
 * and a client does not burst it, and both read the same bit. */
bool mp_enemy_burst_carries_player(uint32_t actor_flags);

/* The host's bursts of one substep, by body, until the removal of that body takes one. A removal
 * in another substep takes nothing, because a body's address is taken again once it is freed. */
#define MP_ENEMY_BURST_RING_SLOTS 8u

typedef struct mp_enemy_burst_entry {
    uint32_t body;
    float    speed;
    uint32_t substep;
    uint32_t caller;   /* where the engine called from, for the line only */
} mp_enemy_burst_entry_t;

typedef struct mp_enemy_burst_ring {
    mp_enemy_burst_entry_t entry[MP_ENEMY_BURST_RING_SLOTS];
    uint32_t               count;
} mp_enemy_burst_ring_t;

/* The same body twice in one substep keeps the newer. False when the ring is full; nothing in it
 * is overwritten. */
bool mp_enemy_burst_ring_put(mp_enemy_burst_ring_t *ring, uint32_t body, float speed,
                             uint32_t substep, uint32_t caller);
bool mp_enemy_burst_ring_find(const mp_enemy_burst_ring_t *ring, uint32_t body, uint32_t substep,
                              float *speed);
bool mp_enemy_burst_ring_take(mp_enemy_burst_ring_t *ring, uint32_t body, uint32_t substep);

/* The first callers of the bursts no removal took, with how often each. */
#define MP_ENEMY_BURST_CALLERS 4u

typedef struct mp_enemy_burst_callers {
    uint32_t address[MP_ENEMY_BURST_CALLERS];
    uint32_t count[MP_ENEMY_BURST_CALLERS];
    uint32_t others;
} mp_enemy_burst_callers_t;

/* Empties the ring, adds its callers to the tally, and answers how many it held. */
uint32_t mp_enemy_burst_ring_drop(mp_enemy_burst_ring_t *ring, mp_enemy_burst_callers_t *callers);

/* What the host attaches to a removal it sends. The speed is written only for CARRIED. */
typedef enum mp_enemy_burst_attach {
    MP_ENEMY_BURST_ATTACH_NONE = 0,   /* this body did not burst in this substep */
    MP_ENEMY_BURST_ATTACH_CARRIED,    /* it did, and the note carries it */
    MP_ENEMY_BURST_ATTACH_NO_SPEED,   /* it did, at a speed the byte cannot hold */
    MP_ENEMY_BURST_ATTACH_PLAYER      /* it did, and the actor carries the player's body */
} mp_enemy_burst_attach_t;

mp_enemy_burst_attach_t mp_enemy_burst_attach(bool found, float speed, uint32_t actor_flags,
                                              uint8_t *quarters);

/* What was read of a body before a burst that then did not happen, to name the gate. The engine's
 * own order is the player's body, a radius of 1.0 or more, a hidden body, and more nodes than
 * twice its free objects, compared unsigned. A reading that did not read names nothing. */
typedef enum mp_enemy_burst_gate {
    MP_ENEMY_BURST_GATE_NONE = 0,   /* no gate the readings can name: the engine stopped inside */
    MP_ENEMY_BURST_GATE_PLAYER,
    MP_ENEMY_BURST_GATE_WIDE,
    MP_ENEMY_BURST_GATE_HIDDEN,
    MP_ENEMY_BURST_GATE_CROWDED     /* too few free objects */
} mp_enemy_burst_gate_t;

typedef struct mp_enemy_burst_reading {
    bool     player_body;
    bool     radius_read;
    float    radius;
    bool     flags_read;
    uint32_t flags;
    bool     nodes_read;
    uint32_t nodes;
    bool     free_read;
    uint32_t free_things;
} mp_enemy_burst_reading_t;

mp_enemy_burst_gate_t mp_enemy_burst_gate_name(const mp_enemy_burst_reading_t *reading);

/* A client's decision for one removal the host sent, once the slot of its replica is judged:
 * whether the slot still holds the actor of this life (alive or a corpse kept for it), whether it
 * is a corpse this side kept, and whether it is alive; and whether the note is about a life the
 * table has moved on from.
 *
 * The removal is decided exactly as before: a note for a newer life touches nothing unless it is
 * the corpse kept for its own life, a slot that holds no actor of the life is only forgotten, and
 * everything else is performed. A burst goes before a performed removal, only on an actor that is
 * alive and of the note's own life: a kept corpse is never burst. */
typedef enum mp_enemy_burst_removal {
    MP_ENEMY_BURST_REMOVAL_STALE = 0,
    MP_ENEMY_BURST_REMOVAL_GONE,
    MP_ENEMY_BURST_REMOVAL_PERFORM
} mp_enemy_burst_removal_t;

typedef struct mp_enemy_burst_verdict {
    mp_enemy_burst_removal_t removal;
    bool                     burst;
} mp_enemy_burst_verdict_t;

mp_enemy_burst_verdict_t mp_enemy_burst_decide(bool actor_here, bool kept, bool alive, bool newer,
                                               uint8_t quarters);

#endif /* MULTIPLAYER_MP_ENEMY_BURST_RULE_H */
