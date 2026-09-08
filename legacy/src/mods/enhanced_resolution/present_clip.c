#include "present_clip.h"

#include "window_fit.h"

#include "common/import_patch.h"
#include "common/logging.h"

#include <windows.h>

/* The wrapper is a module in its own right, loaded by the ddraw.dll stub that sits beside the
 * game, so it has an import table of its own to correct. If a future build renames it, this
 * resolves to nothing and the feature reports itself off rather than misbehaving. */
#define WRAPPER_MODULE "dxwrapper.dll"
#define OWNER_MODULE   "user32.dll"
#define IMPORT_NAME    "GetClientRect"

typedef BOOL(WINAPI *get_client_rect_fn)(HWND, LPRECT);

static struct {
    get_client_rect_fn original;
    bool               armed;
    bool               reported_lie;
} clip_state;

/* True when the client area covers the whole of the rendered rectangle, which is the borderless
 * window at the monitor's origin. Nothing is clipped in that case and nothing needs correcting. */
static bool client_covers_render(const RECT *client, int render_width, int render_height)
{
    return client->left <= 0 && client->top <= 0 &&
           client->right >= render_width && client->bottom >= render_height;
}

/* True when the two do not touch at all, which already produces the whole-surface copy. */
static bool client_misses_render(const RECT *client, int render_width, int render_height)
{
    return client->left >= render_width || client->top >= render_height ||
           client->right <= 0 || client->bottom <= 0;
}

static BOOL WINAPI corrected_get_client_rect(HWND window, LPRECT rect)
{
    RECT  desktop_client;
    POINT origin = { 0, 0 };
    int   render_width = 0;
    int   render_height = 0;

    BOOL answered = clip_state.original(window, rect);
    if (!answered || rect == NULL || !clip_state.armed) {
        return answered;
    }
    if (window != window_fit_game_window()) {
        return answered;                     /* some other window of the process, not ours to touch */
    }
    if (!window_fit_current_mode_size(&render_width, &render_height) ||
        render_width <= 0 || render_height <= 0) {
        return answered;                     /* no mode yet, so there is nothing to compare against */
    }

    /* The wrapper does this same mapping immediately after this call returns, and compares the
     * result against a rectangle that begins at the origin. Doing it here is what lets this decide
     * which of the three cases the window is in. */
    if (!ClientToScreen(window, &origin)) {
        return answered;
    }
    desktop_client.left   = origin.x;
    desktop_client.top    = origin.y;
    desktop_client.right  = origin.x + (rect->right - rect->left);
    desktop_client.bottom = origin.y + (rect->bottom - rect->top);

    if (client_covers_render(&desktop_client, render_width, render_height) ||
        client_misses_render(&desktop_client, render_width, render_height)) {
        return answered;
    }

    rect->left   = 0;
    rect->top    = 0;
    rect->right  = 0;
    rect->bottom = 0;

    if (!clip_state.reported_lie) {
        clip_state.reported_lie = true;
        log_info("the window's client area overlaps the rendered rectangle in part, which is the "
                 "case the wrapper clips wrongly, so it is being given an empty client rectangle "
                 "and takes its whole-surface path instead. The picture fills the window from here. "
                 "Client %d,%d to %d,%d on the desktop; the engine renders %dx%d.",
                 (int)desktop_client.left, (int)desktop_client.top,
                 (int)desktop_client.right, (int)desktop_client.bottom,
                 render_width, render_height);
    }
    return answered;
}

void present_clip_set_enabled(bool enabled)
{
    if (clip_state.original == NULL) {
        return;                        /* never installed, so there is nothing to switch */
    }
    if (clip_state.armed != enabled) {
        clip_state.armed = enabled;
        clip_state.reported_lie = false;   /* so the next correction says so again */
        log_info("windowed fill is now %s", enabled ? "on" : "off");
    }
}

bool present_clip_install(const present_clip_config_t *config)
{
    void *original = NULL;

    if (config == NULL) {
        return false;
    }
    if (!config->enabled) {
        /* Says which of the two switched it off rather than naming a value the file may not hold.
         * The caller asks for this only when WindowedFill AND WindowedPresent are both on, so a
         * flat "WindowedFill=0" here was a lie whenever the device was the reason. */
        log_info("the windowed fill correction is not installed, because %s. It only ever applies "
                 "to a windowed device, and there it lets a window away from the screen's origin "
                 "show the whole picture instead of part of it stretched over the whole window.",
                 config->windowed_present ? "WindowedFill is off"
                                          : "the device is not windowed");
        return false;
    }

    if (!import_patch_replace(WRAPPER_MODULE, OWNER_MODULE, IMPORT_NAME,
                              (void *)corrected_get_client_rect, &original) ||
        original == NULL) {
        log_warning("%s's import of %s!%s could not be replaced, so a window away from the "
                    "screen's origin will still show only part of the picture. That module may "
                    "not be loaded, which is the case on a setup with no graphics wrapper at all, "
                    "and there is nothing to correct there.",
                    WRAPPER_MODULE, OWNER_MODULE, IMPORT_NAME);
        return false;
    }

    clip_state.original = (get_client_rect_fn)original;
    clip_state.armed    = true;
    log_info("windowed fill armed: %s now reads the client area through this module. Our own code "
             "still gets the truth from the same function, which focus_guard and window_fit both "
             "depend on. The correction applies only while the client area overlaps the rendered "
             "rectangle in part, which is the one case the wrapper gets wrong.",
             WRAPPER_MODULE);
    return true;
}
