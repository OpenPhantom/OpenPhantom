/* mp_seat_internal.h: what the search and the wish share, and nobody else.
 *
 * Layer 2. The seat module is two files along the seam its size named. mp_seat.c asks the engine's
 * three probes about one anchor, keeps the far players as the pump hands them in, keeps the seats
 * that ended a life and prints the report; mp_seat_wish.c is the wish that asks it, stage by stage,
 * and nothing in it touches the engine but the probe and the reading of this player's own body.
 * The public interface of both is mp_seat.h; this header is only the joint between them.
 */
#ifndef MULTIPLAYER_MP_SEAT_INTERNAL_H
#define MULTIPLAYER_MP_SEAT_INTERNAL_H

#include "mp_seat.h"
#include "mp_seat_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* What a wish's search keeps away from beyond the bodies: where the player died, when it knows,
 * and the seats that ended a life of his. `in_the_world` is how many of the bodies handed in stand
 * in the world; any after them are seats the lower slots of the session are foreseen to take, and
 * a candidate refused for one of those alone is counted as held for it. */
typedef struct mp_seat_avoid {
    const float *death;
    bool         locks;
    size_t       in_the_world;
} mp_seat_avoid_t;

/* One search around one anchor or point, as mp_seat_probe does it, with what a wish keeps away
 * from. `avoid` NULL keeps away from the bodies alone, which is what a scene's seating and a
 * foresight for another slot ask. */
mp_seat_outcome_t mp_seat_probe_avoiding(const float target[3], bool beside, uint8_t slot,
                                         const mp_seat_body_t *bodies, size_t body_count,
                                         const mp_seat_avoid_t *avoid, mp_seat_counts_t *counts,
                                         float seat[3]);

/* The far players as the pump last handed them in, MP_SEAT_FAR_BODIES of them, the index a bank
 * less one. */
const mp_seat_body_t *mp_seat_far_bodies(void);

/* The far players of a frame have all been handed in: the wish's watch on the seat it last handed
 * out reads them here. */
void mp_seat_wish_far_bodies_noted(void);

/* The level ended, and a watch on a seat of it ends with it. */
void mp_seat_wish_world_ended(void);

#endif /* MULTIPLAYER_MP_SEAT_INTERNAL_H */
