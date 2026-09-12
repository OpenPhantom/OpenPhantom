/* enhanced_resolution.c: lift the 4:3 lock, and cap the array it would otherwise overflow.
 *
 * ==============================================================================================
 * Byte basis
 *
 * graphics_buildModeList 0x46C592 filters the platform's raw DirectDraw table [0x862740]
 * (count [0x862014], stride 0x54) into this device's list. The acceptance rule, in code order:
 *     1. kind == 1 && bpp == 0x10           <- 16-bit only; ModeBitDepth can open all three
 *                                              depth gates, and the software 2-D layer then fails
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
 * SIZE NOTE: past the 600 line mark, and about half of it is the byte evidence above and at each
 * hooked site. That evidence belongs at the site. A patch that rewrites a conditional jump cannot
 * be reviewed without it: with no acceptance rule written out in code order, `EB 32` is an
 * unaccountable two bytes.
 *
 * The seam taken was the mode table, now mode_table.c: reading the raw display list out of the
 * instructions around the aspect gate, dumping it on request, and capping what the options
 * screen is handed. It reached this file exactly 900 lines, the hard limit, with a note that
 * said only 600 and so granted permission instead of warning. Only two values cross the new
 * boundary, the two table pointers window_fit measures a requested mode against.
 *
 * The seam measured and rejected was the configuration block, which is longer but is read by
 * every part of this file, so moving it would have replaced one long file with two coupled
 * ones. The next seam, if this grows again, is the forced startup resolution together with the
 * menu gates: they share only the site table with everything else here.
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
#include "mode_table.h"
#include "sw_blit_guard.h"
#include "ending_resolution.h"
#include "credits_skip.h"
#include "subtitle_scale.h"
#include "pointer_cage.h"
#include "window_fit.h"
#include "window_mode.h"
#include "present_clip.h"
#include "pointer_release.h"
#include "window_poll.h"
#include "windowed_device.h"

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
    /* Both functions are detoured, mode_table.c's cap on the first and this file's hook on the
     * second, so both are declared as detour targets: a plain pattern is searched for whole, and
     * a DLL that detoured either first has replaced exactly the bytes it opens with. */
    SIGNATURE_ENTRY_DETOUR("graphics_enum_modes", SIG_ENUM_MODES, ENUM_MODES_PROLOGUE_SIZE),
    SIGNATURE_ENTRY_DETOUR("graphics_set_resolution", SIG_SET_RESOLUTION,
                           SET_RESOLUTION_PROLOGUE_SIZE),
    SIGNATURE_ENTRY("menu_gate_enter",        SIG_MENU_GATE_ENTER),
    SIGNATURE_ENTRY("menu_gate_usable",       SIG_MENU_GATE_USABLE)
};

#define MAX_MENU_MODES_LIMIT  63
#define MIN_MENU_MODES_LIMIT   4

typedef int32_t (__cdecl *set_resolution_fn_t)(uint32_t width, uint32_t height);

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
    int32_t window_mode;              /* 0 authentic, 1 borderless, 2 windowed */
    int32_t windowed_width;           /* 0 = the display mode */
    int32_t windowed_height;
    bool windowed_present;
    bool windowed_fill;
    int32_t pointer_release_key;
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

    detour_t            set_resolution_detour;


} resolution_state_t;

static resolution_state_t resolution_state;
static patch_journal_t bolt_journal;

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
    config->window_mode           = ini_read_int (RESOLUTION_SECTION, "WindowMode", 0);
    config->windowed_width        = ini_read_int (RESOLUTION_SECTION, "WindowedWidth", 0);
    config->windowed_height       = ini_read_int (RESOLUTION_SECTION, "WindowedHeight", 0);
    config->windowed_present      = ini_read_bool(RESOLUTION_SECTION, "WindowedPresent", false);
    config->windowed_fill         = ini_read_bool(RESOLUTION_SECTION, "WindowedFill", true);
    config->pointer_release_key   = ini_read_int (RESOLUTION_SECTION, "PointerReleaseKey", 0x91);

    /* Default ON, and the reason it is safe to default a behaviour change on: on a window that sits
     * at screen 0,0, which is where the engine puts it and where it stays without the line above,
     * the correction is the identity, bit for bit. It can only change what happens on a window this
     * DLL has moved somewhere else, and there the current behaviour is already wrong. */
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
     * Known cost, not yet reproduced here, and the reason this default is under review: the pause
     * screens repair themselves through the menu toolkit's damage rectangles, which live in canvas
     * coordinates clipped to the same hard-coded 640x480 every blit in that toolkit clips to. A
     * cursor quad drawn partly outside the island cannot be expressed as a damage rectangle, so it
     * is never erased, and a 3840x2160 field report describes every crossing of the island's edge
     * leaving a permanent stamp of the cursor's blue glow on the border until the screen closes.
     * The front end never shows it because its 3-D room repaints every pixel every frame. Every
     * clickable widget lives inside the island either way, so if that report reproduces here this
     * default becomes OFF. */
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

    /* Default 0, which is automatic rather than off: a mounted artwork set decides the ratio, and
     * the display's own resolution decides it when there is none. Fractional ratios are ordinary,
     * 1600 wide gives 2.5. Any other number is an explicit multiple of the authored 640x480 and
     * exists for testing. The ceiling is 4095/640, which the run length row format imposes. It
     * declines to install when the menu cursor cage is not widened. */
    config->menu_scale =
        ini_read_float(RESOLUTION_SECTION, "MenuScale", 0.0f);

    /* A folder of the reader's own menu artwork. One folder rather than seventy loose files beside
     * WMAIN.EXE, mounted through the engine's own resource chain, and named here so it can be
     * moved or emptied without touching the DLL. Empty declines the mount. */
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
#define RESOLUTION_READ_INI 0xFFFFFFFFu

/* Enough to cover a whole session's mode changes and nowhere near enough to be a flood. */
#define RESOLUTION_CALL_LOG_MAX 32u

static int32_t __cdecl hook_set_resolution(uint32_t width, uint32_t height)
{
    set_resolution_fn_t original =
        (set_resolution_fn_t)resolution_state.set_resolution_detour.original;

    /* This detour exists for ForceWidth/ForceHeight only. The window fit used to be
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

    /* Both operands or neither: with one repointed the entry test and the usable-width test
     * would disagree about the ceiling, which is a state the engine never had. */
    patch_journal_reset(&bolt_journal);
    if (patch_journal_repoint_operand(&bolt_journal, enter_operand, enter_cell, our_cell)
            != PATCH_RESULT_OK ||
        patch_journal_repoint_operand(&bolt_journal, usable_operand, enter_cell, our_cell)
            != PATCH_RESULT_OK) {
        patch_journal_undo(&bolt_journal);
        log_error("repointing the menu bolts failed, both operands are as they were");
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
        log_warning("graphics_set_resolution did not resolve, so ForceWidth/ForceHeight are "
                    "IGNORED, LogResolutionCalls can report nothing, and obi.ini decides the "
                    "startup resolution");
        return;
    }

    if (detour_install(&resolution_state.set_resolution_detour, site,
                       (const void *)hook_set_resolution, SET_RESOLUTION_PROLOGUE_SIZE)) {
        if (forcing) {
            log_info("hooked graphics_setResolution at %08X: the startup resolution is forced to "
                     "%dx%d instead of being read from obi.ini",
                     (unsigned)site, resolution_state.config.force_width,
                     resolution_state.config.force_height);
        } else {
            log_info("hooked graphics_setResolution at %08X for LogResolutionCalls only: every "
                     "call is reported with the address that made it, and nothing is changed",
                     (unsigned)site);
        }
    } else {
        log_error("the graphics_setResolution detour at %08X FAILED, so ForceWidth/ForceHeight "
                  "are IGNORED", (unsigned)site);
    }
}

/* The window mode is the player's choice of shape and lives in window_mode.c. It is installed
 * AFTER the fit, because it asks window_fit_current_mode_size() for the size the engine is
 * rendering and that accessor is resolved inside window_fit_install(). */
static bool install_window_mode(void)
{
    window_mode_config_t mode_config;

    mode_config.mode = (window_mode_kind_t)resolution_state.config.window_mode;
    mode_config.windowed_width  = resolution_state.config.windowed_width;
    mode_config.windowed_height = resolution_state.config.windowed_height;
    mode_config.windowed_present = resolution_state.config.windowed_present;

    return window_mode_install(&mode_config);
}

/* The window fit is a whole responsibility of its own and lives in window_fit.c; this hands it
 * only the two table pointers that were resolved here. */
static bool install_window_fit(void)
{
    window_fit_config_t fit_config;

    fit_config.enabled        = resolution_state.config.fit_window_to_mode;
    fit_config.raw_modes      = mode_table_raw_modes();
    fit_config.raw_mode_count = mode_table_raw_mode_count();

    return window_fit_install(&fit_config);
}

/* The window: the device change, whichever of the fit and the mode moves the window, and the
 * four features that read whether it was moved.
 *
 * EITHER of the fit and the mode counts as moving the window, and the OR is the whole reason
 * `window_is_moved` is one variable rather than two. The two features that read it report whether
 * they are load-bearing, and the answer is "load-bearing exactly when this DLL moves the window".
 * A window mode that centres a window, or puts a borderless one on a monitor whose origin is not
 * (0,0), breaks the engine's own pointer arithmetic exactly as the fit does, so counting only the
 * fit would leave them calling themselves insurance while they were carrying the feature.
 *
 * The fit runs first because the mode asks it for the display mode size. */
static void install_window_group(void)
{
    windowed_device_config_t device_config;
    focus_guard_config_t     focus_config;
    present_clip_config_t    clip_config;
    pointer_release_config_t release_config;
    window_poll_config_t     poll_config;
    bool fit_moved;
    bool mode_moved;
    bool window_is_moved;

    /* The device change is a single byte written into code the engine runs later, so its
     * position in this sequence decides nothing. It is here because this is where the window
     * work lives, and for no other reason. */
    device_config.enabled = resolution_state.config.windowed_present;
    (void)windowed_device_install(&device_config);

    /* Only one of them may move the window. Each is a complete opinion about where the
     * window goes: the fit puts it at the monitor's corner at the size of the display mode and
     * re-applies for frames afterwards, the mode centres it at a size of the player's choosing
     * and applies from the same site. Run together the fit acts last and wins, so the player
     * silently gets neither what they asked for nor a warning.
     *
     * The mode wins, because it is the more specific request and because the fit documents
     * itself as a last resort for a setup with no graphics wrapper at all. */
    if (resolution_state.config.fit_window_to_mode &&
        resolution_state.config.window_mode != (int32_t)WINDOW_MODE_AUTHENTIC) {
        log_warning("FitWindowToMode=1 and WindowMode=%d both decide where the window goes, "
                    "and they disagree. WindowMode wins and the fit is not installed: it is "
                    "the older setting and it documents itself as a last resort for a machine "
                    "with no graphics wrapper. Set WindowMode=0 if you wanted the fit.",
                    (int)resolution_state.config.window_mode);
        fit_moved = false;
    } else {
        fit_moved = install_window_fit();
    }
    mode_moved = install_window_mode();

    window_is_moved = fit_moved || mode_moved;

    /* This repairs the one piece of engine arithmetic that assumed the window would never be
     * moved at all. */
    (void)cursor_anchor_install(resolution_state.config.keep_cursor_in_window, window_is_moved);

    /* And this covers what the repaired arithmetic still cannot: the warp only fires when a
     * mouse message arrives, and a pointer that crossed the edge stops generating them. */
    focus_config.confine_pointer = resolution_state.config.clip_pointer_to_window;
    focus_config.reacquire_input = resolution_state.config.reacquire_input_on_focus;
    focus_config.window_is_moved = window_is_moved;
    (void)focus_guard_install(&focus_config);

    /* Last of the window group: both read what the calls above settled. */
    clip_config.windowed_present = resolution_state.config.windowed_present;
    clip_config.enabled = resolution_state.config.windowed_fill &&
                          clip_config.windowed_present;
    (void)present_clip_install(&clip_config);

    release_config.key = resolution_state.config.pointer_release_key;
    (void)pointer_release_install(&release_config);

    /* Seeded with what was just installed, so the first poll compares against what is in force
     * rather than re-applying everything a second in. */
    poll_config.mode                = resolution_state.config.window_mode;
    poll_config.windowed_width      = resolution_state.config.windowed_width;
    poll_config.windowed_height     = resolution_state.config.windowed_height;
    poll_config.pointer_release_key = resolution_state.config.pointer_release_key;
    poll_config.windowed_fill       = resolution_state.config.windowed_fill;
    (void)window_poll_install(&poll_config);
}

/* The menus: the artwork mount, the scale, and the three features sized from the canvas the
 * scale settles on. */
static void install_menu_group(void)
{
    int32_t canvas_width;
    int32_t canvas_height;

    /* The artwork mount comes first of all, because menu_scale reads the converted
     * artwork's own size to decide the canvas, and that file lives in this folder. The mount
     * itself happens later, when the engine starts its menu system; this only arms it. */
    (void)menu_art_source_install(resolution_state.config.menu_art_directory[0] != '\0',
                                  resolution_state.config.menu_art_directory);

    /* menu_scale FIRST now, because the cage is sized from the canvas it draws and
     * asks menu_scale_canvas() for the size. The two used to be the other way
     * round, when the cage widened to the display mode and the scale had to ask
     * whether it had armed. */
    (void)menu_scale_install(resolution_state.config.menu_scale,
                             resolution_state.config.widen_menu_cursor_area);

    menu_scale_canvas(&canvas_width, &canvas_height);

    /* AFTER install_window_fit(), and this is an ordering constraint rather than a
     * reading order: the cage asks window_fit_current_mode_size() for the display mode,
     * and that accessor is resolved inside window_fit_install(). Installing the cage
     * first would find it unresolved and decline for a reason that has nothing to do
     * with the cage.
     *
     * The two are otherwise unrelated: this one is about the cursor the MENUS draw and
     * is useful whether or not the window is ever moved. */
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
     * of MenuKeepsResolution: it closes the blue stamp the hovered button's halo leaves
     * on the island's border, drawn against the screen and repaired against the canvas. */
    (void)menu_island_clip_install(resolution_state.config.clamp_menu_sprites_to_island,
                                   canvas_width, canvas_height);
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
    mode_table_resolve(sites[SITE_ASPECT_GATE].address);

    resolution_state.installed = true;

    /* FIRST of all the patches here, an ordering constraint rather than a reading
     * order. The display mode enumeration runs once, inside graphics startup, so the depth choice
     * and the filter can only work on an enumeration that has not happened yet, and the filter has
     * to agree with whatever depth this settled on. Everything below acts on the list that
     * enumeration produced. */
    if (mode_depth_install((uint32_t)resolution_state.config.mode_bit_depth) == 32u) {
        /* The 2-D layer is software and writes two-byte pixels, so it has to be silenced or
         * the first menu bitmap faults. See sw_blit_guard.h for what that costs. */
        (void)sw_blit_guard_install();
    }
    (void)mode_filter_install(resolution_state.config.filter_mode_enumeration);

    install_aspect_gate();
    mode_table_install_cap(sites[SITE_ENUM_MODES].address,
                           resolution_state.config.max_menu_modes,
                           resolution_state.config.log_mode_table);
    install_forced_startup_resolution();
    ending_resolution_install(resolution_state.config.ending_keeps_resolution);
    credits_skip_install(resolution_state.config.skip_credits);
    subtitle_scale_install();
    install_menu_resolution_gate();

    /* LAST, and this is an ordering constraint and not merely a reading order: both features below
     * report whether they are load-bearing or merely insurance, and the answer is "load-bearing
     * exactly when this DLL moves the window". That is not known until the window fit has either
     * gone in or failed, so neither may be installed before install_window_fit() has returned. */
    install_window_group();

    /* And after the window group, because the cursor cage asks window_fit_current_mode_size()
     * for the display mode, and that accessor is resolved inside window_fit_install(). */
    install_menu_group();
}

void enhanced_resolution_shutdown(void)
{
    /* The only thing this DLL holds outside its own process image is the cursor clip rectangle,
     * and it is released here as well as on every focus loss, because an orderly exit is one path
     * on which no further frame is rendered. */
    focus_guard_shutdown();
}
