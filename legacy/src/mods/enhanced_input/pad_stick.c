/* pad_stick.c: see pad_stick.h. */
#include "pad_stick.h"

#include "input_mode.h"
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

/* Long enough that a player who has not reached for the pad yet is never accused of a broken one,
 * short enough to be in the log before anybody gives up and files a report. The title screen and
 * the opening movies comfortably exceed it. */
#define SILENT_PAD_MS 15000u

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
    bool  running;          /* latched across substeps, so the hysteresis works */

    bool     absent;
    ULONGLONG absent_next_tick;
    bool     logged_present;

    /* Said once, the first time a poll finds nothing. Every other line this file writes describes
       what it WOULD do with a stick, so without this a pad that cannot be seen reads exactly like
       a pad that works. That silence cost a Steam Deck session. */
    bool     reported_absent;

    /* A connected pad that sends NOTHING is its own failure and looks like neither of the two
       above. XInputGetState succeeds, every line here says the stick is being read, and the device
       never puts anything in any field. Measured on a Steam Deck in desktop mode, where Steam holds
       the controls in its desktop layout and drives mouse and keyboard with them: Wine enumerates a
       pad, so the call succeeds, and not one report ever carries a value.

       Time tells it apart from a pad sitting at rest, because at rest is exactly what the
       first report of a healthy pad looks like. Nothing is claimed until the pad has been connected
       a while and every field of every report in that time has been zero. */
    ULONGLONG connected_since;
    bool      seen_any_report;
    bool      reported_silent;
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

    /* The bindings are not ours to ignore, and this is where taking the stick has to pay for
     * itself. A dialogue with a choice menu stops the player moving so the stick can pick an
     * answer, and the engine does that by swapping the whole binding set rather than by testing
     * anything. Reading XInput directly is not bound by anything and sailed straight past it: the
     * menu scrolled and the player walked at the same time.
     *
     * Standing down here rather than at the one place that WRITES movement, because the run
     * button and the air steer read this stick without going through that place, and a gate they
     * each have to remember is a gate one of them will not. A stick with nothing to say is a
     * state every consumer already handles, so this says exactly that. */
    if (!input_mode_is_gameplay()) {
        pad_state.running = false;
        return;
    }
    if (pad_state.absent && GetTickCount64() < pad_state.absent_next_tick) {
        return;
    }

    ZeroMemory(&state, sizeof state);
    result = XInputGetState((DWORD)pad_state.controller_index, &state);
    if (result != ERROR_SUCCESS) {
        if (!pad_state.reported_absent) {
            pad_state.reported_absent = true;
            log_info("no XInput controller was found in slot %d, so the left stick and the "
                     "run threshold do nothing. That is not a fault in this patch and nothing "
                     "further will be "
                     "reported about it; a pad plugged in later is picked up on its own. "
                     "If one is plugged in NOW then it is a pad this cannot see, because only "
                     "XInput devices are visible here. An XBOX pad works as it is; anything else "
                     "has to be presented as one. Add the game to Steam as a non-Steam game and "
                     "launch it from there, where Steam Input presents almost any controller as "
                     "an XInput pad, or run something that emulates XInput such as DS4Windows "
                     "for a PlayStation pad. Without one of those, an older or off-brand pad, a "
                     "PlayStation controller plugged straight in or a flight stick is invisible "
                     "here; the game's own Controls screen still reads those.",
                     pad_state.controller_index);
        }
        pad_state.absent           = true;
        pad_state.absent_next_tick = GetTickCount64() + ABSENT_RECHECK_MS;
        pad_state.running          = false;

        /* Restarted, so the silent watch below times how long THIS connection has been quiet. The
         * line it writes says "connected for N seconds", and carrying the clock across a gap when
         * the pad was not connected at all would make that sentence untrue. */
        pad_state.connected_since  = 0u;
        return;
    }
    pad_state.absent          = false;
    pad_state.reported_absent = false;

    {
        ULONGLONG now = GetTickCount64();

        if (pad_state.connected_since == 0u) {
            pad_state.connected_since = now;
        }
        if (state.Gamepad.sThumbLX != 0 || state.Gamepad.sThumbLY != 0 ||
            state.Gamepad.sThumbRX != 0 || state.Gamepad.sThumbRY != 0 ||
            state.Gamepad.bLeftTrigger != 0 || state.Gamepad.bRightTrigger != 0 ||
            state.Gamepad.wButtons != 0) {
            pad_state.seen_any_report = true;
        } else if (!pad_state.seen_any_report && !pad_state.reported_silent &&
                   now - pad_state.connected_since >= SILENT_PAD_MS) {
            pad_state.reported_silent = true;
            log_info("pad: slot %d has been connected for %u seconds and every field of every "
                     "report in that time has been zero. The device is answering and sending "
                     "nothing, which is a third thing, different from no pad at all and from a pad "
                     "this cannot see. On a Steam Deck in desktop mode that is Steam holding the "
                     "controls in its desktop layout, where they drive mouse and keyboard instead "
                     "of a gamepad. The fix is to ADD the game to Steam as a non-Steam game and "
                     "launch it from Steam: that is the configuration confirmed working on a Deck, "
                     "and it gives the window modes and every input feature here at the same time. "
                     "Elsewhere this usually means another program has taken the pad exclusively. "
                     "Nothing is wrong with this patch and it goes on watching; touch the stick "
                     "and it will be used.",
                     pad_state.controller_index, (unsigned)(SILENT_PAD_MS / 1000u));
        }
    }

    if (!pad_state.logged_present) {
        pad_state.logged_present = true;
        log_info("pad: the left stick is read from XInput slot %d as a direction and a magnitude, "
                 "with a %.0f%% radial deadzone. The engine's own path is not used for it: that "
                 "one cuts a square deadzone of thirty per cent per axis without rescaling, and "
                 "the shipped bindings bind each half axis to the same control twice, so the sum "
                 "saturates at half the stick's travel and a diagonal past that point reads as a "
                 "corner whichever way it is really pointing",
                 pad_state.controller_index, (double)(pad_state.deadzone * 100.0f));

        /* The raw report, once, stated and not interpreted. All zero here is the normal reading
         * for a pad nobody is touching, as a pad at the title screen always is, so this
         * line proves the call works and nothing more. The claim that a pad is SILENT is made
         * below, on time, and only after nothing has arrived for long enough to mean it. */
        log_info("pad: first raw report from slot %d, left stick %d,%d right stick %d,%d "
                 "triggers %u,%u buttons %04X",
                 pad_state.controller_index,
                 (int)state.Gamepad.sThumbLX, (int)state.Gamepad.sThumbLY,
                 (int)state.Gamepad.sThumbRX, (int)state.Gamepad.sThumbRY,
                 (unsigned)state.Gamepad.bLeftTrigger, (unsigned)state.Gamepad.bRightTrigger,
                 (unsigned)state.Gamepad.wButtons);
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
                            bool sideways_walk, float *out_strafe, float *out_forward)
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

    /* The move bits hear about the sideways deflection only when something is going to turn the
     * travel angle to match it. With the sideways walk off nothing does, and a sideways push then
     * set the walk-forward bit and full forward drive with nothing to redirect them: the stick
     * steered correctly, through the engine's own turn, and ran the player straight ahead at the
     * same time. Measured at a full left push with the sideways walk off: move bit 0 set, drive
     * 0.563, the same number a full forward push writes, and speed climbing to the 3.50 run cap
     * while the stick asked for no forward at all.
     *
     * The handback path below has always passed a zero here, for this reason. */
    strafe_walk_apply_stick_move(record, forward, sideways_walk ? strafe : 0.0f);

    *out_forward = forward;
    *out_strafe  = strafe_invert ? -strafe : strafe;
    return true;
}

bool pad_stick_take_handback(uint8_t *record, bool stand_mode, float *out_turn,
                             float *out_forward)
{
    float forward;

    pad_stick_poll();
    if (!pad_stick_is_active() || !stand_mode || record == NULL ||
        out_turn == NULL || out_forward == NULL) {
        return false;
    }

    forward = pad_stick_y();
    if (forward < 0.0f && forward > -PAD_BACKWARD_THRESHOLD) {
        forward = 0.0f;
    }

    /* No sideways component, so the move bits say walk or back-pedal and nothing else. The
     * sideways deflection leaves here as a turn instead. */
    strafe_walk_apply_stick_move(record, forward, 0.0f);

    *out_forward = forward;
    *out_turn    = pad_stick_x();
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
