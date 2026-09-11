/* mover_slots.h: the side table that remembers a mover subnode's previous pose, and gives it back.
 *
 * One entry per subnode the game has ticked, keyed by its address. Open addressing on the pointer,
 * so a lookup on the draw path is a couple of loads. The table is deliberately much larger than
 * the 24 live movers a level was measured with, because the bound belongs to the data rather than
 * to that one observation, and a full table degrades to "not interpolated" rather than to a wrong
 * answer.
 *
 * Two things empty it, and both were missing once. Nothing ever gave a slot back, so the third
 * level of a session found the table owned by the first two and was drawn stepped from its first
 * frame. Now a level opening forgets everything, and a slot whose subnode has not ticked for
 * MOVER_SLOT_STALE_FRAMES rendered frames may be handed to a newcomer. Ten seconds at 30 frames a
 * second and two at 144, either of which is far longer than any mover pauses between moves, and a
 * mover that really has stopped for longer is drawn where it stands anyway.
 *
 * No engine in this file: it is arithmetic on addresses and frame counts, so it can be tested on
 * its own, and the emptying is the part worth a test.
 */
#ifndef MOVER_SLOTS_H
#define MOVER_SLOTS_H

#include "mover_blend.h"
#include "mover_evenness.h"

#include <stdbool.h>
#include <stdint.h>

#define MOVER_SLOT_COUNT        512u
#define MOVER_SLOT_STALE_FRAMES 300u

typedef struct mover_slot {
    const void *subnode;
    bool        usable;
    float       previous[MOVER_WORLD_FLOATS];
    /* How much world time the move that produced the current pose covered, and the substep alpha
     * at the frame it happened on. Together they say how far into that move the frame being drawn
     * now stands, without either an absolute clock or an assumption about the frame rate. */
    float       interval;
    float       tick_alpha;
    uint32_t    tick_stamp;
    /* LogMoverEvenness only: this subnode's own drawn history. */
    mover_evenness_state_t evenness;
    /* What this subnode's last ordinary tick did to it, the yardstick a track wrap is measured
     * against: how far its origin moved and how far its first basis row turned. */
    float       ordinary_step;
    float       ordinary_angle;
    bool        have_ordinary;
} mover_slot_t;

typedef struct mover_slot_table {
    mover_slot_t slots[MOVER_SLOT_COUNT];
} mover_slot_table_t;

/* The slot holding this subnode, or NULL. Never allocates. */
mover_slot_t *mover_slots_find(mover_slot_table_t *table, const void *subnode);

/* The slot holding this subnode, or a fresh one for it: the first free slot on its probe path, or
 * failing that the first stale one, cleared. NULL when the table is full of live movers, and then
 * the subnode is not smoothed. `frame_stamp` is the current rendered frame, against which
 * staleness is measured. */
mover_slot_t *mover_slots_reserve(mover_slot_table_t *table, const void *subnode,
                                  uint32_t frame_stamp);

/* Everything forgotten at once, for the moment a level opens. Every subnode address the old level
 * had is free to be reused by the new one, and a slot that matched on address alone would hand the
 * newcomer a stranger's previous pose. */
void mover_slots_forget(mover_slot_table_t *table);

#endif /* MOVER_SLOTS_H */
