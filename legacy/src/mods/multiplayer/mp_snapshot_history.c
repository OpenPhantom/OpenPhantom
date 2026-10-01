/* mp_snapshot_history.c: a ring by tick, with the one check that makes it correct.
 *
 * The slot for a tick is tick modulo the ring length. Storing overwrites; fetching checks the
 * stored tick against the asked-for one, so a slot reused by a tick a full ring later returns NULL
 * for the old tick rather than the wrong snapshot. The newest tick uses the wrapping comparison, so
 * it stays right across the point where the tick counter itself wraps.
 */
#include "mp_snapshot_history.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void mp_snapshot_history_init(mp_snapshot_history_t *history)
{
    size_t i;

    history->have_newest = false;
    history->newest_tick = 0u;
    for (i = 0; i < MP_SNAPSHOT_HISTORY; ++i) {
        history->used[i] = false;
    }
}

/* Whether lhs is the more recent of two wrapping tick numbers, the same test the channel uses on
 * sequence numbers, so the newest stays right when the counter wraps past its maximum. */
static bool tick_newer(uint32_t lhs, uint32_t rhs)
{
    return (lhs != rhs) && ((uint32_t)(lhs - rhs) < 0x80000000u);
}

void mp_snapshot_history_store(mp_snapshot_history_t *history, const mp_snapshot_t *snapshot)
{
    size_t slot = (size_t)(snapshot->tick % MP_SNAPSHOT_HISTORY);

    history->ring[slot] = *snapshot;
    history->used[slot] = true;

    if (!history->have_newest || tick_newer(snapshot->tick, history->newest_tick)) {
        history->have_newest = true;
        history->newest_tick = snapshot->tick;
    }
}

const mp_snapshot_t *mp_snapshot_history_get(const mp_snapshot_history_t *history, uint32_t tick)
{
    size_t slot = (size_t)(tick % MP_SNAPSHOT_HISTORY);

    if (!history->used[slot] || history->ring[slot].tick != tick) {
        return NULL;
    }
    return &history->ring[slot];
}

const mp_snapshot_t *mp_snapshot_history_newest(const mp_snapshot_history_t *history)
{
    if (!history->have_newest) {
        return NULL;
    }
    return mp_snapshot_history_get(history, history->newest_tick);
}

bool mp_snapshot_history_bracket(const mp_snapshot_history_t *history, uint32_t from_tick,
                                 uint32_t to_tick, const mp_snapshot_t **from,
                                 const mp_snapshot_t **to)
{
    const mp_snapshot_t *a = mp_snapshot_history_get(history, from_tick);
    const mp_snapshot_t *b = mp_snapshot_history_get(history, to_tick);

    if (a == NULL || b == NULL) {
        return false;
    }
    if (from != NULL) {
        *from = a;
    }
    if (to != NULL) {
        *to = b;
    }
    return true;
}
