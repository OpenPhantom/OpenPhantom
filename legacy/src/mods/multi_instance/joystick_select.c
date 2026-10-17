/* joystick_select.c: see joystick_select.h. */
#include "joystick_select.h"

#include "common/import_patch.h"
#include "common/logging.h"

#include <windows.h>
#include <mmsystem.h>

#include <stdbool.h>

#define JOYSTICK_ID_MAX 15

typedef MMRESULT (WINAPI *joy_get_pos_ex_fn_t)(UINT, LPJOYINFOEX);
typedef MMRESULT (WINAPI *joy_get_dev_caps_fn_t)(UINT_PTR, LPJOYCAPSA, UINT);

static UINT                  selected;
static joy_get_pos_ex_fn_t   original_pos;
static joy_get_dev_caps_fn_t original_caps;

static MMRESULT WINAPI hook_get_pos_ex(UINT id, LPJOYINFOEX info)
{
    (void)id;
    return original_pos(selected, info);
}

static MMRESULT WINAPI hook_get_dev_caps(UINT_PTR id, LPJOYCAPSA caps, UINT size)
{
    (void)id;
    return original_caps(selected, caps, size);
}

/* One line per id that answers, so a pad that turns out to sit under a different id than its
 * XInput slot can be found from the log instead of by trial. These calls are this DLL's own and do
 * not pass through the replaced entries. */
static void log_joysticks(void)
{
    UINT count = joyGetNumDevs();
    UINT id;
    bool any = false;

    for (id = 0; id < count && id <= JOYSTICK_ID_MAX; ++id) {
        JOYINFOEX info;
        JOYCAPSA  caps;

        ZeroMemory(&info, sizeof info);
        info.dwSize  = sizeof info;
        info.dwFlags = JOY_RETURNALL;
        if (joyGetPosEx(id, &info) != JOYERR_NOERROR) {
            continue;
        }
        if (joyGetDevCapsA(id, &caps, sizeof caps) != JOYERR_NOERROR) {
            caps.szPname[0] = '\0';
        }
        log_info("joystick %u answers: '%s', %u buttons", id, caps.szPname,
                 (unsigned)caps.wNumButtons);
        any = true;
    }
    if (!any) {
        log_info("no joystick answers right now; one plugged in later is read under the same id");
    }
}

void joystick_select_install(int joystick)
{
    void *pos  = NULL;
    void *caps = NULL;

    if (joystick < 0) {
        log_info("Joystick=-1, the game chooses its own joystick, the first that answers");
        return;
    }
    if (joystick > JOYSTICK_ID_MAX) {
        log_warning("Joystick=%d is out of range (0 to %d), the game chooses its own", joystick,
                    JOYSTICK_ID_MAX);
        return;
    }
    log_joysticks();

    selected = (UINT)joystick;
    if (!import_patch_replace(NULL, "winmm.dll", "joyGetPosEx", (void *)&hook_get_pos_ex, &pos)) {
        log_warning("the game's joyGetPosEx import could not be replaced, it chooses its own "
                    "joystick");
        return;
    }
    original_pos = (joy_get_pos_ex_fn_t)pos;
    /* The position read alone already decides which joystick the walk keeps; capabilities are asked
     * of the same id, and answering them from another pad would mix two devices' ranges. */
    if (!import_patch_replace(NULL, "winmm.dll", "joyGetDevCapsA", (void *)&hook_get_dev_caps,
                              &caps)) {
        log_warning("joyGetDevCapsA could not be replaced: positions come from joystick %u, "
                    "capabilities from the game's own choice", selected);
        return;
    }
    original_caps = (joy_get_dev_caps_fn_t)caps;
    log_info("this copy reads joystick %u, whichever window is in front", selected);
}
