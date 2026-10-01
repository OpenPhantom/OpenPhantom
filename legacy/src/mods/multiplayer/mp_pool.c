/* mp_pool.c: the reserve latch in front of the engine's object pool.
 *
 * ===================================== Why a latch at all =====================================
 *
 * The engine allocator itself is honest: at a full pool it returns NULL cleanly. What is not
 * honest is what happens next, because three of its seven callers dereference that NULL without a
 * check, and the assert handler that would have turned the mistake into a message is NULL in the
 * retail build. So the pool's failure mode is an access violation in engine code, some frames
 * after whoever took the last slot has gone.
 *
 * This feature is the one tenant this tree adds to that pool on purpose, so it takes every slot
 * through this file: occupancy is read first, the allocation only proceeds while more than the
 * reserve would stay free, and the return value is checked even then. The latch can only refuse
 * OUR allocations; the engine's own take what they always took.
 *
 * ================================== What the latch cannot do ==================================
 *
 * It cannot protect the engine from itself. The worst measured engine demand is 252 shots arising
 * in one substep, which no reserve on a 255 slot pool covers. The guarantee here is smaller and
 * real: the slots this feature holds are bounded, checked, and given back through the same door,
 * so when the pool does fill, none of the unchecked engine callers got their NULL because of us.
 */
#include "mp_pool.h"

#include "mp_capacity.h"
#include "mp_signatures.h"

#include "common/logging.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void *(__cdecl *thing_alloc_fn)(void);
typedef void(__cdecl *thing_free_fn)(void *thing);

/* Enough for every slot a 255 capacity pool can hand out, with room to notice a pool that hands
 * out more than its own header admits to. Static because two kilobytes do not belong on the stack
 * of a frame message. */
#define EXHAUST_MAX 512u

typedef struct mp_pool_state {
    uint32_t reserve;
    uint32_t outstanding;
    uint32_t refusals;
} mp_pool_state_t;

static mp_pool_state_t state = { MP_POOL_RESERVE_DEFAULT, 0u, 0u };

void mp_pool_configure(uint32_t reserve)
{
    if (reserve > MP_POOL_RESERVE_MAX) {
        log_warning("a pool reserve of %u would refuse nearly everything, clamped to %u",
                    (unsigned)reserve, (unsigned)MP_POOL_RESERVE_MAX);
        reserve = MP_POOL_RESERVE_MAX;
    }
    state.reserve = reserve;
}

uint32_t mp_pool_reserve(void)
{
    return state.reserve;
}

uint32_t mp_pool_outstanding(void)
{
    return state.outstanding;
}

uint32_t mp_pool_refusals(void)
{
    return state.refusals;
}

/* Every refusal is counted; not every one is written. A caller retrying a spawn once per substep
 * would otherwise put thirty-two identical lines a second into the log, so after the first eight
 * the log gets a heartbeat rather than a stream. */
static bool count_refusal(void)
{
    ++state.refusals;
    return state.refusals <= 8u || (state.refusals & 0xFFu) == 0u;
}

/* The allocator at 0x41223E fails cleanly: at 0x412253 it is `cmp [ebp-4], 0 / jne / xor eax, eax`,
 * so a full pool answers NULL rather than trapping. The hazard is downstream: of its seven callers
 * in the retail image three dereference the return without a check, among them the hero spawn
 * (the call at 0x447EC1), which follows the allocation with a `rep stosd` over the object, and
 * the assert handler at 0x868640 that would have caught the class is NULL in the retail build,
 * 97 readers and no writer.
 *
 * Occupancy is sampled through the capacity module's own walk of the in-use chain and not through
 * the engine's free count function, because that function iterates with the list's SHARED cursor
 * and calling it from module context moves another subsystem's cursor in the middle of its walk. */
void *mp_pool_take(const char *what)
{
    const mp_capacity_reading_t *reading;
    thing_alloc_fn               engine_alloc;
    uintptr_t                    site = mp_signatures_address(MP_SITE_THING_ALLOC);
    void                        *thing;

    if (what == NULL) {
        what = "an unnamed caller";
    }

    if (site == 0) {
        if (count_refusal()) {
            log_warning("the allocator did not resolve on this build, so %s gets no slot", what);
        }
        return NULL;
    }

    mp_capacity_sample();
    reading = mp_capacity_last();
    if (!reading->objects_readable) {
        if (count_refusal()) {
            log_warning("the object pool is not readable, so its occupancy is unknown and %s "
                        "gets no slot", what);
        }
        return NULL;
    }

    if (!mp_pool_may_take(reading->objects_live, reading->objects_capacity, state.reserve)) {
        if (count_refusal()) {
            log_warning("the reserve held: %u live of %u leaves %u free against a reserve of %u, "
                        "so %s gets no slot (refusal %u)",
                        (unsigned)reading->objects_live, (unsigned)reading->objects_capacity,
                        (unsigned)(reading->objects_capacity > reading->objects_live
                                       ? reading->objects_capacity - reading->objects_live : 0u),
                        (unsigned)state.reserve, what, (unsigned)state.refusals);
        }
        return NULL;
    }

    engine_alloc = (thing_alloc_fn)site;
    thing        = engine_alloc();
    if (thing == NULL) {
        /* The walk and the allocator disagree about whether there was room. That is a finding
         * about one of the two instruments, and it is always written, because a heartbeat is for
         * a known condition repeating and this one is never expected at all. */
        ++state.refusals;
        log_error("the walk said %u live of %u but the allocator still answered NULL, so the two "
                  "disagree and %s gets no slot",
                  (unsigned)reading->objects_live, (unsigned)reading->objects_capacity, what);
        return NULL;
    }

    ++state.outstanding;
    return thing;
}

void mp_pool_give(void *thing)
{
    thing_free_fn engine_free;
    uintptr_t     site = mp_signatures_address(MP_SITE_THING_FREE);

    if (thing == NULL) {
        log_error("a NULL was handed back to the pool latch, which is a caller error and not a "
                  "slot");
        return;
    }
    if (site == 0) {
        /* Cannot happen after a successful take on the same build, so it is a caller passing a
         * pointer that never came through the latch. The slot cannot be given back safely. */
        log_error("the release did not resolve on this build, so a pointer that cannot have come "
                  "from this latch was not freed");
        return;
    }

    engine_free = (thing_free_fn)site;
    engine_free(thing);

    if (state.outstanding != 0u) {
        --state.outstanding;
    } else {
        log_error("more slots were given back than were ever taken, so some caller returned one "
                  "twice or returned somebody else's");
    }
}

/* ==============================================================================================
 * The provocation: the one way to see the latch refuse before the day it matters.
 *
 * Everything happens inside this one call, on the thread the frame hook runs on, and nothing
 * engine-side executes between the taking and the giving back: no draw, no tick, no allocation
 * but ours. That is the same guarantee the smaller pool probe has already carried through a green
 * field run. The pool is genuinely full for a moment, and nobody is looking while it is.
 * ============================================================================================== */

static uint32_t release_slots(void **taken, uint32_t held, uint32_t how_many,
                              thing_free_fn engine_free)
{
    while (how_many != 0u && held != 0u) {
        --held;
        --how_many;
        engine_free(taken[held]);
    }
    return held;
}

void mp_pool_provoke_full(void)
{
    static void *taken[EXHAUST_MAX];

    const mp_capacity_reading_t *reading;
    thing_alloc_fn               engine_alloc;
    thing_free_fn                engine_free;
    uintptr_t                    alloc_site = mp_signatures_address(MP_SITE_THING_ALLOC);
    uintptr_t                    free_site  = mp_signatures_address(MP_SITE_THING_FREE);
    uint32_t                     before;
    uint32_t                     capacity;
    uint32_t                     high_mark;
    uint32_t                     held = 0;
    uint32_t                     band;
    void                        *attempt;

    if (alloc_site == 0 || free_site == 0) {
        log_warning("the full pool provocation needs the allocator and the release and one of "
                    "them did not resolve, so it is skipped");
        return;
    }

    mp_capacity_sample();
    reading = mp_capacity_last();
    if (!reading->objects_readable) {
        log_warning("the full pool provocation cannot read the pool it would fill, so it is "
                    "skipped");
        return;
    }
    before   = reading->objects_live;
    capacity = reading->objects_capacity;

    /* The load below is artificial, and an instrument that reports its own probe as the game's
     * peak has already happened once in this feature. The mark taken here goes back at the end. */
    high_mark = mp_capacity_high_water_mark();

    engine_alloc = (thing_alloc_fn)alloc_site;
    engine_free  = (thing_free_fn)free_site;

    log_info("provoking a full pool on purpose: %u of %u live before, reserve %u",
             (unsigned)before, (unsigned)capacity, (unsigned)state.reserve);

    while (held < EXHAUST_MAX) {
        void *slot = engine_alloc();

        if (slot == NULL) {
            break;
        }
        taken[held] = slot;
        ++held;
    }
    if (held == EXHAUST_MAX) {
        log_error("  the pool handed out %u slots without refusing, more than its capacity of %u "
                  "admits to; everything goes back and the provocation stops",
                  (unsigned)EXHAUST_MAX, (unsigned)capacity);
        held = release_slots(taken, held, held, engine_free);
        mp_capacity_restore_high_water(high_mark);
        return;
    }
    log_info("  the pool refused after %u more slots, so it is now truly full", (unsigned)held);

    attempt = mp_pool_take("the provocation at a full pool");
    if (attempt != NULL) {
        log_error("  the latch handed out a slot at a full pool, so it does not hold");
        mp_pool_give(attempt);
    } else {
        log_info("  the latch refused at zero free, as it must");
    }

    /* The band between empty and the reserve is the case the latch actually exists for, and zero
     * free does not exercise it. A reserve of zero has no band, and then this stays untested
     * because it does not exist. */
    band = state.reserve / 2u;
    if (band == 0u && state.reserve != 0u) {
        band = 1u;
    }
    if (band > held) {
        band = held;
    }
    if (band != 0u) {
        held = release_slots(taken, held, band, engine_free);

        attempt = mp_pool_take("the provocation inside the reserve band");
        if (attempt != NULL) {
            log_error("  the latch handed out a slot with %u free against a reserve of %u, so "
                      "the band does not hold", (unsigned)band, (unsigned)state.reserve);
            mp_pool_give(attempt);
        } else {
            log_info("  the latch refused with %u free against a reserve of %u, as it must",
                     (unsigned)band, (unsigned)state.reserve);
        }
    }

    held = release_slots(taken, held, held, engine_free);

    mp_capacity_sample();
    reading = mp_capacity_last();
    if (reading->objects_readable && reading->objects_live != before) {
        log_error("  occupancy is %u and started at %u, so the provocation leaked",
                  (unsigned)reading->objects_live, (unsigned)before);
    }

    /* Only expect the ordinary allocation to pass where the latch itself says it should: a busy
     * level can legitimately sit inside the reserve band with everything given back. */
    if (reading->objects_readable &&
        mp_pool_may_take(reading->objects_live, capacity, state.reserve)) {
        attempt = mp_pool_take("the provocation after the release");
        if (attempt != NULL) {
            mp_pool_give(attempt);
            log_info("  an ordinary allocation passes again, so the latch only holds when it "
                     "should");
        } else {
            log_error("  the pool is back to %u live of %u and the latch still refuses, which "
                      "is wrong", (unsigned)reading->objects_live, (unsigned)capacity);
        }
    }

    mp_capacity_restore_high_water(high_mark);
    log_info("  the provocation is over and every slot it took is given back");
}

void mp_pool_report(const char *why)
{
    log_info("the pool latch at %s: reserve %u, %u slot(s) held, %u refusal(s)",
             why, (unsigned)state.reserve, (unsigned)state.outstanding,
             (unsigned)state.refusals);
}
