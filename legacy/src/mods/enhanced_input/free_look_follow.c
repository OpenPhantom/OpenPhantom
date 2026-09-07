/* free_look_follow.c: see free_look_follow.h. */
#include "free_look_follow.h"

#include "free_look.h"
#include "free_look_math.h"
#include "frame_clock.h"
#include "mouse_look.h"
#include "player_record.h"
#include "strafe_walk.h"

#include "common/logging.h"

#include <math.h>


/* ---- Why the drift stopped ------------------------------------------------------------------
 *
 * The arming gate has a transition log of its own and it does not cover this. That log answers
 * "the camera was released", and the four tests below can all refuse on a frame where the gate is
 * perfectly armed and free look is still turning the camera by hand. From the player's side the
 * two are one symptom, the drift stops, so a report of it is exactly as likely to be either, and
 * a session logged with only the first half can come back silent and cost a test run.
 *
 * The hold-off is deliberately NOT one of the states. It changes several times a second while
 * anybody is looking around, which is the feature working, and logging it would bury the one line
 * that matters under hundreds that do not. Waiting and stepping are both RUNNING here. */
typedef enum follow_stall {
    FOLLOW_RUNNING = 0,     /* stepping, or holding off, which is the same health */
    FOLLOW_SWITCHED_OFF,
    FOLLOW_FREE_LOOK_OFF,
    FOLLOW_AIM_STANCE,
    FOLLOW_NO_CAMERA_YAW,
    FOLLOW_FRAME_UNUSABLE
} follow_stall_t;

static const char *const FOLLOW_STALL_TEXT[] = {
    "is drifting again",
    "is switched off",
    "is idle because free look is off, which it is built on and cannot run without",
    "is standing off because the aim trigger is held and the body is pointed at the camera",
    "has no camera yaw to move, so the arming gate has released and its own log names why",
    "saw a frame it could not use: either no time passed or the gap was long enough to be a "
    "level load"
};

/* One line per CHANGE, never one per frame, and capped for the same reason the arming log is:
 * a player standing exactly on a boundary can flip this every frame, and then the COUNT is the
 * finding rather than any one line. */
#define FOLLOW_STALL_LINES 200

static struct {
    follow_stall_t last;
    int            lines;
} stall_log = { FOLLOW_RUNNING, 0 };

static void note_stall(const free_look_state_t *state, follow_stall_t stall)
{
    if (!state->config.log_transitions || stall == stall_log.last) {
        return;
    }
    stall_log.last = stall;

    stall_log.lines++;
    if (stall_log.lines > FOLLOW_STALL_LINES) {
        if (stall_log.lines == FOLLOW_STALL_LINES + 1) {
            log_warning("the passive camera has changed state %d times and reports no more this "
                        "session. A count that high is itself the finding: it is flapping rather "
                        "than stopping, and the lines above name what between",
                        FOLLOW_STALL_LINES);
        }
        return;
    }
    log_info("the passive camera %s", FOLLOW_STALL_TEXT[stall]);
}


/* THE PASSIVE CAMERA: one damped step toward the body, once per DRAWN FRAME.
 *
 * WHY THE FRAME AND NOT THE SUBSTEP. The first version stepped this in phase 7, at the simulation's
 * fixed 32 Hz, and it was visibly jittery. The camera is published every rendered frame, so a yaw
 * that only changes thirty two times a second is held for two, three or four frames at a time and
 * then jumps: a staircase, and above about 60 fps an obvious one. strafe_walk.c already learned
 * this about the model root and answered it by interpolating between substeps. The camera does not
 * need that answer, because it is not simulation state: it is free to move on the render clock, and
 * `interpolated` is right here, already smoothed across the substep by the engine's own alpha. So
 * the step and the thing it aims at are both on the same clock and there is nothing left to stair.
 *
 * WHY THE HEADING AND NEVER THE TRAVEL ANGLE. Under free look the stick is measured against the
 * CAMERA. Point the camera at the direction of travel and that is a loop with a gain of one: the
 * camera turns toward where you are going, the direction the stick means turns with it, and the
 * player rotates for as long as they hold it. The heading closes no loop, because nothing measures
 * the stick against the heading. And free_look_steer has already turned the body to face its
 * travel, so behind the body and behind the direction of travel are the same place. That is what
 * makes one step toward the heading read as the camera following the movement.
 *
 * THE AUTHORED ANGLE IS KEPT. Drifting at the bare heading was considered once before and rejected,
 * because seventeen shipped follow regions author a real over-the-shoulder yaw and three of them a
 * full ninety degrees, and a camera pulling to dead centre would fight every one. The region's own
 * authored yaw is the third argument of the engine's recentre and camera_sites already resolves it,
 * so the target is the heading PLUS that. When the site did not resolve this falls back to the bare
 * heading, the same degraded mode the rest of this file takes.
 *
 * IT WAITS, AND IT NEVER MOVES UNDER THE PLAYER'S HAND. A hold-off measured in real time, rather
 * than a test of whether this particular frame carried input: the right stick arrives as
 * synthesized mouse motion with a fractional remainder carried between polls, so it lands on some
 * frames and not others, and a frame-by-frame test started and stopped the drift several times a
 * second. That stutter was the other half of the jitter. */
void free_look_follow_step(free_look_state_t *state, float interpolated)
{
    float frame_seconds = frame_clock_seconds();
    float target;
    float step;

    if (!state->config.passive_follow) {
        note_stall(state, FOLLOW_SWITCHED_OFF);
        return;
    }
    if (!free_look_is_enabled()) {
        note_stall(state, FOLLOW_FREE_LOOK_OFF);
        return;
    }
    if (free_look_aim_stance()) {
        note_stall(state, FOLLOW_AIM_STANCE);
        return;
    }
    if (!state->camera_yaw_valid) {
        note_stall(state, FOLLOW_NO_CAMERA_YAW);
        return;
    }
    if (!(frame_seconds > 0.0f) || !(frame_seconds < 0.5f)) {
        /* No time passed, or a hitch long enough to be a level load. */
        note_stall(state, FOLLOW_FRAME_UNUSABLE);
        return;
    }
    note_stall(state, FOLLOW_RUNNING);

    if (state->look_seen) {
        state->look_seen         = false;
        state->look_idle_seconds = 0.0f;
        return;
    }
    state->look_idle_seconds += frame_seconds;
    if (state->look_idle_seconds < state->config.passive_hold_seconds) {
        return;
    }

    target = interpolated;
    if (state->camera.region_yaw != NULL) {
        target = free_look_wrap360(interpolated + *state->camera.region_yaw);
    }

    step = strafe_walk_damp_step(0.0f, free_look_wrap180(target - state->camera_yaw),
                                 frame_seconds, state->config.passive_settle_seconds,
                                 state->config.passive_rate);
    if (!isfinite(step)) {
        return;
    }
    state->camera_yaw = free_look_wrap360(state->camera_yaw + step);
}

