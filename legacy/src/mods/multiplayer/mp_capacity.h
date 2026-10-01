/* mp_capacity.h: how close the engine is to its own hard walls, measured and never patched.
 *
 * The question this exists for is how many players the engine carries, and the honest way to answer
 * it is to watch the accounts fill during ordinary play rather than to divide a constant by four.
 *
 * There are three walls. Two are readable from outside and one is not:
 *
 *   the object pool     255 slots, and every body, bolt, corpse, muzzle flash and severed limb
 *                       comes out of it. Readable: the list keeps its own capacity and its in use
 *                       chain can be walked without touching the engine's iteration cursor.
 *   the task pool       64 slots. Readable: the scheduler keeps a count in a cell of its own.
 *   the drawable array  255 entries, and it is a STACK array inside the world draw with no bounds
 *                       check, so overrunning it is a return address rather than a dropped object.
 *                       NOT readable from here: the fill count is a local of a function two other
 *                       modules already detour, and it does not exist yet when a detour on the
 *                       entry runs. It is also bounded by the pool: the draw gathers from the
 *                       object list and nothing else, so 255 entries cannot be exceeded by 255
 *                       objects, and a measurement would report a ceiling the engine is unable
 *                       to reach. The wall moves with the pool, and the day the pool is raised
 *                       this array is the first thing that overruns; the two belong in one
 *                       change.
 *
 * Nothing here writes to the engine. The optional load below allocates from the object pool and
 * gives every slot back in the same call, which is the only way to see whether the instrument
 * responds at all.
 */
#ifndef MULTIPLAYER_MP_CAPACITY_H
#define MULTIPLAYER_MP_CAPACITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The engine's own bound on a walk of the object chain. A list that does not end within its own
 * capacity is a corrupt list, and saying so beats hanging. */
#define MP_CAPACITY_WALK_SLACK 8u

typedef struct mp_capacity_reading {
    uint32_t objects_live;      /* slots currently taken out of the object pool */
    uint32_t objects_capacity;  /* what the list says its own capacity is */
    uint32_t objects_high;      /* the most seen at once during this run */
    uint32_t tasks_live;
    uint32_t tasks_high;
    bool     objects_readable;
    bool     tasks_readable;
} mp_capacity_reading_t;

/* One sample. Cheap enough for once per frame: a bounded pointer walk and one dword read. */
void mp_capacity_sample(void);

const mp_capacity_reading_t *mp_capacity_last(void);

/* The walks of the object chain that did not finish since the process began: stopped at a link
 * that would not read, or run past the list's own capacity. The first of each is logged. */
void mp_capacity_broken_walks(uint32_t *unreadable, uint32_t *overlong);

/* Take `count` slots out of the object pool and give all of them back before returning, reporting
 * the free count on both sides of it. It exists so that the instrument can be seen responding; an
 * instrument that has only ever reported a quiet system has not been tested.
 *
 * It never keeps a slot and it never approaches the drawable wall, because nothing is drawn between
 * the taking and the giving back. */
void mp_capacity_probe_pool(uint32_t count);

/* For a caller about to create artificial load: take the mark before, put it back after, and the
 * high water stays the game's own. An instrument here has already once reported its own probe as
 * the session's peak, and these two exist so the next artificial load does not repeat that. */
uint32_t mp_capacity_high_water_mark(void);
void     mp_capacity_restore_high_water(uint32_t mark);

void mp_capacity_report(const char *why);

#endif /* MULTIPLAYER_MP_CAPACITY_H */
