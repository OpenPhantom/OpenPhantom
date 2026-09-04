/* enhanced_resolution.c: lift the 4:3 lock, and cap the array it would otherwise overflow.
 *
 * ==============================================================================================
 * BYTE BASIS
 *
 * graphics_buildModeList 0x46C592 filters the platform's raw DirectDraw table [0x862740]
 * (count [0x862014], stride 0x54) into this device's list. The acceptance rule, in code order:
 *     1. kind == 1 && bpp == 0x10           <- 16-bit only, and see mode_depth.h for why
 *     2. 640x480 is accepted unconditionally
 *     3. else: w*h*6 <= VRAM limit, w >= 640, h >= 480,
 *              AND (w == 1280 && h == 1024) OR (w/4 == h/3)      <- THE 4:3 LOCK
 *
 *   0x46C6D9  81 7D F4 00 05 00 00   cmp [ebp-0x0C], 0x500      w == 1280 ?
 *   0x46C6E0  75 09                  jne ...
 *   0x46C6E2  81 7D EC 00 04 00 00   cmp [ebp-0x14], 0x400      h == 1024 ?
 *   0x46C6E9  74 22                  je 0x46C70D                -> ACCEPT
 *   0x46C708  E9 35 FF FF FF         jmp 0x46C642               -> REJECT
 *
 * Writing EB 32 at 0x46C6D9 jumps straight to the accept label: the aspect test is gone, the
 * 640x480 floor and the VRAM test still stand.
 *
 * Two things that patch alone would get wrong:
 *
 *   a) graphics_setResolution 0x46BE3D never tested the aspect in the first place. A widescreen
 *      resolution in obi.ini already works today; the lock only ever hid it from the MENU.
 *
 *   b) the menu label array holds 64 ENTRIES. graphics_enumModes 0x46C932 writes
 *      (int32 index, char *label) pairs into [0x4AF988]; the next live datum is the menu
 *      descriptor at [0x4AFB88], i.e. 0x200 bytes = 64 slots, and game/menu.c passes 0x40 as the
 *      search bound. A modern driver can enumerate more 16-bit modes than that, and the overflow
 *      would land in the menu descriptor. We wrap the enumerator, run it into a private buffer
 *      and hand back at most MaxMenuModes entries, freeing the rest through the engine's own
 *      allocator.
 *
 * ==============================================================================================
 * SIZE NOTE (rule 9): a little over 600 lines, of which about 400 are code. The rest is the byte
 * evidence above and at each hooked site. Rule 8 requires that evidence at the site, and it is what
 * makes a patch that rewrites a conditional jump reviewable at all: without the acceptance rule
 * written out in code order, `EB 32` is an unaccountable two bytes.
 */
#include "enhanced_resolution.h"

#include "cursor_anchor.h"
#include "focus_guard.h"
#include "menu_island_clip.h"
#include "menu_loading_bar.h"
#include "menu_art_source.h"
#include "menu_scale.h"
#include "mode_depth.h"
#include "mode_filter.h"
#include "sw_blit_guard.h"
#include "ending_resolution.h"
#include "credits_skip.h"
#include "pointer_cage.h"
#include "window_fit.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"

#include <intrin.h>          /* _ReturnAddress, for LogResolutionCalls */
#include "common/memory.h"
#include "common/patch.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define RESOLUTION_SECTION "enhanced_resolution"

/* --- 0x0046C6D9  graphics_buildModeList: the 4:3 gate ---------------------------------------- */
static const uint8_t SIG_ASPECT_GATE[] = {
    0x81, 0x7D, 0xF4, 0x00, 0x05, 0x00, 0x00, 0x75, 0x09,
    0x81, 0x7D, 0xEC, 0x00, 0x04, 0x00, 0x00, 0x74, 0x22
};
/* `jmp +0x32` lands exactly where the engine's own accept branch does. */
static const uint8_t ASPECT_GATE_PATCH[2] = { 0xEB, 0x32 };

/* --- 0x0046C932  graphics_enumModes (function start) ----------------------------------------- */
static const uint8_t SIG_ENUM_MODES[] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x08, 0x08, 0x00, 0x00, 0x56, 0x57,
    0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x45
};
#define ENUM_MODES_PROLOGUE_SIZE 9u

/* --- 0x0046BE3D  graphics_setResolution (function start) ------------------------------------- */
static const uint8_t SIG_SET_RESOLUTION[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x5C, 0x83, 0x3D, 0x60, 0x63, 0x6D, 0x00, 0x00,
    0x75, 0x07, 0x33, 0xC0, 0xE9, 0x49, 0x01
};
#define SET_RESOLUTION_PROLOGUE_SIZE 6u

/* --- 0x0045F7AC  swmenu_enterMenuMode: the bolt that forces every menu to 640x480 ------------- *
 *   3B 05 44 68 4B 00     cmp  eax,[g_maxMenuWidth]   <- operand at +0x02
 *   76 37                 jbe  ... (all good)
 *   C7 05 40 68 4B 00 ..  mov  [g_menuModeFlag], ...
 *   68 E0 01 00 00        push 0x1E0                  ; 480
 *   68 80 02 00 00        push 0x280                  ; 640
 *   E8 <rel32>            call graphics_setResolution
 *
 * [0x4B6844] is `max_menu_Width` from obi.ini and reads 640 in all four installations. Playing at
 * 1920 therefore means operating EVERY menu at 480p, and paying for a full D3D9 device rebuild
 * on every open and close. Six of those in 22 seconds appeared in the user's log, and the
 * graphics wrapper hung inside exactly that rebuild.
 *
 * WARNING: this pattern contains an absolute .data address. Under forced ASLR it stops resolving
 * and the patch disables itself with a log line, safe-fail. Address-free variants were measured
 * and rejected (6 and 10 hits respectively). */
static const uint8_t SIG_MENU_GATE_ENTER[] = {
    0x3B, 0x05, 0x44, 0x68, 0x4B, 0x00, 0x76, 0x37, 0xC7, 0x05, 0x40, 0x68, 0x4B, 0x00
};
#define OFFSET_MENU_GATE_ENTER_OPERAND 0x02u

/* --- 0x0045F686  swmenu_modeIsUsable: the same comparison, second site ------------------------ *
 * This one rejects "adopt resolution" for every widescreen mode. Both sites must read the SAME
 * cell or we have not found what we think we found, the cross-check is deliberate. */
static const uint8_t SIG_MENU_GATE_USABLE[] = {
    0x8B, 0x45, 0xFC, 0x3B, 0x05, 0x44, 0x68, 0x4B, 0x00, 0x77, 0x10,
    0x83, 0x3D, 0x40, 0x68, 0x4B, 0x00, 0x00, 0x74, 0x07
};
#define OFFSET_MENU_GATE_USABLE_OPERAND 0x05u

enum {
    SITE_ASPECT_GATE,
    SITE_ENUM_MODES,
    SITE_SET_RESOLUTION,
    SITE_MENU_GATE_ENTER,
    SITE_MENU_GATE_USABLE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY("graphics_aspect_gate",   SIG_ASPECT_GATE),
    SIGNATURE_ENTRY("graphics_enum_modes",    SIG_ENUM_MODES),
    SIGNATURE_ENTRY("graphics_set_resolution",SIG_SET_RESOLUTION),
    SIGNATURE_ENTRY("menu_gate_enter",        SIG_MENU_GATE_ENTER),
    SIGNATURE_ENTRY("menu_gate_usable",       SIG_MENU_GATE_USABLE)
};

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

#define MENU_LABEL_SLOTS      64   /* [0x4AF988] .. [0x4AFB88] */
#define MAX_MENU_MODES_LIMIT  63
#define MIN_MENU_MODES_LIMIT   4
#define SCRATCH_MODE_SLOTS   512
#define MAX_RAW_MODES_LOGGED 512

typedef struct mode_label {
    int32_t index;
    char   *label;
} mode_label_t;

typedef int32_t (__cdecl *enum_modes_fn_t)(mode_label_t *out);
typedef int32_t (__cdecl *set_resolution_fn_t)(uint32_t width, uint32_t height);
typedef void    (__cdecl *engine_free_fn_t)(void *block);

typedef struct resolution_config {
    bool enabled;
    bool widescreen_modes;
    int  max_menu_modes;
    int  force_width;
    bool log_resolution_calls;
    bool ending_keeps_resolution;
    bool skip_credits;
    int  force_height;
    bool log_mode_table;
    bool menu_keeps_resolution;
    bool fit_window_to_mode;
    bool keep_cursor_in_window;
    bool clip_pointer_to_window;
    bool reacquire_input_on_focus;
    bool widen_menu_cursor_area;
    bool clamp_menu_sprites_to_island;
    bool filter_mode_enumeration;
    int  mode_bit_depth;
    float menu_scale;
    char  menu_art_directory[192];
} resolution_config_t;

typedef struct resolution_state {
    bool                installed;
    resolution_config_t config;

    detour_t            enum_modes_detour;
    detour_t            set_resolution_detour;

    const uint8_t      *raw_modes;
    const uint32_t     *raw_mode_count;
    engine_free_fn_t    engine_free;
    bool                logged_mode_table;

    mode_label_t        scratch[SCRATCH_MODE_SLOTS];
} resolution_state_t;

static resolution_state_t resolution_state;

/* The cell both menu bolts are repointed at. 0x7FFFFFFF makes the comparison never true. */
static uint32_t menu_width_cell = 0x7FFFFFFFu;

/* ============================================================================================ */
static void load_config(void)
{
    resolution_config_t *config = &resolution_state.config;

    config->enabled               = ini_read_bool(RESOLUTION_SECTION, "Enabled", true);
    config->widescreen_modes      = ini_read_bool(RESOLUTION_SECTION, "WidescreenModes", true);
    config->max_menu_modes        = ini_read_int (RESOLUTION_SECTION, "MaxMenuModes", 63);
    config->force_width           = ini_read_int (RESOLUTION_SECTION, "ForceWidth", 0);
    config->log_resolution_calls  = ini_read_bool(RESOLUTION_SECTION,
                                                  "LogResolutionCalls", false);
    config->ending_keeps_resolution = ini_read_bool(RESOLUTION_SECTION,
                                                    "EndingKeepsResolution", true);
    config->skip_credits          = ini_read_bool(RESOLUTION_SECTION,
                                                  "SkipCredits", true);
    config->force_height          = ini_read_int (RESOLUTION_SECTION, "ForceHeight", 0);
    /* Off, like every other diagnostic in this tree. A release ships nothing switched on that
     * only writes to the log, so the default has to be off in the code as well as in the shipped
     * ini: that ini invites deleting any line you do not want to change, and a diagnostic that
     * comes back when its line is deleted is on by accident. */
    config->log_mode_table        = ini_read_bool(RESOLUTION_SECTION, "LogModeTable", false);
    config->menu_keeps_resolution = ini_read_bool(RESOLUTION_SECTION, "MenuKeepsResolution", true);

    /* DEFAULT OFF, and it is the only setting in this file that defaults to leaving the engine
     * alone rather than repairing it. Moving the game's window is the one thing here that argues
     * with whatever renders afterwards over the same object, and everything else in this tree works
     * regardless of which wrapper that is. It is a last resort for a setup with no wrapper at all,
     * where the window really can end up smaller than the display mode. The price of turning it on
     * is that the engine's window stops sitting at screen 0,0, which is the assumption its own
     * pointer handling rests on, and repairing that is what the two features below are for. */
    config->fit_window_to_mode    = ini_read_bool(RESOLUTION_SECTION, "FitWindowToMode", false);

    /* Default ON, and the reason it is safe to default a behaviour change on: on a window that
     * sits at screen 0,0, which is where the engine puts it and where it stays without the line
     * above, the correction is the identity, bit for bit. It can only change what happens on a
     * window this DLL has moved somewhere else, and there the current behaviour is already wrong. */
    config->keep_cursor_in_window = ini_read_bool(RESOLUTION_SECTION, "KeepCursorInWindow", true);

    /* Default ON, and the reason a new piece of process-global OS state may default on: the engine
     * hides the pointer over its own window unconditionally, the window procedure answers
     * WM_SETCURSOR with SetCursor(NULL) at 0x004990D3 and returns 1, and the menus draw their own
     * cursor from the accumulated mouse deltas. There is therefore no screen on which the real
     * pointer is meant to be reachable, so a "free" pointer here is only ever an invisible one
     * sitting on another monitor waiting to be clicked. It is released the moment the game window
     * is not the foreground window. */
    config->clip_pointer_to_window =
        ini_read_bool(RESOLUTION_SECTION, "ClipPointerToWindow", true);

    /* Default ON because the alternative is a game that stops responding to the keyboard after the
     * first Alt-Tab: the devices are opened FOREGROUND, Windows unacquires them when the window
     * goes to the background, and no code in the retail build ever acquires them again. */
    config->reacquire_input_on_focus =
        ini_read_bool(RESOLUTION_SECTION, "ReacquireInputOnFocus", true);

    /* Default ON. The engine clamps the cursor it DRAWS for its own menus to a 607x447 box
     * anchored at the menu origin, which is 640x480 minus the cursor quad, so at 1080p the
     * pointer cannot be moved out of a small island in the middle of the screen. At 640x480 the
     * widened clamp is arithmetically identical to the shipped one, so this can only change what
     * happens on a mode the engine's own constant was never written for. It does not move or
     * rescale any menu: the engine already centres those itself.
     *
     * KNOWN COST, not yet reproduced here, and the reason this default is under review: the pause
     * screens repair themselves through the menu toolkit's damage rectangles, which live in canvas
     * coordinates clipped to the same hard-coded 640x480 every blit in that toolkit clips to. A
     * cursor quad drawn partly outside the island cannot be expressed as a damage rectangle, so it
     * is never erased, and a 3840x2160 field report describes every crossing of the island's edge
     * leaving a permanent stamp of the cursor's blue glow on the border until the screen closes.
     * The front end never shows it because its 3-D room repaints every pixel every frame. Every
     * clickable widget lives inside the island either way, so if that report reproduces here this
     * default becomes OFF; see pointer_cage.c's header for the full mechanism. */
    config->widen_menu_cursor_area =
        ini_read_bool(RESOLUTION_SECTION, "WidenMenuCursorArea", true);

    /* Default ON. This is the other half of MenuKeepsResolution: with the menus running as a
     * 640x480 island instead of bolting the mode down, a menu sprite can reach where the menu's
     * own erase cannot (the toolkit repairs itself in canvas coordinates clipped to 640x480), and
     * the hovered button's halo does exactly that, stamping the island's border blue for the life
     * of the screen. The clamp is bit-identical for every sprite that fits the island, which is
     * every authored widget, so there is no configuration in which it costs anything; the switch
     * exists so the repair can be singled out while diagnosing, not because leaving it off is ever
     * the better picture. Unlike the cursor cage above, this one is about the sprites the WIDGETS
     * draw, and the two defects are independent: the reported smears survive WidenMenuCursorArea=0
     * and track the hovered button rather than the pointer. */
    config->clamp_menu_sprites_to_island =
        ini_read_bool(RESOLUTION_SECTION, "ClampMenuSpritesToIsland", true);

    /* Default ON. The engine records every bit depth the driver reports into a table of 64 and
     * cancels the enumeration when it is full, then keeps only the 16-bit entries. Two thirds of
     * those records are therefore spent on modes the options screen can never show, and which
     * resolutions survive depends on the order the driver enumerated in. Filtering costs nothing
     * on a driver that only reports 16 bit, because then there is nothing to filter. */
    config->mode_bit_depth        = ini_read_int (RESOLUTION_SECTION, "ModeBitDepth", 16);
    config->filter_mode_enumeration =
        ini_read_bool(RESOLUTION_SECTION, "FilterModeEnumeration", true);

    /* Default 1, which is off and is exactly the behaviour that shipped. Whole multiples only:
     * the menu bitmaps are blitted one source pixel to one destination pixel, so a fractional
     * canvas would leave the artwork sitting at a size the layout does not agree with. See
     * menu_scale.h for why this is only half a feature without upscaled artwork, and for why it
     * declines to install when the menu cursor cage is not widened. */
    config->menu_scale =
        ini_read_float(RESOLUTION_SECTION, "MenuScale", 0.0f);

    /* Where tools\convert_menu.ps1 wrote the upscaled artwork. One folder rather than seventy
     * loose files beside WMAIN.EXE, mounted through the engine's own resource chain, and named
     * here so it can be moved or emptied without touching the DLL. Empty declines the mount. */
    ini_read_string(RESOLUTION_SECTION, "MenuArtDirectory", MENU_ART_DEFAULT_DIRECTORY,
                    config->menu_art_directory, sizeof config->menu_art_directory);

    if (config->max_menu_modes > MAX_MENU_MODES_LIMIT) {
        config->max_menu_modes = MAX_MENU_MODES_LIMIT;
    }
    if (config->max_menu_modes < MIN_MENU_MODES_LIMIT) {
        config->max_menu_modes = MIN_MENU_MODES_LIMIT;
    }
    if (config->force_width < 0)  { config->force_width = 0; }
    if (config->force_height < 0) { config->force_height = 0; }
}

/* ============================================================================================ */
static void resolve_mode_table(void)
{
    uintptr_t gate = sites[SITE_ASPECT_GATE].address;
    uint8_t   opcodes[3];
    uint32_t  address;

    if (gate == 0 || gate < BACKWARD_NUM_RAW_MODES) {
        return;
    }

    if (memory_read(gate - BACKWARD_RAW_MODES, opcodes, 3) &&
        opcodes[0] == 0xC7 && opcodes[1] == 0x45 && opcodes[2] == 0xFC &&
        memory_read_u32(gate - BACKWARD_RAW_MODES + 3, &address) &&
        memory_is_inside_image(address, RAW_MODE_STRIDE)) {
        resolution_state.raw_modes = (const uint8_t *)(uintptr_t)address;
    }

    if (memory_read(gate - BACKWARD_NUM_RAW_MODES, opcodes, 2) &&
        opcodes[0] == 0x8B && opcodes[1] == 0x0D &&
        memory_read_u32(gate - BACKWARD_NUM_RAW_MODES + 2, &address) &&
        memory_is_inside_image(address, sizeof(uint32_t))) {
        resolution_state.raw_mode_count = (const uint32_t *)(uintptr_t)address;
    }

    {
        uintptr_t call_site = gate - BACKWARD_MEM_FREE;
        uintptr_t target;
        if (patch_read_call_target(call_site, &target)) {
            resolution_state.engine_free = (engine_free_fn_t)target;
        }
    }

    log_info("g_aRawMode=%08X g_numRawModes=%08X mem_free=%08X",
             (unsigned)(uintptr_t)resolution_state.raw_modes,
             (unsigned)(uintptr_t)resolution_state.raw_mode_count,
             (unsigned)(uintptr_t)resolution_state.engine_free);
}

static void log_mode_table(void)
{
    uint32_t count;
    uint32_t index;

    if (resolution_state.raw_modes == NULL || resolution_state.raw_mode_count == NULL) {
        log_warning("the mode table did not resolve, cannot list what DirectDraw offers");
        return;
    }

    count = *resolution_state.raw_mode_count;
    log_info("DirectDraw reports %u raw modes (only kind==1 && bpp==16 are usable):",
             (unsigned)count);

    for (index = 0; index < count && index < MAX_RAW_MODES_LOGGED; ++index) {
        const uint8_t *mode = resolution_state.raw_modes + index * RAW_MODE_STRIDE;
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
    enum_modes_fn_t original = (enum_modes_fn_t)resolution_state.enum_modes_detour.original;
    int32_t         produced;
    int32_t         kept;
    int32_t         index;

    /* The raw table is EMPTY at load time, stdDisplay_startup has not run yet. The first
     * enumeration is the earliest point at which it is populated. */
    if (!resolution_state.logged_mode_table && resolution_state.config.log_mode_table) {
        resolution_state.logged_mode_table = true;
        log_mode_table();
    }

    memset(resolution_state.scratch, 0, sizeof(resolution_state.scratch));
    produced = original(resolution_state.scratch);
    if (produced < 0) {
        produced = 0;
    }
    if (produced > SCRATCH_MODE_SLOTS - 1) {
        produced = SCRATCH_MODE_SLOTS - 1;
    }

    kept = (produced > resolution_state.config.max_menu_modes)
         ? resolution_state.config.max_menu_modes
         : produced;

    for (index = 0; index < kept; ++index) {
        out[index] = resolution_state.scratch[index];
    }
    out[kept].index = -1;
    out[kept].label = NULL;

    /* Hand the dropped labels back to the engine's allocator; the caller only ever frees the ones
     * it received. */
    if (produced > kept && resolution_state.engine_free != NULL) {
        for (index = kept; index < produced; ++index) {
            if (resolution_state.scratch[index].label != NULL) {
                resolution_state.engine_free(resolution_state.scratch[index].label);
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

/* ============================================================================================ */
#define RESOLUTION_READ_INI 0xFFFFFFFFu

/* Enough to cover a whole session's mode changes and nowhere near enough to be a flood. */
#define RESOLUTION_CALL_LOG_MAX 32u

static int32_t __cdecl hook_set_resolution(uint32_t width, uint32_t height)
{
    set_resolution_fn_t original =
        (set_resolution_fn_t)resolution_state.set_resolution_detour.original;

    /* This detour exists for ForceWidth/ForceHeight and nothing else. The window fit used to be
     * driven from here and that was the defect: this function is not the choke point, and the mode
     * changes a player triggers do not pass through it. */
    /* MEASUREMENT, off unless asked for. graphics_setResolution has SIX callers and
     * MenuKeepsResolution neutralises exactly one of them, the gate inside swmenu_enterMenuMode.
     * A screen that drops to 640x480 while every other menu holds its size is therefore reaching
     * this function by one of the other five, and the only way to know WHICH is to ask the call
     * itself. The return address is the caller: a detour is entered through a jump patched over
     * the prologue, so the stack still carries the address the engine will return to.
     *
     * Capped, because a mode change is rare but a runaway log is not worth risking. */
    if (resolution_state.config.log_resolution_calls) {
        static unsigned reported = 0;

        if (reported < RESOLUTION_CALL_LOG_MAX) {
            ++reported;
            log_info("graphics_setResolution(%u, %u) called from %08X%s",
                     (unsigned)width, (unsigned)height,
                     (unsigned)(uintptr_t)_ReturnAddress(),
                     (reported == RESOLUTION_CALL_LOG_MAX) ? " (last one reported)" : "");
        }
    }

    if (resolution_state.config.force_width > 0 && resolution_state.config.force_height > 0 &&
        width == RESOLUTION_READ_INI && height == RESOLUTION_READ_INI) {
        log_info("startup resolution forced to %dx%d (was 'read obi.ini')",
                 resolution_state.config.force_width, resolution_state.config.force_height);
        width  = (uint32_t)resolution_state.config.force_width;
        height = (uint32_t)resolution_state.config.force_height;
    }

    return original(width, height);
}

/* ============================================================================================
 * The menu keeps the resolution
 *
 * Rather than patching the comparison away, we repoint its OPERAND at our own cell holding
 * 0x7FFFFFFF. The comparison is then never true, the engine keeps its code, and the change is
 * idempotent: a second run no longer finds the expected address there and refuses.
 *
 * Both sites must read THE SAME cell, otherwise we have not found what we believed we found.
 * ============================================================================================ */
static void install_menu_resolution_gate(void)
{
    uintptr_t enter_site  = sites[SITE_MENU_GATE_ENTER].address;
    uintptr_t usable_site = sites[SITE_MENU_GATE_USABLE].address;
    uintptr_t enter_operand;
    uintptr_t usable_operand;
    uint32_t  enter_cell;
    uint32_t  usable_cell;
    uint32_t  our_cell = (uint32_t)(uintptr_t)&menu_width_cell;

    if (!resolution_state.config.menu_keeps_resolution) {
        return;
    }
    if (enter_site == 0 || usable_site == 0) {
        log_warning("menu gate %s%s did not resolve, the menus stay at 640x480 and every switch "
                    "costs a device rebuild",
                    (enter_site == 0) ? "enter " : "", (usable_site == 0) ? "usable" : "");
        return;
    }

    enter_operand  = enter_site  + OFFSET_MENU_GATE_ENTER_OPERAND;
    usable_operand = usable_site + OFFSET_MENU_GATE_USABLE_OPERAND;
    if (!memory_read_u32(enter_operand, &enter_cell) ||
        !memory_read_u32(usable_operand, &usable_cell)) {
        return;
    }
    if (enter_cell != usable_cell) {
        log_error("the two menu bolts read different cells (%08X vs %08X), refused",
                  (unsigned)enter_cell, (unsigned)usable_cell);
        return;
    }
    if (!memory_is_inside_image(enter_cell, sizeof(uint32_t))) {
        log_error("max_menu_Width would be at %08X, outside the image, refused",
                  (unsigned)enter_cell);
        return;
    }

    if (patch_repoint_operand(enter_operand, enter_cell, our_cell) != PATCH_RESULT_OK ||
        patch_repoint_operand(usable_operand, enter_cell, our_cell) != PATCH_RESULT_OK) {
        log_error("repointing the menu bolts failed, unchanged");
        return;
    }

    log_info("the menus keep the resolution (max_menu_Width [%08X] -> our cell %08X = 0x7FFFFFFF, "
             "sites %08X/%08X). No more 640x480 switch on opening and closing, and therefore no "
             "D3D9 device rebuild.",
             (unsigned)enter_cell, (unsigned)our_cell, (unsigned)enter_site, (unsigned)usable_site);
    log_warning("visible consequence: the front end, the pause screens and the loading screen are "
                "now a 640x480 island in the picture (14.8 %% of the area at 1080p), and the "
                "mouse pointer stays inside that box. That is the price of the resolution "
                "standing still.");
}

/* ============================================================================================ */
static void install_aspect_gate(void)
{
    uintptr_t gate = sites[SITE_ASPECT_GATE].address;

    if (!resolution_state.config.widescreen_modes) {
        log_info("WidescreenModes=0, the mode list stays 4:3");
        return;
    }
    if (gate == 0) {
        log_warning("graphics_aspect_gate did not resolve, the mode list STAYS 4:3");
        return;
    }
    if (patch_write_bytes(gate, ASPECT_GATE_PATCH, sizeof(ASPECT_GATE_PATCH)) == PATCH_RESULT_OK) {
        log_info("4:3 lock lifted at %08X (EB 32 -> accept). The 640x480 floor and the VRAM test "
                 "are untouched.", (unsigned)gate);
    } else {
        log_error("the aspect-gate patch at %08X could not be written", (unsigned)gate);
    }
}

static void install_enum_modes_cap(void)
{
    uintptr_t site = sites[SITE_ENUM_MODES].address;

    if (site == 0) {
        log_warning("graphics_enum_modes did not resolve, with the 4:3 lock lifted the options "
                    "screen can overflow its %d-slot array. Consider WidescreenModes=0.",
                    MENU_LABEL_SLOTS);
        return;
    }
    if (detour_install(&resolution_state.enum_modes_detour, site,
                       (const void *)hook_enum_modes, ENUM_MODES_PROLOGUE_SIZE)) {
        log_info("hooked graphics_enumModes at %08X (cap %d entries)",
                 (unsigned)site, resolution_state.config.max_menu_modes);
    } else {
        log_error("the graphics_enumModes detour FAILED - with the 4:3 lock lifted the options "
                  "screen can overflow its %d-slot array", MENU_LABEL_SLOTS);
    }
}

static void install_forced_startup_resolution(void)
{
    uintptr_t site = sites[SITE_SET_RESOLUTION].address;

    const bool forcing = (resolution_state.config.force_width > 0 &&
                          resolution_state.config.force_height > 0);

    if (!forcing && !resolution_state.config.log_resolution_calls) {
        /* Named rather than skipped in silence: this used to be hooked whenever window fitting was
         * on, and it no longer is. A reader comparing two logs has to be able to see that the
         * detour is absent because nothing asked for it, not because it failed. */
        log_info("ForceWidth/ForceHeight are 0, obi.ini decides the startup resolution and "
                 "graphics_setResolution is not hooked at all");
        return;
    }
    if (site == 0) {
        log_warning("graphics_set_resolution did not resolve - ForceWidth/ForceHeight are IGNORED, "
                    "LogResolutionCalls can report nothing, and obi.ini decides the startup "
                    "resolution");
        return;
    }

    if (detour_install(&resolution_state.set_resolution_detour, site,
                       (const void *)hook_set_resolution, SET_RESOLUTION_PROLOGUE_SIZE)) {
        if (forcing) {
            log_info("hooked graphics_setResolution at %08X - the startup resolution is forced to "
                     "%dx%d instead of being read from obi.ini",
                     (unsigned)site, resolution_state.config.force_width,
                     resolution_state.config.force_height);
        } else {
            log_info("hooked graphics_setResolution at %08X for LogResolutionCalls only: every "
                     "call is reported with the address that made it, and nothing is changed",
                     (unsigned)site);
        }
    } else {
        log_error("the graphics_setResolution detour at %08X FAILED - ForceWidth/ForceHeight are "
                  "IGNORED", (unsigned)site);
    }
}

/* The window fit is a whole responsibility of its own and lives in window_fit.c; this hands it the
 * two table pointers that were resolved here and nothing else. */
static bool install_window_fit(void)
{
    window_fit_config_t fit_config;

    fit_config.enabled        = resolution_state.config.fit_window_to_mode;
    fit_config.raw_modes      = resolution_state.raw_modes;
    fit_config.raw_mode_count = resolution_state.raw_mode_count;

    return window_fit_install(&fit_config);
}

void enhanced_resolution_install(void)
{
    log_init("enhanced_resolution", false);

    if (resolution_state.installed) {
        return;
    }
    if (!host_image_resolve()) {
        log_error("no 32-bit host image, the resolution handling is NOT patched");
        return;
    }

    load_config();
    if (!resolution_state.config.enabled) {
        log_info("Enabled=0, the 4:3 mode list and the 640x480 menus stay as they shipped");
        return;
    }

    signature_resolve_table(sites, SITE_COUNT);
    resolve_mode_table();

    resolution_state.installed = true;

    /* FIRST of all the patches here, and that is an ordering constraint rather than a reading
     * order. The display mode enumeration runs once, inside graphics startup, and the filter can
     * only work on an enumeration that has not happened yet. Everything below acts on the list
     * that enumeration produced. */
    /* BEFORE the filter, and before the aspect gate: graphics_buildModeList runs during graphics
     * startup, and the filter has to agree with whatever depth the gates ended up at. */
    if (mode_depth_install((uint32_t)resolution_state.config.mode_bit_depth) == 32u) {
        /* The 2-D layer is software and writes two-byte pixels, so it has to be silenced or
         * the first menu bitmap faults. See sw_blit_guard.h for what that costs. */
        (void)sw_blit_guard_install();
    }
    (void)mode_filter_install(resolution_state.config.filter_mode_enumeration);

    install_aspect_gate();
    install_enum_modes_cap();
    install_forced_startup_resolution();
    ending_resolution_install(resolution_state.config.ending_keeps_resolution);
    credits_skip_install(resolution_state.config.skip_credits);
    install_menu_resolution_gate();

    /* LAST, and this is an ordering constraint and not merely a reading order: both features below
     * report whether they are load-bearing or merely insurance, and the answer is "load-bearing
     * exactly when this DLL moves the window". That is not known until the window fit has either
     * gone in or failed, so neither may be installed before install_window_fit() has returned. */
    {
        bool window_is_moved = install_window_fit();
        focus_guard_config_t focus_config;

        /* This repairs the one piece of engine arithmetic that assumed the window would never be
         * moved at all. */
        (void)cursor_anchor_install(resolution_state.config.keep_cursor_in_window, window_is_moved);

        /* And this covers what the repaired arithmetic still cannot: the warp only fires when a
         * mouse message arrives, and a pointer that crossed the edge stops generating them. */
        focus_config.confine_pointer = resolution_state.config.clip_pointer_to_window;
        focus_config.reacquire_input = resolution_state.config.reacquire_input_on_focus;
        focus_config.window_is_moved = window_is_moved;
        (void)focus_guard_install(&focus_config);

        /* AFTER install_window_fit(), and this is an ordering constraint rather than a reading
         * order: the cage asks window_fit_current_mode_size() for the display mode, and that
         * accessor is resolved inside window_fit_install(). Installing the cage first would find
         * it unresolved and decline for a reason that has nothing to do with the cage.
         *
         * The two are otherwise unrelated: this one is about the cursor the MENUS draw and is
         * useful whether or not the window is ever moved. */
        /* The artwork mount comes first of all, because menu_scale reads the converted
         * artwork's own size to decide the canvas, and that file lives in this folder. The mount
         * itself happens later, when the engine starts its menu system; this only arms it. */
        (void)menu_art_source_install(resolution_state.config.menu_art_directory[0] != '\0',
                                      resolution_state.config.menu_art_directory);

        /* menu_scale FIRST now, because the cage is sized from the canvas it draws and
         * asks menu_scale_current() for the multiple. The two used to be the other way
         * round, when the cage widened to the display mode and the scale had to ask
         * whether it had armed. */
        (void)menu_scale_install(resolution_state.config.menu_scale,
                                 resolution_state.config.widen_menu_cursor_area);

        {
            int32_t canvas_width;
            int32_t canvas_height;

            menu_scale_canvas(&canvas_width, &canvas_height);
            pointer_cage_install(resolution_state.config.widen_menu_cursor_area,
                                 canvas_width, canvas_height);

            /* Same ordering constraint again, and the same reason: the loading bar is drawn by
             * hand off the menu origin rather than as widgets, so it is sized from the canvas
             * menu_scale settled on rather than from the setting. */
            (void)menu_loading_bar_install(canvas_width, canvas_height);

            /* Same ordering constraint as the cage: the island's origin is derived from
             * window_fit_current_mode_size(), and its SIZE is the canvas menu_scale settled on.
             * Clamping to the authored 640x480 while the menus draw on a scaled canvas cuts real
             * widgets off at a border that is no longer there. This is the erase-side companion
             * of MenuKeepsResolution, see menu_island_clip.c for the defect it closes. */
            (void)menu_island_clip_install(resolution_state.config.clamp_menu_sprites_to_island,
                                           canvas_width, canvas_height);
        }

    }
}

void enhanced_resolution_shutdown(void)
{
    /* The only thing this DLL holds outside its own process image is the cursor clip rectangle,
     * and it is released here as well as on every focus loss, because an orderly exit is one path
     * on which no further frame is rendered. */
    focus_guard_shutdown();
}
