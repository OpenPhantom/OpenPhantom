/* mp_knockback_rule.h: what a contact with code 0x22 does to an actor, and what a record entering
 * the knockback looks like on a client.
 *
 * Layer 1, no engine. The host's module reads the actor and the contact and hands the values in;
 * the enemy block's report does the same with the records it writes. Both halves are here because
 * they answer one question from two sides: whether a force wave threw a droid on the host, and
 * whether the replica on the client was then shown that throw.
 *
 * ================================ What the handler does with 0x22 ==============================
 *
 * The enemy contact handler turns back before any arm when the actor has no health left, is a
 * corpse, or stands by. The arm for 0x22 then does nothing to an actor already thrown or landing
 * from a throw. Otherwise it sets the actor's push event, and unless the actor carries the
 * no-throw flag it calls the knockback, which puts the actor into state 6 when the sender is a
 * wave. Nothing else in that arm moves the state, so the state after the call says which of the
 * four happened.
 *
 * The no-throw flag is not only a placement's own: a droid's script sets it itself when it sees
 * the push event, and clears it again when it has stood up. A push event that arrives a substep
 * before the wave therefore turns the wave away.
 */
#ifndef MULTIPLAYER_MP_KNOCKBACK_RULE_H
#define MULTIPLAYER_MP_KNOCKBACK_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The actor states the handler compares, the engine's own ordinals. 7 has no name in the
 * reconstruction; it is the fall that ends a throw. */
#define MP_KNOCKBACK_STATE_STANDBY 3
#define MP_KNOCKBACK_STATE_THROWN  6
#define MP_KNOCKBACK_STATE_LANDING 7
#define MP_KNOCKBACK_STATE_GET_UP  9
#define MP_KNOCKBACK_STATE_CORPSE  14

/* The actor flag the 0x22 arm tests before it throws. */
#define MP_KNOCKBACK_FLAG_NO_THROW 0x20u

typedef enum mp_knockback_touch {
    MP_KNOCKBACK_DOWN = 0,     /* no health, a corpse or standing by: the handler turns back */
    MP_KNOCKBACK_ALREADY,      /* thrown or landing already: the arm neither sets nor throws */
    MP_KNOCKBACK_NO_THROW,     /* the arm sets the push event and the no-throw flag holds it */
    MP_KNOCKBACK_THROWN,       /* the arm reaches the knockback */
    MP_KNOCKBACK_TOUCH_COUNT
} mp_knockback_touch_t;

/* Which of the four a contact with code 0x22 meets, from the actor as it is before the handler
 * runs, in the order the handler tests it. */
mp_knockback_touch_t mp_knockback_touch_of(int32_t health, int32_t state, uint32_t state_flags);

/* Whether the state the handler left agrees with that answer: a throw leaves state 6, and every
 * other answer leaves the state as it found it. */
bool mp_knockback_touch_agrees(mp_knockback_touch_t touch, int32_t state_before,
                               int32_t state_after);

/* ================================ Which touch is a wave's first ===============================
 *
 * The pair pass posts a contact for every substep a wave and an actor overlap, so one push is
 * several contacts. Only the first says what the push did; after a throw every later one reads
 * "already thrown", and before it every later one would read "no-throw". A touch of the same wave
 * and actor in the same substep or the one before is the same overlap going on.
 *
 * And a report the host refused, because its own copy of the wave performs it, is matched against
 * the touches of that player's wave on the same actor, before and after it. One that no wave meets
 * inside the window is what the rule costs: a push event on the client's side that the host never
 * had. */

/* How many substeps a refused report and a touch may lie apart and still be the same push. */
#define MP_KNOCKBACK_WINDOW 16u

/* How many pairs of wave and actor, and how many refused reports, are held at once. */
#define MP_KNOCKBACK_PAIRS   32u
#define MP_KNOCKBACK_WAITING 32u

typedef struct mp_knockback_pair {
    uint32_t object;    /* the wave's object; 0 for an empty entry */
    uint32_t actor;
    uint32_t substep;   /* the last substep the pair touched */
    uint8_t  bank;      /* whose wave: 0 this machine's own player, 1 and up a far one */
} mp_knockback_pair_t;

typedef struct mp_knockback_waiting {
    uint32_t actor;     /* 0 for an empty entry */
    uint32_t substep;
    uint8_t  bank;
} mp_knockback_waiting_t;

typedef struct mp_knockback_memory {
    mp_knockback_pair_t    pair[MP_KNOCKBACK_PAIRS];
    mp_knockback_waiting_t waiting[MP_KNOCKBACK_WAITING];
    uint32_t               refused;     /* every refused report noted */
    uint32_t               met;         /* a touch of that player's wave on that actor in time */
    uint32_t               unmet;       /* the window passed with none */
    uint32_t               cut;         /* still waiting when a level or a session ended */
    uint32_t               unwatched;   /* no actor to watch, or no room to wait */
} mp_knockback_memory_t;

/* A touch with code 0x22 of `actor` by the wave `object` of `bank` at `substep`. True when it is
 * the pair's first touch, false when it goes on an overlap. A far wave's touch also settles every
 * refused report of the same player on the same actor that waits inside the window. */
bool mp_knockback_touch(mp_knockback_memory_t *memory, uint32_t object, uint32_t actor,
                        uint8_t bank, uint32_t substep);

/* A report of far player `bank` on `actor` the host refused at `substep`. Met at once by a touch
 * of that player's wave already held inside the window, or waits for one. An actor of 0 cannot be
 * watched. */
void mp_knockback_refused(mp_knockback_memory_t *memory, uint32_t actor, uint8_t bank,
                          uint32_t substep);

/* The refused reports still inside their window at `substep`, after the ones past it have been
 * counted as unmet. With it the five numbers add up to `refused`. */
uint32_t mp_knockback_waiting_count(mp_knockback_memory_t *memory, uint32_t substep);

/* A level or a session ended. Every report still waiting is counted as cut off, and every pair
 * is forgotten: both name actors and objects by address, and a new level reuses the addresses. */
void mp_knockback_forget(mp_knockback_memory_t *memory);

/* ================================ A throw, as a client sees it ================================
 *
 * The replica never runs the handler; it is shown what the host's records say. A throw is a
 * record entering state 6. The host reads its records at the end of a substep, after the pair
 * pass that set the state, and the engine starts the throw's clip only in the next substep's pose
 * commit, so the record that enters state 6 still names the clip before it, and the one after it
 * brings the throw's clip. That is why the clip is counted on every record in state 6 and not
 * only on the one that enters it. The pose writer starts a clip when the record's differs from
 * the body's current one; on a parked replica, whose clips come from these records, the one last
 * written stands in for that. */

typedef struct mp_knockback_edges {
    uint32_t throws;      /* written records entering state 6 from a state written before */
    uint32_t clip_moved;  /* written records in state 6 whose clip is not the one last written */
    uint32_t no_before;   /* written records in state 6 with nothing written before them */
    uint32_t in_throw;    /* written records in states 6 to 9 */
} mp_knockback_edges_t;

/* Counts one record the flush handled. `known` says whether anything was written to the replica
 * before it in the same life, and `state_before` and `clip_before` are what was; `wrote` whether
 * this record reached the replica, and nothing is counted when it did not. The state words are
 * the record's own, with the presence bits above the state, and the clips are the record's
 * field. */
void mp_knockback_edges_count(mp_knockback_edges_t *edges, bool known, uint32_t state_before,
                              uint32_t clip_before, uint32_t state, uint32_t clip, bool wrote);

#endif /* MULTIPLAYER_MP_KNOCKBACK_RULE_H */
