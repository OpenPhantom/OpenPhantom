/* background_input.c: see background_input.h.
 *
 * ==============================================================================================
 * The keyboard poll and its tables
 *
 * stdControl_pollKeyboard (retail 0x0048DCEF) asks DirectInput for up to 32 buffered events and
 * folds each one into three tables indexed by the DIK scan code, the same tables the engine's
 * readers behind control_isDown look a binding's key up in:
 *
 *     0x0048DDB9  cmp dword ptr [eax*4 + 0x861068], 0     held, 0 or 1
 *     0x0048DDE6  mov dword ptr [eax*4 + 0x860BC8], edx   hold time
 *     0x0048DDF3  mov edx, dword ptr [ecx*4 + 0x861510]   presses, counted up on every down
 *     0x0048DE4C  mov edx, dword ptr [0x8619D8]           the hold a release starts from when the
 *                                                         down left none
 *
 * A window that is not in front has an unacquired keyboard, so every GetDeviceData fails and the
 * tables never move. Writing them after the poll returns is the same thing the poll would have
 * done. The edges below follow the poll's own GetDeviceState branch (0x0048DEE5 on), where the
 * event time is the poll time: a down sets held, zeroes the hold time and counts a press; an up
 * clears held and, when the hold time is still zero, sets it to the release value.
 *
 * Both patterns are address free; the five tables and cells are read out of operands in the body
 * pattern, which must sit exactly 0xC4 bytes into the function the head pattern found, so the two
 * cannot have matched different code. The head pattern's first nine bytes are the prologue the
 * detour overwrites.
 *
 * ==============================================================================================
 * The relative axis
 *
 * control_readAxisRelative (retail 0x004652C3) answers action 0 with the mouse's turn in raw
 * device ticks, and the steer reads it once per substep before it falls back to the keyboard turn.
 * The pattern is multiplayer.dll's own for the same function. That DLL also detours it; this one
 * loads first, so its hook sits behind multiplayer's and only answers the reads multiplayer passes
 * through, which are this player's own and never a far body's.
 */
#include "background_input.h"

#include "look_counts.h"

#include "common/detour.h"
#include "common/host_image.h"
#include "common/logging.h"
#include "common/signature.h"
#include "common/stick.h"

#include <stdint.h>
#include <string.h>

#define DIK_LMENU 0x38u
#define DIK_LEFT  0xCBu
#define DIK_RIGHT 0xCDu

#define KEYBOARD_POLL_PROLOGUE  9u
#define KEYBOARD_TABLES_OFFSET  0xC4u
#define OPERAND_HELD            0x09u
#define OPERAND_HOLD            0x36u
#define OPERAND_PRESSES         0x43u
#define OPERAND_RELEASE_HOLD    0x9Bu

#define RELATIVE_AXIS_PROLOGUE  6u
#define ACTION_TURN             0

static const uint8_t SIG_KEYBOARD_POLL[74] = {
    0x55, 0x8B, 0xEC, 0x81, 0xEC, 0x24, 0x03, 0x00, 0x00, 0xC7, 0x45, 0xF8,
    0x20, 0x00, 0x00, 0x00, 0x83, 0x3D, 0xC4, 0x19, 0x86, 0x00, 0x00, 0x74,
    0x09, 0x83, 0x3D, 0xB8, 0x19, 0x86, 0x00, 0x00, 0x74, 0x05, 0xE9, 0xCA,
    0x02, 0x00, 0x00, 0x6A, 0x00, 0x8D, 0x45, 0xF8, 0x50, 0x8D, 0x8D, 0xF8,
    0xFD, 0xFF, 0xFF, 0x51, 0x6A, 0x10, 0x8B, 0x15, 0xC4, 0x19, 0x86, 0x00,
    0x52, 0xA1, 0xC4, 0x19, 0x86, 0x00, 0x8B, 0x08, 0xFF, 0x51, 0x28, 0x89,
    0x45, 0xFC
};
static const uint8_t MSK_KEYBOARD_POLL[74] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF
};

static const uint8_t SIG_KEYBOARD_TABLES[159] = {
    0x8B, 0x85, 0xEC, 0xFC, 0xFF, 0xFF, 0x83, 0x3C, 0x85, 0x68, 0x10, 0x86,
    0x00, 0x00, 0x75, 0x49, 0x8B, 0x8D, 0xEC, 0xFC, 0xFF, 0xFF, 0xC7, 0x04,
    0x8D, 0x68, 0x10, 0x86, 0x00, 0x01, 0x00, 0x00, 0x00, 0x8B, 0x15, 0xCC,
    0x19, 0x86, 0x00, 0x2B, 0x95, 0xE4, 0xFC, 0xFF, 0xFF, 0x8B, 0x85, 0xEC,
    0xFC, 0xFF, 0xFF, 0x89, 0x14, 0x85, 0xC8, 0x0B, 0x86, 0x00, 0x8B, 0x8D,
    0xEC, 0xFC, 0xFF, 0xFF, 0x8B, 0x14, 0x8D, 0x10, 0x15, 0x86, 0x00, 0x83,
    0xC2, 0x01, 0x8B, 0x85, 0xEC, 0xFC, 0xFF, 0xFF, 0x89, 0x14, 0x85, 0x10,
    0x15, 0x86, 0x00, 0xEB, 0x74, 0x83, 0xBD, 0xE8, 0xFC, 0xFF, 0xFF, 0x00,
    0x75, 0x6B, 0x8B, 0x8D, 0xEC, 0xFC, 0xFF, 0xFF, 0x83, 0x3C, 0x8D, 0x68,
    0x10, 0x86, 0x00, 0x00, 0x74, 0x5B, 0x8B, 0x95, 0xEC, 0xFC, 0xFF, 0xFF,
    0xC7, 0x04, 0x95, 0x68, 0x10, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x8B,
    0x85, 0xEC, 0xFC, 0xFF, 0xFF, 0x83, 0x3C, 0x85, 0xC8, 0x0B, 0x86, 0x00,
    0x00, 0x75, 0x13, 0x8B, 0x8D, 0xEC, 0xFC, 0xFF, 0xFF, 0x8B, 0x15, 0xD8,
    0x19, 0x86, 0x00
};
static const uint8_t MSK_KEYBOARD_TABLES[159] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00,
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00,
    0x00, 0x00, 0x00
};

static const uint8_t SIG_RELATIVE_AXIS[28] = {
    0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x1C, 0xC7, 0x45, 0xF8, 0x00, 0x00, 0x00,
    0x00, 0x8B, 0x45, 0x08, 0x69, 0xC0, 0x24, 0x01, 0x00, 0x00, 0x8B, 0x88,
    0x58, 0x37, 0x6D, 0x00
};
static const uint8_t MSK_RELATIVE_AXIS[28] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0x00, 0x00, 0x00, 0x00
};

typedef void (__cdecl *poll_keyboard_fn_t)(void);
typedef float (__cdecl *read_axis_fn_t)(int32_t action);

enum { KEY_ALT, KEY_LEFT, KEY_RIGHT, KEY_COUNT };
static const uint32_t key_codes[KEY_COUNT] = { DIK_LMENU, DIK_LEFT, DIK_RIGHT };

typedef struct background_state {
    bool              keys_ready;
    bool              look_ready;
    detour_t          poll;
    detour_t          relative;

    volatile int32_t *held;
    volatile int32_t *hold;
    volatile int32_t *presses;
    volatile int32_t *release_hold;

    /* Written by the polling thread, applied by the keyboard hook on the game's thread. */
    volatile LONG     wanted[KEY_COUNT];
    bool              applied[KEY_COUNT];
    volatile LONG     look_counts;

    /* The polling thread's own. */
    look_carry_t      look_carry;
    bool              start_was_down;
    ULONGLONG         roll_since[2];
    HWND              window;
} background_state_t;

static background_state_t bg;

/* ==============================================================================================
 * The game's thread
 * ============================================================================================ */

static void press(uint32_t key)
{
    if (bg.held[key] == 0) {
        bg.held[key]  = 1;
        bg.hold[key]  = 0;
        bg.presses[key] += 1;
    }
}

static void release(uint32_t key)
{
    if (bg.held[key] != 0) {
        bg.held[key] = 0;
        if (bg.hold[key] == 0) {
            bg.hold[key] = *bg.release_hold;
        }
    }
}

static void __cdecl hook_poll_keyboard(void)
{
    size_t i;

    ((poll_keyboard_fn_t)bg.poll.original)();
    for (i = 0; i < KEY_COUNT; ++i) {
        bool want = InterlockedCompareExchange(&bg.wanted[i], 0, 0) != 0;

        if (want == bg.applied[i]) {
            continue;
        }
        if (want) {
            press(key_codes[i]);
        } else {
            release(key_codes[i]);
        }
        bg.applied[i] = want;
    }
}

static float __cdecl hook_relative_axis(int32_t action)
{
    float value = ((read_axis_fn_t)bg.relative.original)(action);

    if (action == ACTION_TURN) {
        value += (float)InterlockedExchange(&bg.look_counts, 0);
    }
    return value;
}

/* ==============================================================================================
 * The polling thread
 * ============================================================================================ */

static void want_keys(bool alt, bool left, bool right)
{
    InterlockedExchange(&bg.wanted[KEY_ALT], alt);
    InterlockedExchange(&bg.wanted[KEY_LEFT], left);
    InterlockedExchange(&bg.wanted[KEY_RIGHT], right);
}

static BOOL CALLBACK pick_window(HWND window, LPARAM best)
{
    DWORD owner = 0;
    RECT  area;
    RECT  best_area;

    GetWindowThreadProcessId(window, &owner);
    if (owner != GetCurrentProcessId() || !IsWindowVisible(window) ||
        GetWindow(window, GW_OWNER) != NULL || !GetClientRect(window, &area)) {
        return TRUE;
    }
    /* The largest of this process's own top level windows: the game's, not a message box. */
    if (*(HWND *)best == NULL ||
        (GetClientRect(*(HWND *)best, &best_area) &&
         area.right * area.bottom > best_area.right * best_area.bottom)) {
        *(HWND *)best = window;
    }
    return TRUE;
}

static void post_escape(DWORD hold_ms)
{
    /* Repeat count 1 and the scan code 0x01, then the previous-state and transition bits on the
     * release, which is the lParam a real Escape produces. */
    const LPARAM down = 0x00010001;
    const LPARAM up   = (LPARAM)0xC0010001u;

    if (bg.window == NULL || !IsWindow(bg.window)) {
        bg.window = NULL;
        EnumWindows(pick_window, (LPARAM)&bg.window);
    }
    if (bg.window == NULL) {
        return;
    }
    PostMessageA(bg.window, WM_KEYDOWN, VK_ESCAPE, down);
    Sleep(hold_ms);
    PostMessageA(bg.window, WM_KEYUP, VK_ESCAPE, up);
}

/* Down for the first `down_ms` of every `period_ms` since the trigger engaged: the tapped arrow
 * the roll binding wants, the same shape the SendInput path taps. */
static bool tap_down(bool engaged, ULONGLONG *since, ULONGLONG now, const background_settings_t *s)
{
    if (!engaged) {
        *since = 0;
        return false;
    }
    if (*since == 0) {
        *since = now;
    }
    return (now - *since) % s->roll_tap_period_ms < s->roll_tap_down_ms;
}

void background_input_poll(const XINPUT_GAMEPAD *pad, double dt,
                           const background_settings_t *settings)
{
    if (bg.look_ready && settings->look_enabled) {
        float x;
        float y;
        long  counts_x;
        long  counts_y;

        if (stick_apply_radial_deadzone(pad->sThumbRX, pad->sThumbRY, settings->deadzone, &x, &y) &&
            look_counts_step(&bg.look_carry, x, 0.0f, settings->look_sensitivity, dt, &counts_x,
                             &counts_y)) {
            InterlockedExchangeAdd(&bg.look_counts, counts_x);
        }
    }

    if (settings->pause_enabled) {
        bool start_down = (pad->wButtons & XINPUT_GAMEPAD_START) != 0;

        if (start_down && !bg.start_was_down) {
            post_escape(settings->escape_hold_ms);
        }
        bg.start_was_down = start_down;
    }

    if (bg.keys_ready && settings->roll_enabled) {
        bool      left  = (int)pad->bLeftTrigger > settings->trigger_threshold;
        bool      right = (int)pad->bRightTrigger > settings->trigger_threshold;
        ULONGLONG now   = GetTickCount64();

        want_keys(left || right, tap_down(left, &bg.roll_since[0], now, settings),
                  tap_down(right, &bg.roll_since[1], now, settings));
    }
}

void background_input_release(const XINPUT_GAMEPAD *pad)
{
    want_keys(false, false, false);
    InterlockedExchange(&bg.look_counts, 0);
    look_counts_reset(&bg.look_carry);
    bg.roll_since[0]  = 0;
    bg.roll_since[1]  = 0;
    bg.start_was_down = (pad->wButtons & XINPUT_GAMEPAD_START) != 0;
}

/* ==============================================================================================
 * Install
 * ============================================================================================ */

static bool read_table(uintptr_t body, size_t offset, volatile int32_t **out)
{
    uintptr_t address;

    memcpy(&address, (const void *)(body + offset), sizeof address);
    if (address < host_image_base() || address >= host_image_end()) {
        return false;
    }
    *out = (volatile int32_t *)address;
    return true;
}

static bool install_keys(void)
{
    uintptr_t head = signature_find_detour_target(SIG_KEYBOARD_POLL, MSK_KEYBOARD_POLL,
                                                  sizeof SIG_KEYBOARD_POLL,
                                                  KEYBOARD_POLL_PROLOGUE);
    uintptr_t body = signature_find_unique(SIG_KEYBOARD_TABLES, MSK_KEYBOARD_TABLES,
                                           sizeof SIG_KEYBOARD_TABLES);

    if (head == 0 || body != head + KEYBOARD_TABLES_OFFSET) {
        log_warning("background roll: the keyboard poll did not resolve (head %08X, tables %08X), "
                    "the triggers roll only in the window in front", (unsigned)head,
                    (unsigned)body);
        return false;
    }
    if (!read_table(body, OPERAND_HELD, &bg.held) || !read_table(body, OPERAND_HOLD, &bg.hold) ||
        !read_table(body, OPERAND_PRESSES, &bg.presses) ||
        !read_table(body, OPERAND_RELEASE_HOLD, &bg.release_hold)) {
        log_warning("background roll: a key table operand lies outside the image, nothing patched");
        return false;
    }
    if (!detour_install(&bg.poll, head, (const void *)&hook_poll_keyboard,
                        KEYBOARD_POLL_PROLOGUE)) {
        log_warning("background roll: the keyboard poll at %08X could not be detoured",
                    (unsigned)head);
        return false;
    }
    log_info("background roll: keyboard poll %08X%s, key tables %08X %08X %08X, release hold %08X",
             (unsigned)head, bg.poll.chained ? " (chained)" : "", (unsigned)(uintptr_t)bg.held,
             (unsigned)(uintptr_t)bg.hold, (unsigned)(uintptr_t)bg.presses,
             (unsigned)(uintptr_t)bg.release_hold);
    return true;
}

static bool install_look(void)
{
    uintptr_t site = signature_find_detour_target(SIG_RELATIVE_AXIS, MSK_RELATIVE_AXIS,
                                                  sizeof SIG_RELATIVE_AXIS,
                                                  RELATIVE_AXIS_PROLOGUE);

    if (site == 0 || !detour_install(&bg.relative, site, (const void *)&hook_relative_axis,
                                     RELATIVE_AXIS_PROLOGUE)) {
        log_warning("background look: the relative axis read did not resolve or could not be "
                    "detoured (%08X), the right stick looks only in the window in front",
                    (unsigned)site);
        return false;
    }
    log_info("background look: relative axis read %08X%s", (unsigned)site,
             bg.relative.chained ? " (chained)" : "");
    return true;
}

bool background_input_install(void)
{
    bg.keys_ready = install_keys();
    bg.look_ready = install_look();
    return bg.keys_ready || bg.look_ready;
}
