/* background_input.h: the right stick, Start and the triggers for a window that is not in front.
 *
 * Everything else in this DLL reaches the game through SendInput, which goes to whichever window
 * has the focus. That is right for one copy of the game and wrong for two side by side on one
 * screen, where the copy behind the other one never sees its own pad's right stick, Start or
 * triggers. This answers the game from inside its own process instead, so focus does not matter:
 *
 *   look   the right stick's counts are added to the engine's relative axis read (action 0), the
 *          read the steer takes mouse turning from, once per substep;
 *   roll   Alt held and Left or Right tapped are written into the engine's key tables after its
 *          keyboard poll, which is where DirectInput's keys land when the window is in front;
 *   pause  Escape is posted to the game's own window, the WM_KEYDOWN the pause hotkey handler takes.
 *
 * Only used while the window is NOT in front, so the window in front keeps the SendInput path it
 * was field tested with. Off unless [controller_input] BackgroundInput=1.
 */
#ifndef CONTROLLER_INPUT_BACKGROUND_INPUT_H
#define CONTROLLER_INPUT_BACKGROUND_INPUT_H

#include <windows.h>
#include <xinput.h>

#include <stdbool.h>

typedef struct background_settings {
    bool  look_enabled;
    bool  pause_enabled;
    bool  roll_enabled;
    float deadzone;
    float look_sensitivity;
    int   trigger_threshold;
    DWORD escape_hold_ms;
    DWORD roll_tap_down_ms;
    DWORD roll_tap_period_ms;
} background_settings_t;

/* Resolves the keyboard poll, its key tables and the relative axis read, and detours the two
 * functions. Each half stands alone: a build where one does not resolve keeps the other. False
 * when neither stands. */
bool background_input_install(void);

/* One poll of the pad while the window is behind another. Called on the polling thread; the hooks
 * on the game's thread pick the result up. */
void background_input_poll(const XINPUT_GAMEPAD *pad, double dt,
                           const background_settings_t *settings);

/* The window came to the front, or the pad went away: every key this put down goes up on the
 * next keyboard poll, owed look is dropped, and the edges start again from what the pad holds. */
void background_input_release(const XINPUT_GAMEPAD *pad);

#endif /* CONTROLLER_INPUT_BACKGROUND_INPUT_H */
