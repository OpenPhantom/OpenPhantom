/* mp_watchpost.h: one call a frame that says whether the foothold, the task and the simulation are
 * doing what this build says they do. See mp_watchpost.c for why the three questions are apart.
 */
#ifndef MULTIPLAYER_MP_WATCHPOST_H
#define MULTIPLAYER_MP_WATCHPOST_H

#include <stdbool.h>

/* Called once per drawn frame, from the frame hook. `arm_dispatcher` is the installer's answer to
 * whether a far body exists to catch contacts for: a spawn puts the engine's own handler back in
 * the dispatch slot, so the slot is re-armed here, idempotently, once a frame. */
void mp_watchpost_frame(bool arm_dispatcher);

/* How the substeps were spread across the drawn frames of this run: the counts, the shape, and
 * the longest run of frames that carried none. Smoothness IS this spread, a constant increment a
 * frame is a straight ramp, so a side that steps in bursts reads differently here from one that
 * steps evenly, at the same rate. */
void mp_watchpost_report(void);

#endif /* MULTIPLAYER_MP_WATCHPOST_H */
