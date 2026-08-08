/* window_fit.h: keep the game window on whatever display mode the engine has actually set.
 *
 * ==============================================================================================
 * This is off by default, and that is the design, not a retreat
 *
 * Everything else in this tree patches the game and leaves the presentation alone, so it behaves
 * the same whether a graphics wrapper renders afterwards or nothing does. Moving the game's window
 * is the one thing here that argues with that wrapper over the same object, and it has cost twice:
 *
 *   1. It broke the engine's own pointer confinement. The engine creates its window at screen
 *      (0,0) at SM_CXSCREEN x SM_CYSCREEN and never moves it; both of its own SetWindowPos calls
 *      carry SWP_NOMOVE, and its re-centring warp compares CLIENT coordinates against the SCREEN
 *      point it warps to. Those two agree at exactly one window origin: (0,0). Moving the window
 *      made the pointer walk out of it. That is what cursor_anchor.c and focus_guard.c repair.
 *
 *   2. It produced a two-frames-per-second field report. The window was shrunk to 640x480 with the
 *      mode, the mode went back up to 2560x1440, the window did not, and whatever rendered
 *      afterwards then downscaled a 1440p image into a 640x480 window on every single frame. Under
 *      the previous wrapper, which ran exclusive fullscreen, the window size did not matter at all
 *      and none of this showed.
 *
 * So it is a last resort, for the one setup that really needs it: no wrapper at all, where the
 * window genuinely can end up smaller than the display mode. Everyone else leaves it at 0 and the
 * window stays exactly where the engine put it.
 *
 * ==============================================================================================
 * When it is on, it must follow the mode in both directions
 *
 * A window left SMALLER than the display mode is the defect above. A window left LARGER is merely
 * ugly. Both are corrected, and the correction is driven from the one engine function every valid
 * mode change passes through, which is graphics_setMode and NOT graphics_setResolution. The byte
 * evidence for that distinction is at the top of window_fit.c, because getting it wrong is what
 * produced the defect in the first place.
 */
#ifndef WINDOW_FIT_H
#define WINDOW_FIT_H

#include <windows.h>

#include <stdbool.h>
#include <stdint.h>

/* ---- the raw display-mode table, as graphics_setMode itself addresses it --------------------- *
 * The engine indexes it with `imul reg, reg, 0x54` and then reads the width at base + 8 and the
 * height at base + 0x0C:
 *
 *   0046BDA4  6B C9 54               imul ecx,ecx,0x54
 *   0046BDA7  8B 91 48 27 86 00      mov  edx,[ecx + g_aRawMode + 0x08]     width
 *   0046BDB6  6B C0 54               imul eax,eax,0x54
 *   0046BDB9  8B 88 4C 27 86 00      mov  ecx,[eax + g_aRawMode + 0x0C]     height
 *
 * The remaining two fields are the acceptance rule graphics_buildModeList applies: only
 * kind == 1 && bpp == 0x10 is a mode this 16-bit engine can use at all.
 *
 * They live in this header because two places read that table, the fit below, to learn how big a
 * REQUESTED mode is, and the mode dump in enhanced_resolution.c, and one layout written down twice is
 * one layout that can disagree with itself. */
#define RAW_MODE_STRIDE 0x54
#define RAW_MODE_WIDTH  0x08
#define RAW_MODE_HEIGHT 0x0C
#define RAW_MODE_KIND   0x1C
#define RAW_MODE_BPP    0x20

typedef struct window_fit_config {
    bool enabled;                     /* the ini gate; false still logs, it never goes quiet */

    /* g_aRawMode and g_numRawModes, or NULL when they did not resolve. They are passed in rather
     * than resolved again here: they are read backwards from the aspect gate, which is the caller's
     * business, and two independent answers to "where is the mode table" could differ. Without them
     * the size of a REQUESTED mode is unknown, so the window can only be corrected after the change
     * instead of before it, which is a real loss, and it is logged as one. */
    const uint8_t  *raw_modes;
    const uint32_t *raw_mode_count;
} window_fit_config_t;

/* Resolves its sites, installs the detour and logs which branch it took, including the branch
 * where the feature is switched off, because a silently absent feature reads exactly like a
 * silently broken one. Returns true only when the window really follows the mode from now on. */
bool window_fit_install(const window_fit_config_t *config);

/* The game's own top-level window, or NULL before it exists. Cached, and re-enumerated when the
 * cached handle stops being a window.
 *
 * Exposed because the pointer confinement has to clip to exactly the window this module moves, and
 * this is where "which window is the game's" is already decided. Resolving it a second time
 * somewhere else would make it possible for the two answers to differ. */
HWND window_fit_game_window(void);

/* The size of the display mode that is REALLY set, read back through the engine's own accessor and
 * range-checked, or false when the accessor did not resolve or answered something implausible.
 *
 * Exposed for the same reason the window handle is: another feature in this DLL needs the current
 * mode size, and this module has already found the one accessor that reports it. Resolving it a
 * second time would mean two answers to one question, and the obvious second route, the pair of
 * width/height getters, is NOT usable: their shape (`push ebp / mov ebp,esp / mov eax,[abs32] /
 * pop ebp / ret`) occurs TWENTY-FIVE times in every shipped image, so a signature for it would be
 * picking one of twenty-five by position.
 *
 * Cheap enough to call once per frame: one call into the engine and two range tests. */
bool window_fit_current_mode_size(int *out_width, int *out_height);

/* ---- the monitor choice, exposed because it is the part that can be tested without a desktop ---
 * Which monitor a mode should be shown on is a policy decision with a default that was wrong once:
 * "the smallest monitor that still fits" sends a 640x480 window from a 2560x1440 primary onto a
 * 1920x1080 secondary, and a field log caught it doing exactly that (`monitor at -1920,0`). The
 * rule now prefers the monitor the window is already on, and this signature is what lets that be
 * checked without two physical displays. */
typedef struct window_fit_monitor {
    int32_t left;
    int32_t top;
    int32_t width;
    int32_t height;
} window_fit_monitor_t;

#define WINDOW_FIT_NO_MONITOR (-1)

/* `current` is the index of the monitor the window is on now, or WINDOW_FIT_NO_MONITOR when that
 * is not known. Returns an index into `monitors`, or WINDOW_FIT_NO_MONITOR when no monitor can
 * show the mode at all. */
int window_fit_choose_monitor(const window_fit_monitor_t *monitors, int count, int current,
                              uint32_t width, uint32_t height);

#endif /* WINDOW_FIT_H */
