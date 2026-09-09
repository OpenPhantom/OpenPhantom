/* window_mode.h: let the player choose the shape of the window, instead of the one the engine
 * picks.
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
 * 0x0043F50F, where DirectDraw is first asked for anything. A window shaped only from
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
 * It changes the window and not the device. Whether the device owns the display is
 * WindowedPresent, in windowed_device.h, and the two are independent settings. With
 * WindowedPresent off the device is still exclusive fullscreen, so a borderless window here is a
 * borderless window over a device that owns the screen, and alt-tab still costs a reset and a
 * texture re-upload.
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
     * An earlier build refused this whenever WindowedPresent was on, because the surfaces were
     * measured at the desktop's size whatever the game rendered. That turned out to belong to the
     * graphics wrapper rather than to the device, and one of its settings decides it; the pairing
     * works and the refusal is gone. See window_poll.h for what is left of the constraint.
     *
     * No resize grip here, which is the only difference between this mode and the next one. The
     * window procedure at 0x0049905E handles neither WM_SIZE nor WM_PAINT and the class style word
     * at 0x00498F74 is a literal zero, so a dragged edge tells the engine nothing. That is a reason
     * to leave the grip off a window whose picture is drawn at a fixed size, and it stopped being a
     * reason once the picture began being scaled to the client area at present time. */
    WINDOW_MODE_WINDOWED = 2,

    /* The same window, with a frame the player can drag to any size, and a maximise button.
     *
     * The note above says WS_THICKFRAME is deliberately left out, and until the present started
     * scaling that was right: the window procedure handles no WM_SIZE, the class word at 0x00498F74
     * is a literal zero so there is no CS_HREDRAW, and the engine publishes its render size once
     * per mode set, so a dragged edge changed nothing about what was drawn. What changed is
     * underneath. With WindowedPresent the picture is scaled to the client area at present time,
     * so the client can be any size and the engine neither knows nor needs to: it keeps rendering
     * its own mode and the result is stretched to fit. The engine is still not told, and still
     * does not need to be.
     *
     * The picture will hold still while a drag is in progress, because Windows runs a modal loop
     * inside the drag and the game's own loop is not being serviced. It catches up when the mouse
     * is let go. */
    WINDOW_MODE_RESIZABLE = 3,

    /* No frame at all, at a chosen size rather than the whole monitor. What a borderless window
     * usually means outside this project, and the mode to use for a borderless picture that is not
     * meant to cover the screen. There is no frame to grab, so moving it needs the pointer release
     * key and a drag on the picture is not one: use the keyboard, or one of the modes with a
     * caption, if the window has to be moved often. */
    WINDOW_MODE_BORDERLESS_SIZED = 4,

    WINDOW_MODE_COUNT
} window_mode_kind_t;

typedef struct window_mode_config {
    window_mode_kind_t mode;

    /* The client size for WINDOW_MODE_WINDOWED. Zero means "the display mode", which is the size
     * the engine is actually rendering, so the window shows it one pixel for one pixel. */
    int32_t windowed_width;
    int32_t windowed_height;

    /* Whether the device is being built windowed, which is WindowedPresent in the same section.
     * Read here only so that a sized mode can say in the log what its picture depends on: the
     * graphics wrapper clips what it presents against the window's client rectangle, and only a
     * window that covers the rendered rectangle, or misses it entirely, receives the whole picture
     * without help. present_clip.c is the help, and it is on by default. */
    bool windowed_present;
} window_mode_config_t;

/* Resolves the site, installs the detour and logs which branch it took, including the branch where
 * the feature is switched off, because a silently absent feature reads exactly like a silently
 * broken one. Returns true only when the window really follows the setting from now on. */
bool window_mode_install(const window_mode_config_t *config);

/* Changes the shape while the game is running, as the dev panel's Window group does.
 * Only the three things a player can choose are settable: whether the device is windowed is not,
 * because that is decided once when the device is built.
 *
 * Returns false when nothing was installed to begin with, so a caller can tell a refused change
 * from one that had no effect. Applying the same values again is allowed and does nothing
 * visible, so a poll can call it without comparing first. */
bool window_mode_reapply(int32_t mode, int32_t windowed_width, int32_t windowed_height);

/* The client size a mode would ask for, answered against the monitor the window is really on and
 * the display mode really in force. This is the same arithmetic the window itself is shaped by,
 * exposed so the RENDER size can be made to agree with it: a window at 1280x720 showing a 4K
 * picture wastes most of a GPU to draw an interface too small to read, and a borderless window
 * filling a 4K monitor with a 640x480 picture is a blur.
 *
 * False for the engine's own shape, which has no size of ours to agree with, and whenever the
 * window or the mode cannot be measured. */
bool window_mode_wanted_client_size(int32_t mode, int32_t windowed_width,
                                    int32_t windowed_height,
                                    int32_t *out_width, int32_t *out_height);

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

/* Shrinks a client rectangle until the WINDOW AROUND IT fits the monitor, and recentres it.
 *
 * `frame_width` and `frame_height` are the pixels the border and caption add to a client, which is
 * zero for the borderless modes and about 22 by 56 for a framed one at the usual metrics.
 *
 * This is a separate step from window_mode_client_rect on purpose. That function answers what size
 * the reader asked for, and it is the same answer whatever frame the mode ends up with; this one
 * answers what will actually fit, and it needs a number that can only be measured from a live
 * window. Keeping them apart is also what lets the second be checked without one.
 *
 * WHY IT MATTERS BEYOND THE WINDOW LOOKING RIGHT. The size that survives this is the size written
 * into the game's own settings file as the resolution to render at. Without it, choosing the
 * monitor's own resolution in a framed mode wrote a render size the window could never be: the
 * outer window came out larger than the screen, its caption above the top of it, and the picture
 * and the window disagreed by the width of the frame from then on.
 *
 * False when the frame alone fills the monitor, which is not a window anybody can use, and then
 * the rectangle is left exactly as it arrived. */
bool window_mode_fit_frame(const window_mode_rect_t *monitor,
                           int32_t frame_width, int32_t frame_height,
                           window_mode_rect_t *client);

/* The style word a mode wants, or 0 for WINDOW_MODE_AUTHENTIC, which is the signal to leave
 * GWL_STYLE exactly as the engine set it. */
uint32_t window_mode_style(window_mode_kind_t mode);

#endif /* WINDOW_MODE_H */
