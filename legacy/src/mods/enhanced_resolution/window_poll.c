#include "window_poll.h"

#include "pointer_release.h"
#include "present_clip.h"
#include "fullscreen_toggle.h"
#include "mode_filter.h"
#include "window_close.h"
#include "windowed_device.h"
#include "window_mode.h"

#include "common/frame_hook.h"
#include "common/host_image.h"
#include "common/ini.h"
#include "common/logging.h"


#include <stdio.h>

#include <windows.h>

#define RESOLUTION_SECTION "enhanced_resolution"

/* At sixty frames a second this is a read a second, and the interval is deliberately not derived
 * from a clock: a frame counter cannot drift with the frame rate into reading far more often than
 * intended, it just reads less often on a slower machine, which is the harmless direction. */
#define POLL_INTERVAL_FRAMES 60u

static struct {
    window_poll_config_t last;
    bool                 last_windowed_present;
    bool                 device_windowed;   /* what the device was BUILT as; never changes */
    uint32_t             countdown;
    bool                 armed;
} poll_state;

void window_poll_note_mode(int32_t mode)
{
    poll_state.last.mode = mode;
}

bool window_poll_shape_is_live(int32_t mode)
{
    return poll_state.device_windowed && mode != (int32_t)WINDOW_MODE_AUTHENTIC;
}

/* The chosen size is also the render size, from the next start.
 *
 * Borderless fullscreen is always the monitor, and a windowed mode is always the size chosen from
 * the panel's list. Both are DELIBERATE sizes, so this is safe where following a drag was not:
 * a drag lands on wherever the mouse was let go, and 2452x1401 is not a display mode.
 *
 * A drag is left alone entirely, and that includes leaving the chosen size alone. The window
 * changes, the picture stretches to fill it, and the resolution stays where the last deliberate
 * choice put it. A drag used to overwrite the chosen size, which made sense while that setting
 * meant "how big the window is"; once it became "the resolution you picked" the same write
 * silently replaced a choice with a mouse gesture, and this file then had no idea anything had
 * changed.
 *
 * Refused rather than approximated when the size is not one this machine reports. Nothing here can
 * make the engine open a mode at startup that no display offers, and writing one anyway is how a
 * working installation was turned into "could not initialize graphics hardware". The panel only
 * offers real modes, so this should now be unreachable from there; it stays because the setting can
 * still be edited by hand. */
#define ENGINE_INI_NAME "obi.ini"
#define ENGINE_SECTION  "options"

static void align_render_size(int32_t mode, int32_t wanted_width, int32_t wanted_height)
{
    char        path[MAX_PATH];
    char        text[16];
    const char *directory = host_directory();
    int32_t     width     = 0;
    int32_t     height    = 0;

    if (directory == NULL ||
        !window_mode_wanted_client_size(mode, wanted_width, wanted_height, &width, &height) ||
        width <= 0 || height <= 0) {
        return;                  /* the engine's own shape, or nothing measurable to agree with */
    }
    if (!mode_filter_has_mode(width, height)) {
        /* Told apart because the two want different things done about them. A size the machine
         * simply does not offer is the reader's to change; a size that shrank on the way here did
         * so because the window's own frame does not fit beside it, and no entry in the panel's
         * list can help with that. */
        if (width < wanted_width || height < wanted_height) {
            log_info("%dx%d does not fit on this monitor once the window's border and caption are "
                     "added, so the window is %dx%d instead, not a mode this machine "
                     "reports. The game's own resolution is left where it is and the picture is "
                     "scaled into the window. A borderless window mode has no frame and can show "
                     "the whole screen at its own size.",
                     (int)wanted_width, (int)wanted_height, (int)width, (int)height);
        } else {
            log_info("%dx%d is the size this window mode asks for, but it is not a mode this "
                     "machine reports, so the game's own resolution is left where it is and the "
                     "picture is scaled into the window instead. Choose a size from the panel's "
                     "own list and the two will match.", (int)width, (int)height);
        }
        return;
    }
    if (_snprintf(path, sizeof path, "%s\\%s", directory, ENGINE_INI_NAME) < 0) {
        return;
    }
    path[sizeof path - 1] = 0;
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        return;
    }
    if ((int32_t)GetPrivateProfileIntA(ENGINE_SECTION, "screen_width", 0, path) == width &&
        (int32_t)GetPrivateProfileIntA(ENGINE_SECTION, "screen_height", 0, path) == height) {
        return;
    }

    _snprintf(text, sizeof text, "%d", (int)width);
    text[sizeof text - 1] = 0;
    (void)WritePrivateProfileStringA(ENGINE_SECTION, "screen_width", text, path);
    _snprintf(text, sizeof text, "%d", (int)height);
    text[sizeof text - 1] = 0;
    (void)WritePrivateProfileStringA(ENGINE_SECTION, "screen_height", text, path);

    log_info("the game will render at %dx%d from the next start, to match the window this mode "
             "asks for. It is not changed now: the engine rebuilds its Direct3D device to change "
             "resolution, and this patch does not start one of those from inside a frame.",
             (int)width, (int)height);
}

static void poll_window_settings(void)
{
    window_poll_config_t now;

    if (!poll_state.armed) {
        return;
    }

    /* Every frame, unlike everything below it. A key that answers within a second is a key that
     * reads as broken, so the press is looked for at the frame rate and only the file reads are
     * spared. */
    fullscreen_toggle_poll();
    window_close_poll();
    if (poll_state.countdown != 0u) {
        poll_state.countdown--;
        return;
    }
    poll_state.countdown = POLL_INTERVAL_FRAMES;

    now.mode                = ini_read_int (RESOLUTION_SECTION, "WindowMode", poll_state.last.mode);
    now.windowed_width      = ini_read_int (RESOLUTION_SECTION, "WindowedWidth",
                                            poll_state.last.windowed_width);
    now.windowed_height     = ini_read_int (RESOLUTION_SECTION, "WindowedHeight",
                                            poll_state.last.windowed_height);
    now.pointer_release_key = ini_read_int (RESOLUTION_SECTION, "PointerReleaseKey",
                                            poll_state.last.pointer_release_key);
    now.windowed_fill       = ini_read_bool(RESOLUTION_SECTION, "WindowedFill",
                                            poll_state.last.windowed_fill);

    /* The three shape settings are applied together even when only one of them moved, because the
     * shape is one answer computed from all three and applying half of it would put the window
     * somewhere neither the old settings nor the new ones asked for. */
    if (now.mode != poll_state.last.mode ||
        now.windowed_width != poll_state.last.windowed_width ||
        now.windowed_height != poll_state.last.windowed_height) {
        if (window_poll_shape_is_live(now.mode)) {
            log_info("a window setting changed while the game was running: mode %d, size %dx%d",
                     (int)now.mode, (int)now.windowed_width, (int)now.windowed_height);
            (void)window_mode_reapply(now.mode, now.windowed_width, now.windowed_height);
        } else {
            log_info("window mode %d is written and will be used from the next start. It is not "
                     "applied now because the device is built once, at startup, and this shape "
                     "needs a device this run does not have.", (int)now.mode);
        }
        align_render_size(now.mode, now.windowed_width, now.windowed_height);
    }
    if (now.pointer_release_key != poll_state.last.pointer_release_key) {
        (void)pointer_release_set_key(now.pointer_release_key);
    }
    if (now.windowed_fill != poll_state.last.windowed_fill) {
        present_clip_set_enabled(now.windowed_fill);
    }

    /* Watched but never applied. The device is built once, so this cannot change what is running;
     * what it can do is have the wrapper's file already saying the right thing by the time the
     * player restarts, as the panel's row tells them to do. */
    {
        bool present_now = ini_read_bool(RESOLUTION_SECTION, "WindowedPresent", false);

        if (present_now != poll_state.last_windowed_present) {
            poll_state.last_windowed_present = present_now;
            windowed_device_align_wrapper(present_now);
        }
    }

    poll_state.last = now;
}

bool window_poll_install(const window_poll_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    poll_state.last      = *config;
    poll_state.last_windowed_present =
        ini_read_bool(RESOLUTION_SECTION, "WindowedPresent", false);
    poll_state.device_windowed = poll_state.last_windowed_present;
    poll_state.countdown = POLL_INTERVAL_FRAMES;
    poll_state.armed     = true;

    if (!frame_hook_add(poll_window_settings)) {
        poll_state.armed = false;
        log_warning("the frame hook was refused, so window settings changed in the dev panel will "
                    "only take effect the next time the game starts");
        return false;
    }
    /* Once at startup as well as on every change, so a file that has never been aligned is put
     * right on the first run rather than only when the setting is next touched. */
    windowed_device_align_wrapper(poll_state.last_windowed_present);
    (void)fullscreen_toggle_install();
    (void)window_close_install();
    log_info("window settings are re-read from the ini about once a second, so the dev panel's "
             "Window group takes effect while the game runs. WindowedPresent is not among them: "
             "the device is built once at startup.");
    return true;
}
