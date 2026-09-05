/* diag_world_mover_census.c: which call site reaches the mover integrator, and how often it
 * finds anything to do.
 *
 * THE SEAM. This is the cut diag_world.c's own size note named before it was made. The census
 * touches none of the detour state the observers around it share, it reads the engine through the
 * one detour that already exists for level 2, and everything it owns is its own. It leaves behind
 * three calls: one from hook_mover_tick, one from diag_trigger_install and one from the per-frame
 * tick. Nothing else in this file is reachable from outside it.
 *
 * The two mover offsets below came with it because nothing else in the module reads them.
 */
#include "diag_world_mover_census.h"

#include "diag_log.h"
#include "diag_world_path_census.h"

#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The mover's own clock, the value the integrator's second early return compares the time it was
 * passed against. */
#define MOVER_TIME_BASE 0x30

/* The absolute operand of the integrator's first early return, read out of the matched pattern
 * rather than written down, so the census works on any build that pattern resolves on. */
#define OFFSET_MOVER_GATE_CELL 0x08u

/* ============================================================================================
 * The mover call-site census. Level 3, and it patches nothing.
 *
 * The design for interpolating movers assumed the integrator runs once per frame from the per-frame
 * message, so that a bracket around the world draw would contain a mover's pose but not its
 * advance. A reading of the call graph says otherwise: of the nine callers, two are reached from
 * the draw, and if either of those reaches a mover first then the mover integrates inside the
 * proposed bracket and the whole design is unbuildable. Both ways it can fail present as "the fix
 * did nothing", which is the worst possible symptom to debug.
 *
 * So this measures it instead of arguing about it. Per call site: how often it is reached, and how
 * often the mover it was handed had a clock older than the time being passed in, which is exactly
 * the condition under which the function does anything at all.
 *
 * The call sites are discovered, not written down. The tick's own address comes from the pattern,
 * and every `call rel32` in the host's code section that targets it is a call site. The census then
 * reports the count it found, which is itself a finding on any build other than the one this was
 * derived from, and no address in this file has to be right for it to work.
 *
 * What that scan found on retail WMAIN.EXE, kept here as evidence rather than as data the code
 * reads. Nine `call rel32` sites target 0x00409170 and no absolute reference to that address exists
 * anywhere, so there is no call through a pointer to miss. The return addresses are
 *
 *   00408B44  00409799  0040A33D  0040A849  0040AF2F  0040AFE8  0040C3F7
 *   004194EC  00419B31
 *
 * and the last two are the ones the whole question turns on. 0x004194E7 sits inside
 * bapmap_polyToWorld 0x00419490 and 0x00419B2C inside bapvrt_transformWorld 0x004199B0, which
 * render_prepareFrame calls at 0x0043F5AC. If either of those reaches a mover before the per-frame
 * message does, that mover integrates inside the draw. Writing those nine addresses into the code
 * would have bought nothing and would have tied the census to one build.
 * ============================================================================================ */
#define MOVER_CALL_SITES_MAX 16u
#define MOVER_CENSUS_FRAMES  200u

typedef struct mover_census {
    bool            armed;
    bool            per_frame;
    const uint32_t *gate_cell;

    size_t          site_count;
    uintptr_t       site_return[MOVER_CALL_SITES_MAX];
    uint32_t        site_calls[MOVER_CALL_SITES_MAX];
    uint32_t        site_stale_clock[MOVER_CALL_SITES_MAX];

    uint32_t        calls_from_nowhere;
    uint32_t        gate_closed;
    uint32_t        frames;
    uint32_t        calls;
} mover_census_t;

static mover_census_t mover_census;

static void mover_census_find_call_sites(uintptr_t tick_address)
{
    uintptr_t text = host_image_text();
    size_t    size = host_image_text_size();
    size_t    index;

    if (text == 0 || size < 5 || !memory_is_readable_range(text, size)) {
        return;
    }

    for (index = 0; index + 5u <= size; ++index) {
        const uint8_t *at = (const uint8_t *)(text + index);
        int32_t        displacement;

        if (*at != 0xE8) {
            continue;
        }
        memcpy(&displacement, at + 1, sizeof(displacement));
        if ((uintptr_t)((intptr_t)(text + index) + 5 + displacement) != tick_address) {
            continue;
        }
        if (mover_census.site_count < MOVER_CALL_SITES_MAX) {
            mover_census.site_return[mover_census.site_count] = text + index + 5u;
            ++mover_census.site_count;
        }
    }
}

void mover_census_record(const void *return_address, const uint8_t *mover, float now)
{
    size_t index;

    if (!mover_census.armed) {
        return;
    }
    ++mover_census.calls;

    if (mover_census.gate_cell != NULL && *mover_census.gate_cell != 0) {
        ++mover_census.gate_closed;
    }

    for (index = 0; index < mover_census.site_count; ++index) {
        if (mover_census.site_return[index] != (uintptr_t)return_address) {
            continue;
        }
        ++mover_census.site_calls[index];
        /* The condition the function's second early return is decided on, read before the call
         * because the call is what changes it. Equal means the mover has already integrated to
         * this time and the call will do nothing. */
        if (mover != NULL && *(const float *)(mover + MOVER_TIME_BASE) != now) {
            ++mover_census.site_stale_clock[index];
        }
        return;
    }
    ++mover_census.calls_from_nowhere;
}

void mover_census_report(void)
{
    uintptr_t base = host_image_base();
    size_t    index;

    if (!mover_census.armed) {
        return;
    }
    ++mover_census.frames;
    if (mover_census.frames < MOVER_CENSUS_FRAMES) {
        return;
    }

    diag_log_write("mvr  census over %u frames: %u calls, %.2f per frame, %u through a closed "
                   "gate, %u from an unrecognised return address",
                   (unsigned)mover_census.frames, (unsigned)mover_census.calls,
                   (double)mover_census.calls / (double)mover_census.frames,
                   (unsigned)mover_census.gate_closed,
                   (unsigned)mover_census.calls_from_nowhere);

    for (index = 0; index < mover_census.site_count; ++index) {
        if (mover_census.site_calls[index] == 0) {
            continue;
        }
        diag_log_write("mvr    site %u return +%06X: %u calls, %u with a clock older than the "
                       "time passed in (%.2f calls per frame)",
                       (unsigned)index,
                       (unsigned)(mover_census.site_return[index] - base),
                       (unsigned)mover_census.site_calls[index],
                       (unsigned)mover_census.site_stale_clock[index],
                       (double)mover_census.site_calls[index] / (double)mover_census.frames);
    }

    mover_census.frames             = 0;
    mover_census.calls              = 0;
    mover_census.gate_closed        = 0;
    mover_census.calls_from_nowhere = 0;
    for (index = 0; index < mover_census.site_count; ++index) {
        mover_census.site_calls[index]       = 0;
        mover_census.site_stale_clock[index] = 0;
    }
}

void mover_census_install(uintptr_t tick_address)
{
    uint32_t gate;

    if (tick_address == 0) {
        return;
    }

    mover_census_find_call_sites(tick_address);
    if (mover_census.site_count == 0) {
        log_warning("the mover census found no call site for the integrator at %08X, so there is "
                    "nothing to bucket against and it stays off",
                    (unsigned)tick_address);
        return;
    }

    if (memory_read_u32(tick_address + OFFSET_MOVER_GATE_CELL, &gate) &&
        memory_is_inside_image(gate, sizeof(uint32_t))) {
        mover_census.gate_cell = (const uint32_t *)(uintptr_t)gate;
    }

    mover_census.per_frame = frame_hook_add(diag_world_census_tick);
    mover_census.armed     = true;

    log_info("mover census armed: %u call sites for the integrator at %08X, gate cell %08X, "
             "reporting every %u frames%s",
             (unsigned)mover_census.site_count, (unsigned)tick_address,
             (unsigned)(uintptr_t)mover_census.gate_cell, (unsigned)MOVER_CENSUS_FRAMES,
             mover_census.per_frame
                 ? ""
                 : ". The per-frame hook is NOT available, so nothing will ever be reported");
}
