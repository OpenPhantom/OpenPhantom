/* mp_enemy_queue_rule.h: the two newest records of one replica, written one substep apart.
 *
 * Layer 1, pure: a key's queue and the records handed to it, nothing of the engine or the table.
 *
 * Why it exists. A client's flush writes each replica once a substep, and the host's payloads do
 * not arrive once a substep: some of the client's substeps take none of them and some take two.
 * Two records of one replica taken between two flushes used to fall together. The flush wrote the
 * newer, the older never reached the body, and whatever the host played between the two, a clip
 * begun again, a throw, a step, was never seen here: the animation skipped and the replica jumped.
 * Kept, the older is written now and the newer in the next substep. That is one substep late at
 * the most, and only when two came; a substep that takes none catches the delay up again.
 *
 * The rule, which is all of it:
 *
 *   a record is kept behind the new one only when the body has not been given it yet, both name
 *   the same life, and the new one goes to a body here;
 *
 *   a third before the flush drops the oldest of them, so the two newest stay. Dropping the two
 *   and writing the newest would make the step the body takes longer than it was without the
 *   queue, and keeping more than two would let the delay grow past one substep;
 *
 *   a new life keeps nothing of the old one. Its record is written at once and whatever of the
 *   old life was still unwritten falls, so a body never shows a life the table has already left,
 *   and the life the flush wrote last is always the table's;
 *
 *   every way out of holding a key empties its queue: a removal, a let go, a reset, a replica gone
 *   by the time of the write, a record that reaches no body.
 *
 * The same shape as the jitter buffer every networked game keeps for its remote entities, cut to
 * its smallest useful depth: Quake 3 holds the snapshots it interpolates between and never blends
 * across a teleport, which is the life rule here. A deeper buffer on a render clock is the full
 * form, and it is not this.
 */
#ifndef MULTIPLAYER_MP_ENEMY_QUEUE_RULE_H
#define MULTIPLAYER_MP_ENEMY_QUEUE_RULE_H

#include "mp_enemy_wire.h"

#include <stdbool.h>
#include <stdint.h>

/* One key's records that the body has not been given yet, besides the mirror the table keeps. */
typedef struct mp_enemy_queue {
    mp_enemy_record_t older;           /* the record in front of the mirror */
    bool              held;            /* `older` holds one */
    bool              older_waited;    /* it was already unwritten when the last flush ended */
    bool              mirror_waited;   /* and the mirror was, too */
    uint32_t          behind;          /* flushes in a row that ended with a record unwritten */
} mp_enemy_queue_t;

/* What a new record did to the queue of its key. */
typedef enum mp_enemy_queue_take {
    MP_ENEMY_QUEUE_ALONE,      /* nothing unwritten stands in front of it */
    MP_ENEMY_QUEUE_KEPT,       /* the unwritten mirror is kept, to be written first */
    MP_ENEMY_QUEUE_THIRD,      /* two were unwritten: the older fell, the mirror is kept */
    MP_ENEMY_QUEUE_NEW_LIFE,   /* a new life: what was unwritten of the old one fell */
    MP_ENEMY_QUEUE_NO_BODY     /* it reaches no body here, and a record kept for one fell */
} mp_enemy_queue_take_t;

/* A new record of the key is about to take the mirror's place. `mirror` is the mirror as it stands,
 * `unwritten` says the body has not been given it, `same_life` that the new record names the
 * mirror's life, `lands` that the new record goes to a body here. Keeps the mirror as the older
 * record where the rule says so; `*fell` is set to how many records will now never reach the body,
 * the unwritten mirror of an old life among them. */
mp_enemy_queue_take_t mp_enemy_queue_take(mp_enemy_queue_t *queue, const mp_enemy_record_t *mirror,
                                          bool unwritten, bool same_life, bool lands,
                                          uint32_t *fell);

/* The record a flush gives the body: the older one while one is kept, else the mirror. `*late`
 * says it was already unwritten when the last flush ended. */
const mp_enemy_record_t *mp_enemy_queue_next(const mp_enemy_queue_t *queue,
                                             const mp_enemy_record_t *mirror, bool *late);

/* The flush has handled the record the queue named, written or refused alike: a record is given
 * once. True while the mirror still waits behind it for the next flush. */
bool mp_enemy_queue_handled(mp_enemy_queue_t *queue);

/* A flush is over for the key, and `unwritten` says its mirror is still to be given. Returns the
 * run of flushes in a row that ended so, 0 when this one did not. */
uint32_t mp_enemy_queue_flushed(mp_enemy_queue_t *queue, bool unwritten);

/* Empties the queue, which every way out of holding a key does. Returns how many records it held
 * that will now never reach the body, 0 or 1. */
uint32_t mp_enemy_queue_clear(mp_enemy_queue_t *queue);

#endif /* MULTIPLAYER_MP_ENEMY_QUEUE_RULE_H */
