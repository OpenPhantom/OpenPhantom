#include "cell_watchdog.h"

#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* --- 0x004064B8  bapdraw_gatherCell: the limit that is only checked ON ENTRY ------------------ *
 *   3D 00 20 00 00   cmp eax,0x2000      <- the operand is at +1, NOT at +2
 *   53 56 57         push ebx/esi/edi
 *   0F 83 <rel32>    jae drop
 *
 * The address is a trap, and it nearly sprang: a review proposed poking one byte at 0x4064B9 from
 * 0x20 to 0x1F. What sits there is the ZERO of the little-endian immediate; the poke would have
 * produced `cmp eax,0x201F` = 8223, the limit RAISED and the overflow forced. That is why the
 * whole dword is written here and the old value is checked first. */
static const uint8_t SIG_GATHER_LIMIT[] = {
    0x3D, 0x00, 0x20, 0x00, 0x00, 0x53, 0x56, 0x57, 0x0F, 0x83
};
#define OFFSET_GATHER_LIMIT_IMMEDIATE 0x01u

/* --- 0x004067CF  bapdraw_gatherCell: the append block with no limit -------------------------- *
 *   8D 04 52              lea  eax,[edx+edx*2]        ; n*3
 *   C1 E0 02              shl  eax,2                  ; n*12  (0x0C per entry)
 *   42                    inc  edx                    ; n++  . WITHOUT re-checking
 *   89 98 84 48 8A 00     mov  [eax+0x8A4884],ebx     <- table base + 4 at +0x09
 *   8D 80 80 48 8A 00     lea  eax,[eax+0x8A4880]
 *   89 04 8D 80 C8 8B 00  mov  [ecx*4+0x8BC880],eax   <- bucket base at +0x16
 *   89 15 BC DE 59 00     mov  [0x59DEBC],edx         <- THE COUNTER at +0x1C
 *
 * The pattern is the address-free leading edge (9 bytes) and is unique in the image; all three
 * addresses are READ out of the operands and cross-checked against each other. */
static const uint8_t SIG_GATHER_APPEND[] = {
    0x8D, 0x04, 0x52, 0xC1, 0xE0, 0x02, 0x42, 0x89, 0x98
};
#define OFFSET_GATHER_TABLE  0x09u   /* operand = base + 4 */
#define OFFSET_GATHER_BUCKET 0x16u
#define OFFSET_GATHER_COUNT  0x1Cu

/* --- 0x0041A0DF  bapvrt_transformWorld: the first vertex cache gate -------------------------- *
 *   81 FA 00 40 00 00   cmp  edx,0x4000        ; 16384 slots of 0x40 B = 1 MiB
 *   89 15 C0 B5 5B 00   mov  [g_numVertCache],edx   <- the counter, operand at +8
 *   0F 8D <rel32>       jge  out                                                                */
static const uint8_t SIG_VERTEX_CACHE_GATE[] = { 0x81, 0xFA, 0x00, 0x40, 0x00, 0x00, 0x89, 0x15 };
#define OFFSET_VERTEX_CACHE_COUNT 0x08u

enum {
    SITE_GATHER_LIMIT,
    SITE_GATHER_APPEND,
    SITE_VERTEX_CACHE_GATE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("gather_limit",      SIG_GATHER_LIMIT),
    SIGNATURE_ENTRY("gather_append",     SIG_GATHER_APPEND),
    SIGNATURE_ENTRY("vertex_cache_gate", SIG_VERTEX_CACHE_GATE)
};

#define CELL_ENTRY_STRIDE   0x0Cu
#define CELL_LIMIT_RETAIL 0x2000u
/* 7168, a reserve of 1024 entries against the unchecked appenders. A single mover carries up to
 * 855 faces and the largest cell of the game has 135 slots, so 990 is the worst case and 1024
 * covers it. */
#define CELL_LIMIT_LOWERED 0x1C00u

/* Lowered after the first playtest. The original alarm at 93.75 % assumed one frame of reaction
 * time; the log refuted it, the counter jumped from under 7680 to 8189 in ONE frame, the gentle
 * back-off never got its turn, only the emergency brake. The cell count is not a continuous
 * quantity; it jumps the moment an open area is entered. */
#define CELL_ALARM_FRACTION 0x1600u   /* 68.75 % of 0x2000 */
#define CELL_PANIC_FRACTION 0x1D00u   /* 90.6 %  of 0x2000 */
#define VIEW_SCALE_STEP        0.15f

/* Above any scale ViewRangeScale will accept (it is clamped to 2.5), so "no ceiling" needs no
 * separate flag and every reader can simply take the smaller of the two numbers. */
#define CEILING_NONE           1000.0f

#define VERTEX_CACHE_LIMIT 0x4000u
/* 75 %, earlier than for the cells, because the damage here survives until the level reloads. */
#define VERTEX_CACHE_ALARM 0x3000u

typedef struct cell_watchdog_state {
    const uint32_t *cell_count;
    const uint32_t *vertex_count;
    uint32_t        cell_limit;
    float           cell_budget;
    uint32_t        cell_high_water;
    uint32_t        vertex_high_water;
    uint32_t        vertex_limit;      /* raised by cell_watchdog_set_vertex_limit() */
    uint32_t        vertex_alarm;      /* scales with vertex_limit, same ratio as retail's 75 % */
    bool            overflow_reported;
    bool            vertex_warned;
    /* LAST, deliberately: the initialiser below is positional, so a field inserted anywhere above
     * this line silently shifts every value after it onto the wrong member. */
    float           ceiling;           /* lowest scale imposed; CEILING_NONE = never had to */
} cell_watchdog_state_t;

static cell_watchdog_state_t watchdog_state = { NULL, NULL, CELL_LIMIT_RETAIL, 1.0f, 0, 0,
                                                VERTEX_CACHE_LIMIT, VERTEX_CACHE_ALARM,
                                                false, false, CEILING_NONE };

/* ============================================================================================ */
static void lower_cell_limit(bool relocation_active)
{
    uintptr_t site = sites[SITE_GATHER_LIMIT].address;
    uintptr_t immediate;
    uint32_t  current;

    if (relocation_active) {
        log_info("lowering the cell limit is skipped, the relocation sets that word itself, and "
                 "two writers on one word would be a bet on who comes last");
        return;
    }
    if (site == 0) {
        log_warning("gather_limit did not resolve, the limit is unchanged");
        return;
    }

    immediate = site + OFFSET_GATHER_LIMIT_IMMEDIATE;
    if (!memory_read_u32(immediate, &current)) {
        return;
    }
    if (current != CELL_LIMIT_RETAIL) {
        log_warning("the cell limit reads %u instead of %u, refused (idempotent: a second run "
                    "finds the new value here)", (unsigned)current, (unsigned)CELL_LIMIT_RETAIL);
        return;
    }

    if (patch_write_u32(immediate, CELL_LIMIT_LOWERED) == PATCH_RESULT_OK) {
        log_info("cell limit %u -> %u (operand %08X). The engine checks it only on ENTRY to "
                 "gatherCell; the reserve of %u entries absorbs the unchecked appenders.",
                 (unsigned)CELL_LIMIT_RETAIL, (unsigned)CELL_LIMIT_LOWERED, (unsigned)immediate,
                 (unsigned)(CELL_LIMIT_RETAIL - CELL_LIMIT_LOWERED));
    } else {
        log_error("the cell limit at %08X is not writable", (unsigned)immediate);
    }
}

static bool resolve_cell_counter(void)
{
    uintptr_t site = sites[SITE_GATHER_APPEND].address;
    uint32_t  table_plus_four;
    uint32_t  bucket;
    uint32_t  counter;
    uint32_t  table;

    if (site == 0) {
        log_warning("gather_append did not resolve - NO cell watchdog. The view distance is "
                    "therefore held at 1.0, because an overflow would otherwise silently "
                    "overwrite the bucket list heads.");
        return false;
    }

    if (!memory_read_u32(site + OFFSET_GATHER_TABLE,  &table_plus_four) ||
        !memory_read_u32(site + OFFSET_GATHER_BUCKET, &bucket) ||
        !memory_read_u32(site + OFFSET_GATHER_COUNT,  &counter)) {
        return false;
    }
    table = table_plus_four - 4;

    /* The cross-check that carries the whole finding: do the bucket heads really sit immediately
     * behind the cell table? If not, we found something other than what we think. */
    if (table + CELL_LIMIT_RETAIL * CELL_ENTRY_STRIDE != bucket ||
        !memory_is_inside_image(counter, sizeof(uint32_t))) {
        log_warning("gather_append does not match expectation (table %08X + 0x2000*12 = %08X, "
                    "buckets %08X, counter %08X), watchdog OFF, view scale held at 1.0",
                    (unsigned)table,
                    (unsigned)(table + CELL_LIMIT_RETAIL * CELL_ENTRY_STRIDE),
                    (unsigned)bucket, (unsigned)counter);
        return false;
    }

    watchdog_state.cell_count = (const uint32_t *)(uintptr_t)counter;
    log_info("cell watchdog active, table %08X, buckets %08X (= table + 8192*12, confirmed), "
             "counter %08X. Alarm from %u of %u cells.",
             (unsigned)table, (unsigned)bucket, (unsigned)counter,
             (unsigned)CELL_ALARM_FRACTION, (unsigned)CELL_LIMIT_RETAIL);
    return true;
}

static void resolve_vertex_counter(void)
{
    uintptr_t site = sites[SITE_VERTEX_CACHE_GATE].address;
    uint32_t  counter;

    if (site == 0) {
        log_warning("vertex_cache_gate did not resolve, the vertex cache is NOT watched, and it "
                    "is the wall whose damage survives until the level reloads");
        return;
    }
    if (!memory_read_u32(site + OFFSET_VERTEX_CACHE_COUNT, &counter) ||
        !memory_is_inside_image(counter, sizeof(uint32_t))) {
        return;
    }
    watchdog_state.vertex_count = (const uint32_t *)(uintptr_t)counter;
    log_info("vertex cache counter %08X, %u slots", (unsigned)counter,
             (unsigned)VERTEX_CACHE_LIMIT);
}

bool cell_watchdog_install(bool lower_limit, bool relocation_active)
{
    signature_resolve_table(sites, SITE_COUNT);

    if (lower_limit) {
        lower_cell_limit(relocation_active);
    } else {
        log_warning("LowerCellLimit=0, the 8192 limit stays, and with it the silent overflow "
                    "into the bucket list heads");
    }

    resolve_vertex_counter();
    return resolve_cell_counter();
}

void cell_watchdog_set_limit(uint32_t new_limit)
{
    if (new_limit < CELL_LIMIT_RETAIL) {
        return;
    }
    watchdog_state.cell_limit  = new_limit;
    watchdog_state.cell_budget = (float)new_limit / (float)CELL_LIMIT_RETAIL;

    log_info("cell budget x%.2f (%u instead of %u). The radius cap opens up accordingly, with a "
             "doubled table a wide field of view carries the full authored range again.",
             (double)watchdog_state.cell_budget, (unsigned)new_limit,
             (unsigned)CELL_LIMIT_RETAIL);
}

float cell_watchdog_budget(void)
{
    return watchdog_state.cell_budget;
}

void cell_watchdog_set_vertex_limit(uint32_t new_limit)
{
    if (new_limit < VERTEX_CACHE_LIMIT) {
        return;
    }
    watchdog_state.vertex_limit = new_limit;
    /* Same 75 % ratio the retail-sized alarm already used, not a fixed offset from the new limit -
     * a fixed offset would mean the alarm fires later, proportionally, exactly where more headroom
     * makes a JUMP in the counter (see vertex_table.c, section 1) more dangerous, not less. */
    watchdog_state.vertex_alarm = (uint32_t)(((uint64_t)new_limit * VERTEX_CACHE_ALARM) /
                                             VERTEX_CACHE_LIMIT);

    log_info("vertex cache budget x%.2f (%u instead of %u), alarm from %u.",
             (double)new_limit / (double)VERTEX_CACHE_LIMIT, (unsigned)new_limit,
             (unsigned)VERTEX_CACHE_LIMIT, (unsigned)watchdog_state.vertex_alarm);
}

/* ============================================================================================ */
static void watch_vertex_cache(float *effective_view_scale)
{
    uint32_t used;
    uint32_t limit = watchdog_state.vertex_limit;
    uint32_t alarm = watchdog_state.vertex_alarm;

    if (watchdog_state.vertex_count == NULL) {
        return;
    }
    used = *watchdog_state.vertex_count;

    /* A counter far beyond four times the limit is not a counter any more; do not act on noise. */
    if (used > limit * 4u) {
        return;
    }
    if (used > watchdog_state.vertex_high_water) {
        watchdog_state.vertex_high_water = used;
    }

    if (used >= limit && !watchdog_state.vertex_warned) {
        watchdog_state.vertex_warned = true;
        log_error("VERTEX CACHE FULL (%u of %u). From here the geometry is torn, and it stays "
                  "torn until the level reloads, gate 2 jumps behind the `touched` reset loop. "
                  "The view scale goes to 1.00 immediately.",
                  (unsigned)used, (unsigned)limit);
        *effective_view_scale = 1.0f;
        /* Recorded, like every other brake here. Without it the frame governor is entitled to
         * hand this scale back thirty seconds later, and this particular brake was applied
         * because the geometry is ALREADY torn and stays torn until the level reloads. */
        watchdog_state.ceiling = 1.0f;
        return;
    }

    if (used >= alarm && *effective_view_scale > 1.0f) {
        float previous = *effective_view_scale;

        *effective_view_scale -= VIEW_SCALE_STEP;
        if (*effective_view_scale < 1.0f) {
            *effective_view_scale = 1.0f;
        }
        watchdog_state.ceiling = *effective_view_scale;
        log_warning("%u of %u vertex cache slots (peak %u), view scale %.2f -> %.2f. Braking "
                    "earlier than for the cells, because an overflow here tears the geometry "
                    "until the level reloads.",
                    (unsigned)used, (unsigned)limit,
                    (unsigned)watchdog_state.vertex_high_water,
                    (double)previous, (double)*effective_view_scale);
    }
}

void cell_watchdog_on_frame(float *effective_view_scale)
{
    uint32_t used;
    uint32_t alarm;
    uint32_t panic;

    if (watchdog_state.cell_count == NULL || effective_view_scale == NULL) {
        return;
    }
    used = *watchdog_state.cell_count;

    /* An earlier version bailed out silently above four times the limit ("obviously not a counter
     * any more"), and thereby hid exactly the case that matters. bapdraw_gatherCellMovers and
     * bapdraw_emitFace hang on with NO limit; the 8192 check is only at gatherCell's entry. The
     * counter CAN stand far above the limit, and that has to be in the log rather than swallowed. */
    if (used > watchdog_state.cell_limit) {
        if (!watchdog_state.overflow_reported) {
            watchdog_state.overflow_reported = true;
            log_error("CELL COUNTER %u IS ABOVE THE LIMIT %u. The table has already overflowed - "
                      "gatherCellMovers and emitFace hang on unchecked. From here the bucket list "
                      "heads are destroyed.", (unsigned)used, (unsigned)watchdog_state.cell_limit);
        }
        if (*effective_view_scale > 1.0f) {
            log_warning("view scale %.2f -> 1.00 (overflow)", (double)*effective_view_scale);
            *effective_view_scale = 1.0f;
            watchdog_state.ceiling = 1.0f;
        }
        if (used > watchdog_state.cell_high_water && used < 0x10000000u) {
            watchdog_state.cell_high_water = used;
        }
        return;
    }
    if (used > watchdog_state.cell_high_water) {
        watchdog_state.cell_high_water = used;
    }

    watch_vertex_cache(effective_view_scale);

    /* Thresholds scale with the live limit, so the message and the test always agree. An earlier
     * version printed CELL_LIMIT while the relocation had long since raised the real limit, and
     * the log read "14400 of 8192 cells", a number that looked like an overflow while it was
     * 88 % of the true limit. A message naming a different number than the test is worse than no
     * message. */
    alarm = (watchdog_state.cell_limit / CELL_LIMIT_RETAIL) * CELL_ALARM_FRACTION;
    panic = (watchdog_state.cell_limit / CELL_LIMIT_RETAIL) * CELL_PANIC_FRACTION;

    if (used >= panic && *effective_view_scale > 1.0f) {
        log_error("%u of %u cells - EMERGENCY BRAKE, view scale %.2f -> 1.00. An overflow would "
                  "have overwritten the bucket list heads. Peak %u.",
                  (unsigned)used, (unsigned)watchdog_state.cell_limit,
                  (double)*effective_view_scale, (unsigned)watchdog_state.cell_high_water);
        *effective_view_scale = 1.0f;
        watchdog_state.ceiling = 1.0f;
    } else if (used >= alarm && *effective_view_scale > 1.0f) {
        float previous = *effective_view_scale;

        *effective_view_scale -= VIEW_SCALE_STEP;
        if (*effective_view_scale < 1.0f) {
            *effective_view_scale = 1.0f;
        }
        watchdog_state.ceiling = *effective_view_scale;
        log_warning("%u of %u cells (peak %u), view scale %.2f -> %.2f. The engine checks its "
                    "limit only on entry to gatherCell and the counter jumps, so this brakes "
                    "early and in coarse steps.",
                    (unsigned)used, (unsigned)watchdog_state.cell_limit,
                    (unsigned)watchdog_state.cell_high_water,
                    (double)previous, (double)*effective_view_scale);
    }
}

float cell_watchdog_ceiling(void)
{
    return watchdog_state.ceiling;
}

void cell_watchdog_reset_ceiling(void)
{
    watchdog_state.ceiling = CEILING_NONE;
}
