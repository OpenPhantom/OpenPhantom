/* mp_placements.h: the level's placement table, walked once and in one place.
 *
 * A placement is what the level AUTHORED, an enemy, a pickup, a spawn point, a scripted prop,
 * as opposed to the actor the engine builds out of it when a player comes close. The table is the
 * only description of a level that survives an actor being deleted, which is why every part of
 * this feature that has to say something durable about a level says it about placements: what the
 * arena buries, where a deathmatch may seat somebody, and which pickups are gone for good.
 *
 * Why it is its own file. Three modules needed the same six reads, and the third one asking was
 * the moment to stop copying them. Two copies had already drifted: one handed the world record
 * back and the other did not, so a caller that wanted both did the first read twice. That is the
 * cheap kind of drift; the expensive kind is a bounds check that gets tightened in one copy.
 *
 * SIZE NOTE: under 120 lines, no seam.
 */
#ifndef MULTIPLAYER_MP_PLACEMENTS_H
#define MULTIPLAYER_MP_PLACEMENTS_H

#include "mp_cells.h"

#include <stdbool.h>
#include <stdint.h>

/* A placement count past this is a pointer that is not a world. The engine's own assert bound on
 * a placement id is 255, so anything in five figures is a misread rather than a big level. The
 * limit is loose on purpose: it is there to stop a walk over garbage, not to say what a level may
 * contain. */
#define MP_PLACEMENTS_COUNT_LIMIT 0x10000u

/* How far into a placement record anything here reads. The spawn state is the last of the three
 * fields, so the readable check has to cover it and nothing beyond. */
#define MP_PLACEMENTS_READ_BYTES (MP_PLACEMENT_SPAWN_STATE + 4u)

/* One placement, in the fields this feature has any use for. */
typedef struct mp_placement {
    uintptr_t address;
    uint32_t  flags;
    int32_t   class_id;      /* the perception and collision band, NOT what the placement builds */
    uint32_t  state;         /* MP_PLACEMENT_STATE_ASLEEP, _LIVE or _BURIED */
    bool      drives_mover;  /* any of the four mover slots is set: this one holds geometry */
} mp_placement_t;

/* The world record and the two numbers describing its table. `world` may be NULL for a caller
 * that only wants to walk. False when no level is open, which is what a menu looks like and is
 * not a fault.
 *
 * The cell is asked for on every call rather than kept, so a call works whether or not anything
 * else has been installed yet and in whichever order the two happened. */
bool mp_placements_table(uintptr_t *world, uint32_t *count, uint32_t *table);

/* One placement whose ADDRESS is already known, which is what a hook on the engine's own spawner
 * has: it is handed the record and never sees the table. */
bool mp_placements_read_at(uintptr_t address, mp_placement_t *out);

/* Placement `index` out of a table obtained above, with its three fields read. False when the
 * slot or the record behind it does not read, which is a fault worth counting, and every caller
 * does count it. */
bool mp_placements_read(uint32_t table, uint32_t index, mp_placement_t *out);

/* The two distances the engine measures a placement by, read live, because a script may rewrite
 * a record after the level has loaded. */
typedef struct mp_placement_reach {
    float wake;          /* the activation scan's radius, 0 for no distance test */
    float keep;          /* the entity loop's removal radius, 0 for never removed */
    float position[3];   /* the authored position the activation scan measures from */
} mp_placement_reach_t;

/* The distances of the placement record at `address`, which is what an actor's own placement
 * pointer names. False when the record does not read. */
bool mp_placements_reach_at(uintptr_t address, mp_placement_reach_t *out);

/* Whether this class builds something a player picks up by walking into it. The engine's own
 * contact handler branches on this band, and a body inside it is deleted by the touch. */
bool mp_placements_is_pickup(int32_t class_id);

#endif /* MULTIPLAYER_MP_PLACEMENTS_H */
