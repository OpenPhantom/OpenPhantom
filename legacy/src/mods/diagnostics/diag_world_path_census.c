/* diag_world_path_census.c: the censuses of the draw path and of the traces underneath it.
 *
 * THE SEAM. The same cut as the mover census next door, taken a second time. These four censuses
 * count and report and patch nothing; the hooks that feed them stay in diag_world.c with the
 * detours and the byte evidence they belong to, and what crosses the boundary is a record call
 * per hook and an install call per trigger level. Together with the mover census that is what
 * took diag_world.c back under the hard limit, and one census file would have landed near it
 * again, which is why there are two.
 *
 * They are together in this one because they are a single question asked three times over, each
 * level earning the next: level 4 counts entries to the two render-path functions, level 5 asks
 * which call site drives bapmap_polyToWorld once that count is seen to explode, and level 6 asks
 * the same of the two traces that answer led to. Splitting them would separate a measurement from
 * the measurement that justifies it.
 *
 * diag_world_census_tick, the one per-frame callback all five censuses share, is here because
 * four of the five reports it makes are.
 */
#include "diag_world_path_census.h"

#include "diag_log.h"
#include "diag_world_mover_census.h"

#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ============================================================================================
 * The render-path census: how often the two known callers of the mover census are entered,
 * not what they do once inside. Level 4, rides on nothing else, arms independently of the mover
 * census so it can answer on its own whether an explosion is "this function ran too many times
 * this frame" or "one run of it walked more than it should have"; the mover census cannot tell
 * those apart because it only sees calls to the integrator, not to its own two callers.
 * ============================================================================================ */
#define RENDER_CENSUS_FRAMES 200u

typedef struct render_census {
    bool     armed;
    bool     per_frame;
    uint32_t poly_to_world_calls;
    uint32_t transform_world_calls;
    uint32_t frames;
} render_census_t;

static render_census_t render_census;

static void render_census_report(void)
{
    if (!render_census.armed) {
        return;
    }
    ++render_census.frames;
    if (render_census.frames < RENDER_CENSUS_FRAMES) {
        return;
    }

    diag_log_write("rdr  census over %u frames: bapmap_polyToWorld %u calls (%.2f per frame), "
                   "bapvrt_transformWorld %u calls (%.2f per frame)",
                   (unsigned)render_census.frames,
                   (unsigned)render_census.poly_to_world_calls,
                   (double)render_census.poly_to_world_calls / (double)render_census.frames,
                   (unsigned)render_census.transform_world_calls,
                   (double)render_census.transform_world_calls / (double)render_census.frames);

    render_census.frames                 = 0;
    render_census.poly_to_world_calls    = 0;
    render_census.transform_world_calls  = 0;
}

/* ============================================================================================
 * Who calls bapmap_polyToWorld. Level 5, and the render census above is what earns it: with
 * bapvrt_transformWorld pinned at exactly one call a frame through the worst of a measured stall
 * while bapmap_polyToWorld climbed from a few hundred calls a frame to over five thousand, the
 * question stopped being "is the world transform re-entered" (measured, no) and became "what is
 * driving one function that is meant to run about once per visible object to run that many times
 * in one frame instead". This answers it the same way the mover census answers the same kind of
 * question about bapmap_tickMover: by finding every `call rel32` in the host's own .text that
 * targets bapmap_polyToWorld and bucketing live traffic against the site it actually came from.
 *
 * A read of the call graph (not written into the code, kept here as evidence) finds fifteen static
 * callers on retail WMAIN.EXE, clustered in two groups: eleven sit close together between 0x40c464
 * and 0x40e672, which by their addresses alone look like one family of per-object-type draw
 * routines (the engine dispatches drawing by object kind, and this is the shape that dispatch
 * takes in the binary); the other four are further out, at 0x42aafe, 0x43a5ff, 0x456e8b and
 * 0x457032. Which of those a live session actually goes through, and in what proportion, is
 * exactly what a call count without a call site cannot say, which is why the mover census took
 * the same approach rather than trusting a hand-written list. */
#define POLY_CALL_SITES_MAX 24u
#define POLY_CENSUS_FRAMES  200u

typedef struct poly_census {
    bool            armed;
    bool            per_frame;

    size_t          site_count;
    uintptr_t       site_return[POLY_CALL_SITES_MAX];
    uint32_t        site_calls[POLY_CALL_SITES_MAX];

    uint32_t        calls_from_nowhere;
    uint32_t        frames;
    uint32_t        calls;
} poly_census_t;

static poly_census_t poly_census;

static void poly_census_find_call_sites(uintptr_t poly_address)
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
        if ((uintptr_t)((intptr_t)(text + index) + 5 + displacement) != poly_address) {
            continue;
        }
        if (poly_census.site_count < POLY_CALL_SITES_MAX) {
            poly_census.site_return[poly_census.site_count] = text + index + 5u;
            ++poly_census.site_count;
        }
    }
}

void poly_census_record(const void *return_address)
{
    size_t index;

    if (!poly_census.armed) {
        return;
    }
    ++poly_census.calls;

    for (index = 0; index < poly_census.site_count; ++index) {
        if (poly_census.site_return[index] != (uintptr_t)return_address) {
            continue;
        }
        ++poly_census.site_calls[index];
        return;
    }
    ++poly_census.calls_from_nowhere;
}

static void poly_census_report(void)
{
    uintptr_t base = host_image_base();
    size_t    index;

    if (!poly_census.armed) {
        return;
    }
    ++poly_census.frames;
    if (poly_census.frames < POLY_CENSUS_FRAMES) {
        return;
    }

    diag_log_write("ply  census over %u frames: %u calls, %.2f per frame, %u from an "
                   "unrecognised return address",
                   (unsigned)poly_census.frames, (unsigned)poly_census.calls,
                   (double)poly_census.calls / (double)poly_census.frames,
                   (unsigned)poly_census.calls_from_nowhere);

    for (index = 0; index < poly_census.site_count; ++index) {
        if (poly_census.site_calls[index] == 0) {
            continue;
        }
        diag_log_write("ply    site %u return +%06X: %u calls (%.2f per frame)",
                       (unsigned)index, (unsigned)(poly_census.site_return[index] - base),
                       (unsigned)poly_census.site_calls[index],
                       (double)poly_census.site_calls[index] / (double)poly_census.frames);
    }

    poly_census.frames             = 0;
    poly_census.calls              = 0;
    poly_census.calls_from_nowhere = 0;
    for (index = 0; index < poly_census.site_count; ++index) {
        poly_census.site_calls[index] = 0;
    }
}

void poly_census_install(uintptr_t poly_address)
{
    if (poly_address == 0) {
        return;
    }

    poly_census_find_call_sites(poly_address);
    if (poly_census.site_count == 0) {
        log_warning("the poly-to-world census found no call site for %08X, so there is nothing "
                    "to bucket against and it stays off",
                    (unsigned)poly_address);
        return;
    }

    poly_census.per_frame = frame_hook_add(diag_world_census_tick);
    poly_census.armed     = true;

    log_info("poly-to-world census armed: %u call sites for %08X, reporting every %u frames%s",
             (unsigned)poly_census.site_count, (unsigned)poly_address,
             (unsigned)POLY_CENSUS_FRAMES,
             poly_census.per_frame
                 ? ""
                 : ". The per-frame hook is NOT available, so nothing will ever be reported");
}

void render_census_install(void)
{
    render_census.per_frame = frame_hook_add(diag_world_census_tick);
    render_census.armed     = true;

    log_info("render census armed: counting entries to bapmap_polyToWorld and "
             "bapvrt_transformWorld, reporting every %u frames%s",
             (unsigned)RENDER_CENSUS_FRAMES,
             render_census.per_frame
                 ? ""
                 : ". The per-frame hook is NOT available, so nothing will ever be reported");
}

/* The counting half of the render census, reached from the two entry hooks in diag_world.c. The
 * armed test is here rather than at the hook so that the flag stays inside the file that owns it;
 * a hook on a function this hot is already a call, so this is one more. */
void render_census_count_poly_to_world(void)
{
    if (render_census.armed) {
        ++render_census.poly_to_world_calls;
    }
}

void render_census_count_transform_world(void)
{
    if (render_census.armed) {
        ++render_census.transform_world_calls;
    }
}

/* ============================================================================================
 * Who calls the two traces. Level 6. The poly-to-world census named FUN_0040e06b's own call site
 * as the dominant one during the stall, and FUN_0040e06b has exactly one job: it is the per-candidate
 * distance test both FUN_0040be00 (the general, mover-aware line trace) and FUN_0040c2be (the floor
 * trace) run inside the same shared broadphase walk. Neither of those two is a callee bapmap_polyToWorld
 * chooses for itself; they are the reason it runs at all in this path, so the next question is
 * which of THEIR OWN callers, spread across player movement, AI and physics, is the one actually
 * asking for a trace thousands of times in one frame. One shared shape, two independent instances,
 * the same reason the poly-to-world census above did not just reuse the mover census's own state. */
#define CALL_CENSUS_SITES_MAX 24u
#define CALL_CENSUS_FRAMES    200u

typedef struct call_census {
    const char     *tag;      /* the log line prefix, e.g. "tgc" */
    const char     *what;     /* named in the arm/report lines */
    bool            armed;
    bool            per_frame;
    size_t          site_count;
    uintptr_t       site_return[CALL_CENSUS_SITES_MAX];
    uint32_t        site_calls[CALL_CENSUS_SITES_MAX];
    uint32_t        calls_from_nowhere;
    uint32_t        frames;
    uint32_t        calls;
} call_census_t;

static void call_census_find_call_sites(call_census_t *census, uintptr_t target_address)
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
        if ((uintptr_t)((intptr_t)(text + index) + 5 + displacement) != target_address) {
            continue;
        }
        if (census->site_count < CALL_CENSUS_SITES_MAX) {
            census->site_return[census->site_count] = text + index + 5u;
            ++census->site_count;
        }
    }
}

static void call_census_record(call_census_t *census, const void *return_address)
{
    size_t index;

    if (!census->armed) {
        return;
    }
    ++census->calls;

    for (index = 0; index < census->site_count; ++index) {
        if (census->site_return[index] != (uintptr_t)return_address) {
            continue;
        }
        ++census->site_calls[index];
        return;
    }
    ++census->calls_from_nowhere;
}

static void call_census_report(call_census_t *census)
{
    uintptr_t base = host_image_base();
    size_t    index;

    if (!census->armed) {
        return;
    }
    ++census->frames;
    if (census->frames < CALL_CENSUS_FRAMES) {
        return;
    }

    diag_log_write("%s  census over %u frames: %s: %u calls, %.2f per frame, %u from an "
                   "unrecognised return address",
                   census->tag, (unsigned)census->frames, census->what, (unsigned)census->calls,
                   (double)census->calls / (double)census->frames,
                   (unsigned)census->calls_from_nowhere);

    for (index = 0; index < census->site_count; ++index) {
        if (census->site_calls[index] == 0) {
            continue;
        }
        diag_log_write("%s    site %u return +%06X: %u calls (%.2f per frame)",
                       census->tag, (unsigned)index, (unsigned)(census->site_return[index] - base),
                       (unsigned)census->site_calls[index],
                       (double)census->site_calls[index] / (double)census->frames);
    }

    census->frames             = 0;
    census->calls              = 0;
    census->calls_from_nowhere = 0;
    for (index = 0; index < census->site_count; ++index) {
        census->site_calls[index] = 0;
    }
}

static void call_census_install(call_census_t *census, uintptr_t target_address)
{
    if (target_address == 0) {
        return;
    }

    call_census_find_call_sites(census, target_address);
    if (census->site_count == 0) {
        log_warning("the %s census found no call site for %08X, so there is nothing to bucket "
                    "against and it stays off",
                    census->what, (unsigned)target_address);
        return;
    }

    census->armed = true;

    log_info("%s census armed: %u call sites for %08X, reporting every %u frames%s",
             census->what, (unsigned)census->site_count, (unsigned)target_address,
             (unsigned)CALL_CENSUS_FRAMES,
             census->per_frame
                 ? ""
                 : ". The per-frame hook is NOT available, so nothing will ever be reported");
}

static call_census_t trace_general_census = { "tgc", "general trace (FUN_0040be00)" };
static call_census_t trace_floor_census   = { "tfc", "floor trace (FUN_0040c2be)" };

static void trace_general_census_report(void) { call_census_report(&trace_general_census); }
static void trace_floor_census_report(void)   { call_census_report(&trace_floor_census); }

/* The record side of the same two, reached from the trace hooks in diag_world.c. */
void trace_general_census_record(const void *return_address)
{
    call_census_record(&trace_general_census, return_address);
}

void trace_floor_census_record(const void *return_address)
{
    call_census_record(&trace_floor_census, return_address);
}

void trace_census_install(uintptr_t trace_general_address, uintptr_t trace_floor_address)
{
    trace_general_census.per_frame = frame_hook_add(diag_world_census_tick);
    call_census_install(&trace_general_census, trace_general_address);

    trace_floor_census.per_frame = frame_hook_add(diag_world_census_tick);
    call_census_install(&trace_floor_census, trace_floor_address);
}

void diag_world_census_tick(void)
{
    mover_census_report();
    render_census_report();
    poly_census_report();
    trace_general_census_report();
    trace_floor_census_report();
}
