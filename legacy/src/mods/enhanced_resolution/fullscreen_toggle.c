#include "fullscreen_toggle.h"

#include "window_mode.h"
#include "window_poll.h"

#include "common/ini.h"
#include "common/logging.h"

#include <windows.h>

#define RESOLUTION_SECTION "enhanced_resolution"

/* Enter. Held with Alt, which is not a setting: see the header. */
#define TOGGLE_KEY_DEFAULT 0x0Du

static struct {
    int32_t key;
    int32_t remembered_mode;      /* what to come back to, when the screen is what we are on */
    bool    key_was_down;
    bool    armed;
} toggle_state;

/* Pressed means the key is down AND Alt is down. Tested as a pair on every frame rather than as a
 * sequence, so it does not matter which of the two went down first. */
static bool combination_is_down(void)
{
    return (GetAsyncKeyState(toggle_state.key) & 0x8000) != 0 &&
           (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
}

void fullscreen_toggle_poll(void)
{
    bool    down;
    int32_t current;
    int32_t target;

    if (!toggle_state.armed) {
        return;
    }

    down = combination_is_down();
    if (down == toggle_state.key_was_down) {
        return;
    }
    toggle_state.key_was_down = down;
    if (!down) {
        return;                                   /* act on the press, not on the release */
    }

    current = ini_read_int(RESOLUTION_SECTION, "WindowMode", 0);
    if (current == (int32_t)WINDOW_MODE_BORDERLESS) {
        target = toggle_state.remembered_mode;
    } else {
        /* Remembered before it is replaced, so pressing this twice returns the mode that was
         * actually set up rather than a guess at which window mode was meant. */
        toggle_state.remembered_mode = current;
        target = (int32_t)WINDOW_MODE_BORDERLESS;
    }

    /* The device goes with the mode, and by the same rule the dev panel's rows use: a window needs
     * the windowed device, because without it the device is exclusive and owns the screen, and
     * fullscreen wants it off because an exclusive device is what fullscreen is. Writing only the
     * mode here is what the first build did, and from the shipped fullscreen state it produced a
     * borderless window the size of the monitor sitting over a device still holding an exclusive
     * mode of a different size. */
    if (!ini_write_int(RESOLUTION_SECTION, "WindowMode", target) ||
        !ini_write_int(RESOLUTION_SECTION, "WindowedPresent",
                       (target == (int32_t)WINDOW_MODE_AUTHENTIC) ? 0 : 1)) {
        log_warning("the window settings could not be written, so Alt+Enter changed the window but "
                    "the next start will not remember it");
    }

    /* Applied here rather than left for the poll to notice, because a second of nothing after a
     * keypress reads as a key that did not work. The poll's own shadow is then told what happened,
     * so it does not see a change it did not make and apply it a second time. */
    if (window_poll_shape_is_live(target)) {
        (void)window_mode_reapply(target,
                                  ini_read_int(RESOLUTION_SECTION, "WindowedWidth", 0),
                                  ini_read_int(RESOLUTION_SECTION, "WindowedHeight", 0));
    }
    window_poll_note_mode(target);

    log_info("Alt and the toggle key: window mode %d, coming back to %d",
             (int)target, (int)toggle_state.remembered_mode);
}

bool fullscreen_toggle_install(void)
{
    int32_t start_mode;

    toggle_state.key = ini_read_int(RESOLUTION_SECTION, "FullscreenToggleKey",
                                    (int32_t)TOGGLE_KEY_DEFAULT);
    if (toggle_state.key <= 0 || toggle_state.key > 0xFF) {
        log_info("FullscreenToggleKey=%d, so there is no key that swaps between a window and the "
                 "whole screen. The dev panel's Window group still does it.",
                 (int)toggle_state.key);
        return false;
    }

    /* Where to come back to when the game is already covering the screen and this is pressed for
     * the first time. A window with a caption, because that is the one a player can move, resize
     * from and close, and because coming back to the engine's own oversized frameless shape would
     * look like the toggle had done nothing at all. */
    start_mode = ini_read_int(RESOLUTION_SECTION, "WindowMode", 0);
    toggle_state.remembered_mode = (start_mode == (int32_t)WINDOW_MODE_BORDERLESS ||
                                    start_mode == (int32_t)WINDOW_MODE_AUTHENTIC)
                                       ? (int32_t)WINDOW_MODE_WINDOWED
                                       : start_mode;

    toggle_state.key_was_down = combination_is_down();
    toggle_state.armed        = true;
    log_info("Alt and virtual key 0x%02X swap between a window and the whole monitor. It changes "
             "how much of the screen the game covers and not what it renders, so it costs nothing "
             "either way.", (unsigned)toggle_state.key);
    return true;
}
