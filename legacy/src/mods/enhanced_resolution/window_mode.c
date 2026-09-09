#include "window_mode.h"

#include "window_fit.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/signature.h"

#include <windows.h>

/* --- 0x00498C86  stdWin95_setDisplayMode(mode, width, height) --------------------------------
 *
 *   55                       push ebp
 *   8B EC                    mov  ebp,esp
 *   8B 45 08                 mov  eax,[ebp+8]              the mode selector
 *   A3 <bWindowed>           mov  [stdWin95_bWindowed],eax        <- operand masked
 *   83 3D <bWindowed> 00     cmp  [stdWin95_bWindowed],0          <- operand masked
 *   75 3F                    jnz  the mode 1 arm
 *   68 00 00 CF 10           push 0x10CF0000               the captioned style word
 *   6A F0                    push -16                      GWL_STYLE
 *
 * The two absolute operands are masked so the anchor is the shape and the style word rather than
 * where that global happens to live, which is what keeps it resolving under forced ASLR. The push
 * of 0x10CF0000 is the part that makes it unique: it is the only place in the image that word is
 * pushed, and it is inside the tail rather than the prologue, so it survives the detour. */
static const uint8_t SIG_SET_DISPLAY_MODE[] = {
    0x55,
    0x8B, 0xEC,
    0x8B, 0x45, 0x08,
    0xA3, 0x00, 0x00, 0x00, 0x00,
    0x83, 0x3D, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x75, 0x3F,
    0x68, 0x00, 0x00, 0xCF, 0x10,
    0x6A, 0xF0
};
static const uint8_t MSK_SET_DISPLAY_MODE[] = {
    0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};
_Static_assert(sizeof SIG_SET_DISPLAY_MODE == sizeof MSK_SET_DISPLAY_MODE,
               "the set-display-mode pattern and its mask are different lengths");

/* Six: push ebp, mov ebp,esp, mov eax,[ebp+8]. Nothing in it is relative, so all of it relocates. */
#define SET_DISPLAY_MODE_PROLOGUE 6u

/* The engine's own frameless word, and it is deliberately the same one. The game already runs with
 * exactly this style, so the borderless mode differs from doing nothing in GEOMETRY alone, which is
 * a far smaller change than also restyling the window. */
#define STYLE_BORDERLESS 0x10000000u

/* A caption, a system menu and a minimise box, and no WS_THICKFRAME or WS_MAXIMIZEBOX. See the
 * header for why the engine's own 0x10CF0000 is not reused: the two bits it adds offer a resize
 * this engine cannot answer. */
#define STYLE_WINDOWED   (WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | \
                          WS_CLIPSIBLINGS | WS_CLIPCHILDREN)

/* The sizable variant adds exactly the two bits the fixed one leaves out. Written as an addition
 * to STYLE_WINDOWED rather than spelled out again, so the two cannot drift apart. */
#define STYLE_RESIZABLE  (STYLE_WINDOWED | WS_THICKFRAME | WS_MAXIMIZEBOX)

/* The same bounds window_fit.c applies to a mode size, for the same reason: a value outside them
 * is not a resolution and acting on it would move the window somewhere absurd. */
#define MODE_SIZE_MIN 64
#define MODE_SIZE_MAX 16384

/* --- 0x0046BC85  graphics_setMode(rawModeIndex) --------------------------------------------
 *
 * The same site window_fit.c hooks, for the same reason, and the pattern is its pattern. Every
 * valid mode change passes through here: eight callers reach it and only one of them is
 * graphics_setResolution.
 *
 * Why this site and not only the style switch beside it. stdWin95_setDisplayMode fires exactly
 * once in a session, from main_openGraphics at 0x0043F542, and by then the device already exists:
 * the same function calls graphics_setResolution fifty one bytes earlier at 0x0043F50F, and that
 * is where DirectDraw is first asked for anything. A window shaped only from the later site is
 * therefore shaped after the first device was built against the shape it replaced, which is what a
 * graphics wrapper was seen doing, reporting the window as the full desktop at device creation and
 * our rectangle only once, later.
 *
 * Hooking here fixes both halves of that. The call before the original puts the window in its
 * final shape before the device is created, on the very first mode set as well as every later one.
 * The call after puts it back if anything moved it during the rebuild.
 *
 * Two detours now sit on this address, this one and window_fit's. common/detour.c chains, so that
 * is safe, and the install order in enhanced_resolution.c puts the fit in first, which makes this
 * the outer hook and its writes the last to land. That ordering is wanted rather than incidental. */
static const uint8_t SIG_GRAPHICS_SET_MODE[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10,
    0x83, 0x7D, 0x08, 0x00, 0x7D, 0x1A,
    0x68, 0x9A, 0x01, 0x00, 0x00
};
#define GRAPHICS_SET_MODE_PROLOGUE 6u

enum {
    SITE_SET_DISPLAY_MODE,
    SITE_GRAPHICS_SET_MODE,
    SITE_COUNT
};

static signature_t sites[SITE_COUNT] = {
    SIGNATURE_ENTRY_DETOUR_MASKED("win95_set_display_mode", SIG_SET_DISPLAY_MODE,
                                  MSK_SET_DISPLAY_MODE, SET_DISPLAY_MODE_PROLOGUE),
    SIGNATURE_ENTRY_DETOUR("graphics_set_mode", SIG_GRAPHICS_SET_MODE,
                           GRAPHICS_SET_MODE_PROLOGUE)
};

typedef uint32_t (__cdecl *set_display_mode_fn_t)(int32_t mode, int32_t width, int32_t height);
typedef int32_t  (__cdecl *graphics_set_mode_fn_t)(int32_t raw_mode_index);

typedef struct window_mode_state {
    window_mode_config_t config;
    detour_t             detour;
    detour_t             set_mode_detour;
    bool                 armed;           /* the hooks are in place and apply may run */
    bool                 warned_no_window;
    int32_t              last_width;      /* the last shape LOGGED, so one line per change */
    int32_t              last_height;
} window_mode_state_t;

static window_mode_state_t mode_state;

/* ==============================================================================================
 * The decision, which is all of the policy and none of the window
 * ============================================================================================ */
uint32_t window_mode_style(window_mode_kind_t mode)
{
    switch (mode) {
    case WINDOW_MODE_BORDERLESS:        return STYLE_BORDERLESS;
    case WINDOW_MODE_WINDOWED:          return STYLE_WINDOWED;
    case WINDOW_MODE_RESIZABLE:         return STYLE_RESIZABLE;
    case WINDOW_MODE_BORDERLESS_SIZED:  return STYLE_BORDERLESS;
    default:                            return 0u;   /* leave GWL_STYLE alone */
    }
}

static bool size_is_plausible(int32_t value)
{
    return value >= MODE_SIZE_MIN && value <= MODE_SIZE_MAX;
}

bool window_mode_client_rect(window_mode_kind_t mode, const window_mode_rect_t *monitor,
                             int32_t mode_width, int32_t mode_height,
                             int32_t wanted_width, int32_t wanted_height,
                             window_mode_rect_t *out)
{
    int32_t width;
    int32_t height;

    /* The low end is tested as well as the high end. ini_read_int hands back whatever the file
     * says, so WindowMode=-1 is reachable, and without this it falls past the borderless arm into
     * the windowed one: the window would be moved and resized while keeping the engine's own
     * frameless style, from a value that means nothing. */
    if (out == NULL || monitor == NULL ||
        monitor->width <= 0 || monitor->height <= 0 ||
        mode <= WINDOW_MODE_AUTHENTIC || mode >= WINDOW_MODE_COUNT) {
        return false;
    }

    if (mode == WINDOW_MODE_BORDERLESS) {
        out->left   = monitor->left;
        out->top    = monitor->top;
        out->width  = monitor->width;
        out->height = monitor->height;
        return true;
    }

    /* Windowed. The default size is what the engine is RENDERING, so the picture is shown one
     * pixel for one pixel and nothing scales it. An explicit pair overrides that, and a mode size
     * that is not plausible falls back to the monitor rather than to an invented number. */
    width  = size_is_plausible(wanted_width)  ? wanted_width  : mode_width;
    height = size_is_plausible(wanted_height) ? wanted_height : mode_height;

    /* Each axis falls back on its own, which is what the setting's own documentation promises.
     * Resetting both because one was implausible would make a single bad number throw away a good
     * one beside it. */
    if (!size_is_plausible(width)) {
        width = monitor->width;
    }
    if (!size_is_plausible(height)) {
        height = monitor->height;
    }

    /* A window larger than the monitor has its caption off the top of the screen once it is
     * centred, and then it cannot be moved. Clamping is what keeps it reachable. */
    if (width > monitor->width) {
        width = monitor->width;
    }
    if (height > monitor->height) {
        height = monitor->height;
    }

    out->width  = width;
    out->height = height;
    out->left   = monitor->left + (monitor->width - width) / 2;
    out->top    = monitor->top + (monitor->height - height) / 2;
    return true;
}

bool window_mode_fit_frame(const window_mode_rect_t *monitor,
                           int32_t frame_width, int32_t frame_height,
                           window_mode_rect_t *client)
{
    int32_t room_width;
    int32_t room_height;

    if (monitor == NULL || client == NULL || frame_width < 0 || frame_height < 0 ||
        monitor->width <= 0 || monitor->height <= 0) {
        return false;
    }

    room_width  = monitor->width  - frame_width;
    room_height = monitor->height - frame_height;
    if (room_width < 1 || room_height < 1) {
        return false;                 /* the frame alone fills the screen: not a usable window */
    }

    if (client->width  > room_width)  { client->width  = room_width;  }
    if (client->height > room_height) { client->height = room_height; }

    /* Centred on the OUTER window rather than on the client. Centring the client puts half the
     * frame off the top of the monitor, which for a framed mode is most of the caption. */
    client->left = monitor->left + (monitor->width  - (client->width  + frame_width))  / 2;
    client->top  = monitor->top  + (monitor->height - (client->height + frame_height)) / 2;
    return true;
}

/* ==============================================================================================
 * The window
 * ============================================================================================ */
static bool monitor_of(HWND window, window_mode_rect_t *out)
{
    HMONITOR     handle = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    MONITORINFO  info;

    if (handle == NULL) {
        return false;
    }
    info.cbSize = sizeof info;
    if (!GetMonitorInfoA(handle, &info)) {
        return false;
    }

    /* The FULL monitor rectangle, not rcWork. A borderless window that stopped at the taskbar
     * would letterbox the picture against it, and the game is not a desktop application. */
    out->left   = info.rcMonitor.left;
    out->top    = info.rcMonitor.top;
    out->width  = info.rcMonitor.right - info.rcMonitor.left;
    out->height = info.rcMonitor.bottom - info.rcMonitor.top;
    return out->width > 0 && out->height > 0;
}

/* What the border and caption add to a client, for the style this mode WILL have rather than the
 * one the window has now. Measured rather than assumed: the engine's own border deltas are short by
 * SM_CYMENU, and a window with no menu is exactly the case that catches. */
static bool frame_extra(window_mode_kind_t mode, HWND window,
                        int32_t *out_width, int32_t *out_height)
{
    RECT     probe = { 0, 0, 1000, 1000 };
    uint32_t style = window_mode_style(mode);

    *out_width  = 0;
    *out_height = 0;
    if (style == 0u) {
        return true;               /* the engine's own shape, which nothing here reshapes */
    }
    if (!AdjustWindowRectEx(&probe, (DWORD)style, GetMenu(window) != NULL,
                            (DWORD)GetWindowLongA(window, GWL_EXSTYLE))) {
        return false;
    }
    *out_width  = (int32_t)(probe.right - probe.left) - 1000;
    *out_height = (int32_t)(probe.bottom - probe.top) - 1000;
    return true;
}

static void apply_window_mode(void)
{
    if (!mode_state.armed) {
        return;                       /* a hook that fired before the state was finished */
    }

    HWND               window = window_fit_game_window();
    window_mode_rect_t monitor;
    window_mode_rect_t client;
    RECT               outer;
    uint32_t           style;
    int                mode_width  = 0;
    int                mode_height = 0;

    if (window == NULL) {
        if (!mode_state.warned_no_window) {
            mode_state.warned_no_window = true;
            log_warning("the game window could not be found, so the window mode is not applied. "
                        "The window keeps the shape the engine gave it.");
        }
        return;
    }
    if (!monitor_of(window, &monitor)) {
        log_warning("the monitor holding the game window could not be measured, so the window "
                    "mode is not applied this time");
        return;
    }

    /* Asked of window_fit, which owns this answer for the whole DLL. Resolving the mode size a
     * second time here would make it possible for the two to differ. A false is not fatal: the
     * borderless mode does not need it, and the windowed mode falls back to the monitor. */
    if (!window_fit_current_mode_size(&mode_width, &mode_height)) {
        mode_width  = 0;
        mode_height = 0;
    }

    if (!window_mode_client_rect(mode_state.config.mode, &monitor,
                                 (int32_t)mode_width, (int32_t)mode_height,
                                 mode_state.config.windowed_width,
                                 mode_state.config.windowed_height, &client)) {
        return;
    }

    /* Before the window is placed, because the rectangle below is grown by the frame and a client
     * the size of the monitor becomes an outer window larger than it. */
    {
        int32_t frame_width  = 0;
        int32_t frame_height = 0;

        if (frame_extra(mode_state.config.mode, window, &frame_width, &frame_height)) {
            (void)window_mode_fit_frame(&monitor, frame_width, frame_height, &client);
        }
    }

    style = window_mode_style(mode_state.config.mode);
    if (style != 0u) {
        SetWindowLongA(window, GWL_STYLE, (LONG)style);
    }

    /* The wanted CLIENT rectangle turned into the outer one the API wants, using the style that is
     * now in force rather than the one that was. GetMenu is asked rather than assumed: this window
     * has no menu today, and an adjustment computed as though it did would be short by SM_CYMENU,
     * which is exactly the mistake the engine's own border deltas make. */
    outer.left   = client.left;
    outer.top    = client.top;
    outer.right  = client.left + client.width;
    outer.bottom = client.top + client.height;
    if (!AdjustWindowRectEx(&outer, (DWORD)GetWindowLongA(window, GWL_STYLE),
                            GetMenu(window) != NULL,
                            (DWORD)GetWindowLongA(window, GWL_EXSTYLE))) {
        log_warning("the window frame could not be measured, so the window mode is not applied "
                    "this time");
        return;
    }

    if (!SetWindowPos(window, NULL, outer.left, outer.top,
                      outer.right - outer.left, outer.bottom - outer.top,
                      SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED)) {
        log_warning("SetWindowPos refused the window mode, so the window keeps the shape the "
                    "engine gave it");
        return;
    }

    if (client.width != mode_state.last_width || client.height != mode_state.last_height) {
        RECT actual;

        mode_state.last_width  = client.width;
        mode_state.last_height = client.height;

        /* Four numbers rather than two, because a graphics wrapper's own log reported this window
         * as 3840x2160 in the same session this file called the monitor 1920x1080, and there is no
         * way to tell from one of them which is scaled. GetSystemMetrics is what the ENGINE sized
         * its own window from, GetMonitorInfo is what this file sizes it from, and the window
         * rectangle read back is what actually happened. If the first two disagree, this is a
         * display-scaling question and the third says which of them the window followed. */
        if (!GetWindowRect(window, &actual)) {
            actual.left = actual.top = actual.right = actual.bottom = 0;
        }
        log_info("window mode %d: asked for client %dx%d at %d,%d on a %dx%d monitor at %d,%d; "
                 "GetSystemMetrics says the screen is %dx%d; the window rect came back %dx%d at "
                 "%d,%d.",
                 (int)mode_state.config.mode, (int)client.width, (int)client.height,
                 (int)client.left, (int)client.top,
                 (int)monitor.width, (int)monitor.height, (int)monitor.left, (int)monitor.top,
                 GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
                 (int)(actual.right - actual.left), (int)(actual.bottom - actual.top),
                 (int)actual.left, (int)actual.top);
    }
}

static int32_t __cdecl hook_graphics_set_mode(int32_t raw_mode_index)
{
    graphics_set_mode_fn_t original =
        (graphics_set_mode_fn_t)mode_state.set_mode_detour.original;
    int32_t result;

    if (original == NULL) {
        return 0;                     /* unreachable once installed; never guess a success value */
    }

    /* Before, so the device about to be built inside is built against the window we want. After,
     * so anything that moved it during the rebuild is corrected. */
    apply_window_mode();
    result = original(raw_mode_index);
    apply_window_mode();
    return result;
}

static uint32_t __cdecl hook_set_display_mode(int32_t mode, int32_t width, int32_t height)
{
    set_display_mode_fn_t original = (set_display_mode_fn_t)mode_state.detour.original;
    uint32_t              result;

    if (original == NULL) {
        return 0u;
    }

    /* Before and after, and the before is the half that matters. window_fit.c reached the same
     * conclusion at the mode change beside this one, and its reason applies here word for word:
     * whatever is rebuilt inside takes the window size as it is at that moment.
     *
     * Correcting only afterwards is what the first build did, and it left everything that reads
     * the window during start-up holding the shape the engine gave it rather than ours. Two things
     * were seen doing exactly that. The graphics wrapper built its device against the old full
     * screen rectangle and only adopted ours on a later reset, which showed as a window with
     * nothing drawn in it. And the movie player sized its child window from the client area at
     * that same moment, so a film played at the wrong scale inside a correctly shaped window.
     *
     * Applying first costs nothing when the shape is already right: apply_window_mode() computes
     * the same rectangle and SetWindowPos on an unchanged rectangle is not a change. */
    apply_window_mode();
    result = original(mode, width, height);
    apply_window_mode();
    return result;
}

/* ==============================================================================================
 * Install
 * ============================================================================================ */
bool window_mode_wanted_client_size(int32_t mode, int32_t windowed_width,
                                    int32_t windowed_height,
                                    int32_t *out_width, int32_t *out_height)
{
    window_mode_rect_t monitor;
    window_mode_rect_t client;
    int                mode_width  = 0;
    int                mode_height = 0;
    HWND               window      = window_fit_game_window();

    if (out_width == NULL || out_height == NULL || window == NULL ||
        mode == (int32_t)WINDOW_MODE_AUTHENTIC) {
        return false;
    }
    if (!monitor_of(window, &monitor)) {
        return false;
    }
    if (!window_fit_current_mode_size(&mode_width, &mode_height)) {
        mode_width  = 0;
        mode_height = 0;
    }
    if (!window_mode_client_rect((window_mode_kind_t)mode, &monitor,
                                 (int32_t)mode_width, (int32_t)mode_height,
                                 windowed_width, windowed_height, &client)) {
        return false;
    }

    /* The same fit the window itself goes through, so what is reported here is the size the client
     * will really have and not the size that was asked for. The caller writes this into the game's
     * settings file as the resolution to render at, and the two disagreeing by the width of a
     * frame is the whole reason this step exists. */
    {
        int32_t frame_width  = 0;
        int32_t frame_height = 0;

        if (frame_extra((window_mode_kind_t)mode, window, &frame_width, &frame_height)) {
            (void)window_mode_fit_frame(&monitor, frame_width, frame_height, &client);
        }
    }

    *out_width  = client.width;
    *out_height = client.height;
    return true;
}

bool window_mode_reapply(int32_t mode, int32_t windowed_width, int32_t windowed_height)
{
    if (!mode_state.armed) {
        return false;
    }
    mode_state.config.mode           = (window_mode_kind_t)mode;
    mode_state.config.windowed_width  = windowed_width;
    mode_state.config.windowed_height = windowed_height;

    /* Forgotten so that the next apply logs the shape even when the numbers happen to repeat an
     * earlier one. Going 2 to 3 and back is a style change with no size change, and without this
     * the log would fall silent on exactly the transitions a player is most likely to be trying
     * to make sense of. */
    mode_state.last_width  = 0;
    mode_state.last_height = 0;

    apply_window_mode();
    return true;
}

bool window_mode_install(const window_mode_config_t *config)
{
    uintptr_t site;

    if (config == NULL) {
        return false;
    }
    mode_state.config = *config;

    if (config->mode == WINDOW_MODE_AUTHENTIC) {
        log_info("WindowMode=0, the window is left exactly as the engine shapes it. That is the "
                 "shipped default and it is not a fallback: the engine restyles and resizes its "
                 "own window during the renderer bring-up, and every previous release ran with "
                 "the result.");
        return false;
    }
    if (config->mode < WINDOW_MODE_AUTHENTIC || config->mode >= WINDOW_MODE_COUNT) {
        log_warning("WindowMode=%d is not a mode this build knows, so the window is left alone",
                    (int)config->mode);
        return false;
    }

    /* Warned about rather than refused. An earlier build refused this pairing outright, on the
     * grounds that a windowed device's surfaces are always the desktop's size. That was measured,
     * and it was true of the configuration it was measured in, but it was not a property of the
     * device: it belongs to the graphics wrapper, and one of its settings decides it. With that
     * setting on, the surfaces take the size the engine is rendering and a window smaller than the
     * screen is fillable. The refusal is gone because the reason for it was not general.
     *
     * What remains is narrower and lives in how the wrapper copies the picture out. It intersects
     * the surface rectangle, which starts at the origin and is the render size, with the window's
     * client rectangle expressed in DESKTOP coordinates, and copies only the overlap. Three cases
     * follow, and only the middle one is wrong:
     *
     *   the client CONTAINS the surface rectangle   nothing is clipped, the whole picture is sent
     *   the client OVERLAPS it in part              a fragment is sent, stretched over the window
     *   the client MISSES it entirely               the wrapper sends the whole surface instead
     *
     * The first is a window at the monitor's origin at least as large as the render size, which is
     * what WINDOW_MODE_BORDERLESS gives. The third is any window whose client starts past the render
     * size on either axis, reachable by rendering small and placing the window right of or below
     * that rectangle. The second is everything between, and it is the case this warns about: a
     * fragment stretched over the window reads as a rendering fault rather than a geometry one, and
     * there is nothing on screen to say otherwise. */
    if (config->mode != WINDOW_MODE_BORDERLESS && config->windowed_present) {
        log_info("WindowMode=%d with WindowedPresent=1: whether the picture fills this window "
                 "depends on where its client area lands relative to the rectangle the engine "
                 "renders. A client that covers that rectangle, and a client that misses it "
                 "completely, both receive the whole picture. A client that overlaps it in part "
                 "receives only the overlap, stretched to fill the window, which looks broken and "
                 "is not. If that is what you see, move the window clear of the render rectangle "
                 "or make it cover that rectangle. WindowedFill=1 does exactly that for you, "
                 "and is on by default.", (int)config->mode);
    }

    signature_resolve_table(sites, SITE_COUNT);
    site = sites[SITE_SET_DISPLAY_MODE].address;
    if (site == 0) {
        log_warning("stdWin95_setDisplayMode did not resolve, so WindowMode=%d cannot be applied "
                    "and the window keeps the shape the engine gives it",
                    (int)config->mode);
        return false;
    }

    if (!detour_install(&mode_state.detour, site, (const void *)hook_set_display_mode,
                        SET_DISPLAY_MODE_PROLOGUE)) {
        log_warning("the detour on stdWin95_setDisplayMode could not be placed, so WindowMode=%d "
                    "is not applied", (int)config->mode);
        return false;
    }

    /* Optional, and the feature is worth having without it. If only the style switch resolves the
     * window is still shaped, just later than the first device build. */
    if (sites[SITE_GRAPHICS_SET_MODE].address != 0 &&
        detour_install(&mode_state.set_mode_detour, sites[SITE_GRAPHICS_SET_MODE].address,
                       (const void *)hook_graphics_set_mode, GRAPHICS_SET_MODE_PROLOGUE)) {
        log_info("the window shape is also reasserted from graphics_setMode at %08X, before and "
                 "after each of the eight paths that rebuild the device",
                 (unsigned)sites[SITE_GRAPHICS_SET_MODE].address);
    } else {
        log_warning("graphics_setMode did not resolve or could not be detoured, so the window is "
                    "shaped once at start-up and not reasserted when the device is rebuilt. "
                    "Whatever renders for the game may then see the window it had before.");
    }

    mode_state.armed = true;
    log_info("WindowMode=%d armed at %08X. The engine calls that function once during the renderer "
             "bring-up, asking for 0x800 by 0x800, so the window it runs in comes out about 2054 "
             "by 2077: neither the size of the screen nor a window. The correction is applied both "
             "before and after the engine's own, because whatever is rebuilt inside takes the "
             "window size as it is at that moment.",
             (int)config->mode, (unsigned)site);
    return true;
}
