/* fog_trace.c: see fog_trace.h. */
#include "fog_trace.h"

#include "common/logging.h"
#include "common/detour.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdint.h>
#include <stdio.h>

/* Forty seconds at a hundred frames a second. Three passes at the same opening cutscene fit,
 * which is the point: the first load shows the fault and the second and third do not, so the
 * three runs are the same scene with and without it and the difference between them is the whole
 * of the evidence.
 *
 * It captures every frame in that window whether the band moved or not. An earlier version
 * stopped at the first settled frame, and once a level began opening on its final band that was
 * frame zero, so it recorded nothing of the window it was built to look at. A frame where
 * nothing happened is evidence too. */
#define TRACE_CAPACITY 4000u

/* The draw path's own ceilings, only one of which this project already measures elsewhere (the
 * deferred array, which render_guard watches). Every one of them can drop
 * geometry with no error and no dip in anything we already watch: the two vertex gates abandon the
 * rest of the frame BEFORE incrementing their counter, and the sort heap accepts a face into the
 * deferred array and its vertices into the pool and then never inserts it into the queue.
 *
 * EVERY ONE OF THESE IS RESOLVED OUT OF AN OPERAND, not written down as an address. Each pattern
 * is a run of instructions that touches the counter, with the counter's own address wildcarded and
 * read back at install, and with every other image address inside the window wildcarded as well so
 * the pattern survives forced ASLR rather than matching only at the preferred base. All thirteen
 * match exactly once. */
typedef struct counter_site {
    const char    *name;
    const uint8_t *pattern;
    const uint8_t *mask;
    size_t         size;
    size_t         operand;   /* byte offset of the wildcarded address inside the pattern */
    uint32_t       ceiling;   /* the value at which the engine starts refusing; 0 where it is a
                               * plain count or a float and there is nothing to be near */
    uintptr_t      address;   /* filled in at install, zero until then */
} counter_site_t;

/* vert_cache    005BB5C0, out of the operand at 004198C8 + 2 */
static const uint8_t SIG_VERT_CACHE[] = {
    0xC7, 0x05, 0xC0, 0xB5, 0x5B, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xC7, 0x40, 0x04, 0x00, 0x00, 0x00,
};
static const uint8_t MSK_VERT_CACHE[] = {
    1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
};

/* sort_heap     00734C0C, out of the operand at 00487C8F + 2 */
static const uint8_t SIG_SORT_HEAP[] = {
    0x90, 0xA1, 0x0C, 0x4C, 0x73, 0x00, 0x56, 0x3D, 0x04, 0x20,
    0x00, 0x00, 0x57, 0x7D, 0x78, 0x8B,
};
static const uint8_t MSK_SORT_HEAP[] = {
    1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
};

/* deferred      00866FB0, out of the operand at 00487D1D + 4 */
static const uint8_t SIG_DEFERRED[] = {
    0x90, 0x90, 0x90, 0xA1, 0xB0, 0x6F, 0x86, 0x00, 0x83, 0xEC,
    0x28, 0xD9, 0x05, 0x78, 0x8D, 0x4A,
};
static const uint8_t MSK_DEFERRED[] = {
    1, 1, 1, 1, 0, 0, 0, 0, 1, 1,
    1, 1, 1, 1, 1, 1,
};

/* vert_pool     004DD6EC, out of the operand at 00401D3E + 2 */
static const uint8_t SIG_VERT_POOL[] = {
    0xC7, 0x05, 0xEC, 0xD6, 0x4D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xE8, 0xF3, 0xD1, 0x00, 0x00, 0xE8,
};
static const uint8_t MSK_VERT_POOL[] = {
    1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
};

/* materials     005959F8, out of the operand at 00401F46 + 2 */
static const uint8_t SIG_MATERIALS[] = {
    0x3B, 0x0D, 0xF8, 0x59, 0x59, 0x00, 0x7D, 0x1D, 0x8B, 0x55,
    0xF8, 0x6B, 0xD2, 0x0C, 0x81, 0xC2,
};
static const uint8_t MSK_MATERIALS[] = {
    1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
};

/* face_recs     004DD6E0, out of the operand at 00401D33 + 3 */
static const uint8_t SIG_FACE_RECS[] = {
    0x51, 0xC7, 0x05, 0xE0, 0xD6, 0x4D, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xC7, 0x05, 0xEC, 0xD6, 0x4D,
};
static const uint8_t MSK_FACE_RECS[] = {
    1, 1, 1, 0, 0, 0, 0, 1, 1, 1,
    1, 1, 1, 1, 1, 1,
};

/* triv_reject   008A0064, out of the operand at 0041A2F9 + 4 */
static const uint8_t SIG_TRIV_REJECT[] = {
    0xEB, 0x14, 0xFF, 0x05, 0x64, 0x00, 0x8A, 0x00, 0xEB, 0x02,
    0xDD, 0xD8, 0x8B, 0x44, 0x24, 0x20, 0xC7, 0x00, 0x00, 0x00,
};
static const uint8_t MSK_TRIV_REJECT[] = {
    1, 1, 0, 0, 0, 0, 0, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
};

/* cells         0059DEBC, out of the operand at 004048CD + 7 */
static const uint8_t SIG_CELLS[] = {
    0x83, 0xC4, 0x04, 0x3B, 0xC3, 0x89, 0x1D, 0xBC, 0xDE, 0x59,
    0x00, 0xD9, 0x1D, 0xCC, 0xE9, 0x8B,
};
static const uint8_t MSK_CELLS[] = {
    1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
    0, 1, 1, 1, 1, 1,
};

/* region_mask   0059DF44, out of the operand at 00404FB1 + 6 */
static const uint8_t SIG_REGION_MASK[] = {
    0x85, 0xC0, 0x74, 0x0F, 0xC6, 0x05, 0x44, 0xDF, 0x59, 0x00,
    0x00, 0x89, 0x35, 0x60, 0xDF, 0x59,
};
static const uint8_t MSK_REGION_MASK[] = {
    1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1,
};

/* crossfade     0059DF60, out of the operand at 00404FB9 + 5 */
static const uint8_t SIG_CROSSFADE[] = {
    0x59, 0x00, 0x00, 0x89, 0x35, 0x60, 0xDF, 0x59, 0x00, 0xEB,
    0x25, 0xC7, 0x05, 0xAC, 0xDE, 0x59,
};
static const uint8_t MSK_CROSSFADE[] = {
    1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1,
};

/* dev_fog_start 00866FA8, out of the operand at 00487AC2 + 7 */
static const uint8_t SIG_DEV_FOG_START[] = {
    0x24, 0x04, 0x8B, 0x4C, 0x24, 0x08, 0xA3, 0xA8, 0x6F, 0x86,
    0x00, 0x89, 0x0D, 0xA4, 0x6F, 0x86,
};
static const uint8_t MSK_DEV_FOG_START[] = {
    1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
    0, 1, 1, 1, 1, 1,
};

/* dev_fog_end   00866FA4, out of the operand at 00487ACB + 4 */
static const uint8_t SIG_DEV_FOG_END[] = {
    0x86, 0x00, 0x89, 0x0D, 0xA4, 0x6F, 0x86, 0x00, 0xC3, 0x90,
    0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
};
static const uint8_t MSK_DEV_FOG_END[] = {
    1, 1, 1, 1, 0, 0, 0, 0, 1, 1,
    1, 1, 1, 1, 1, 1,
};

/* vertex_fog_on 004DD6D0, out of the operand at 00401DF1 + 5 */
static const uint8_t SIG_VERTEX_FOG_ON[] = {
    0x00, 0x00, 0x00, 0xC7, 0x05, 0xD0, 0xD6, 0x4D, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x83, 0x3D, 0x60,
};
static const uint8_t MSK_VERTEX_FOG_ON[] = {
    1, 1, 1, 1, 1, 0, 0, 0, 0, 1,
    1, 1, 1, 1, 1, 1,
};

static counter_site_t COUNTERS[] = {
    { "vertCache", SIG_VERT_CACHE, MSK_VERT_CACHE, sizeof SIG_VERT_CACHE,
      2u, 0x4000u },   /* the two gates abandon the rest of the frame before incrementing */
    { "sortHeap", SIG_SORT_HEAP, MSK_SORT_HEAP, sizeof SIG_SORT_HEAP,
      2u, 0x2004u },   /* accepted into the deferred array, then never queued */
    { "deferred", SIG_DEFERRED, MSK_DEFERRED, sizeof SIG_DEFERRED,
      4u, 0x2004u },   /* what render_guard watches */
    { "vertPool", SIG_VERT_POOL, MSK_VERT_POOL, sizeof SIG_VERT_POOL,
      2u, 0x2000u },   /* bapdraw_reserveVerts */
    { "materials", SIG_MATERIALS, MSK_MATERIALS, sizeof SIG_MATERIALS,
      2u, 0x0040u },   /* 64 distinct since the last flush */
    { "faceRecs", SIG_FACE_RECS, MSK_FACE_RECS, sizeof SIG_FACE_RECS,
      3u, 0x1000u },   /* one record per submitted face */
    { "trivReject", SIG_TRIV_REJECT, MSK_TRIV_REJECT, sizeof SIG_TRIV_REJECT,
      4u, 0u },   /* a count, not a ceiling */
    { "cells", SIG_CELLS, MSK_CELLS, sizeof SIG_CELLS,
      7u, 0x2000u },   /* the draw entry table */
    { "regionMask", SIG_REGION_MASK, MSK_REGION_MASK, sizeof SIG_REGION_MASK,
      6u, 0u },   /* which surfaces the gather is allowed to push */
    { "crossfade", SIG_CROSSFADE, MSK_CROSSFADE, sizeof SIG_CROSSFADE,
      5u, 0u },   /* 1 while a region crossfade is running */
    { "devFogStart", SIG_DEV_FOG_START, MSK_DEV_FOG_START, sizeof SIG_DEV_FOG_START,
      7u, 0u },   /* the device's own band, raw float bits */
    { "devFogEnd", SIG_DEV_FOG_END, MSK_DEV_FOG_END, sizeof SIG_DEV_FOG_END,
      4u, 0u },   /* the device's own band, raw float bits */
    { "vertexFogOn", SIG_VERTEX_FOG_ON, MSK_VERTEX_FOG_ON, sizeof SIG_VERTEX_FOG_ON,
      5u, 0u },   /* 1 while every drawn thing takes its fog from the engine's own table */
};

#define COUNTER_COUNT (sizeof COUNTERS / sizeof COUNTERS[0])

typedef struct trace_sample {
    float hfov;
    float reference_cut;
    float live_cut;
    float settled_cut;
    float target_start;
    float target_end;
    float current_start;
    float current_end;
    bool  cut_observed;
    bool  wrote;
    char  branch;          /* 'n' the ordinary path, anything else is an early return */
    uint32_t cells;
    uint32_t vertices;
    uint32_t engine[COUNTER_COUNT];
    float    seconds;
} trace_sample_t;

static struct {
    trace_sample_t samples[TRACE_CAPACITY];
    unsigned       write;          /* total samples ever taken; the ring index is this modulo */
    bool           enabled;
    bool           capturing;
    bool           flushed;
} trace;

void fog_trace_configure(bool enabled)
{
    trace.enabled = enabled;
}

void fog_trace_begin(void)
{
    if (!trace.enabled) {
        return;
    }
    /* A level change does NOT throw the window away, it marks it. The fault being chased shows on
     * the FIRST load of a level and not on the second, so the capture has to hold several passes
     * at once and say where each began. */
    trace.capturing = true;
    fog_trace_aside('L', 0.0f, 0.0f);
}

void fog_trace_sample(float horizontal_fov_degrees, float reference_cut, float live_cut,
                      float settled_cut, bool cut_observed,
                      const fog_regime_band_t *target, const fog_regime_band_t *current,
                      bool wrote)
{
    trace_sample_t *out;

    if (!trace.capturing || target == NULL || current == NULL) {
        return;
    }
    out = &trace.samples[trace.write % TRACE_CAPACITY];
    ++trace.write;
    out->hfov          = horizontal_fov_degrees;
    out->reference_cut = reference_cut;
    out->live_cut      = live_cut;
    out->settled_cut   = settled_cut;
    out->target_start  = target->start;
    out->target_end    = target->end;
    out->current_start = current->start;
    out->current_end   = current->end;
    out->cut_observed  = cut_observed;
    out->wrote         = wrote;
    out->branch        = 'n';
}

static bool counters_ready;

/* --- 0x00402155  the batch flush ------------------------------------------------------------- *
 *   55 8B EC                push ebp / mov ebp,esp
 *   81 EC 60080000          sub esp,0x860                 nine bytes, a clean boundary
 *   83 3D <materials> 00    cmp dword [materialCount],0
 *   7F 05                   jg ...
 *
 * Sampling had to move here. Five of the counters read ZERO on every frame of the first
 * capture, and that was the instrument's fault: the tick that recorded them runs on
 * render_frameEnd, by which time this flush and the deferred drain have already reset every
 * cursor. The values only exist between the draw and the reset, so they are read on ENTRY to the
 * thing that resets them, and the largest seen since the last frame is what the capture keeps.
 *
 * The material counter's address is wildcarded and read back out of the operand, so the pattern
 * carries no address of its own. It matches once. */
static const uint8_t SIG_BATCH_FLUSH[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x60, 0x08, 0x00, 0x00, 0x83,
    0x3D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x05
};
static const uint8_t MSK_BATCH_FLUSH[] = {
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 0, 0, 0, 0, 1, 1, 1
};
#define BATCH_FLUSH_PROLOGUE 9u

typedef void (__cdecl *batch_flush_fn_t)(void);
static detour_t flush_detour;
static uint32_t peak[COUNTER_COUNT];

static void sample_peaks(void)
{
    size_t i;

    for (i = 0; i < COUNTER_COUNT; ++i) {
        uint32_t now;

        if (COUNTERS[i].address == 0) {
            continue;                          /* never resolved; install refuses a partial set */
        }
        now = *(const volatile uint32_t *)COUNTERS[i].address;

        /* The last three are not cursors: two are a float band, where a maximum of the raw bits
         * means nothing, and the third is a flag. Everything before them is a cursor, where the
         * peak between resets is the whole point. */
        if (i >= COUNTER_COUNT - 3u || now > peak[i]) {
            peak[i] = now;
        }
    }
}

static void __cdecl hook_batch_flush(void)
{
    batch_flush_fn_t original = (batch_flush_fn_t)flush_detour.original;

    sample_peaks();                            /* before it resets the cursors */
    original();
}

void fog_trace_counters_install(void)
{
    uintptr_t resolved[COUNTER_COUNT];
    size_t    i;
    uintptr_t site;

    if (!trace.enabled) {
        return;                                /* nothing here runs in an ordinary session */
    }
    /* Resolve every counter before recording any. All or none: a partial set is worse than an
     * empty one, because a column reading zero looks like a measurement rather than a gap. Nothing
     * is written to `address` until the whole set has come back, so a refusal here leaves the
     * table exactly as it was. */
    for (i = 0; i < COUNTER_COUNT; ++i) {
        uintptr_t at = signature_find_unique(COUNTERS[i].pattern, COUNTERS[i].mask,
                                             COUNTERS[i].size);
        uint32_t  address = 0;

        if (at == 0) {
            log_warning("fog trace: the site holding %s did not resolve, so the engine counters "
                        "are NOT recorded", COUNTERS[i].name);
            return;
        }
        if (!memory_read_u32(at + COUNTERS[i].operand, &address) ||
            !memory_is_inside_image(address, sizeof(uint32_t))) {
            log_warning("fog trace: the operand at %08X reads %08X, which is not an address "
                        "inside the image, so the engine counters are NOT recorded",
                        (unsigned)(at + COUNTERS[i].operand), (unsigned)address);
            return;
        }
        resolved[i] = (uintptr_t)address;
    }
    for (i = 0; i < COUNTER_COUNT; ++i) {
        COUNTERS[i].address = resolved[i];
    }
    site = signature_find_unique(SIG_BATCH_FLUSH, MSK_BATCH_FLUSH, sizeof SIG_BATCH_FLUSH);
    if (site == 0 || !detour_install(&flush_detour, site, (const void *)hook_batch_flush,
                                     BATCH_FLUSH_PROLOGUE)) {
        log_warning("fog trace: the batch flush did not resolve, so the five per-batch counters "
                    "will read zero exactly as they did before");
    }
    counters_ready = true;
    log_info("fog trace: recording %u engine draw-path counters, peak between flushes. Every one "
             "was read out of an operand, so this carries no address of its own. Nothing here runs "
             "unless LogFogBand is set.", (unsigned)COUNTER_COUNT);
}

void fog_trace_counts(uint32_t cells, uint32_t vertices, float frame_seconds)
{
    trace_sample_t *last;
    size_t          i;

    if (!trace.capturing || trace.write == 0u) {
        return;
    }
    last = &trace.samples[(trace.write - 1u) % TRACE_CAPACITY];
    last->seconds  = frame_seconds;
    last->cells    = cells;
    last->vertices = vertices;
    if (!counters_ready) {
        return;
    }
    /* The peak since the last frame, then start again. A counter with no flush between frames
       still reads live, which is what the two the flush does not touch need. */
    sample_peaks();
    for (i = 0; i < COUNTER_COUNT; ++i) {
        last->engine[i] = peak[i];
        peak[i] = 0u;
    }
}

void fog_trace_aside(char branch, float record_start, float record_end)
{
    trace_sample_t *out;

    if (!trace.capturing) {
        return;
    }
    out = &trace.samples[trace.write % TRACE_CAPACITY];
    ++trace.write;
    out->branch        = branch;
    out->current_start = record_start;
    out->current_end   = record_end;
}

/* One line per frame, columns fixed so that a column that oscillates is visible by running an eye
 * down it rather than by reading the numbers. */
void fog_trace_flush(const char *why)
{
    unsigned index;
    unsigned first;
    unsigned held;

    if (!trace.capturing || trace.flushed) {
        return;
    }
    trace.capturing = false;
    trace.flushed   = true;

    if (trace.write == 0u) {
        return;
    }
    first = (trace.write > TRACE_CAPACITY) ? (trace.write - TRACE_CAPACITY) : 0u;
    held  = trace.write - first;

    log_info("fog trace: the last %u frames of %u since the level loaded, %s. Columns are the "
             "frame, the horizontal field of view this module was last told about, the reference "
             "cut, the live cut as reported, the live cut after easing, then the target band and "
             "the eased band, then W where the tick wrote and . where it found nothing to do.",
             held, trace.write, (why != NULL) ? why : "no reason given");

    for (index = 0; index < held; ++index) {
        const trace_sample_t *s = &trace.samples[(first + index) % TRACE_CAPACITY];

        if (s->branch != 'n') {
            log_info("fog trace %3u  ASIDE '%c'                                              "
                     "                          record %7.2f..%7.2f",
                     index, s->branch, (double)s->current_start, (double)s->current_end);
            continue;
        }
        log_info("fog trace %3u  fov %7.3f  ref %6.2f  live %6.2f  eased %6.2f %s  target "
                 "%7.2f..%7.2f  band %7.2f..%7.2f  %s",
                 index, (double)s->hfov, (double)s->reference_cut, (double)s->live_cut,
                 (double)s->settled_cut, s->cut_observed ? "seen" : "----",
                 (double)s->target_start, (double)s->target_end,
                 (double)s->current_start, (double)s->current_end,
                 s->wrote ? "W" : ".");
        log_info("fog trace %3u  cells %6u  verts %6u  frame %7.2f ms",
                 index, s->cells, s->vertices, (double)(s->seconds * 1000.0f));
        if (counters_ready) {
            log_info("fog trace %3u  vertCache %6u  sortHeap %6u  deferred %6u  vertPool %6u  "
                     "materials %4u  faceRecs %5u  trivRej %6u  cells %5u  regionMask %08X  "
                     "crossfade %u  devFog %08X..%08X  vertexFogOn %u",
                     index, s->engine[0], s->engine[1], s->engine[2], s->engine[3], s->engine[4],
                     s->engine[5], s->engine[6], s->engine[7], s->engine[8], s->engine[9],
                     s->engine[10], s->engine[11], s->engine[12]);
        }
    }
}
