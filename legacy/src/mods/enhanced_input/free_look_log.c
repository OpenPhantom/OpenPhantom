/* free_look_log.c: naming the one event in free look that nobody can otherwise name.
 *
 * When the arming gate closes, this feature stops writing two cells. Nothing else happens: no
 * message, no setting change, no byte written anywhere. The engine's own recentre then pulls the
 * camera back toward the region's authored yaw at four per cent of the remaining gap per rendered
 * frame, and what reaches the player is a camera that turns by itself for a reason he cannot see.
 * Ten separate conditions produce that identical symptom.
 *
 * So each line carries the condition BY NAME, the camera region under the player and its flags, the
 * yaw on screen against the yaw free look wants, and the camera's PITCH and EYE HEIGHT.
 *
 * The last two are there to be WATCHED rather than because this feature can move them. It writes
 * neither: the pitch is built by a different lerp with a rate pushed as an immediate, and the eye
 * height is composed by adding an UNROTATED Z, so a horizontal free look provably cannot tilt the
 * view or raise the eye. When those two numbers move across a transition, what moved them is the
 * authored pitch and camera offset of the region named on the same line, and having them printed
 * either side of every transition is what turns that argument from a promise into something a
 * player can check.
 *
 * It is off unless the ini asks for it, because a normal session should be silent.
 */
#include "free_look_log.h"

#include "camera_sites.h"
#include "free_look_math.h"

#include "common/logging.h"
#include "common/memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Bounded, because the gate is read once per rendered frame and a player standing exactly on a
 * region boundary can flip it every frame. Four hundred lines is a whole level's worth of honest
 * transitions and about eighty kilobytes of that pathological case. */
#define MAX_TRANSITION_LINES 400

typedef struct free_look_log_state {
    const camera_sites_t *camera;
    bool                  enabled;

    /* `reason` is kept as well as `armed` so that a release which changes its reason, a region
     * handing over to a cutscene, is one event rather than a silence. */
    bool                  state_known;
    bool                  armed;
    free_look_release_t   reason;
    int                   lines;
} free_look_log_state_t;

static free_look_log_state_t log_state;

void free_look_log_init(const camera_sites_t *camera, bool enabled)
{
    log_state.camera      = camera;
    log_state.enabled     = enabled && (camera != NULL);
    log_state.state_known = false;
    log_state.lines       = 0;
}

int free_look_log_line_budget(void)
{
    return MAX_TRANSITION_LINES;
}

bool free_look_log_is_new(bool armed, free_look_release_t reason)
{
    if (!log_state.enabled) {
        return false;
    }
    if (log_state.state_known && log_state.armed == armed && log_state.reason == reason) {
        return false;
    }

    log_state.state_known = true;
    log_state.armed       = armed;
    log_state.reason      = reason;
    return true;
}

void free_look_log_transition(bool armed, free_look_release_t reason, const char *note,
                              const free_look_gate_t *gate, const uint8_t *region,
                              bool wanted_valid, float wanted_yaw)
{
    const uint8_t *view;
    float          shown_yaw  = 0.0f;
    float          pitch      = 0.0f;
    float          eye_height = 0.0f;
    bool           view_read  = false;
    char           wanted[80];

    if (!log_state.enabled || gate == NULL || note == NULL) {
        return;
    }

    log_state.lines++;
    if (log_state.lines > MAX_TRANSITION_LINES) {
        if (log_state.lines == MAX_TRANSITION_LINES + 1) {
            log_warning("free look has logged %d gate transitions and logs no more this session. A "
                        "count that high is itself the finding: the gate is flapping rather than "
                        "changing, and the last lines above name what it is flapping on.",
                        MAX_TRANSITION_LINES);
        }
        return;
    }

    view = (const uint8_t *)*log_state.camera->view;
    if (view != NULL && memory_is_readable_range((uintptr_t)view, BAPVIEW_READ_SIZE)) {
        view_read  = true;
        shown_yaw  = *(const float *)(view + BAPVIEW_EULER_YAW_OFFSET);
        pitch      = *(const float *)(view + BAPVIEW_EULER_PITCH_OFFSET);
        eye_height = *(const float *)(view + BAPVIEW_EYE_Z_OFFSET);
    }

    if (wanted_valid && view_read) {
        _snprintf(wanted, sizeof(wanted), "%.1f (%+.1f from what is on screen)",
                  (double)wanted_yaw, (double)free_look_wrap180(wanted_yaw - shown_yaw));
    } else if (wanted_valid) {
        _snprintf(wanted, sizeof(wanted), "%.1f", (double)wanted_yaw);
    } else {
        _snprintf(wanted, sizeof(wanted), "nothing, the wanted yaw is dropped");
    }
    wanted[sizeof(wanted) - 1] = '\0';

    log_info("free look %s: %s%s%s. Camera yaw %.1f, free look wants %s. PITCH %.1f, EYE HEIGHT "
             "%.3f%s. Region %08X flags %02X, its authored yaw %.1f. gOver %d, snap %d, camera "
             "state %d, module %d, mode %d.",
             armed ? "ARMED" : "RELEASED",
             free_look_release_name(reason),
             (note[0] != '\0') ? ", " : "", note,
             (double)shown_yaw, wanted, (double)pitch, (double)eye_height,
             view_read ? "" : " (the camera object was unreadable, so those three read 0)",
             (unsigned)(uintptr_t)region, (unsigned)gate->region_flags,
             (double)*log_state.camera->region_yaw,
             (int)gate->camera_override, (int)gate->snap_countdown, (int)gate->view_state,
             (int)gate->module_state, (int)gate->mode_index);
}
