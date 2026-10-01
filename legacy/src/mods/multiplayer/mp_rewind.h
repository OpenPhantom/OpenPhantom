/* mp_rewind.h: the position history a host rewinds for a fair hit test.
 *
 * Layer 1, pure logic, the lag-compensation memory. A shooter fires at bodies it sees a little in
 * the past, because it interpolates remote bodies behind the newest snapshot; so the host, to judge
 * that shot fairly, has to know where those bodies were at the moment the shooter saw them, not
 * where they are now. This keeps a short ring of each body's position by tick, and answers "where
 * was this body at time T" by interpolating between the two samples that bracket T.
 *
 * The rewind is clamped to a small window. The engine keeps a full second, but a shot is only
 * rewound about a quarter second: further back and the "shot behind cover and still hit" that a
 * victim feels grows without bound, and the modern consensus is to cap it rather than honour an
 * arbitrarily old view. What this module does NOT do is the hit test itself, which needs the
 * collision geometry and belongs on the engine side; it answers the one pure question, where was
 * the body, so that side can rewind, test, and restore.
 */
#ifndef MULTIPLAYER_MP_REWIND_H
#define MULTIPLAYER_MP_REWIND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One second of history at the feature's rate, and the window a shot may actually reach back:
 * eight ticks is a quarter second, the cap the consensus recommends against a peeker's
 * advantage. */
#define MP_REWIND_HISTORY   32u
#define MP_REWIND_MAX_TICKS 8u

typedef struct mp_rewind {
    bool     have_newest;
    uint32_t newest_tick;
    bool     used[MP_REWIND_HISTORY];
    uint32_t tick[MP_REWIND_HISTORY];
    float    position[MP_REWIND_HISTORY][3];
} mp_rewind_t;

void mp_rewind_init(mp_rewind_t *rewind);

/* Records a body's position at a tick. Called every tick the host simulates. */
void mp_rewind_store(mp_rewind_t *rewind, uint32_t tick, const float position[3]);

/* Where the body was at the fractional tick `from_tick` plus `alpha` (0..1) toward `from_tick + 1`,
 * interpolated between the two stored samples. `from_tick` is clamped into the window before the
 * lookup, so a request further back than MP_REWIND_MAX_TICKS is answered at the window's edge
 * rather than refused, and a request in the future is answered at the newest. False only when the
 * history is empty or the bracketing samples are missing, in which case the caller uses the body's
 * current position and takes the small unfairness rather than inventing a past. */
bool mp_rewind_sample(const mp_rewind_t *rewind, uint32_t from_tick, float alpha, float out[3]);

/* The oldest tick a rewind will honour right now, newest minus the window, for a caller that wants
 * to clamp its own rewind time before asking. False when the history is empty. */
bool mp_rewind_oldest_allowed(const mp_rewind_t *rewind, uint32_t *tick);

#endif /* MULTIPLAYER_MP_REWIND_H */
