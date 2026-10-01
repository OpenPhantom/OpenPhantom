/* mp_capacity.c: watching the engine's accounts fill, without touching them.
 *
 * ============================ Why the chain and not the engine's own count ====================
 *
 * The engine has a function that answers this: the free count at 0x00414CE6 rewinds the object
 * list, walks it with the list's next call until that answers zero, and returns 0xFF minus what
 * it counted. Calling it would be shorter and it is the wrong thing to do, because it walks
 * using the list's SHARED iteration cursor. Several subsystems iterate the same list, and a
 * call landing between somebody's rewind and their last next would move their cursor under them.
 * The engine's own single caller is safe because of where it sits in the frame; a module message
 * is not the same place. Two things are worth keeping from that function anyway: the capacity
 * really is 255, written as 0xFF in the engine's own arithmetic, and its caller is a per object
 * draw gate that skips an extra pass once the pool is more than half committed, so the engine
 * already degrades a visual effect as the pool fills, and a second player's bodies, bolts and
 * corpses will make that gate fire earlier than it ever did alone.
 *
 * So the chain is walked here instead, through the link word each slot begins with. That reads the
 * same thing and moves nothing. The bound is the list's own capacity plus a little slack: a list
 * that does not end within its own capacity is corrupt, and reporting that beats hanging in a
 * frame message.
 *
 * ================================= The wall that is not here ==================================
 *
 * The third wall, the 255 entry gather array inside the world draw, is not readable from this
 * layer. Its fill count is a local of that function, so it does not exist yet when a detour on the
 * entry point runs, and reading it would mean patching into the middle of a function that two other
 * modules in this tree already branch over. That is a different kind of change from anything here,
 * and it is not made for a measurement.
 */
#include "mp_capacity.h"

#include "mp_cells.h"
#include "mp_signatures.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The list header, at the offsets the engine's own allocator writes them, all byte proven out of
 * the list functions: the list's own address at +0x00 (a fork stores its negation), the head of
 * the in use chain at +0x04, pushed front by the allocator, the shared iteration cursor at +0x08,
 * the payload size at +0x0C (not the slot stride), the slot count at +0x10, which is the bound
 * the allocator's scan answers NULL at, and a lock at +0x14 that is never taken in the shipped
 * build. The object list itself sits at 0x008A01DC in the retail image and the task count at
 * 0x00868720; both are read out of operands, never written down. */
#define LIST_HEAD_OFFSET     0x04u
#define LIST_CAPACITY_OFFSET 0x10u

/* An allocation that hands back the same slot twice, or a chain that loops, would otherwise be a
 * hang inside a frame message. */
#define PROBE_MAX 64u

/* The allocator at 0x0041223E takes nothing and the release at 0x004123B2 takes one pointer,
 * both cdecl. The release is a site another module in this tree already detours, which is why
 * the call goes through the resolved address and the signature carries a prefix of its own: the
 * bare name collided with that module's pattern in the site inventory and one silently replaced
 * the other until the mask registration check refused the pair. */
typedef void *(__cdecl *thing_alloc_fn)(void);
typedef void(__cdecl *thing_free_fn)(void *thing);

static mp_capacity_reading_t reading;

/* The walks that did not finish. The walk runs every frame, so a chain that stays broken would
 * write its line every frame: each kind is logged once and counted after that. */
typedef struct broken_walks {
    uint32_t unreadable;   /* stopped at a link that would not read */
    uint32_t overlong;     /* ran past the list's own capacity */
    bool     unreadable_logged;
    bool     overlong_logged;
} broken_walks_t;

static broken_walks_t broken;

const mp_capacity_reading_t *mp_capacity_last(void)
{
    return &reading;
}

void mp_capacity_broken_walks(uint32_t *unreadable, uint32_t *overlong)
{
    if (unreadable != NULL) {
        *unreadable = broken.unreadable;
    }
    if (overlong != NULL) {
        *overlong = broken.overlong;
    }
}

/* The in use chain, walked through the link word each slot begins with. Returns false when the
 * list is not readable or does not end where it should. */
static bool count_objects(uint32_t *live_out, uint32_t *capacity_out)
{
    uintptr_t cell = mp_cells_address(MP_CELL_OBJ_LIST);
    uint32_t  list = 0;
    uint32_t  capacity = 0;
    uint32_t  node = 0;
    uint32_t  live = 0;

    if (cell == 0 || !memory_try_read_u32(cell, &list) || list == 0) {
        return false;
    }
    if (!memory_try_readable((uintptr_t)list, LIST_CAPACITY_OFFSET + 4u) ||
        !memory_try_read_u32((uintptr_t)list + LIST_CAPACITY_OFFSET, &capacity) ||
        !memory_try_read_u32((uintptr_t)list + LIST_HEAD_OFFSET, &node)) {
        return false;
    }
    if (capacity == 0 || capacity > 0x10000u) {
        return false;   /* not a list header we understand */
    }

    while (node != 0) {
        if (live > capacity + MP_CAPACITY_WALK_SLACK) {
            ++broken.overlong;
            if (!broken.overlong_logged) {
                broken.overlong_logged = true;
                log_error("the object chain does not end within its own capacity of %u, so it is "
                          "not walkable and no occupancy is reported; later walks like it are "
                          "counted, not logged", (unsigned)capacity);
            }
            return false;
        }
        ++live;
        if (!memory_try_readable((uintptr_t)node, 4u) ||
            !memory_try_read_u32((uintptr_t)node, &node)) {
            ++broken.unreadable;
            if (!broken.unreadable_logged) {
                broken.unreadable_logged = true;
                log_error("the object chain is not readable %u slots in; later walks that stop at "
                          "a link are counted, not logged", (unsigned)live);
            }
            return false;
        }
    }

    *live_out     = live;
    *capacity_out = capacity;
    return true;
}

void mp_capacity_sample(void)
{
    uint32_t live = 0;
    uint32_t capacity = 0;
    uintptr_t task_cell;
    uint32_t tasks = 0;

    reading.objects_readable = count_objects(&live, &capacity);
    if (reading.objects_readable) {
        reading.objects_live     = live;
        reading.objects_capacity = capacity;
        if (live > reading.objects_high) {
            reading.objects_high = live;
        }
    }

    task_cell = mp_cells_address(MP_CELL_NUM_TASKS);
    reading.tasks_readable = (task_cell != 0) && memory_try_read_u32(task_cell, &tasks);
    if (reading.tasks_readable) {
        reading.tasks_live = tasks;
        if (tasks > reading.tasks_high) {
            reading.tasks_high = tasks;
        }
    }
}

uint32_t mp_capacity_high_water_mark(void)
{
    return reading.objects_high;
}

void mp_capacity_restore_high_water(uint32_t mark)
{
    reading.objects_high = mark;
}

void mp_capacity_probe_pool(uint32_t count)
{
    void          *taken[PROBE_MAX];
    thing_alloc_fn engine_alloc;
    thing_free_fn  engine_free;
    uintptr_t      alloc_site = mp_signatures_address(MP_SITE_THING_ALLOC);
    uintptr_t      free_site  = mp_signatures_address(MP_SITE_THING_FREE);
    uint32_t       got = 0;
    uint32_t       index;
    uint32_t       before;
    uint32_t       after;
    uint32_t       saved_high;

    if (count == 0) {
        return;
    }
    if (alloc_site == 0 || free_site == 0) {
        log_warning("the pool probe needs both the allocator and the release and one of them did "
                    "not resolve, so it is skipped");
        return;
    }
    if (count > PROBE_MAX) {
        count = PROBE_MAX;
    }

    mp_capacity_sample();
    before = reading.objects_live;

    /* The probe is an artificial load, so its own peak must not be reported later as something the
     * game did. Without this the high water reads 32 on a session whose real peak was 22, and the
     * number that matters is the one the game reached on its own. */
    saved_high = mp_capacity_high_water_mark();

    engine_alloc = (thing_alloc_fn)alloc_site;
    engine_free  = (thing_free_fn)free_site;

    for (index = 0; index < count; ++index) {
        taken[index] = engine_alloc();
        if (taken[index] == NULL) {
            break;      /* the pool said no, which is itself the answer */
        }
        ++got;
    }

    mp_capacity_sample();
    after = reading.objects_live;

    /* Everything goes back, in the same call, before anything can draw. */
    for (index = 0; index < got; ++index) {
        engine_free(taken[index]);
    }
    mp_capacity_sample();
    mp_capacity_restore_high_water(saved_high);

    log_info("pool probe: asked for %u slots and got %u; occupancy went %u -> %u -> %u of %u",
             (unsigned)count, (unsigned)got, (unsigned)before, (unsigned)after,
             (unsigned)reading.objects_live, (unsigned)reading.objects_capacity);

    if (got != count) {
        log_warning("  the pool refused after %u, so it was already close to full", (unsigned)got);
    }
    if (reading.objects_live != before) {
        log_error("  occupancy did not come back to where it started, so a slot was leaked");
    }
}

/* What one quiet level read: at most 22 and then 27 of 255 object slots occupied at frame begin,
 * 0 live at the quit, so the pool is given back completely at teardown; and five tasks of 64,
 * the four the engine registers plus ours. That is a lower bound twice over: the sample is one
 * instant per frame, so anything created and destroyed inside a frame is invisible to it, and one
 * quiet level is not the busy case. */
void mp_capacity_report(const char *why)
{
    if (reading.objects_readable) {
        log_info("capacity at %s: objects %u live, high water %u of %u", why,
                 (unsigned)reading.objects_live, (unsigned)reading.objects_high,
                 (unsigned)reading.objects_capacity);
        if (reading.objects_capacity != 0 &&
            reading.objects_high * 2u > reading.objects_capacity) {
            log_warning("  the object pool passed half full during this run, and every body, bolt, "
                        "corpse and severed limb of a second player comes out of the same account");
        }
    } else {
        log_warning("capacity at %s: the object pool could not be read", why);
    }

    if (reading.tasks_readable) {
        log_info("  tasks %u live, high water %u of 64", (unsigned)reading.tasks_live,
                 (unsigned)reading.tasks_high);
    } else {
        log_warning("  the task count could not be read");
    }

    log_info("  the drawable array is NOT measured here; its fill count is a local of the world "
             "draw and reading it would mean patching inside a function two other modules already "
             "branch over");
}
