/* mp_snapshot_history.h: the ring of recent snapshots each side keeps.
 *
 * Layer 1, pure logic. The host keeps the snapshots it has built so it can delta encode against the
 * one a client acknowledged; the client keeps the snapshots it has received so it can interpolate
 * between the two that bracket its render tick. Both are the same ring: store by tick, fetch by
 * tick, and the newest.
 *
 * A ring indexed by tick has one hazard worth naming: two ticks a ring length apart land in one
 * slot, so a fetch has to check the stored snapshot's tick actually matches the one asked for, or
 * it would hand back a snapshot a whole ring older wearing the wrong number. That check is the
 * reason this is a module and not two lines at each call site.
 */
#ifndef MULTIPLAYER_MP_SNAPSHOT_HISTORY_H
#define MULTIPLAYER_MP_SNAPSHOT_HISTORY_H

#include "mp_snapshot.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Thirty-two snapshots, one second at the feature's rate. Long enough that a client's
 * acknowledgement of a baseline is still held when it arrives a round trip later, and that the two
 * snapshots bracketing an interpolation target three intervals back are both present. */
#define MP_SNAPSHOT_HISTORY 32u

typedef struct mp_snapshot_history {
    bool          have_newest;
    uint32_t      newest_tick;
    bool          used[MP_SNAPSHOT_HISTORY];
    mp_snapshot_t ring[MP_SNAPSHOT_HISTORY];
} mp_snapshot_history_t;

void mp_snapshot_history_init(mp_snapshot_history_t *history);

/* Stores a snapshot under its own tick, overwriting whatever shared that ring slot. The newest
 * tick is tracked so a caller need not scan. */
void mp_snapshot_history_store(mp_snapshot_history_t *history, const mp_snapshot_t *snapshot);

/* The snapshot held for exactly `tick`, or NULL when the ring holds a different tick in that slot
 * or none. The tick check is what makes a fetch after a full ring of newer snapshots return NULL
 * rather than a stale one. */
const mp_snapshot_t *mp_snapshot_history_get(const mp_snapshot_history_t *history, uint32_t tick);

/* The most recent snapshot stored, or NULL when nothing is. */
const mp_snapshot_t *mp_snapshot_history_newest(const mp_snapshot_history_t *history);

/* True when both `from_tick` and `to_tick` are held, and points the two out. This is the client's
 * interpolation fetch: the clock names the pair of ticks and the alpha between them, and this hands
 * back the two snapshots to blend. False when either is missing, in which case the caller holds the
 * last good pose rather than interpolating across a gap it cannot fill. */
bool mp_snapshot_history_bracket(const mp_snapshot_history_t *history, uint32_t from_tick,
                                 uint32_t to_tick, const mp_snapshot_t **from,
                                 const mp_snapshot_t **to);

#endif /* MULTIPLAYER_MP_SNAPSHOT_HISTORY_H */
