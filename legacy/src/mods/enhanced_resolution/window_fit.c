/* window_fit.c: move and size the game window to match the display mode, from the site that
 * actually sees every mode change.
 *
 * ==============================================================================================
 * BYTE BASIS
 *
 * --- 0x0046BC85  graphics_setMode(rawModeIndex) -----------------------------------------------
 *   0046BC85  55 8B EC 83 EC 10      push ebp / mov ebp,esp / sub esp,0x10   (6-byte prologue)
 *   0046BC8B  83 7D 08 00 / 7D 1A    if (rawModeIndex < 0) -> the assertion below
 *   0046BC91  68 9A 01 00 00         push 410, the assertion's line number
 *   0046BCAE  3B 15 F0 77 4B 00      cmp  edx,[g_curGraphicsMode]
 *   0046BCB4  75 0A                  jne  0046BCC0
 *   0046BCB6  B8 01 00 00 00         mov  eax,1     <- already in this mode: reports success and
 *   0046BCBB  E9 79 01 00 00         jmp  0046BE39     changes NOTHING. Trusting the return value
 *                                                      here would announce a change that never
 *                                                      happened, so the size is read back instead.
 *   0046BCC0  E8 <rel32>             call graphics_shutdownMode
 *   0046BCD1  E8 <rel32>             call stdDisplay_setMode  <- the device is rebuilt in here
 *   0046BDA7  8B 91 48 27 86 00      mov  edx,[ecx*0x54 + g_aRawMode + 0x08]   the new width
 *   0046BDAD  89 15 14 63 6D 00      mov  [g_screenWidthI],edx
 *   0046BDB9  8B 88 4C 27 86 00      mov  ecx,[eax*0x54 + g_aRawMode + 0x0C]   the new height
 *   0046BDBF  89 0D 2C 63 6D 00      mov  [g_screenHeightI],ecx
 *   0046BE34  B8 01 00 00 00         mov  eax,1     the only success return that changed anything
 *
 * Why here and not at graphics_setResolution 0x0046BE3D. Eight `call rel32` sites reach 0046BC85
 * and only one of them is inside graphics_setResolution (0046BF67). The two mode changes a player
 * actually triggers do not pass through graphics_setResolution at all, and ONE function contains
 * both of them, swmenu_enterMenuMode 0x0045F772, which is asymmetric in exactly the way the field
 * report was:
 *
 *   0045F7A7  E8 <rel32>   call graphics_getWidth
 *   0045F7AC  3B 05 ...    cmp  eax,[max_menu_Width]
 *   0045F7C8  E8 <rel32>   call graphics_setResolution(640,480)  ENTERING a menu: the mode goes DOWN
 *   0045F7F4  E8 <rel32>   call graphics_setMode(savedIndex)     LEAVING  a menu: the mode goes UP
 *
 * and the options screen's own apply calls graphics_setMode directly too, twice (0x0044162C and
 * 0x0044163C). A window fit driven from graphics_setResolution therefore heard the mode drop to
 * 640x480 and never heard it come back: the window was left at 640x480 while the mode returned to
 * 2560x1440, and whatever rendered afterwards then downscaled a 1440p image into a 640x480 window
 * on every frame. That is the two frames per second, and it was invisible under a wrapper that
 * ran exclusive fullscreen, where the window size does not matter.
 *
 * The bare prologue is not a signature. Those six bytes are the standard MSVC frame setup with a
 * 16-byte local area and they occur 74 times in the code section; a pattern that short resolves to
 * "74 matches" and switches the patch off. The argument test and the assertion line number behind
 * it are what make the anchor unique, and the tail from byte six is unique on its own, so the site
 * still resolves after another DLL has branched over the prologue, which is the normal case here,
 * because the HUD aspect fix wants this same function and the detours chain.
 *
 * One argument, __cdecl, returns non-zero on success: all eight call sites clean up with
 * `add esp, 4` and test eax.
 *
 * --- 0x0049385A  stdDisplay_getModeSize -------------------------------------------------------
 *   55 8B EC
 *   83 7D 08 00 / 74 13         if (out_width)
 *   A1 2C278600                     mov  eax,[g_curRawMode]        <- operand wildcarded
 *   6B C0 54                        imul eax,0x54            ; the raw-mode stride
 *   8B 4D 08
 *   8B 90 48278600                  mov  edx,[eax + g_aRawMode + 0x08]   <- operand wildcarded
 *
 * Called, never patched. It is how the size that is REALLY set can be read back after the call,
 * which is what makes the hook correct on all three of the engine's exits: a real change, the
 * shortcut for a mode already in use, and a failure that leaves the previous mode standing.
 *
 * WARNING: we may be arguing with a wrapper that owns the same window. The correction is therefore
 * repeated for a few frames after the mode change, but only while it is still needed and at most
 * RETRY_FRAMES times, a permanent duel would be worse than the problem.
 *
 * ==============================================================================================
 * SIZE NOTE (rule 9): a little over 600 lines, of which about 380 are code. The rest is the byte
 * evidence above and at each hooked site, which rule 8 requires there. It is one responsibility,
 * keep the window on the mode, and splitting the engine site away from the window arithmetic was
 * tried and rejected: the two halves have nothing to say to each other except a width and a
 * height, and the split bought two more files and one more indirection for exactly that.
 * ============================================================================================ */
#include "window_fit.h"

#include "common/detour.h"
#include "common/frame_hook.h"
#include "common/logging.h"
#include "common/memory.h"
#include "common/signature.h"

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RETRY_FRAMES 24

/* Anything smaller than this is a tool window, not the game. */
#define MIN_GAME_WINDOW_WIDTH  160
#define MIN_GAME_WINDOW_HEIGHT 120

/* More displays than this and the extra ones are ignored rather than the array overrun. Sixteen is
 * already past every desktop this will ever meet, and a fixed bound keeps the enumeration callback
 * free of allocation. The prefix is not decoration: the Windows SDK defines a bare MAX_MONITORS in
 * ddeml.h, which WIN32_LEAN_AND_MEAN happens to hide here and would not hide everywhere. */
#define WINDOW_FIT_MAX_MONITORS 16

/* Plausibility bounds, not taste: before a mode is configured the accessor reports zeroes, the
 * shutdown path leaves the size globals negative, and a garbage pair would move the window
 * somewhere absurd. */
#define MODE_SIZE_MIN 64
#define MODE_SIZE_MAX 16384

static const uint8_t SIG_SET_MODE_ENTRY[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10,
    0x83, 0x7D, 0x08, 0x00, 0x7D, 0x1A,
    0x68, 0x9A, 0x01, 0x00, 0x00
};
#define SET_MODE_PROLOGUE 6u

/* The two absolute .data operands are wildcarded, so the anchor is the SHAPE, the out-pointer
 * test, the `imul eax,0x54` stride and the two loads either side of it, and not where the mode
 * table happens to live. That keeps it resolving under forced ASLR and on the recompiled build,
 * where the same three instructions sit 0x60 lower and the operands have moved with them. It stays
 * unique in all three builds, which is what makes wildcarding them safe here. */
static const uint8_t SIG_MODE_SIZE_ACCESSOR[] = {
    0x55, 0x8B, 0xEC, 0x83, 0x7D, 0x08, 0x00, 0x74, 0x13,
    0xA1, 0x00, 0x00, 0x00, 0x00, 0x6B, 0xC0, 0x54, 0x8B, 0x4D, 0x08,
    0x8B, 0x90, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t MSK_MODE_SIZE_ACCESSOR[] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00
};
_Static_assert(sizeof(SIG_MODE_SIZE_ACCESSOR) == sizeof(MSK_MODE_SIZE_ACCESSOR),
               "the mode-size pattern and its mask are different lengths");

enum {
    SITE_SET_MODE,
    SITE_MODE_SIZE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR("graphics_set_mode",  SIG_SET_MODE_ENTRY, SET_MODE_PROLOGUE),
    SIGNATURE_ENTRY_MASKED("mode_size_accessor", SIG_MODE_SIZE_ACCESSOR, MSK_MODE_SIZE_ACCESSOR)
};

typedef int32_t (__cdecl *set_mode_fn_t)(int32_t raw_mode_index);
typedef void    (__cdecl *mode_size_fn_t)(int32_t *out_width, int32_t *out_height);

typedef struct monitor_scan {
    window_fit_monitor_t monitors[WINDOW_FIT_MAX_MONITORS];
    int                  count;
    HMONITOR             wanted;      /* the monitor the window is on now, or NULL */
    int                  current;     /* its index in `monitors`, or WINDOW_FIT_NO_MONITOR */
} monitor_scan_t;

typedef struct window_fit_state {
    bool                installed;
    bool                active;
    window_fit_config_t config;

    detour_t            set_mode_detour;
    mode_size_fn_t      engine_mode_size;

    HWND                window;
    uint32_t            retry_width;
    uint32_t            retry_height;
    int                 retries_left;

    /* The size the window was last fitted to. It is the guard against the shortcut that reports
     * success for a mode that was already set, and it is what makes the hook idempotent when a
     * caller retries, which the resolution path does. */
    int                 last_fit_width;
    int                 last_fit_height;

    bool                warned_no_raw_size;
    bool                warned_no_read_back;
} window_fit_state_t;

static window_fit_state_t window_state;

/* ============================================================================================ */
static BOOL CALLBACK pick_window(HWND window, LPARAM user_data)
{
    DWORD  process_id = 0;
    RECT   bounds;
    HWND  *out = (HWND *)user_data;

    GetWindowThreadProcessId(window, &process_id);
    if (process_id != GetCurrentProcessId()) {
        return TRUE;
    }
    if (!IsWindowVisible(window)) {
        return TRUE;
    }
    if (GetWindow(window, GW_OWNER) != NULL) {
        return TRUE;                               /* no dialogs, no tool windows */
    }
    if (!GetWindowRect(window, &bounds)) {
        return TRUE;
    }
    if (bounds.right - bounds.left < MIN_GAME_WINDOW_WIDTH ||
        bounds.bottom - bounds.top < MIN_GAME_WINDOW_HEIGHT) {
        return TRUE;
    }

    *out = window;
    return TRUE;
}

HWND window_fit_game_window(void)
{
    HWND found = NULL;

    if (window_state.window != NULL && IsWindow(window_state.window)) {
        return window_state.window;
    }
    EnumWindows(pick_window, (LPARAM)&found);
    window_state.window = found;
    return found;
}

/* ============================================================================================
 * THE MONITOR CHOICE
 *
 * Pure, so that the rule can be checked without a second physical display. The order of the three
 * arms is the whole content of the decision:
 *
 *   1. the monitor the window is already on, whenever it can show the mode. Sending the game to
 *      another display is a bigger surprise to the player than a window that is not flush with the
 *      edges of its own monitor, and the previous rule did exactly that.
 *   2. an exact size match, so a mode that fills a display gets that display.
 *   3. otherwise the SMALLEST monitor that still fits, because a mode shown on a much larger
 *      monitor leaves the game in a box in one corner of it.
 * ============================================================================================ */
int window_fit_choose_monitor(const window_fit_monitor_t *monitors, int count, int current,
                              uint32_t width, uint32_t height)
{
    int     best = WINDOW_FIT_NO_MONITOR;
    int64_t best_area = 0;
    int     index;

    if (monitors == NULL || count <= 0 || width == 0 || height == 0) {
        return WINDOW_FIT_NO_MONITOR;
    }

    if (current >= 0 && current < count &&
        monitors[current].width  >= 0 && (uint32_t)monitors[current].width  >= width &&
        monitors[current].height >= 0 && (uint32_t)monitors[current].height >= height) {
        return current;
    }

    for (index = 0; index < count; ++index) {
        int64_t area;

        if (monitors[index].width < 0 || monitors[index].height < 0) {
            continue;
        }
        if ((uint32_t)monitors[index].width == width &&
            (uint32_t)monitors[index].height == height) {
            return index;                          /* an exact match beats everything left */
        }
        if ((uint32_t)monitors[index].width < width ||
            (uint32_t)monitors[index].height < height) {
            continue;
        }

        area = (int64_t)monitors[index].width * (int64_t)monitors[index].height;
        if (best == WINDOW_FIT_NO_MONITOR || area < best_area) {
            best = index;
            best_area = area;
        }
    }

    return best;
}

/* ============================================================================================ */
static BOOL CALLBACK collect_monitor(HMONITOR monitor, HDC device_context, LPRECT clip,
                                     LPARAM user_data)
{
    monitor_scan_t *scan = (monitor_scan_t *)user_data;
    MONITORINFO     information;

    (void)device_context;
    (void)clip;

    if (scan->count >= WINDOW_FIT_MAX_MONITORS) {
        return FALSE;
    }

    information.cbSize = sizeof(information);
    if (!GetMonitorInfoA(monitor, &information)) {
        return TRUE;
    }

    scan->monitors[scan->count].left   = information.rcMonitor.left;
    scan->monitors[scan->count].top    = information.rcMonitor.top;
    scan->monitors[scan->count].width  = information.rcMonitor.right - information.rcMonitor.left;
    scan->monitors[scan->count].height = information.rcMonitor.bottom - information.rcMonitor.top;

    if (scan->wanted != NULL && monitor == scan->wanted) {
        scan->current = scan->count;
    }

    ++scan->count;
    return TRUE;
}

/* Where the window's top-left goes. The chosen monitor when one can show the mode; otherwise the
 * monitor it is already on, so that a mode no display can show at least does not also teleport the
 * window; and only when even that is unknown, the desktop origin. */
static void chosen_origin(const monitor_scan_t *scan, int chosen, LONG *out_left, LONG *out_top)
{
    int index = (chosen != WINDOW_FIT_NO_MONITOR) ? chosen : scan->current;

    if (index >= 0 && index < scan->count) {
        *out_left = scan->monitors[index].left;
        *out_top  = scan->monitors[index].top;
        return;
    }
    *out_left = 0;
    *out_top  = 0;
}

/* Which of the four outcomes the monitor rule produced, in words, for the log. Kept separate so
 * that the log line states the case it is really in: "moved to another monitor" is false when the
 * current one was never identified, and a reader who takes it at face value goes looking for a
 * display change that did not happen. */
static const char *monitor_reason(int chosen, int current)
{
    if (chosen == WINDOW_FIT_NO_MONITOR) {
        return "NO connected monitor can show this mode";
    }
    if (current == WINDOW_FIT_NO_MONITOR) {
        return "the monitor it was on could not be identified, so the best fit was used";
    }
    if (chosen == current) {
        return "the monitor it was already on";
    }
    return "moved off the monitor it was on; that one cannot show this mode";
}

/* Returns true when a connected monitor can show the mode at all. `source` names whatever drove
 * this fit and appears in the log line; NULL stays silent, which is what the retries do. */
static bool apply_fit(uint32_t width, uint32_t height, const char *source)
{
    HWND           window = window_fit_game_window();
    monitor_scan_t scan;
    RECT           client;
    RECT           wanted;
    LONG           style;
    LONG           extended_style;
    LONG           drawing_left;
    LONG           drawing_top;
    int            client_width;
    int            client_height;
    int            chosen;

    if (window == NULL || width == 0 || height == 0) {
        return false;
    }
    if (!GetClientRect(window, &client)) {
        return false;
    }

    client_width  = client.right - client.left;
    client_height = client.bottom - client.top;

    scan.count   = 0;
    scan.current = WINDOW_FIT_NO_MONITOR;
    scan.wanted  = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    EnumDisplayMonitors(NULL, NULL, collect_monitor, (LPARAM)&scan);

    chosen = window_fit_choose_monitor(scan.monitors, scan.count, scan.current, width, height);

    if ((uint32_t)client_width == width && (uint32_t)client_height == height) {
        return chosen != WINDOW_FIT_NO_MONITOR;    /* already the right size, do not touch it */
    }

    chosen_origin(&scan, chosen, &wanted.left, &wanted.top);
    drawing_left = wanted.left;                    /* kept for the log: the adjustment below moves
                                                    * the rect to the OUTER frame, and a line that
                                                    * quoted that would name a different point than
                                                    * the one the drawing area starts at. */
    drawing_top  = wanted.top;
    wanted.right  = wanted.left + (LONG)width;
    wanted.bottom = wanted.top  + (LONG)height;

    /* The frame counts: SetWindowPos wants the OUTER size, we want the drawing area. */
    style          = GetWindowLongA(window, GWL_STYLE);
    extended_style = GetWindowLongA(window, GWL_EXSTYLE);
    AdjustWindowRectEx(&wanted, (DWORD)style, GetMenu(window) != NULL, (DWORD)extended_style);

    SetWindowPos(window, NULL, wanted.left, wanted.top,
                 wanted.right - wanted.left, wanted.bottom - wanted.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    if (source != NULL) {
        log_info("%s: window %08X drawing area %dx%d -> %ux%u at screen %ld,%ld (%s)",
                 source, (unsigned)(uintptr_t)window, client_width, client_height, width, height,
                 drawing_left, drawing_top, monitor_reason(chosen, scan.current));
    }
    return chosen != WINDOW_FIT_NO_MONITOR;
}

static void on_frame(void)
{
    if (window_state.retries_left <= 0) {
        return;
    }
    --window_state.retries_left;
    (void)apply_fit(window_state.retry_width, window_state.retry_height, NULL);
}

/* ============================================================================================ */
/* The size that is really set, asked of the engine after the fact.
 *
 * Public, because the menu cursor cage in this same DLL needs the same number and this is where the
 * one accessor that reports it has already been found and range-checked. */
bool window_fit_current_mode_size(int *out_width, int *out_height)
{
    int32_t width = 0;
    int32_t height = 0;

    if (window_state.engine_mode_size == NULL) {
        return false;
    }

    window_state.engine_mode_size(&width, &height);
    if (width  < MODE_SIZE_MIN || width  > MODE_SIZE_MAX ||
        height < MODE_SIZE_MIN || height > MODE_SIZE_MAX) {
        return false;
    }

    *out_width  = (int)width;
    *out_height = (int)height;
    return true;
}

static bool current_mode_size(int *out_width, int *out_height)
{
    return window_fit_current_mode_size(out_width, out_height);
}

/* The size the engine is ABOUT to select, read from the same table and the same two field offsets
 * graphics_setMode itself reads out of it. It has to be knowable before the call: the device is
 * rebuilt inside stdDisplay_setMode and is created with whatever size the window has at that
 * moment, so correcting the window only afterwards is structurally one step too late. */
static bool requested_mode_size(int32_t raw_mode_index, int *out_width, int *out_height)
{
    const uint8_t *mode;
    uint32_t       width;
    uint32_t       height;

    if (window_state.config.raw_modes == NULL || window_state.config.raw_mode_count == NULL) {
        return false;
    }
    if (raw_mode_index < 0 || (uint32_t)raw_mode_index >= *window_state.config.raw_mode_count) {
        return false;
    }

    mode = window_state.config.raw_modes + (size_t)raw_mode_index * RAW_MODE_STRIDE;
    if (!memory_is_readable_range((uintptr_t)mode, RAW_MODE_STRIDE)) {
        return false;
    }

    width  = *(const uint32_t *)(mode + RAW_MODE_WIDTH);
    height = *(const uint32_t *)(mode + RAW_MODE_HEIGHT);
    if (width  < (uint32_t)MODE_SIZE_MIN || width  > (uint32_t)MODE_SIZE_MAX ||
        height < (uint32_t)MODE_SIZE_MIN || height > (uint32_t)MODE_SIZE_MAX) {
        return false;
    }

    *out_width  = (int)width;
    *out_height = (int)height;
    return true;
}

static void fit_before_the_device_is_rebuilt(int32_t raw_mode_index)
{
    int width = 0;
    int height = 0;

    if (requested_mode_size(raw_mode_index, &width, &height)) {
        /* Quiet: the announcement belongs after the engine has confirmed that something really
         * changed, and this runs on every call including the ones that change nothing. */
        (void)apply_fit((uint32_t)width, (uint32_t)height, NULL);
        return;
    }

    if (!window_state.warned_no_raw_size) {
        window_state.warned_no_raw_size = true;
        log_warning("the raw mode table did not resolve, so the window can only be fitted AFTER a "
                    "mode change and not before it. Whatever renders for the game may then still "
                    "read the old window size while it rebuilds its device.");
    }
}

static int32_t __cdecl hook_set_mode(int32_t raw_mode_index)
{
    set_mode_fn_t original = (set_mode_fn_t)window_state.set_mode_detour.original;
    int32_t       result;
    int           width = 0;
    int           height = 0;

    if (original == NULL) {
        return 0;                     /* unreachable once installed; never guess a success value */
    }
    if (!window_state.active) {
        /* Only reachable in the few instructions between the branch being written and the state
         * being armed. Nothing of ours is set up yet, so do exactly what the engine would. */
        return original(raw_mode_index);
    }

    fit_before_the_device_is_rebuilt(raw_mode_index);

    result = original(raw_mode_index);

    /* The return value is not the signal. The engine answers a request for the mode already in use
     * with success without changing anything, and a failed change leaves the previous mode
     * standing. Both are covered by acting on the size that can be READ BACK afterwards: it is the
     * new size on a real change, the unchanged size on the shortcut, and the still-current size
     * after a failure, which is the size the window should have in each of the three cases. */
    if (!current_mode_size(&width, &height)) {
        if (!window_state.warned_no_read_back) {
            window_state.warned_no_read_back = true;
            log_warning("the display size could not be read back after a mode change, so the "
                        "window is fitted from the mode table only and never verified afterwards");
        }
        return result;
    }

    if (width == window_state.last_fit_width && height == window_state.last_fit_height) {
        return result;
    }

    window_state.last_fit_width  = width;
    window_state.last_fit_height = height;

    log_info("display mode is now %dx%d (raw mode index %d)%s", width, height, (int)raw_mode_index,
             (result != 0) ? "" : ", the change FAILED and the previous mode still stands");

    if (!apply_fit((uint32_t)width, (uint32_t)height, "graphics_setMode")) {
        log_warning("%dx%d fits NO connected monitor. Whatever renders for the game then has to "
                    "scale every frame, which is the cost this feature exists to avoid. Choose a "
                    "mode a monitor can show natively.", width, height);
    }

    /* Whatever renders for the game owns this window too and moves it during its own rebuild, so
     * the correction is repeated for a few frames. apply_fit returns immediately once the window
     * already has the right drawing area. */
    window_state.retry_width  = (uint32_t)width;
    window_state.retry_height = (uint32_t)height;
    window_state.retries_left = RETRY_FRAMES;

    return result;
}

/* ============================================================================================
 * Every branch below logs, including the one that does nothing: a feature that is silently absent
 * reads exactly like a feature that is silently broken.
 * ============================================================================================ */
bool window_fit_install(const window_fit_config_t *config)
{
    uintptr_t site;

    if (window_state.installed || config == NULL) {
        return window_state.active;
    }
    window_state.installed = true;
    window_state.config = *config;

    /* Resolved before the enable gate, and that order is load-BEARING. The mode-size accessor is
     * not only this module's: window_fit_current_mode_size() is the DLL's single answer to "how big
     * is the display mode really", and the menu cursor cage asks it once per frame. Window fitting
     * defaults to OFF, so resolving this after the gate would have left that accessor NULL in every
     * default installation, a feature switched off here silently disabling a different one. */
    signature_resolve_table(sites, SITE_COUNT);
    if (sites[SITE_MODE_SIZE].address != 0) {
        window_state.engine_mode_size = (mode_size_fn_t)sites[SITE_MODE_SIZE].address;
    }

    if (!config->enabled) {
        log_info("FitWindowToMode=0, the game window is left exactly where the engine created it: "
                 "at screen 0,0, the size of the desktop, and never moved. That is the setting for "
                 "anyone running a graphics wrapper, because moving the window is the one thing "
                 "here that argues with whatever renders afterwards over the same object. Turn it "
                 "on only with no wrapper at all, where the window really can end up smaller than "
                 "the display mode.");
        return false;
    }

    site = sites[SITE_SET_MODE].address;

    if (site == 0) {
        log_warning("FitWindowToMode=1 but graphics_set_mode did not resolve, the window is NOT "
                    "fitted to anything and stays wherever it is. Nothing else in this DLL depends "
                    "on that site, so the rest of the resolution handling is unaffected.");
        return false;
    }

    if (!detour_install(&window_state.set_mode_detour, site, (const void *)hook_set_mode,
                        SET_MODE_PROLOGUE)) {
        log_error("the graphics_setMode detour at %08X FAILED - the window is NOT fitted to the "
                  "display mode", (unsigned)site);
        return false;
    }
    window_state.active = true;

    log_info("hooked graphics_setMode at %08X - the window follows the display mode in BOTH "
             "directions from there. It is the only site that sees every valid mode change: eight "
             "callers reach it and just one of them is graphics_setResolution, which is why a fit "
             "driven from graphics_setResolution heard a menu drop the mode to 640x480 and never "
             "heard it come back up.", (unsigned)site);

    /* Two real losses, and each is stated rather than hidden. Neither disables the feature: the
     * fit still happens at the mode change itself, which is the case that matters most. */
    if (window_state.engine_mode_size == NULL) {
        log_warning("the display-size accessor did not resolve, a mode change can only be fitted "
                    "from the REQUESTED mode, so a request the engine rejects would leave the "
                    "window on a size that was never set");
    }
    if (!frame_hook_add(on_frame)) {
        log_warning("no per-frame hook, window fitting acts once at the mode change and does not "
                    "follow up while whatever renders for the game rebuilds its device");
    }

    return true;
}
