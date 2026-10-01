/* mp_payload_prefix.h: the two things an unreliable payload opens with, as bytes.
 *
 * Layer 1, pure. No engine and no session: a payload is a range of bytes here, and everybody who
 * writes one or reads one, the bridge in both roles, its loopback, the dedicated server and the
 * tests, asks this file instead of laying the bytes out a second time. Three copies of the client's
 * prefix length stood in three files before this one, and the dedicated server's world went out
 * without the enemy length the client reads first, so a client of that server read the low half of
 * the tick as a length and refused every world.
 *
 * THE CLIENT'S PREFIX says what this side holds of the host's world. The newest host tick it
 * holds, and beside it one bit for each of the thirty one ticks before that one: bit k is set when
 * the tick `newest - 1 - k` is held as well. A tick is held once its snapshot decoded and its enemy
 * block was taken or carried none, which is exactly what the host needs to know to tell a payload
 * that never arrived from one that arrived and was merely stepped over by a newer one. Bit 31 is
 * always clear: the history keeps thirty two ticks, and the tick thirty two before the newest
 * shares the newest's place in it, so nothing can say it is held.
 *
 *     offset 0, 4 bytes   the newest host tick held, 0 for none
 *     offset 4, 4 bytes   the bits
 *     offset 8            the snapshot of this side's own body, or the loopback's commands
 *
 * THE HOST'S WORLD opens with the length of the enemy block, two bytes, then the block, then the
 * snapshot. A world with no enemies says a length of nought; the length is there all the same,
 * because it is where the reader looks first.
 */
#ifndef MULTIPLAYER_MP_PAYLOAD_PREFIX_H
#define MULTIPLAYER_MP_PAYLOAD_PREFIX_H

#include "mp_budget_rule.h"
#include "mp_snapshot.h"
#include "mp_snapshot_history.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- the client's prefix --------------------------------------------------------------------- */

#define MP_PAYLOAD_ACK_BYTES  8u

/* The ticks the bits can name behind the newest one: every place of the history but the newest's
 * own. */
#define MP_PAYLOAD_ACK_WINDOW (MP_SNAPSHOT_HISTORY - 1u)
#define MP_PAYLOAD_ACK_UNUSED 0x80000000u   /* bit 31, which no history can set */

_Static_assert(MP_PAYLOAD_ACK_WINDOW == 31u, "the bits are one word with its top bit unused");

typedef struct mp_payload_ack {
    uint32_t newest;   /* the newest host tick held, 0 for none */
    uint32_t bits;     /* bit k: the tick newest - 1 - k is held too */
} mp_payload_ack_t;

/* What a history holds, as the prefix says it. Nothing held is newest 0 and no bits. */
void mp_payload_ack_from(const mp_snapshot_history_t *history, mp_payload_ack_t *out);

/* Whether bit k of an acknowledgement names `tick`, for a tick older than its newest: the one
 * question the host asks of the bits for every substep an acknowledgement steps over. False for
 * the newest itself and for anything further back than the window. */
bool mp_payload_ack_holds(const mp_payload_ack_t *ack, uint32_t tick);

/* The longest run of ticks missing between two held ones, the newest counted as held, in ticks.
 * Clear bits past the oldest held tick are where the history begins and are no gap. */
uint32_t mp_payload_ack_widest_gap(const mp_payload_ack_t *ack);

/* The prefix onto the wire and off it. Both refuse bit 31 and bits beside a newest of nought, the
 * two things no history produces, so a decoder takes exactly what an encoder writes. */
bool mp_payload_put_ack(uint8_t *buffer, size_t capacity, const mp_payload_ack_t *ack);
bool mp_payload_get_ack(const uint8_t *payload, size_t bytes, mp_payload_ack_t *out);

/* ---- the host's world ------------------------------------------------------------------------ */

#define MP_PAYLOAD_ENEMY_LENGTH_BYTES MP_BUDGET_ENEMY_LENGTH_BYTES
#define MP_PAYLOAD_ENEMY_LENGTH_MAX   0xFFFFu

/* What is kept back in a world for its bodies: the snapshot's header, the two ticks and a state
 * byte per slot, and the largest record per body it carries. */
#define MP_PAYLOAD_SNAPSHOT_HEADER_BYTES (8u + MP_SNAPSHOT_MAX_BODIES)
size_t mp_payload_world_reserve(size_t bodies);

/* The length in front of the enemy block. Refuses a length the two bytes cannot say. */
bool mp_payload_put_enemy_length(uint8_t *buffer, size_t capacity, size_t enemy_bytes);

/* Where a world's parts lie: the enemy block from MP_PAYLOAD_ENEMY_LENGTH_BYTES for
 * `enemy_bytes`, the snapshot from `snapshot_at` to the end. False for a payload too short for its
 * length or for the block its length names, which is a torn or foreign payload whose snapshot is
 * not trusted either. */
bool mp_payload_split_world(const uint8_t *payload, size_t bytes, size_t *enemy_bytes,
                            size_t *snapshot_at);

#endif /* MULTIPLAYER_MP_PAYLOAD_PREFIX_H */
