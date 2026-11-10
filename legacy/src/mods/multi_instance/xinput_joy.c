/* xinput_joy.c: see xinput_joy.h. */
#include "xinput_joy.h"

#include "common/import_patch.h"
#include "common/logging.h"
#include "common/platform.h"

#include <stdlib.h>

#pragma comment(lib, "xinput.lib")
#pragma comment(lib, "winmm.lib")

#define AXIS_CENTRE   32767u
#define POV_CENTRED   0xFFFFu
#define COMPARE_LINES 12

typedef MMRESULT (WINAPI *joy_get_pos_ex_fn_t)(UINT, LPJOYINFOEX);
typedef MMRESULT (WINAPI *joy_get_dev_caps_fn_t)(UINT_PTR, LPJOYCAPSA, UINT);

static DWORD               xinput_slot;
static joy_get_pos_ex_fn_t original_pos;
static UINT                compare_id = (UINT)-1;
static int                 compare_logged;

static DWORD stick(SHORT value)
{
    return (DWORD)((LONG)value + 32768);
}

/* WinMM's vertical axes grow downwards; XInput's grow upwards. */
static DWORD stick_down(SHORT value)
{
    return 65535u - stick(value);
}

static DWORD pov(WORD buttons)
{
    int  x = ((buttons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0) - ((buttons & XINPUT_GAMEPAD_DPAD_LEFT) != 0);
    int  y = ((buttons & XINPUT_GAMEPAD_DPAD_UP) != 0) - ((buttons & XINPUT_GAMEPAD_DPAD_DOWN) != 0);
    static const DWORD table[3][3] = {
        /* y = -1 (down)  0       +1 (up) */
        { 22500u, 27000u, 31500u },        /* x = -1 (left) */
        { 18000u, POV_CENTRED, 0u },       /* x =  0        */
        { 13500u,  9000u,  4500u },        /* x = +1 (right) */
    };
    return table[x + 1][y + 1];
}

void xinput_joy_position(const XINPUT_GAMEPAD *pad, JOYINFOEX *out)
{
    static const WORD order[10] = {
        XINPUT_GAMEPAD_A, XINPUT_GAMEPAD_B, XINPUT_GAMEPAD_X, XINPUT_GAMEPAD_Y,
        XINPUT_GAMEPAD_LEFT_SHOULDER, XINPUT_GAMEPAD_RIGHT_SHOULDER,
        XINPUT_GAMEPAD_BACK, XINPUT_GAMEPAD_START,
        XINPUT_GAMEPAD_LEFT_THUMB, XINPUT_GAMEPAD_RIGHT_THUMB,
    };
    DWORD buttons = 0;
    DWORD pressed = 0;
    int   i;

    for (i = 0; i < 10; ++i) {
        if (pad->wButtons & order[i]) {
            buttons |= 1u << i;
            ++pressed;
        }
    }
    out->dwXpos          = stick(pad->sThumbLX);
    out->dwYpos          = stick_down(pad->sThumbLY);
    /* Left trigger pushes Z up from centre, right trigger down, as the HID pad does. */
    out->dwZpos          = (DWORD)((LONG)AXIS_CENTRE + ((LONG)pad->bLeftTrigger - (LONG)pad->bRightTrigger) * 128);
    out->dwRpos          = stick_down(pad->sThumbRY);
    out->dwUpos          = stick(pad->sThumbRX);
    out->dwVpos          = 0;
    out->dwButtons       = buttons;
    out->dwButtonNumber  = pressed;
    out->dwPOV           = pov(pad->wButtons);
    out->dwReserved1     = 0;
    out->dwReserved2     = 0;
}

void xinput_joy_caps(JOYCAPSA *out)
{
    ZeroMemory(out, sizeof *out);
    out->wMid        = 0x045E;
    out->wPid        = 0x02E0;
    lstrcpynA(out->szPname, "Microsoft PC-joystick driver", MAXPNAMELEN);
    out->wXmax       = 65535;
    out->wYmax       = 65535;
    out->wZmax       = 65535;
    out->wNumButtons = 16;
    out->wPeriodMin  = 10;
    out->wPeriodMax  = 1000;
    out->wRmax       = 65535;
    out->wUmax       = 65535;
    out->wVmax       = 65535;
    out->wCaps       = JOYCAPS_HASZ | JOYCAPS_HASR | JOYCAPS_HASU | JOYCAPS_HASPOV | JOYCAPS_POV4DIR;
    out->wMaxAxes    = 6;
    out->wNumAxes    = 5;
    out->wMaxButtons = 32;
    lstrcpynA(out->szRegKey, "DINPUT.DLL", MAXPNAMELEN);
}

static BOOL far_apart(DWORD a, DWORD b)
{
    return labs((LONG)a - (LONG)b) > 4096;
}

/* In front the real pad still answers, so the first disagreements are logged to check the layout. */
static void compare_with_real(const JOYINFOEX *ours)
{
    JOYINFOEX real;

    if (compare_id == (UINT)-1 || compare_logged >= COMPARE_LINES || !platform_foreground_is_ours()) {
        return;
    }
    ZeroMemory(&real, sizeof real);
    real.dwSize  = sizeof real;
    real.dwFlags = JOY_RETURNALL;
    if (original_pos(compare_id, &real) != JOYERR_NOERROR) {
        return;
    }
    if (!far_apart(real.dwXpos, ours->dwXpos) && !far_apart(real.dwYpos, ours->dwYpos) &&
        !far_apart(real.dwZpos, ours->dwZpos) && !far_apart(real.dwRpos, ours->dwRpos) &&
        !far_apart(real.dwUpos, ours->dwUpos) && real.dwButtons == ours->dwButtons &&
        real.dwPOV == ours->dwPOV) {
        return;
    }
    ++compare_logged;
    log_warning("xinput pad differs from WinMM joystick %u: x %lu/%lu y %lu/%lu z %lu/%lu r %lu/%lu "
                "u %lu/%lu buttons %lx/%lx pov %lu/%lu (ours/real)", compare_id,
                ours->dwXpos, real.dwXpos, ours->dwYpos, real.dwYpos, ours->dwZpos, real.dwZpos,
                ours->dwRpos, real.dwRpos, ours->dwUpos, real.dwUpos, ours->dwButtons,
                real.dwButtons, ours->dwPOV, real.dwPOV);
}

static MMRESULT WINAPI hook_get_pos_ex(UINT id, LPJOYINFOEX info)
{
    static DWORD failures;
    XINPUT_STATE state;
    DWORD        result;

    (void)id;
    result = XInputGetState(xinput_slot, &state);
    if (result != ERROR_SUCCESS) {
        /* One failed read makes the engine drop the pad for the session (0x0048E15B), so a pad
         * that drops out answers centred instead and is read again next frame. */
        if (failures++ < COMPARE_LINES) {
            log_warning("XInput slot %lu did not answer (error %lu), the pad reads centred",
                        xinput_slot, result);
        }
        ZeroMemory(&state, sizeof state);
    }
    xinput_joy_position(&state.Gamepad, info);
    if (result == ERROR_SUCCESS) {
        compare_with_real(info);
    }
    return JOYERR_NOERROR;
}

static MMRESULT WINAPI hook_get_dev_caps(UINT_PTR id, LPJOYCAPSA caps, UINT size)
{
    JOYCAPSA ours;

    (void)id;
    if (caps == NULL || size == 0) {
        return MMSYSERR_INVALPARAM;
    }
    xinput_joy_caps(&ours);
    CopyMemory(caps, &ours, size < sizeof ours ? size : sizeof ours);
    return JOYERR_NOERROR;
}

static void find_compare_id(void)
{
    UINT count = joyGetNumDevs();
    UINT id;

    for (id = 0; id < count && id < 16; ++id) {
        JOYINFOEX info;

        ZeroMemory(&info, sizeof info);
        info.dwSize  = sizeof info;
        info.dwFlags = JOY_RETURNALL;
        if (joyGetPosEx(id, &info) == JOYERR_NOERROR) {
            compare_id = id;
            return;
        }
    }
}

BOOL xinput_joy_install(int slot)
{
    void         *pos  = NULL;
    void         *caps = NULL;
    XINPUT_STATE  state;

    if (slot < 0 || slot > 3) {
        log_warning("XInputSlot=%d is out of range (0 to 3), the WinMM joystick is read as before", slot);
        return FALSE;
    }
    xinput_slot = (DWORD)slot;
    find_compare_id();
    if (!import_patch_replace(NULL, "winmm.dll", "joyGetPosEx", (void *)&hook_get_pos_ex, &pos)) {
        log_warning("the game's joyGetPosEx import could not be replaced, the pad stays on WinMM");
        return FALSE;
    }
    original_pos = (joy_get_pos_ex_fn_t)pos;
    if (!import_patch_replace(NULL, "winmm.dll", "joyGetDevCapsA", (void *)&hook_get_dev_caps, &caps)) {
        log_warning("joyGetDevCapsA could not be replaced: positions come from XInput, capabilities "
                    "from WinMM");
    }
    log_info("this copy's joystick is XInput slot %d (%s now), read whether or not the window is in "
             "front; disagreements with WinMM joystick %d while in front are logged",
             slot, XInputGetState(xinput_slot, &state) == ERROR_SUCCESS ? "connected" : "not connected",
             compare_id == (UINT)-1 ? -1 : (int)compare_id);
    return TRUE;
}
