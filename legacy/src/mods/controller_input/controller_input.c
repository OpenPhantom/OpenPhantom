/* controller_input.c: see controller_input.h for why this exists and why it works this way.
 *
 * No signature, no detour, no patch, no engine memory touched anywhere in this file. Reads the pad
 * through XInputGetState, Microsoft's own API, not Xidi and not WinMM. Writes out through
 * SendInput, the same mechanism any external macro or accessibility tool uses. Both ends are plain
 * Win32, on this DLL's own dedicated background thread rather than common/frame_hook.h's usual
 * once-per-rendered-frame site.
 *
 * THAT IS A DELIBERATE DEPARTURE FROM THIS PROJECT'S USUAL PATTERN, AND HERE IS WHY. The first
 * build of this feature used frame_hook, like every other per-frame need in this tree. Look worked
 * immediately. Skipping a playing movie with Start never did, on any test. The reason: fmv_player's
 * own movie playback (vlc_playback.c) runs a dedicated `for (;;)` pump loop on the game's own
 * thread that does not call sys_frame/render_frameEnd at all while a movie plays, so frame_hook's
 * site never fires for the whole duration, and this DLL never got a chance to run. A real keyboard
 * Escape press still worked during a movie, because fmv_player's own skip check
 * (GetAsyncKeyState(VK_ESCAPE)) reads global OS keyboard state, which does not depend on which loop
 * the game's thread happens to be parked in. A background thread gives this DLL that same
 * independence: it keeps polling and keeps able to call SendInput no matter what the game's own
 * thread is doing, including inside fmv_player's pump loop, a blocking menu, or anything else.
 */
#include "controller_input.h"

#include "common/ini.h"
#include "common/logging.h"

#include <windows.h>
#include <xinput.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#pragma comment(lib, "xinput.lib")

#define CONTROLLER_SECTION "controller_input"

/* Ordinary poll cadence once a pad has been seen. Independent of the game's own frame rate on
 * purpose, this thread has nothing to do with rendering. 125 Hz is comfortably below what a human
 * can perceive as latency and comfortably above what would ever feel like polling too slowly. */
#define POLL_INTERVAL_MS 8u

/* XInputGetState is documented to cost noticeably more when the requested slot is NOT connected,
 * because the runtime re-scans for hardware on every such call rather than answering from a cached
 * state. Polling an empty slot at the ordinary cadence would reintroduce a smaller version of
 * exactly the polling cost this DLL exists to remove elsewhere. While no pad has ever been seen,
 * this checks only once every DISCONNECTED_POLL_INTERVAL_MS instead; the instant one is found, the
 * ordinary cadence begins and stays on for the rest of the session. */
#define DISCONNECTED_POLL_INTERVAL_MS 500u

/* How long a synthesized Escape is held down before its release is sent. A down and an up in the
 * same SendInput call opened the pause menu fine (that path is a plain WM_KEYDOWN dispatch) but did
 * not close it again and did not skip a playing movie: closing the menu goes through
 * TranslateMessage producing WM_CHAR, and fmv_player's own skip check polls GetAsyncKeyState, and
 * both want the key genuinely observed as held rather than a down and up collapsed into the same
 * instant. A run of one game frame's worth was the minimum that fixed the menu; this is measured in
 * real milliseconds instead, since this thread has nothing to do with frames any more. */
#define ESCAPE_HOLD_MS 60u

/* Radial deadzone, applied to the magnitude of the stick vector rather than per axis, which is what
 * Microsoft's own XInput documentation recommends: a per-axis deadzone leaves a square dead region
 * that still lets a small diagonal push through on both axes at once. 0.24 is close to
 * XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE (8689 of 32767, about 0.265) rounded to a plainer default. */
#define DEFAULT_DEADZONE 0.24f

/* Counts per second of synthesized relative mouse movement at full stick deflection, in the same
 * units enhanced_input.dll's own MouseDegreesPerCount scales from. Chosen so that, at that
 * feature's own default of 0.050 degrees per count, full deflection turns at 200 degrees per
 * second, brisk but controllable; independent of and not read from enhanced_input's own
 * configuration, since this DLL does not depend on that one at run time. */
#define DEFAULT_LOOK_SENSITIVITY 4000.0f

/* How far a trigger has to travel before it counts as pressed, matching Microsoft's own
 * XINPUT_GAMEPAD_TRIGGER_THRESHOLD (30 of 255). Below this a trigger at rest, which is not
 * perfectly zero on real hardware, would not chatter the roll keys on and off. */
#define DEFAULT_TRIGGER_THRESHOLD 30

/* Real play is "hold Alt, then TAP Left or Right", not hold the direction key down; a held
 * direction key produced a diagonal drift rather than a clean roll, field-reported and reproduced
 * on the first version of this feature, which held it for as long as the trigger stayed pulled.
 * A trigger held down is reproduced as repeated taps instead: one immediately on the first poll a
 * trigger crosses the threshold, then one more every ROLL_TAP_PERIOD_MS for as long as it stays
 * past it, each tap held down for ROLL_TAP_DOWN_MS before its release, the same shape
 * synthesize_pause_press already uses for Escape. */
#define ROLL_TAP_DOWN_MS   50u
#define ROLL_TAP_PERIOD_MS 150u

typedef struct controller_input_config {
    bool  enabled;
    bool  look_enabled;
    bool  pause_enabled;
    bool  roll_enabled;
    int   controller_index;
    float deadzone;
    float look_sensitivity;
    int   trigger_threshold;
} controller_input_config_t;

typedef struct controller_input_state {
    bool                       installed;
    controller_input_config_t config;

    HANDLE thread;
    bool   pad_connected;

    LARGE_INTEGER qpc_frequency;
    LARGE_INTEGER last_tick;
    bool          have_last_tick;

    bool start_was_down;

    /* Roll: the game's own binding, confirmed directly from its options screen rather than
     * assumed, is Left Alt or Right Alt held plus the Left or Right arrow key TAPPED, not held.
     * Alt is shared between both triggers and is pressed once on the first trigger to engage and
     * released once the last one disengages, rather than once per trigger, so pulling both at
     * once does not send two Alt-down events. Each side tracks whether its trigger was engaged on
     * the previous poll and, while it stays engaged, when its next tap is due. */
    bool      alt_held;
    bool      roll_left_was_engaged;
    bool      roll_right_was_engaged;
    ULONGLONG roll_left_next_tap_tick;
    ULONGLONG roll_right_next_tap_tick;

    double remainder_x;  /* fractional synthesized mouse counts carried across polls */
    double remainder_y;
} controller_input_state_t;

static controller_input_state_t ci_state;

/* The magnitude-scaled stick vector, deadzone already applied, both axes in [-1, 1]. Returns false
 * (and leaves the two outputs untouched) when the stick is inside the deadzone. */
static bool apply_radial_deadzone(SHORT raw_x, SHORT raw_y, float deadzone, float *out_x,
                                  float *out_y)
{
    float x = (float)raw_x / 32767.0f;
    float y = (float)raw_y / 32767.0f;
    float magnitude = (float)sqrt((double)(x * x + y * y));
    float scaled;

    if (magnitude < deadzone) {
        return false;
    }
    if (magnitude > 1.0f) {
        magnitude = 1.0f;
    }
    scaled = (magnitude - deadzone) / (1.0f - deadzone);
    *out_x = (x / magnitude) * scaled;
    *out_y = (y / magnitude) * scaled;
    return true;
}

static double seconds_since_last_poll(void)
{
    LARGE_INTEGER now;
    double        elapsed;

    QueryPerformanceCounter(&now);
    if (!ci_state.have_last_tick) {
        ci_state.last_tick = now;
        ci_state.have_last_tick = true;
        return 0.0;
    }
    elapsed = (double)(now.QuadPart - ci_state.last_tick.QuadPart) /
              (double)ci_state.qpc_frequency.QuadPart;
    ci_state.last_tick = now;

    /* A long stall (the disconnected poll interval, a debugger break, the machine sleeping)
     * produces a huge gap; treat it as one ordinary poll rather than firing a single enormous,
     * disorienting look turn when polling resumes. */
    if (elapsed > 0.25) {
        elapsed = 0.0;
    }
    return elapsed;
}

static void synthesize_look(float x, float y, double dt)
{
    double desired_x;
    double desired_y;
    LONG   send_x;
    LONG   send_y;
    INPUT  input;

    if (dt <= 0.0) {
        return;
    }

    /* Y is inverted here on purpose: XInput's right stick reports +1 as "up", and pushing the
     * stick up is meant to look up, which is a negative (upward) mouse movement on screen. */
    desired_x = (double)x * (double)ci_state.config.look_sensitivity * dt + ci_state.remainder_x;
    desired_y = (double)(-y) * (double)ci_state.config.look_sensitivity * dt + ci_state.remainder_y;

    send_x = (LONG)desired_x;
    send_y = (LONG)desired_y;
    ci_state.remainder_x = desired_x - (double)send_x;
    ci_state.remainder_y = desired_y - (double)send_y;

    if (send_x == 0 && send_y == 0) {
        return;
    }

    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_MOUSE;
    input.mi.dx = send_x;
    input.mi.dy = send_y;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    (void)SendInput(1, &input, sizeof(INPUT));
}

/* Blocks this thread, and only this thread, for ESCAPE_HOLD_MS. Nothing else in the process is
 * waiting on this thread for anything, so a several-millisecond pause here is invisible to the
 * player and does not delay the game, fmv_player's own pump loop, or look input for any long. */
static void synthesize_pause_press(void)
{
    INPUT input;

    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_ESCAPE;
    (void)SendInput(1, &input, sizeof(INPUT));

    Sleep(ESCAPE_HOLD_MS);

    input.ki.dwFlags = KEYEVENTF_KEYUP;
    (void)SendInput(1, &input, sizeof(INPUT));
}

/* Whether this game's movement/roll keys are read through WM_KEYDOWN, GetAsyncKeyState or
 * DirectInput's own polled keyboard state has not been confirmed the way Escape's path was; this
 * game's use of a dinput.dll loader in the first place is evidence DirectInput reads at least some
 * of its input, which is a real, different question from the two paths Escape was proven to
 * reach. Field-test before trusting this. */
static void set_alt_held(bool want_held)
{
    INPUT input;

    if (want_held == ci_state.alt_held) {
        return;
    }
    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_MENU;
    if (!want_held) {
        input.ki.dwFlags = KEYEVENTF_KEYUP;
    }
    (void)SendInput(1, &input, sizeof(INPUT));
    ci_state.alt_held = want_held;
}

/* One tap: down, held for ROLL_TAP_DOWN_MS, then up, blocking this thread only, the same shape
 * synthesize_pause_press already uses for Escape. The arrow keys are extended keys on a real
 * keyboard (the block they share a physical position with is the numeric keypad), and
 * KEYEVENTF_EXTENDEDKEY is what tells the receiving code which one a synthesized press means, the
 * same way a real keyboard's own scan code would. */
static void synthesize_roll_tap(WORD vk)
{
    INPUT input;

    ZeroMemory(&input, sizeof(input));
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
    (void)SendInput(1, &input, sizeof(INPUT));

    Sleep(ROLL_TAP_DOWN_MS);

    input.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
    (void)SendInput(1, &input, sizeof(INPUT));
}

/* Fires the first tap the instant a trigger crosses the threshold, then one more every
 * ROLL_TAP_PERIOD_MS for as long as it stays past it. Releasing the trigger simply stops further
 * taps; nothing needs to be released, since each tap already released its own key before this
 * function returns. */
static void handle_roll_side(bool engaged, bool *was_engaged, ULONGLONG *next_tap_tick, WORD vk)
{
    ULONGLONG now;

    if (!engaged) {
        *was_engaged = false;
        return;
    }
    now = GetTickCount64();
    if (!*was_engaged || now >= *next_tap_tick) {
        synthesize_roll_tap(vk);
        *next_tap_tick = now + ROLL_TAP_PERIOD_MS;
    }
    *was_engaged = true;
}

static void handle_roll_triggers(const XINPUT_GAMEPAD *pad, int threshold)
{
    bool left_engaged  = (int)pad->bLeftTrigger  > threshold;
    bool right_engaged = (int)pad->bRightTrigger > threshold;
    bool want_alt      = left_engaged || right_engaged;

    /* Alt down before either direction tap, so a tap is never sent while Alt is up, the same
     * order a player's own hand would produce holding the modifier first. */
    if (want_alt && !ci_state.alt_held) {
        set_alt_held(true);
    }

    handle_roll_side(left_engaged, &ci_state.roll_left_was_engaged,
                     &ci_state.roll_left_next_tap_tick, VK_LEFT);
    handle_roll_side(right_engaged, &ci_state.roll_right_was_engaged,
                     &ci_state.roll_right_next_tap_tick, VK_RIGHT);

    /* Alt up only once neither trigger is engaged any more. */
    if (!want_alt && ci_state.alt_held) {
        set_alt_held(false);
    }
}

/* Whether this game owns the foreground.
 *
 * WHY EVERY INJECTION IS GATED ON THIS. SendInput does not aim at a window, it goes to whatever has
 * focus. Without this check, a stick pushed while the game is alt tabbed moves the mouse in the
 * player's browser, a trigger fires Alt chords into it, and Start sends it an Escape. That is not a
 * quirk, it is this DLL typing into somebody else's application, and it shipped enabled by default.
 *
 * The foreground window's owning process is compared to this one rather than a HWND of our own
 * being tracked, which needs nothing set up anywhere else here. Byte for byte the same check
 * cheats_openphantom.c already makes before it reads a held key, so the pattern was in the tree and
 * this code simply did not use it. */
static bool is_game_foreground(void)
{
    HWND  foreground = GetForegroundWindow();
    DWORD owner_pid = 0;

    if (foreground == NULL) {
        return false;
    }
    GetWindowThreadProcessId(foreground, &owner_pid);
    return owner_pid == GetCurrentProcessId();
}

/* Everything this thread might be holding down, released, and every edge it tracks reset.
 *
 * Called when the game does not own the foreground. RELEASING HAS TO HAPPEN ANYWAY, which is why
 * this is not simply an early return: a synthetic Alt left down belongs to whichever window has
 * focus now, and leaving it there is worse than anything the gate prevents. The audit that found
 * the missing gate found this alongside it, and it is the half that outlives the alt tab.
 *
 * The edges are recorded rather than cleared so that returning to the game with Start or a trigger
 * already held does not fire a press the player made somewhere else. */
static void release_everything_held(const XINPUT_GAMEPAD *pad, int threshold)
{
    set_alt_held(false);
    ci_state.start_was_down = (pad->wButtons & XINPUT_GAMEPAD_START) != 0;
    ci_state.roll_left_was_engaged  = (int)pad->bLeftTrigger  > threshold;
    ci_state.roll_right_was_engaged = (int)pad->bRightTrigger > threshold;
    ci_state.remainder_x = 0.0;
    ci_state.remainder_y = 0.0;
}

static void poll_once(void)
{
    XINPUT_STATE state;
    DWORD        result;
    double       dt;

    ZeroMemory(&state, sizeof(state));
    result = XInputGetState((DWORD)ci_state.config.controller_index, &state);
    if (result != ERROR_SUCCESS) {
        if (ci_state.pad_connected) {
            log_info("controller %d disconnected, checking every %u ms until it returns",
                     ci_state.config.controller_index, (unsigned)DISCONNECTED_POLL_INTERVAL_MS);
        }
        ci_state.pad_connected = false;
        return;
    }
    if (!ci_state.pad_connected) {
        log_info("controller %d connected", ci_state.config.controller_index);
    }
    ci_state.pad_connected = true;

    dt = seconds_since_last_poll();

    /* Nothing is injected unless this game is the window the player is actually looking at. The
       poll itself keeps running, so the pad stays tracked and a return to the game is instant. */
    if (!is_game_foreground()) {
        release_everything_held(&state.Gamepad, ci_state.config.trigger_threshold);
        return;
    }

    if (ci_state.config.look_enabled) {
        float x, y;

        if (apply_radial_deadzone(state.Gamepad.sThumbRX, state.Gamepad.sThumbRY,
                                  ci_state.config.deadzone, &x, &y)) {
            synthesize_look(x, y, dt);
        }
    }

    if (ci_state.config.pause_enabled) {
        bool start_down = (state.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0;

        if (start_down && !ci_state.start_was_down) {
            synthesize_pause_press();
        }
        ci_state.start_was_down = start_down;
    }

    if (ci_state.config.roll_enabled) {
        handle_roll_triggers(&state.Gamepad, ci_state.config.trigger_threshold);
    }
}

static DWORD WINAPI poll_thread_proc(LPVOID unused_parameter)
{
    (void)unused_parameter;

    for (;;) {
        poll_once();
        Sleep(ci_state.pad_connected ? POLL_INTERVAL_MS : DISCONNECTED_POLL_INTERVAL_MS);
    }
}

static void load_config(controller_input_config_t *config)
{
    config->enabled          = ini_read_bool (CONTROLLER_SECTION, "Enabled", true);
    config->look_enabled     = ini_read_bool (CONTROLLER_SECTION, "LookEnabled", true);
    config->pause_enabled    = ini_read_bool (CONTROLLER_SECTION, "PauseEnabled", true);
    config->roll_enabled     = ini_read_bool (CONTROLLER_SECTION, "RollEnabled", true);
    config->controller_index = ini_read_int  (CONTROLLER_SECTION, "ControllerIndex", 0);
    config->deadzone         = ini_read_float(CONTROLLER_SECTION, "Deadzone", DEFAULT_DEADZONE);
    config->look_sensitivity = ini_read_float(CONTROLLER_SECTION, "LookSensitivity",
                                              DEFAULT_LOOK_SENSITIVITY);
    config->trigger_threshold = ini_read_int (CONTROLLER_SECTION, "TriggerThreshold",
                                              DEFAULT_TRIGGER_THRESHOLD);

    if (config->controller_index < 0 || config->controller_index > 3) {
        log_warning("ControllerIndex=%d is out of range (0 to 3), using 0",
                    config->controller_index);
        config->controller_index = 0;
    }
    if (!(config->deadzone >= 0.0f) || config->deadzone >= 1.0f) {
        log_warning("Deadzone=%.3f is out of range (0 to just under 1), using %.3f",
                    (double)config->deadzone, (double)DEFAULT_DEADZONE);
        config->deadzone = DEFAULT_DEADZONE;
    }
    if (config->trigger_threshold < 0 || config->trigger_threshold > 255) {
        log_warning("TriggerThreshold=%d is out of range (0 to 255), using %d",
                    config->trigger_threshold, DEFAULT_TRIGGER_THRESHOLD);
        config->trigger_threshold = DEFAULT_TRIGGER_THRESHOLD;
    }
}

void controller_input_install(void)
{
    DWORD thread_id = 0;

    if (ci_state.installed) {
        return;
    }
    ci_state.installed = true;

    log_init("controller_input", false);

    load_config(&ci_state.config);
    if (!ci_state.config.enabled) {
        log_info("Enabled=0, the controller's right stick, Start button and triggers do nothing "
                 "here");
        return;
    }
    if (!ci_state.config.look_enabled && !ci_state.config.pause_enabled &&
        !ci_state.config.roll_enabled) {
        log_info("LookEnabled=0, PauseEnabled=0 and RollEnabled=0, nothing for this DLL to do");
        return;
    }

    if (!QueryPerformanceFrequency(&ci_state.qpc_frequency) ||
        ci_state.qpc_frequency.QuadPart == 0) {
        log_error("QueryPerformanceCounter is not available, controller look cannot be timed");
        return;
    }

    ci_state.thread = CreateThread(NULL, 0, poll_thread_proc, NULL, 0, &thread_id);
    if (ci_state.thread == NULL) {
        log_error("the polling thread could not be created (error %lu), controller look and "
                  "pause are not installed",
                  GetLastError());
        return;
    }

    log_info("armed on its own thread (id %lu): controller %d, look %s (sensitivity %.0f, "
             "deadzone %.2f), pause %s, roll %s (trigger threshold %d)",
             (unsigned long)thread_id, ci_state.config.controller_index,
             ci_state.config.look_enabled ? "on" : "off",
             (double)ci_state.config.look_sensitivity, (double)ci_state.config.deadzone,
             ci_state.config.pause_enabled ? "on" : "off",
             ci_state.config.roll_enabled ? "on" : "off", ci_state.config.trigger_threshold);
}
