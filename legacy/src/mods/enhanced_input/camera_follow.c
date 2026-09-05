/* camera_follow.c: see camera_follow.h. */
#include "camera_follow.h"

#include "free_look.h"
#include "strafe_walk.h"

#include "common/logging.h"

#include <math.h>


static struct {
    bool  enabled;          /* the ini said yes AND strafe is on; see camera_follow_configure */
    bool  strafe_enabled;
    float settle_seconds;
    float max_rate;
    float strength;
    float max_degrees;
    float offset;           /* the damped angle, in degrees off the interpolated heading */
    bool  logged;
} follow_state;

void camera_follow_configure(bool enabled, bool strafe_enabled, float settle_seconds,
                             float max_rate_deg_per_second, float strength,
                             float max_degrees)
{
    follow_state.strafe_enabled = strafe_enabled;
    follow_state.enabled        = enabled && strafe_enabled;
    follow_state.settle_seconds = settle_seconds;
    follow_state.max_rate       = max_rate_deg_per_second;
    follow_state.strength       = strength;
    follow_state.max_degrees    = max_degrees;
    follow_state.offset         = 0.0f;

    if (enabled && !strafe_enabled) {
        log_warning("CameraFollow=1 but Strafe=0, so the camera follow is not armed: without "
                    "strafe the walk never leaves the heading, so there is nothing to follow");
    }
}

bool camera_follow_wants_camera(void)
{
    /* Free look owns the same hold, and the two wanting it at once is the fault the first version
     * of this file had against the engine's recentre. One driver at a time, and free look wins
     * because the player asked for it explicitly. */
    return follow_state.enabled && !free_look_is_enabled();
}

float camera_follow_offset_degrees(void)
{
    return follow_state.enabled ? follow_state.offset : 0.0f;
}

void camera_follow_reset(void)
{
    follow_state.offset = 0.0f;
}

void camera_follow_step(float travel_degrees, float frame_seconds)
{
    float target;

    if (!camera_follow_wants_camera()) {
        camera_follow_reset();
        return;
    }
    if (!(frame_seconds > 0.0f) || !(frame_seconds < 0.5f)) {
        return;                    /* no time passed, or a hitch long enough to be a level load */
    }
    if (!isfinite(travel_degrees)) {
        return;
    }

    /* A SHARE of the angle the walk is going in, not the angle itself, and capped. Following it
     * outright was the first version's mistake: the travel angle reaches a right angle on a held
     * sidestep, the view swung round behind it, and the player was left pushing a stick whose
     * sideways direction no longer pointed sideways on screen. See camera_follow.h.
     *
     * Zero when the stick is centred, which is what makes letting go a drift home rather than a
     * case of its own. The damper is strafe's, so the camera and the body settle with the same
     * shape of curve and only their constants differ. */
    target = travel_degrees * follow_state.strength;
    if (target >  follow_state.max_degrees) { target =  follow_state.max_degrees; }
    if (target < -follow_state.max_degrees) { target = -follow_state.max_degrees; }

    follow_state.offset = strafe_walk_damp_step(follow_state.offset, target, frame_seconds,
                                                follow_state.settle_seconds, follow_state.max_rate);
    if (!isfinite(follow_state.offset)) {
        follow_state.offset = 0.0f;      /* never hand the camera a value that is not a number */
    }

    if (!follow_state.logged && follow_state.offset != 0.0f) {
        follow_state.logged = true;
        log_info("camera follow: the camera drifts toward the direction of travel, taking %.0f%% "
                 "of the angle and never more than %.0f degrees of it, 90%% of a gap in %.2f s "
                 "and never faster than %.0f deg/s. It takes a share rather than the whole angle "
                 "because the stick is heading-relative: a view swung the full right angle of a "
                 "held sidestep leaves sideways on the stick pointing somewhere else on screen. "
                 "It is driven through free look's own camera hold, so the recentre is frozen "
                 "and authored regions release it the same way they release free look",
                 (double)(follow_state.strength * 100.0f), (double)follow_state.max_degrees,
                 (double)follow_state.settle_seconds, (double)follow_state.max_rate);
    }
}
