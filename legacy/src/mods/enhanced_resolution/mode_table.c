/* mode_table.c: see mode_table.h. */
#include "mode_table.h"

#include "mode_filter.h"
#include "window_fit.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/patch.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The three data references the mode dump wants all live in graphics_buildModeList, at fixed
 * distances BEHIND the aspect gate (which is inside the same function). Each is verified by its
 * opcode bytes before the operand is believed, so a build that moved them degrades to "not
 * resolved" and the dump simply does not run.
 *
 *   gate - 0x0A7 : C7 45 FC <g_aRawMode>        (0x46C632, seeds the walk pointer)
 *   gate - 0x12C : 8B 0D    <g_numRawModes>     (0x46C5AD)
 *   gate - 0x0F4 : E8       <rel32 -> mem_free> (0x46C5E5)                                      */
#define BACKWARD_RAW_MODES     0x0A7
#define BACKWARD_NUM_RAW_MODES 0x12C
#define BACKWARD_MEM_FREE      0x0F4

/* RAW_MODE_STRIDE and the four field offsets live in window_fit.h: the same layout is read there
 * to find out how big a requested mode is, and one layout described in two places is one layout
 * that can disagree with itself. */


typedef struct mode_label {
    int32_t index;
    char   *label;
} mode_label_t;

typedef int32_t (__cdecl *enum_modes_fn_t)(mode_label_t *out);
typedef void    (__cdecl *engine_free_fn_t)(void *block);

#define MENU_LABEL_SLOTS      64   /* [0x4AF988] .. [0x4AFB88] */
#define SCRATCH_MODE_SLOTS   512
#define MAX_RAW_MODES_LOGGED 512

static struct {
    const uint8_t   *raw_modes;
    const uint32_t  *raw_mode_count;
    engine_free_fn_t engine_free;
    bool             logged;

    detour_t         detour;
    int              max_menu_modes;
    bool             log_table;

    /* The engine hands its enumerator an array and this is where it goes, so it has to
     * hold whatever the driver offers before the cap is applied. */
    mode_label_t     scratch[SCRATCH_MODE_SLOTS];
} mode_state;

/* ============================================================================================ */
void mode_table_resolve(uintptr_t aspect_gate_site)
{
    uintptr_t gate = aspect_gate_site;
    uint8_t   opcodes[3];
    uint32_t  address;

    if (gate == 0 || gate < BACKWARD_NUM_RAW_MODES) {
        return;
    }

    if (memory_read(gate - BACKWARD_RAW_MODES, opcodes, 3) &&
        opcodes[0] == 0xC7 && opcodes[1] == 0x45 && opcodes[2] == 0xFC &&
        memory_read_u32(gate - BACKWARD_RAW_MODES + 3, &address) &&
        memory_is_inside_image(address, RAW_MODE_STRIDE)) {
        mode_state.raw_modes = (const uint8_t *)(uintptr_t)address;
    }

    if (memory_read(gate - BACKWARD_NUM_RAW_MODES, opcodes, 2) &&
        opcodes[0] == 0x8B && opcodes[1] == 0x0D &&
        memory_read_u32(gate - BACKWARD_NUM_RAW_MODES + 2, &address) &&
        memory_is_inside_image(address, sizeof(uint32_t))) {
        mode_state.raw_mode_count = (const uint32_t *)(uintptr_t)address;
    }

    {
        uintptr_t call_site = gate - BACKWARD_MEM_FREE;
        uintptr_t target;
        if (patch_read_call_target(call_site, &target)) {
            mode_state.engine_free = (engine_free_fn_t)target;
        }
    }

    log_info("g_aRawMode=%08X g_numRawModes=%08X mem_free=%08X",
             (unsigned)(uintptr_t)mode_state.raw_modes,
             (unsigned)(uintptr_t)mode_state.raw_mode_count,
             (unsigned)(uintptr_t)mode_state.engine_free);
}

static void log_mode_table(void)
{
    uint32_t count;
    uint32_t index;

    if (mode_state.raw_modes == NULL || mode_state.raw_mode_count == NULL) {
        log_warning("the mode table did not resolve, cannot list what DirectDraw offers");
        return;
    }

    count = *mode_state.raw_mode_count;
    log_info("DirectDraw reports %u raw modes (only kind==1 && bpp==16 are usable):",
             (unsigned)count);

    for (index = 0; index < count && index < MAX_RAW_MODES_LOGGED; ++index) {
        const uint8_t *mode = mode_state.raw_modes + index * RAW_MODE_STRIDE;
        uint32_t width;
        uint32_t height;
        uint32_t kind;
        uint32_t bits;

        if (!memory_is_readable_range((uintptr_t)mode, RAW_MODE_STRIDE)) {
            break;
        }
        width  = *(const uint32_t *)(mode + RAW_MODE_WIDTH);
        height = *(const uint32_t *)(mode + RAW_MODE_HEIGHT);
        kind   = *(const uint32_t *)(mode + RAW_MODE_KIND);
        bits   = *(const uint32_t *)(mode + RAW_MODE_BPP);

        log_info("   [%3u] %5u x %5u  kind %u  bpp %2u  %s",
                 (unsigned)index, (unsigned)width, (unsigned)height, (unsigned)kind,
                 (unsigned)bits,
                 (kind == 1 && bits == 16)
                     ? ((width >= 640 && height >= 480) ? "USABLE" : "too small")
                     : "-");
    }
}

/* ============================================================================================ */
static int32_t __cdecl hook_enum_modes(mode_label_t *out)
{
    enum_modes_fn_t original = (enum_modes_fn_t)mode_state.detour.original;
    int32_t         produced;
    int32_t         kept;
    int32_t         index;

    /* The raw table is EMPTY at load time, stdDisplay_startup has not run yet. The first
     * enumeration is the earliest point at which it is populated. */
    if (!mode_state.logged && mode_state.log_table) {
        mode_state.logged = true;
        log_mode_table();
    }

    memset(mode_state.scratch, 0, sizeof(mode_state.scratch));
    produced = original(mode_state.scratch);
    if (produced < 0) {
        produced = 0;
    }
    if (produced > SCRATCH_MODE_SLOTS - 1) {
        produced = SCRATCH_MODE_SLOTS - 1;
    }

    kept = (produced > mode_state.max_menu_modes)
         ? mode_state.max_menu_modes
         : produced;

    for (index = 0; index < kept; ++index) {
        out[index] = mode_state.scratch[index];
    }
    out[kept].index = -1;
    out[kept].label = NULL;

    /* Hand the dropped labels back to the engine's allocator; the caller only ever frees the ones
     * it received. */
    if (produced > kept && mode_state.engine_free != NULL) {
        for (index = kept; index < produced; ++index) {
            if (mode_state.scratch[index].label != NULL) {
                mode_state.engine_free(mode_state.scratch[index].label);
            }
        }
    }

    if (produced != kept) {
        log_warning("enumModes produced %d modes, TRUNCATED to %d (the menu array holds %d)",
                    (int)produced, (int)kept, MENU_LABEL_SLOTS);
    }

    /* Unconditional, and it stays unconditional. This is the only line that says what the options
     * screen was actually handed, and the first field report, "the list only had 800x600",
     * arrived with no way to tell whether the enumerator or the filter was at fault, because this
     * used to sit behind a verbose flag. */
    /* What the filter kept out of the enumeration, next to what the enumeration produced. The
     * two numbers only mean something together: a short list with nothing filtered is a driver
     * offering little, a short list with a lot filtered is the table having been full. */
    mode_filter_log_summary();

    log_info("enumModes handed %d mode(s) to the options screen:", (int)kept);
    for (index = 0; index < kept; ++index) {
        log_info("   [%2d] index %-3d \"%s\"", (int)index, (int)out[index].index,
                 (out[index].label != NULL) ? out[index].label : "(no label)");
    }
    if (kept <= 1) {
        log_warning("that is one mode or none. The engine builds this list in "
                    "graphics_buildModeList during GRAPHICS startup, so anything that has to "
                    "influence it, the 4:3 gate above all, must be patched before then. If "
                    "the gate was lifted after the list was built, only the 4:3 entries survive.");
    }

    return kept;
}


void mode_table_install_cap(uintptr_t enum_modes_site, int max_menu_modes, bool log_table)
{
    mode_state.max_menu_modes = max_menu_modes;
    mode_state.log_table      = log_table;

    uintptr_t site = enum_modes_site;

    if (site == 0) {
        log_warning("graphics_enum_modes did not resolve, with the 4:3 lock lifted the options "
                    "screen can overflow its %d-slot array. Consider WidescreenModes=0.",
                    MENU_LABEL_SLOTS);
        return;
    }
    if (detour_install(&mode_state.detour, site,
                       (const void *)hook_enum_modes, ENUM_MODES_PROLOGUE_SIZE)) {
        log_info("hooked graphics_enumModes at %08X (cap %d entries)",
                 (unsigned)site, mode_state.max_menu_modes);
    } else {
        log_error("the graphics_enumModes detour FAILED. With the 4:3 lock lifted the options "
                  "screen can overflow its %d-slot array", MENU_LABEL_SLOTS);
    }
}

const uint8_t *mode_table_raw_modes(void)
{
    return mode_state.raw_modes;
}

const uint32_t *mode_table_raw_mode_count(void)
{
    return mode_state.raw_mode_count;
}
