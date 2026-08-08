/* free_look_camera.c: the half of free look that runs inside the engine's camera update.
 *
 * ==============================================================================================
 * The engine already owns the number
 *
 * The follow camera's yaw is one addition:
 *
 *     cameraYaw = interpolatedPlayerHeading + cameraYawOffset
 *
 * and that offset is a persistent global the engine keeps, wraps, saves and restores. Horizontal
 * free look is therefore a two-cell data patch, write the offset, freeze the one damper that
 * pulls it home, with no byte written anywhere in the pitch path. The camera pitch is a
 * different field, built from a different lerp with a different rate that is pushed as an
 * immediate rather than read from the frozen cell, and the eye height is composed by adding an
 * UNROTATED Z, so a horizontal free look provably cannot tilt the view or raise the eye.
 *
 * ==============================================================================================
 * Why the write happens in a detour on updateCam and not in a frame callback
 *
 * The obvious site is the per-frame hook this DLL already owns. It is wrong, and the way it is
 * wrong is the exact defect the offset exists to remove. The frame ends AFTER the camera has
 * already consumed the offset, so a value written there is read by the NEXT frame's camera, by
 * which time the substeps have moved the interpolated heading the offset was computed against.
 * The camera then lands at "what we wanted, plus the frame's heading change": nothing at all while
 * the body stands still, and up to nine degrees of swing per frame while it turns fast. A free
 * look that is exact only when the body is not moving is not a free look.
 *
 * The only instant at which the interpolated heading the camera is about to use already exists is
 * between the last substep and the camera update. So the offset is written from a chained detour
 * on the camera update's own prologue, immediately before the original runs, out of the same three
 * globals it is about to read. There is no residual and no prediction.
 *
 * Freezing the recentre from there is better as well: the rate cell is read once inside that call
 * and rewritten from an immediate at its own tail, so our 1.0 exists for exactly the one read it
 * is meant for. A frame that runs no camera update, a paused frame, where the engine skips the
 * whole broadcast, never sees our write at all.
 *
 * A consequence worth stating because somebody will otherwise reach for the obvious tool: THIS
 * Feature registers no per-FRAME CALLBACK, and it must not grow one. The mouse bank it drains is
 * filled by a callback at the frame end, and two callbacks on the same frame hook run in
 * registration order, a coupling that is invisible in the type system and that a later edit
 * would silently invert. Writing from inside the camera update instead puts the engine's own frame
 * layout between the fill and the drain, where no edit of ours can reorder it.
 *
 * ==============================================================================================
 * What must be exempt, and why the release is "STOP WRITING"
 *
 * A held offset applied when a scripted camera begins starts that shot rotated. That is bounded:
 * the engine's own recentre swings it into place over about half a second, and a region flagged as
 * a cut snaps it instantly. What would NOT be bounded is a frozen recentre left behind, the
 * swing would never happen and the scripted shot would stay rotated for good. The protection is
 * structural rather than careful: the camera update rewrites the rate from its immediate at the
 * end of every call, so the release is literally to stop writing, and there is no restore path
 * that can be got wrong.
 *
 * And the two kinds of release are not the same release, which is the thing this feature used to
 * get wrong. A SCRIPT taking the camera, a cutscene, a warp, a load, the death arm, ends with
 * the camera deliberately placed, so the wanted yaw is DROPPED and the next armed frame seeds it
 * from the live cell: coming back from a cutscene puts the camera where the engine has just
 * recentred it, behind the player, instead of where the mouse left it a minute ago.
 *
 * An AUTHORED CAMERA REGION on the floor under the player's feet is a different event with the same
 * shape, and every shipped level carries between one and eight of them, chosen per floor polygon.
 * A region therefore REMEMBERS the wanted yaw across its hold and gives it back, bounded by an
 * angle. The reasoning is at release_for(), where the decision is made.
 *
 * The save game is settled by the scripted half. The offset is part of the save block, so a save
 * taken under free look carries a rotated one, but a load sets the camera's snap countdown, the
 * exemption releases while that countdown runs, and the engine's own snap overwrites the restored
 * offset with the region's authored yaw before we ever write again. A save taken under free look
 * therefore restores with the camera behind the player. That is deliberate, not emergent.
 *
 * ==============================================================================================
 * Why the hook stands even while the feature is off
 *
 * That release is also what makes the control mode a mid-session setting rather than a choice
 * frozen at launch. Switching off IS a release: it costs nothing and restores nothing. Switching
 * on re-seeds the wanted yaw from the live cell, which is the path every re-arming already takes.
 * Both directions were therefore already implemented and both are exact.
 *
 * What that requires is the detour standing whether the feature is on or off. It does, and while
 * the feature is off it writes not one byte, the arming gate refuses on the switch itself,
 * before it gathers anything. The alternative, resolving the camera and patching its prologue the
 * moment a check box is ticked, would write to code from inside a menu screen's own loop, and
 * would have to find a prologue this DLL may by then have overwritten itself.
 *
 * ==============================================================================================
 * The camera has two yaw arms, and this feature only ever worked on one of them
 *
 * The addition above is the SIMPLE arm. There is a second one that EASES the yaw toward the wanted
 * angle over a frame, and which of the two runs is decided by a global holding the signed per-frame
 * change of the BODY's target heading: zero picks the simple arm, anything else picks the eased one.
 *
 * The eased arm's 0/360 seam handling is derived from the sign of that change. While the camera is
 * bolted to the body that is a fair proxy for "which way round should I go"; free look is exactly
 * The thing that makes it wrong. Measured: a camera 2.7 degrees from its target was read as 357.3
 * degrees away and moved 78 degrees in one frame.
 *
 * So the ARM IS FORCED. On armed frames this feature writes the interpolated heading the camera is
 * about to use into the cell the engine compares against, the engine measures a change of exactly
 * zero, and the simple arm runs. The full listing, the reason the eased arm must NOT instead be
 * inverted, and the proof that the cell has exactly two references image-wide are all at the
 * pattern in camera_sites.c, which is where the bytes are.
 *
 * WHAT THAT COSTS, stated here because it is a real change and not a pure repair: on armed frames
 * the engine's one-frame easing of the camera YAW is gone and the yaw becomes exactly what free
 * look asked for. The eye position keeps its own lag, the pitch is untouched; it is built further
 * upstream from a different lerp, and on released frames the engine chooses its arm as it always
 * did. If the cell does not resolve, free look still runs with the defect and the install log says
 * so in as many words.
 */
#include "free_look_internal.h"

#include "camera_sites.h"
#include "camera_watch.h"
#include "free_look.h"
#include "free_look_log.h"
#include "free_look_math.h"
#include "enhanced_input.h"
#include "mouse_look.h"
#include "player_record.h"
#include "player_sites.h"

#include "common/detour.h"
#include "common/logging.h"
#include "common/memory.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Writing this into the recentre rate makes the engine's angular lerp return its first argument
 * unchanged, i.e. leaves the offset exactly where we put it. It is not a strict identity: the
 * lerp returns the target outright when the two are within a thousandth of a degree, before it
 * looks at the rate at all. Inside that band the offset is snapped to the region's authored yaw
 * whatever we write, which is a thousandth of a degree and is named rather than hidden. */
#define RECENTRE_FROZEN 1.0f

typedef int32_t (__cdecl *update_cam_fn_t)(void);

/* The one instance lives in free_look.c; this is the pointer handed over at install time. NULL
 * until then, and the detour is not placed until it is set, so no hook can dereference it. */
static free_look_state_t *free_state;

void free_look_camera_release(void)
{
    free_state->armed               = false;
    free_state->camera_yaw_valid    = false;
    free_state->body_target_valid   = false;
    free_state->region_memory_valid = false;
}

/* One line per CHANGE of the gate, never one per frame. The line itself, its budget and its four
 * state fields belong to free_look_log.c; what stays here is only the decision to emit one. */
static void log_gate(bool armed, free_look_release_t reason, const char *note,
                     const free_look_gate_t *gate, const uint8_t *region)
{
    if (free_look_log_is_new(armed, reason)) {
        free_look_log_transition(armed, reason, note, gate, region,
                                 free_state->camera_yaw_valid, free_state->camera_yaw);
    }
}

static void refuse_this_frame(const char *what, const free_look_gate_t *gate,
                              const uint8_t *region)
{
    log_gate(false, FREE_LOOK_RELEASE_NOT_A_NUMBER, what, gate, region);

    free_look_camera_release();
    if (!free_state->warned_not_finite) {
        free_state->warned_not_finite = true;
        log_warning("%s is not a finite number, so nothing was written to the camera. Free look "
                    "releases until it becomes one again; this is reported once.", what);
    }
}

/* ==============================================================================================
 * The question a release has to answer, and did not
 *
 * Dropping the wanted yaw is right when a SCRIPT or the engine has taken the shot: a cutscene, a
 * warp, a load and the death arm each end with the camera deliberately placed, and putting a
 * minute-old mouse angle back on top of that would undo the placement.
 *
 * It is wrong when the level author merely placed a camera on the floor the player is standing on.
 * The player walks in and out of those in a second or two, and while he is inside the engine's own
 * recentre is eating his aim at four per cent a frame. Dropping the yaw means the camera he gets
 * back is wherever that eating happened to stop, which depends on how many frames he spent on
 * those polygons and on nothing else. That is what "the camera rotates at random while walking"
 * looks like from the inside.
 *
 * So an authored region REMEMBERS the yaw, once, at the moment it takes the camera. It is not
 * refreshed while the region holds: refreshing it would track the engine's recentre and there would
 * be nothing left to recover.
 * ============================================================================================ */
static void release_for(free_look_release_t reason)
{
    bool  keep = free_look_release_is_authored_region(reason) &&
                 free_state->config.region_recover_degrees > 0.0f &&
                 (free_state->region_memory_valid || free_state->camera_yaw_valid);
    float remembered = free_state->region_memory_valid ? free_state->region_memory_yaw
                                                       : free_state->camera_yaw;

    free_look_camera_release();
    if (keep) {
        free_state->region_memory_valid = true;
        free_state->region_memory_yaw   = remembered;
    }
}

/* Picks the camera up exactly where the engine has it, so that arming never jumps, or, when an
 * authored region has just let go and the engine's recentre has turned the camera only a little way
 * home in the meantime, takes the player's own yaw back instead and undoes that turn.
 *
 * The seed is read from the camera, not reconstructed from its inputs. This used to compute
 * wrap360(interpolatedHeading + the live offset), which is the SIMPLE arm's formula, and the
 * simple arm is not the one that ran on the frame being picked up from. On any frame the body was
 * turning, the engine had produced an eased value instead, so the seed was a number that had never
 * been on screen and arming visibly jumped.
 *
 * The camera object's own euler.y IS the answer, whichever arm produced it, so it is read straight
 * out of the field the engine stored it in. There is no fallback worth having: a camera object that
 * cannot be read is a camera whose yaw we could not honour anyway, so the seed refuses and the next
 * frame tries again.
 *
 * Returns a note for the transition log, never NULL. */
static const char *seed_camera_yaw(void)
{
    const uint8_t *view = (const uint8_t *)*free_state->camera.view;
    float          engine_yaw;
    bool           recovered = false;

    if (view == NULL || !memory_is_readable_range((uintptr_t)view, BAPVIEW_READ_SIZE)) {
        return "";
    }

    engine_yaw = *(const float *)(view + BAPVIEW_EULER_YAW_OFFSET);
    if (!isfinite(engine_yaw)) {
        return "";
    }
    engine_yaw = free_look_wrap360(engine_yaw);

    if (free_state->region_memory_valid) {
        recovered = free_look_recovers_wanted_yaw(free_state->region_memory_yaw, engine_yaw,
                                                  free_state->config.region_recover_degrees);
    }

    free_state->camera_yaw       = recovered ? free_state->region_memory_yaw : engine_yaw;
    free_state->camera_yaw_valid = true;

    /* Consumed either way. A memory kept past the arming that could have used it would be applied
     * to some later, unrelated release and would then be a genuinely random rotation. */
    free_state->region_memory_valid = false;

    return recovered ? "the yaw the player had before the region is taken back" : "";
}

/* Everything the arming decision looks at, read at the one instant it is about to matter. */
static void build_gate(free_look_gate_t *gate, uint8_t **out_record, const uint8_t **out_region)
{
    uint8_t          *record = player_sites_record(free_state->player);
    void             *view   = *free_state->camera.view;
    const uint8_t    *region = NULL;

    gate->enabled         = free_state->installed && free_state->config.enabled;
    gate->record_valid    = (record != NULL);
    gate->module_state    = (record != NULL) ? *(const int32_t *)(record + PLAYER_MODULE_STATE) : 0;
    gate->mode_index      = player_sites_mode_index(free_state->player, record);
    gate->view_valid      = (view != NULL) &&
                            memory_is_readable_range((uintptr_t)view, sizeof(int32_t));
    gate->view_state      = gate->view_valid ? *(const int32_t *)view : 0;
    gate->camera_override = *free_state->camera.camera_override;
    gate->snap_countdown  = *free_state->camera.snap_countdown;
    gate->region_known    = false;
    gate->region_flags    = 0;

    if (free_state->camera.current_region != NULL) {
        region = *free_state->camera.current_region;
        if (region != NULL && memory_is_readable_range((uintptr_t)region, sizeof(uint32_t))) {
            gate->region_known = true;
            gate->region_flags = *(const uint32_t *)region;
        }
    }

    *out_record = record;
    *out_region = region;
}

/* Runs once per rendered frame, immediately before the camera update reads the two cells. */
static void update_camera_yaw(void)
{
    free_look_gate_t     gate;
    free_look_release_t  reason;
    uint8_t             *record = NULL;
    const uint8_t       *region = NULL;
    const char          *note   = "";
    float                interpolated;
    float                offset;

    /* The switch is tested before anything is gathered, because the machinery is installed even
     * while the feature is off and this hook then runs on every rendered frame. build_gate reads
     * eight cells to answer a question the switch has already answered, so it is called here only
     * on the one frame the switch actually changes, and only when the log is asked for. */
    if (!free_look_is_enabled()) {
        if (free_look_log_is_new(false, FREE_LOOK_RELEASE_SWITCHED_OFF)) {
            build_gate(&gate, &record, &region);
            free_look_log_transition(false, FREE_LOOK_RELEASE_SWITCHED_OFF, "", &gate, region,
                                     free_state->camera_yaw_valid, free_state->camera_yaw);
        }
        free_look_camera_release();

        /* ---- And the other control mode needs the same arm, which this file first missed --------
         *
         * Forcing the plain yaw arm was built for free look and gated on free look, on the reading
         * that the eased arm only misbehaves once the camera is decoupled from the body. That was
         * half the truth. The eased arm carries the camera toward the body at about a fifth of the
         * remaining gap per frame, fine when the body turns at the engine's own 120 deg/s ceiling,
         * which is all a keyboard could ever ask for. MOUSE LOOK writes the heading directly and can
         * turn the body a quarter turn in a single substep, so the camera falls hundreds of degrees
         * behind and then spends a third of a second catching up. Measured in the field with free
         * look OFF and the yaw offset at exactly zero: the camera 225 degrees off the body, hauling
         * itself back in forty-four steps of thirty to ninety degrees each.
         *
         * The camera is not decoupled here, so there is no offset to compute and none is written,
         * this only tells the engine the target did not move, which makes the camera sit exactly
         * where the engine's own arithmetic already wants it instead of easing toward it. That is
         * what a shooter camera does, and it is why turning the two optional features off no longer
         * means turning this off with them.
         *
         * Gated on everything free look gates on except free look itself, so a cutscene, a scripted
         * camera, an authored fixed region, a snap or a parked player all keep the engine's easing.
         * The switch is there because this is a change in FEEL rather than a repair of a fault. */
        if (!free_state->config.rigid_mouse_look_camera ||
            free_state->camera.last_interp == NULL ||
            !enhanced_input_is_active() || free_look_is_enabled()) {
            return;
        }

        build_gate(&gate, &record, &region);
        gate.enabled = true;                    /* ask every question except "is free look on" */
        if (free_look_gate_refusal(&gate) != FREE_LOOK_ARMED) {
            return;
        }

        interpolated = free_look_interpolated_heading(*free_state->camera.head_previous,
                                                      *free_state->camera.head_current,
                                                      *free_state->camera.substep_alpha);
        if (isfinite(interpolated)) {
            *free_state->camera.last_interp = interpolated;
        }
        return;
    }

    build_gate(&gate, &record, &region);
    reason = free_look_gate_refusal(&gate);
    if (reason != FREE_LOOK_ARMED) {
        /* Logged BEFORE the release, so the line can still say what is being given up. */
        log_gate(false, reason, "", &gate, region);
        release_for(reason);
        return;
    }

    interpolated = free_look_interpolated_heading(*free_state->camera.head_previous,
                                                  *free_state->camera.head_current,
                                                  *free_state->camera.substep_alpha);
    if (!isfinite(interpolated)) {
        refuse_this_frame("the camera's interpolated heading", &gate, region);
        return;
    }

    if (!free_state->camera_yaw_valid) {
        note = seed_camera_yaw();

        /* The seed reads the camera object and can decline. Nothing may be written on a frame with
         * no seed: the wanted yaw would be whatever was last in the field, which is either zero or
         * a yaw from before the last release. */
        if (!free_state->camera_yaw_valid) {
            refuse_this_frame("the camera object's own yaw", &gate, region);
            return;
        }
    }

    /* Frames that ran no substep banked mouse motion nobody has taken. Taking it here is what
     * makes the camera advance on EVERY rendered frame instead of only on the roughly one frame
     * in five that runs a substep at a high frame rate.
     *
     * The ordering this rests on, and it is in no type system. Within one frame the engine runs
     * the substeps (where phase 2 drains the bank), then this camera update, and only afterwards
     * the frame end, where the bank is FILLED. So the fill can never fall between the two drains,
     * and a drain that finds the bank already emptied by phase 2 returns zero rather than the same
     * sample twice. That is the engine's own frame layout doing the work, which is why free look
     * needs no per-frame callback of its own and must not grow one: a callback would sit next to
     * the bank's filler at the frame end and its result would depend on registration order.
     *
     * It is safe only because the bank consumes and ZEROES. On the degraded path the axis is read
     * live and unzeroed, both readers would receive the same sample, and this is switched off. */
    if (free_state->drain_per_frame) {
        free_state->camera_yaw =
            free_look_wrap360(free_state->camera_yaw + mouse_look_take_step_degrees());
    }

    /* Nothing clamps the camera against the body here, and nothing may.
     *
     * There was a leash: the camera was pulled back whenever it sat more than a set angle off the
     * heading. It was wrong in the one way that matters, because it made the camera a function of
     * the body again, which is the single thing this whole feature exists to undo. It showed up
     * as three separate complaints that were one defect: the camera could not be orbited past the
     * limit; a mouse held at the limit felt stuck, because every frame re-clamped it; and pressing
     * back turned the body a half turn, which instantly exceeded the limit and dragged the camera
     * round with it.
     *
     * The safety it was supposed to buy was already there without it. The body is turned toward
     * the camera the moment the player asks to move, so any gap closes by itself in a fraction of
     * a second, and the arming gate releases the camera outright in every state where the body
     * cannot be turned at all. */
    offset = free_look_offset(free_state->camera_yaw, interpolated);
    if (!isfinite(offset) || !isfinite(free_state->camera_yaw)) {
        refuse_this_frame("the camera yaw offset", &gate, region);
        return;
    }

    /* Logged before the stores, so the "camera yaw" it quotes is still the engine's own and the
     * difference from what free look wants is the size of this frame's step. */
    log_gate(true, FREE_LOOK_ARMED, note, &gate, region);

    /* THE ARM SELECT, and it has to be this value rather than any other.
     *
     * A few instructions into the original the engine takes the wrapped difference between this
     * cell and the very heading computed above, negates it, and stores it as "how far the target
     * heading moved this frame". Anything but zero there selects the eased yaw arm, whose seam
     * handling is built for a camera bolted to the body and is wrong for this one.
     *
     * Writing the same heading the engine is about to interpolate makes that difference zero. It
     * does not have to be bit-exact: the engine zeroes the result below 0.05 degrees a few
     * instructions later, and that deadband absorbs the float error between its interpolation and
     * ours. The engine overwrites this cell with its own value inside the same call, so the write
     * leaves nothing behind and there is nothing to restore on release.
     *
     * NULL when the site did not resolve. The feature then runs exactly as it did before this was
     * understood, with the defect, and the install log says so. */
    if (free_state->camera.last_interp != NULL) {
        *free_state->camera.last_interp = interpolated;
    }

    *free_state->camera.camera_yaw_offset = offset;
    *free_state->camera.yaw_lag           = RECENTRE_FROZEN;
    free_state->armed                     = true;
}

static int32_t __cdecl hook_update_cam(void)
{
    update_cam_fn_t original = (update_cam_fn_t)free_state->update_detour.original;
    int32_t         result;

    update_camera_yaw();

    /* The inputs are recorded here, before the original: it rewrites the yaw offset at its own
     * tail, so a value read afterwards is not the one the yaw was built from. */
    camera_watch_before_update();
    result = original();

    /* AFTER the original, because the yaw this watches is written inside it. It is deliberately
     * outside every gate above: a camera that swings while free look is RELEASED is exactly the
     * case worth catching, and the watch costs one float compare when nothing is wrong. */
    camera_watch_sample();
    return result;
}

bool free_look_camera_install(free_look_state_t *state)
{
    /* BEFORE detour_install, never after. The branch is live the instant it is written and the
     * hook dereferences this pointer on its first line. */
    free_state = state;

    if (!detour_install(&state->update_detour, state->camera.update_cam,
                        (const void *)hook_update_cam, CAMERA_UPDATE_PROLOGUE_SIZE)) {
        log_error("the camera update at %08X could not be detoured, free look stays OFF and "
                  "nothing has been changed", (unsigned)state->camera.update_cam);
        return false;
    }
    return true;
}
