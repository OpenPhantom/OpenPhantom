/* window_mode.h: let the player choose the shape of the window, instead of the one the engine picks.
 *
 * ==============================================================================================
 * The engine already switches window styles, and the one it picks is wrong on a modern display
 *
 * stdWin95_setDisplayMode at 0x00498C86 takes a mode selector and two sizes, and it holds both
 * style words already:
 *
 *     mode 0   GWL_STYLE = 0x10CF0000   WS_VISIBLE|WS_CAPTION|WS_SYSMENU|WS_THICKFRAME|
 *                                       WS_MINIMIZEBOX|WS_MAXIMIZEBOX, a real sizable window
 *     mode 1   GWL_STYLE = 0x10000000   WS_VISIBLE alone, no frame at all
 *
 * and then, in both arms, SetWindowPos(hwnd, NULL, 0, 0, w + borderW, h + borderH, 6), where 6 is
 * SWP_NOZORDER|SWP_NOMOVE, so the origin stays wherever the window was created.
 *
 * The retail path calls it exactly once, from main_openGraphics 0x0043F4D4 at 0x0043F542, as
 * setDisplayMode(1, 0x800, 0x800). Bytes at 0x0043F536:
 *
 *     68 00 08 00 00    push 0x800        height
 *     68 00 08 00 00    push 0x800        width
 *     6A 01             push 1            mode
 *     E8 3F 97 05 00    call 0x00498C86
 *
 * Two thousand and forty eight is not a resolution, it is a number large enough that in 1999 no
 * display could be bigger. So the window the game actually runs in is frameless at about 2054 by
 * 2077, anchored at (0,0), and covers the screen only by being larger than it. On a 2560 or 3840
 * wide desktop it does not cover it at all.
 *
 * The mode 0 arm is unreachable in retail. Its only callers are 0x0046AC7B and 0x0046AC8E inside
 * 0x0046AC63, whose one caller 0x0046A4F7 has no references. So nothing in a shipped run ever
 * gives this window a caption, and both style words below are ours to use.
 *
 * ==============================================================================================
 * Where the shape is applied, and why it is three places rather than one
 *
 * The style switch above is hooked, and the correction runs both before and after the original,
 * because it writes stdWin95_bWindowed and performs its own style and size change and none of that
 * should be skipped.
 *
 * That site alone is not enough. It fires once per session, and by the time it does the first
 * device already exists: the same function calls graphics_setResolution fifty one bytes earlier, at
 * 0x0043F50F, and that is where DirectDraw is first asked for anything. A window shaped only from
 * the later site is therefore shaped after the first device was built against the shape it
 * replaced. That was observed: a graphics wrapper reported the window as the full desktop at device
 * creation and our rectangle only later, and the movie player, which sizes its child window from
 * the client area, sized it from the same stale rectangle.
 *
 * So graphics_setMode at 0x0046BC85 is hooked as well, before and after, which is the site every
 * valid mode change passes through: eight callers reach it and only one of them is
 * graphics_setResolution. window_fit.c hooks the same address for the same reason and states it in
 * the same words. Two detours on one site is safe because common/detour.c chains.
 *
 * ==============================================================================================
 * What this does NOT do
 *
 * It changes the window and not the device. Whether the device owns the display is WindowedPresent,
 * in windowed_device.h, and the two are independent settings. With WindowedPresent off the device is
 * still exclusive fullscreen, so a borderless window here is a borderless window over a device that
 * owns the screen, and alt-tab still costs a reset and a texture re-upload.
 */
#ifndef WINDOW_MODE_H
#define WINDOW_MODE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum window_mode_kind {
    /* Leave the engine alone entirely, which is the default. The window keeps the frameless
     * oversized shape described above, exactly as every previous release shipped it. */
    WINDOW_MODE_AUTHENTIC = 0,

    /* Frameless, at the true size of the monitor the window is on, positioned at that monitor's
     * origin. This is what most players mean by fullscreen on a modern desktop, and it is the one
     * mode that makes the window match the screen rather than overhang it. */
    WINDOW_MODE_BORDERLESS = 1,

    /* A caption and a border, centred on the monitor the window is on. The size is the display
     * mode by default, so nothing is scaled, and WindowedWidth/WindowedHeight override it.
     *
     * IT IS REFUSED WHEN WindowedPresent IS ON, and the reason is in the config field below: a
     * windowed device's surfaces are the desktop's size, and the presentation is clipped to the
     * client area, so a window smaller than the desktop shows only part of the picture. The two
     * settings are each correct alone and cannot both be had at once.
     *
     * WS_THICKFRAME is deliberately NOT set, although the engine's own mode 0 word sets it. The
     * window procedure at 0x0049905E handles neither WM_SIZE nor WM_PAINT, the class style word at
     * 0x00498F74 is a literal zero so there is no CS_HREDRAW or CS_VREDRAW, and the engine
     * publishes its render size once per mode set. A window the player can drag-resize would
     * therefore change nothing about what is rendered and repaint nothing while it happened. */
    WINDOW_MODE_WINDOWED = 2,

    WINDOW_MODE_COUNT
} window_mode_kind_t;

typedef struct window_mode_config {
    window_mode_kind_t mode;

    /* The client size for WINDOW_MODE_WINDOWED. Zero means "the display mode", which is the size
     * the engine is actually rendering, so the window shows it one pixel for one pixel. */
    int32_t windowed_width;
    int32_t windowed_height;

    /* Whether the device is being built windowed, which is WindowedPresent in the same section.
     * It is here only so that WINDOW_MODE_WINDOWED can refuse itself, and the reason is measured
     * rather than defensive. A device asked for DDSCL_NORMAL takes the DESKTOP as its primary
     * surface, and the engine's back buffer comes off that primary's flip chain, so both surfaces
     * are the desktop's size whatever resolution the game renders at. The presentation is then
     * clipped to the window's client area, so a window smaller than the desktop receives only that
     * much of the picture and the rest of the window is left black. Measured at 1600x900 on a
     * 3840x2160 desktop: the picture arrived at 1600/3840 of the window, in the corner. */
    bool windowed_present;
} window_mode_config_t;

/* Resolves the site, installs the detour and logs which branch it took, including the branch where
 * the feature is switched off, because a silently absent feature reads exactly like a silently
 * broken one. Returns true only when the window really follows the setting from now on. */
bool window_mode_install(const window_mode_config_t *config);

/* ---- the geometry, exposed because it is the part that can be tested without a desktop --------
 * Everything this feature decides is in one function: given a mode, a monitor rectangle and the
 * size the engine is rendering, what client rectangle should the window have. The rest is
 * SetWindowLong and SetWindowPos, which cannot be tested without a window. */
typedef struct window_mode_rect {
    int32_t left;
    int32_t top;
    int32_t width;
    int32_t height;
} window_mode_rect_t;

/* Returns false when the mode wants nothing done, which is WINDOW_MODE_AUTHENTIC and any monitor
 * or mode size that is not positive. `out` is untouched in that case. */
bool window_mode_client_rect(window_mode_kind_t mode, const window_mode_rect_t *monitor,
                             int32_t mode_width, int32_t mode_height,
                             int32_t wanted_width, int32_t wanted_height,
                             window_mode_rect_t *out);

/* The style word a mode wants, or 0 for WINDOW_MODE_AUTHENTIC, which is the signal to leave
 * GWL_STYLE exactly as the engine set it. */
uint32_t window_mode_style(window_mode_kind_t mode);

#endif /* WINDOW_MODE_H */
