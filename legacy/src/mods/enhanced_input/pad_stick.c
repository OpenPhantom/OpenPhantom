/* pad_stick.c: see pad_stick.h. */
#include "pad_stick.h"

#include "strafe_walk.h"

#include "common/logging.h"
#include "common/stick.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <xinput.h>

#pragma comment(lib, "xinput.lib")


/* XInputGetState against a slot with nothing in it is documented to cost noticeably more than
 * against a live one, and this runs every substep. So an empty slot is asked about once every so
 * often rather than thirty two times a second, and a pad plugged in mid-session is still picked up
 * within that interval. controller_input.dll makes the same allowance for the same reason. */
#define ABSENT_RECHECK_MS 2000u

static struct {
    bool  enabled;
    int   controller_index;
    float deadzone;
    float run_threshold;
    float hysteresis;

    bool  active;
    float x;
    float y;
    float magnitude;
    bool  running;          /* latched across substeps, which is what makes the hysteresis work */

    bool     absent;
    ULONGLONG absent_next_tick;
    bool     logged_present;
} pad_state;

void pad_stick_configure(bool enabled, int controller_index, float deadzone,
                         float walk_run_threshold, float threshold_hysteresis)
{
    pad_state.enabled          = enabled;
    pad_state.controller_index = controller_index;
    pad_state.deadzone         = deadzone;
    pad_state.run_threshold    = walk_run_threshold;
    pad_state.hysteresis       = threshold_hysteresis;
    pad_state.active           = false;
    pad_state.running          = false;
    pad_state.absent           = false;
    pad_state.absent_next_tick = 0;
}

void pad_stick_poll(void)
{
    XINPUT_STATE state;
    DWORD        result;
    float        x = 0.0f;
    float        y = 0.0f;

    pad_state.active = false;

    if (!pad_state.enabled) {
        return;
    }
    if (pad_state.absent && GetTickCount64() < pad_state.absent_next_tick) {
        return;
    }

    ZeroMemory(&state, sizeof state);
    result = XInputGetState((DWORD)pad_state.controller_index, &state);
    if (result != ERROR_SUCCESS) {
        pad_state.absent           = true;
        pad_state.absent_next_tick = GetTickCount64() + ABSENT_RECHECK_MS;
        pad_state.running          = false;
        return;
    }
    pad_state.absent = false;

    if (!pad_state.logged_present) {
        pad_state.logged_present = true;
        log_info("pad: the left stick is read from XInput slot %d as a direction and a magnitude, "
                 "with a %.0f%% radial deadzone. The engine's own path is not used for it: that "
                 "one cuts a square deadzone of thirty per cent per axis without rescaling, and "
                 "the shipped bindings bind each half axis to the same control twice, so the sum "
                 "saturates at half the stick's travel and a diagonal past that point reads as a "
                 "corner whichever way it is really pointing",
                 pad_state.controller_index, (double)(pad_state.deadzone * 100.0f));
    }

    /* No sign change: XInput reports Y positive UP, and up is forward, which is the sense every
     * consumer here wants. The right stick in controller_input.dll DOES flip it, because there up
     * has to become a downward mouse movement, and the two conventions are easy to confuse. */
    if (!stick_apply_radial_deadzone(state.Gamepad.sThumbLX, state.Gamepad.sThumbLY,
                                     pad_state.deadzone, &x, &y)) {
        pad_state.running = false;      /* letting go drops back to a walk with no boundary logic */
        return;
    }

    pad_state.x         = x;
    pad_state.y         = y;
    pad_state.magnitude = stick_magnitude(x, y);
    pad_state.active    = true;

    /* The hysteresis is a band, not a point, and it is latched: once running, it takes a push
     * BELOW the lower edge to drop back to a walk, and once walking a push ABOVE the upper edge to
     * break into a run. A single threshold would flicker the clip and the speed cap on and off
     * while the stick rested on it, which reads as the character stuttering. */
    if (pad_state.running) {
        if (pad_state.magnitude < pad_state.run_threshold - pad_state.hysteresis) {
            pad_state.running = false;
        }
    } else if (pad_state.magnitude > pad_state.run_threshold + pad_state.hysteresis) {
        pad_state.running = true;
    }
}

/* How far DOWN the stick has to go before it means back-pedal rather than sidestep. Well clear
 * of the wander in a hand holding the stick horizontally; see pad_stick.h. */
#define PAD_BACKWARD_THRESHOLD 0.35f

bool pad_stick_take_substep(uint8_t *record, bool stand_mode, bool strafe_invert,
                            float *out_strafe, float *out_forward)
{
    float forward;
    float strafe;

    pad_stick_poll();
    if (!pad_stick_is_active() || !stand_mode || record == NULL ||
        out_strafe == NULL || out_forward == NULL) {
        return false;
    }

    forward = pad_stick_y();
    if (forward < 0.0f && forward > -PAD_BACKWARD_THRESHOLD) {
        forward = 0.0f;
    }
    strafe = pad_stick_x();

    strafe_walk_apply_stick_move(record, forward, strafe);

    *out_forward = forward;
    *out_strafe  = strafe_invert ? -strafe : strafe;
    return true;
}

bool pad_stick_is_active(void)
{
    return pad_state.active;
}

float pad_stick_x(void)
{
    return pad_state.active ? pad_state.x : 0.0f;
}

float pad_stick_y(void)
{
    return pad_state.active ? pad_state.y : 0.0f;
}

float pad_stick_magnitude(void)
{
    return pad_state.active ? pad_state.magnitude : 0.0f;
}

bool pad_stick_wants_run(void)
{
    return pad_state.active && pad_state.running;
}
