/* custom_resolution.c: a player-typed resolution the engine's own mode list never enumerated.
 *
 * ==============================================================================================
 * WHY THIS IS A NEW RECORD, NOT A NEW CODE PATH
 *
 * The engine's own resolution machinery is entirely index based, from the options screen down to
 * the device that finally gets created, and every step operates on ONE shared table: the raw
 * DirectDraw mode list (count, stride 0x54, capacity 64 - the same table enhanced_resolution's own
 * mode_filter.c already documents, resolved here in mode_table_sites.c).
 *
 *   graphics_setResolution 0x46BE3D   looks up an EXACT (width, height, 16-bit RGB) match in the
 *                                     raw table (FUN_0046BBB5) and, failing that, the CLOSEST
 *                                     existing match by a scored search (FUN_0049065A). Either
 *                                     way it ends by calling graphics_setMode with that record's
 *                                     own INDEX. There is no path here that ever invents a mode:
 *                                     a resolution nobody enumerated is silently replaced by
 *                                     whatever is closest, not applied.
 *
 *   graphics_buildModeList 0x46C592  filters the SAME raw table into a per-device "usable" array,
 *                                     one slot per raw index, gated on kind==1 (RGB), bpp==0x10,
 *                                     a 640x480 floor, a VRAM test, and a 4:3-or-1280x1024 aspect
 *                                     gate (the lock enhanced_resolution's own WidescreenModes
 *                                     already lifts; this file carries its own copy of the same
 *                                     patch, see mode_table_sites.c).
 *
 *   graphics_enumModes 0x46C932      walks that SAME per-device array and, for every slot the
 *                                     filter marked usable, formats "%4d x %4d" from ITS OWN
 *                                     width/height and hands the pair (raw index, label) to the
 *                                     options screen's listbox. No other data reaches the label;
 *                                     it is a straight sprintf of two integers.
 *
 *   graphics_setMode(index) 0x46BC85 calls FUN_0048EC54(index, ...), which computes
 *                                     `&raw_table + index*0x54` - A POINTER STRAIGHT INTO THE RAW
 *                                     RECORD ITSELF - and hands THAT to the actual device
 *                                     creation call. Confirmed by decompiling it directly: nothing
 *                                     in this chain reads a cached handle, a GUID or anything else
 *                                     tied to how a record was originally populated. Every field
 *                                     the deeper device-creation code uses is read straight out of
 *                                     the record at the moment it is asked for, which is what
 *                                     makes a fabricated record behave identically to a real one.
 *
 * So a resolution DirectDraw never reported cannot be requested through this machinery, not
 * because the device could not create it, but because nothing before device creation ever builds
 * a record for it. The fix here is a data change, in exactly the spirit menu_patcher.c already
 * uses for widget arrays: add ONE more record to the table the engine already trusts, built by
 * cloning a genuine, already-validated record and overwriting only its width and height, rather
 * than inventing a code path the engine was never given.
 *
 * Cloning rather than hand-building: the record is 0x54 (84) bytes and only about half of it has
 * a known meaning (kind, bit depth, the three channel widths, width, height - see the offsets
 * below, all confirmed against mode_filter.c's own field reads). The remaining bytes are copied
 * verbatim from a real, working record instead of guessed at, so whatever they mean, they are
 * internally consistent values a real record actually shipped with.
 *
 * ==============================================================================================
 * WHY THE HOOK SITE IS graphics_buildModeList, NOT SOMETHING TIMED BY HAND
 *
 * The record has to exist before graphics_buildModeList consumes the table (for menu visibility)
 * and before graphics_setResolution's own startup lookup runs (for the ini-driven apply). Rather
 * than reason about ENGINE STARTUP ORDER separately from BOTH of those, this hooks
 * graphics_buildModeList's own entry and injects immediately before calling the original: that
 * function is called exactly once, during graphics startup, strictly after the real DirectDraw
 * enumeration has already populated the raw table (its whole job is to consume that), and
 * strictly before the options screen or the startup resolution lookup can run. Piggy-backing on
 * the engine's own call is what makes the timing correct by construction rather than a guess.
 * ============================================================================================ */
#include "custom_resolution.h"

#include "mode_table_sites.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INI_SECTION "custom_resolution"

/* --- The raw record's own layout, offsets confirmed against mode_filter.c's own field reads,
 * which the enumeration callback itself writes them from (its own comment cites the exact store
 * instructions at 0x492929 onward). This file adds nothing new here; it reuses evidence that was
 * already load bearing elsewhere in this tree. */
#define RECORD_WIDTH_OFFSET    0x08u
#define RECORD_HEIGHT_OFFSET   0x0Cu
#define RECORD_KIND_OFFSET     0x1Cu   /* must be 1: DDPF_RGB, confirmed via FUN_0046BBB5         */
#define RECORD_BITDEPTH_OFFSET 0x20u   /* must be 0x10: the engine is 16-bit only, no 32-bit path */
#define RECORD_RED_OFFSET      0x24u   /* must be >= 5, same floor graphics_findMode itself uses  */
#define RECORD_GREEN_OFFSET    0x28u
#define RECORD_BLUE_OFFSET     0x2Cu
#define RECORD_KIND_RGB        1u
#define RECORD_BITDEPTH_16     0x10u
#define RECORD_MIN_CHANNEL_BITS 5u
#define RECORD_STRIDE           0x54u
#define RECORD_TABLE_CAP        64u

/* The engine's own floor, read directly out of graphics_setResolution's own clamp
 * (`cmp eax,0x280` / `cmp eax,0x1e0`, unconditional, before any table lookup runs at all). A
 * requested size below this is clamped UP before the lookup ever sees it, so a raw record with a
 * smaller width or height could never be found by an exact match regardless of what this file
 * does - there is no way around it, only a floor to respect. */
#define ENGINE_MIN_WIDTH  0x280u   /* 640 */
#define ENGINE_MIN_HEIGHT 0x1E0u   /* 480 */

/* Not an engine limit, a typo guard: nothing here has reason to refuse a real 8K panel, but a
 * value that arrived from a fat-fingered ini entry should not reach a VRAM allocation attempt
 * unexamined. */
#define MAX_SANE_WIDTH  7680u
#define MAX_SANE_HEIGHT 4320u

typedef void (__cdecl *build_mode_list_fn_t)(void);
typedef int32_t (__cdecl *set_resolution_fn_t)(uint32_t width, uint32_t height);

/* graphics_setResolution's own contract for "read it from obi.ini instead", confirmed in
 * enhanced_resolution.c's own hook_set_resolution and reused unchanged: only the ONE startup call
 * that passes this exact sentinel pair is ever touched. Every other call graphics_setResolution
 * receives - entering or leaving a menu, playing a movie - is left completely alone, because
 * those are the engine managing its own presentation, not a player asking for a resolution. */
#define RESOLUTION_READ_INI 0xFFFFFFFFu

typedef struct custom_resolution_state {
    bool      installed;
    uint32_t  width;
    uint32_t  height;

    mode_table_sites_t   sites;
    bool                 injected;   /* the record has been added; do not add a second one */

    detour_t              build_mode_list_detour;
    build_mode_list_fn_t  build_mode_list_original;

    detour_t              set_resolution_detour;
    set_resolution_fn_t   set_resolution_original;
} custom_resolution_state_t;

static custom_resolution_state_t state;

/* ============================================================================================ */

static void load_config(void)
{
    int32_t width  = ini_read_int(INI_SECTION, "Width", 0);
    int32_t height = ini_read_int(INI_SECTION, "Height", 0);

    if (width <= 0 || height <= 0) {
        state.width  = 0;
        state.height = 0;
        return;
    }

    if ((uint32_t)width < ENGINE_MIN_WIDTH) {
        log_warning("Width=%d is below the engine's own %u floor, raised to it - nothing smaller "
                    "can ever be matched, the startup lookup clamps up before it looks",
                    width, ENGINE_MIN_WIDTH);
        width = (int32_t)ENGINE_MIN_WIDTH;
    }
    if ((uint32_t)height < ENGINE_MIN_HEIGHT) {
        log_warning("Height=%d is below the engine's own %u floor, raised to it, same reason as "
                    "Width", height, ENGINE_MIN_HEIGHT);
        height = (int32_t)ENGINE_MIN_HEIGHT;
    }
    if ((uint32_t)width > MAX_SANE_WIDTH) {
        log_warning("Width=%d is implausibly large, capped to %u", width, MAX_SANE_WIDTH);
        width = (int32_t)MAX_SANE_WIDTH;
    }
    if ((uint32_t)height > MAX_SANE_HEIGHT) {
        log_warning("Height=%d is implausibly large, capped to %u", height, MAX_SANE_HEIGHT);
        height = (int32_t)MAX_SANE_HEIGHT;
    }

    state.width  = (uint32_t)width;
    state.height = (uint32_t)height;
}

/* The first record already in the table that satisfies every check FUN_0046BBB5 itself applies -
 * not necessarily index 0: without enhanced_resolution's own mode_filter.c also installed, the
 * raw table can hold non-16-bit records too (see this file's own header comment), and cloning one
 * of those would build a record that could never be found by anything. */
static bool find_template_record(uint32_t count, uintptr_t *out_record)
{
    uint32_t index;

    for (index = 0; index < count; ++index) {
        uintptr_t record = state.sites.table_address + (uintptr_t)index * RECORD_STRIDE;
        uint32_t  kind = 0, depth = 0, red = 0, green = 0, blue = 0;

        if (!memory_read_u32(record + RECORD_KIND_OFFSET, &kind) ||
            !memory_read_u32(record + RECORD_BITDEPTH_OFFSET, &depth) ||
            !memory_read_u32(record + RECORD_RED_OFFSET, &red) ||
            !memory_read_u32(record + RECORD_GREEN_OFFSET, &green) ||
            !memory_read_u32(record + RECORD_BLUE_OFFSET, &blue)) {
            continue;               /* an unreadable slot is not ours to judge, try the next one */
        }
        if (kind == RECORD_KIND_RGB && depth == RECORD_BITDEPTH_16 &&
            red >= RECORD_MIN_CHANNEL_BITS && green >= RECORD_MIN_CHANNEL_BITS &&
            blue >= RECORD_MIN_CHANNEL_BITS) {
            *out_record = record;
            return true;
        }
    }
    return false;
}

/* Clones a genuine record byte for byte and overwrites only width and height - see this file's
 * own header comment for why the other ~0x48 bytes are copied rather than guessed at. Appends
 * rather than replacing: the real modes DirectDraw enumerated are untouched, this is one more
 * entry, not a substitution. */
static void inject_custom_record(void)
{
    uint32_t  count = 0;
    uintptr_t template_record = 0;
    uint8_t   record_bytes[RECORD_STRIDE];
    uintptr_t new_record;

    if (!state.sites.table_resolved || state.injected) {
        return;
    }
    if (!memory_read_u32(state.sites.count_address, &count)) {
        log_warning("could not read the mode count at %08X, refused",
                    (unsigned)state.sites.count_address);
        return;
    }
    if (count >= RECORD_TABLE_CAP) {
        log_warning("the mode table is already full at %u records, no room for a custom "
                    "resolution - consider FilterModeEnumeration in enhanced_resolution.ini if "
                    "that DLL is also installed", (unsigned)count);
        return;
    }
    if (!find_template_record(count, &template_record)) {
        log_warning("no valid 16-bit RGB record exists yet to clone, no custom resolution added");
        return;
    }
    if (!memory_read(template_record, record_bytes, sizeof record_bytes)) {
        log_warning("could not read the template record at %08X, refused",
                    (unsigned)template_record);
        return;
    }

    *(uint32_t *)(record_bytes + RECORD_WIDTH_OFFSET)  = state.width;
    *(uint32_t *)(record_bytes + RECORD_HEIGHT_OFFSET) = state.height;

    new_record = state.sites.table_address + (uintptr_t)count * RECORD_STRIDE;
    if (patch_write_bytes(new_record, record_bytes, sizeof record_bytes) != PATCH_RESULT_OK) {
        log_warning("could not write the synthetic record at %08X, refused", (unsigned)new_record);
        return;
    }
    if (patch_write_u32(state.sites.count_address, count + 1u) != PATCH_RESULT_OK) {
        log_warning("the record at %08X was written but the mode count could not be updated - it "
                    "exists but nothing will ever look at it, refused", (unsigned)new_record);
        return;
    }

    state.injected = true;
    log_info("custom resolution %ux%u added as mode table record %u (cloned from %08X)",
             (unsigned)state.width, (unsigned)state.height, (unsigned)count,
             (unsigned)template_record);
}

static void __cdecl hook_build_mode_list(void)
{
    inject_custom_record();
    state.build_mode_list_original();
}

/* Forces ONLY the one startup call that reads "as obi.ini says" - see RESOLUTION_READ_INI's own
 * comment. Every other call (menus, movies) passes real numbers and is handed straight through. */
static int32_t __cdecl hook_set_resolution(uint32_t width, uint32_t height)
{
    if (state.width > 0 && state.height > 0 &&
        width == RESOLUTION_READ_INI && height == RESOLUTION_READ_INI) {
        log_info("startup resolution forced to the custom %ux%u (was 'read obi.ini')",
                 (unsigned)state.width, (unsigned)state.height);
        width  = state.width;
        height = state.height;
    }
    return state.set_resolution_original(width, height);
}

/* ============================================================================================ */

static void install_build_mode_list_hook(void)
{
    if (state.sites.build_mode_list_site == 0) {
        return;               /* mode_table_sites_resolve() already logged why */
    }
    if (!detour_install(&state.build_mode_list_detour, state.sites.build_mode_list_site,
                        (const void *)&hook_build_mode_list, BUILD_MODE_LIST_PROLOGUE_SIZE)) {
        log_warning("graphics_buildModeList at %08X could not be detoured, refused",
                    (unsigned)state.sites.build_mode_list_site);
        return;
    }
    state.build_mode_list_original =
        (build_mode_list_fn_t)state.build_mode_list_detour.original;
    log_info("graphics_buildModeList hooked at %08X, the custom resolution will be added just "
             "before it runs", (unsigned)state.sites.build_mode_list_site);
}

static void install_set_resolution_hook(void)
{
    if (state.sites.set_resolution_site == 0) {
        return;               /* mode_table_sites_resolve() already logged why */
    }
    if (!detour_install(&state.set_resolution_detour, state.sites.set_resolution_site,
                        (const void *)&hook_set_resolution, SET_RESOLUTION_PROLOGUE_SIZE)) {
        log_warning("graphics_setResolution at %08X could not be detoured, the custom resolution "
                    "will not be applied automatically at startup",
                    (unsigned)state.sites.set_resolution_site);
        return;
    }
    state.set_resolution_original = (set_resolution_fn_t)state.set_resolution_detour.original;
    log_info("graphics_setResolution hooked at %08X, the startup resolution will be forced to "
             "the custom size", (unsigned)state.sites.set_resolution_site);
}

/* Best effort and independent of everything else here: a non-4:3 custom resolution needs this to
 * ever show up in the options screen's own list, but the startup apply above does not need it at
 * all - graphics_setResolution's own lookup reads the raw table directly and was never gated on
 * this test (see this file's own header comment). So a failure here is logged and left there,
 * never treated as a reason to give up on the rest of the feature. */
static void install_aspect_gate_lift(void)
{
    if (state.sites.aspect_gate_site == 0) {
        return;               /* mode_table_sites_resolve() already logged why */
    }
    if (patch_write_bytes(state.sites.aspect_gate_site, MODE_TABLE_ASPECT_GATE_PATCH,
                          sizeof MODE_TABLE_ASPECT_GATE_PATCH) == PATCH_RESULT_OK) {
        log_info("4:3 aspect gate lifted at %08X, a non-4:3 custom resolution can now appear in "
                 "the options screen's own list", (unsigned)state.sites.aspect_gate_site);
    } else {
        log_warning("the aspect gate at %08X could not be patched, a non-4:3 custom resolution "
                    "may not appear in the options screen's own list",
                    (unsigned)state.sites.aspect_gate_site);
    }
}

/* ============================================================================================ */

void custom_resolution_install(void)
{
    log_init("custom_resolution", false);

    if (state.installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, custom resolution is NOT patched");
        return;
    }

    load_config();
    if (state.width == 0 || state.height == 0) {
        log_info("Width/Height are 0, no custom resolution is configured");
        state.installed = true;
        return;
    }

    mode_table_sites_resolve(&state.sites);
    if (!state.sites.table_resolved) {
        state.installed = true;
        return;
    }

    install_build_mode_list_hook();
    install_set_resolution_hook();
    install_aspect_gate_lift();

    state.installed = true;
}
