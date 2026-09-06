#ifndef FREE_LOOK_INTERNAL_H
#define FREE_LOOK_INTERNAL_H

#include "camera_sites.h"
#include "free_look_math.h"
#include "player_sites.h"

#include "common/detour.h"

#include <stdbool.h>
#include <stdint.h>

/* Shared between free_look.c and free_look_camera.c, and between nothing else.
 *
 * The feature is one thing with two halves that run at different times: the CAMERA half runs once
 * per rendered frame from inside the engine's camera update, and the BODY half runs once per
 * substep from the phase thunks. They were one file until it passed the size limit.
 *
 * What is declared here is a type. The single instance lives in free_look.c and the camera half
 * receives a pointer to it at install time, so there is no mutable global in a header and no
 * second copy of anything.
 *
 * ==============================================================================================
 * The four fields both halves touch, written down at both ends because no compiler checks them
 *
 * Splitting a file cannot split a shared invariant; it can only hide half of it. These four are
 * the whole of what crosses, and each has an order that is decided by the engine's frame layout
 * rather than by anything in this code:
 *
 *   camera_yaw          the camera half seeds it from the camera object and advances it by the
 *                       per-frame mouse bank; the body half advances it by the per-substep mouse
 *                       step. Two writers on purpose: the mouse arrives on both clocks and each
 *                       write consumes a bank that zeroes itself, so a sample cannot be counted
 *                       twice. Nothing else may write it.
 *   armed               written once per frame by the camera half, read by the body half on the
 *                       substeps that follow. The body therefore acts on the PREVIOUS frame's
 *                       decision, and that one frame of lateness is deliberate: it is a gate, not
 *                       a value, and a gate that lags by a frame merely releases a frame late.
 *   camera_yaw_valid    cleared with `armed` on every release, set only by the seed. The body
 *                       half must test it before reading camera_yaw, because a released frame
 *                       leaves the last yaw in the field.
 *   body_target_valid   the body half sets it in phase 2 and consumes it in phase 7 of the SAME
 *                       substep; the camera half only ever clears it, as part of a release.
 *
 * Everything else in the struct is either written once at install or belongs to one half alone.
 * ============================================================================================ */

typedef struct free_look_config {
    bool  enabled;
    /* Force the plain yaw arm in MOUSE LOOK too, where the camera is still bolted to the body but
     * the body now turns far faster than the engine's easing was built for. A change in feel, so
     * it has a switch. */
    bool  rigid_mouse_look_camera;
    bool  aim_snap;
    bool  aim_keeps_movement;
    float aim_twist_max;
    bool  log_transitions;
    float body_settle_seconds;
    float body_turn_rate;
    float region_recover_degrees;

    /* THE PASSIVE CAMERA. Drift the camera back behind the body while the player is not
     * looking, which is the whole of what a console third-person camera does that this game
     * never did.
     *
     * The target is the BODY'S HEADING, and it may never be the travel angle. Under free look
     * the stick is measured against the camera, so a camera that chased the travel direction
     * would close a loop with a gain of one: push sideways, the camera follows, the direction
     * the stick means rotates with it, and the player spins for as long as they hold it. The
     * heading closes no loop, because nothing measures the stick against the heading. And once
     * the body has been turned to face its travel, behind the body and behind the direction of
     * travel are the same place, which is why this reads as following the movement. */
    bool  passive_follow;
    float passive_settle_seconds;
    float passive_rate;
    float passive_hold_seconds;   /* the beat before it starts, after the player stops looking */

    /* Steering a jump. The engine is already willing: Plr_UpdateJump moves only the vertical,
     * and the horizontal is the ordinary facing times curSpeed, with both the steer and the
     * integrate running in the air. What stops it is this file's own Stand gate, which holds
     * the body's heading outside Stand and leaves the player flying wherever they launched.
     * Slower than the body's own turn on the ground, because a jump that can be pivoted is a
     * different game rather than a repair. */
    bool  air_control;
    float air_settle_seconds;
    float air_turn_rate;
} free_look_config_t;

typedef struct free_look_state {
    bool                installed;
    free_look_config_t  config;

    const player_sites_t *player;
    camera_sites_t        camera;
    detour_t              update_detour;
    detour_t              auto_aim_detour;
    detour_t              fire_shot_detour;

    /* True while the two cells are ours. Set and cleared once per rendered frame by the camera
     * update; read by the phase thunks, which run BEFORE it in the same frame and therefore act
     * on the previous frame's decision. That one frame of lateness is a gate, not a value. */
    bool  armed;
    bool  camera_yaw_valid;
    float camera_yaw;
    /* Which constants the pending body target was built with. The aim stance and the ground
     * travel turn at the body's own rate; a jump turns at the air rate, and the integrate that
     * consumes the target cannot tell which it was handed without being told. */
    float target_settle_seconds;
    float target_turn_rate;

    bool  look_seen;          /* set by any look input, consumed by the drift on the next frame */

    /* The gate's answer about the WORLD, asked every rendered frame with this feature's own
     * switch forced on, so it says whether the level has taken the camera rather than whether
     * free look happens to be running. The sideways walk and the pad stick read it: where the
     * author placed a camera, both hand the player back to the engine's own scheme. */
    free_look_release_t world_gate;
    float look_idle_seconds;  /* how long since the last of it, in real time                    */

    /* Carried across an AUTHORED-REGION release and across nothing else. It is the yaw the player
     * had when he stepped onto the region's floor, and it is taken once at the transition rather
     * than refreshed while the region holds, refreshing it would track the engine's own recentre
     * and there would be nothing left to recover. */
    bool  region_memory_valid;
    float region_memory_yaw;

    /* One substep of lifetime: written by phase 2, consumed by phase 7. */
    bool  fire_shot_armed;   /* the shot aims itself, so the snap may release the walk */
    bool  aim_stance;        /* trigger held: face the camera, let the feet strafe */
    bool  aim_lock_valid;    /* an attack is live and aim_lock holds the engine's own target lock */
    float aim_lock;          /* Plr_AutoAim's bearing, relative to the CAMERA, captured once */
    bool  body_target_valid;
    float body_target;

    /* Seconds of aim snap left. Set by the auto-aim thunk, counted down in phase 7. */
    float aim_hold_seconds;

    /* The mouse bank is drained once per rendered frame only while the bank is live. On the
     * degraded path the axis is read live and unzeroed, so a second reader would double-count. */
    bool  drain_per_frame;

    bool  warned_not_finite;
} free_look_state_t;

/* ==============================================================================================
 * The camera half. Implemented in free_look_camera.c.
 * ============================================================================================ */

/* Takes the state pointer and installs the chained detour on the engine's camera update.
 *
 * The pointer is stored before the detour is placed, and that order is not cosmetic: the moment
 * the branch is written the hook can be entered from another thread's frame, and it dereferences
 * the state immediately. Placed first, the first camera update of the session would read through
 * a null pointer.
 *
 * Returns false when the detour could not be placed, in which case NOTHING has been written into
 * the host and the caller must abandon the whole feature rather than install its other hooks. */
bool free_look_camera_install(free_look_state_t *state);

/* Stop claiming the camera: clears the arming gate and drops the wanted yaw and the body target.
 *
 * There is no restore path to get wrong. The engine's camera update rewrites the recentre rate
 * from its own immediate before every return, so ceasing to write IS the release. */
void free_look_camera_release(void);

#endif /* FREE_LOOK_INTERNAL_H */
