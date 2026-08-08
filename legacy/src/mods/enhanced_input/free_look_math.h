#ifndef FREE_LOOK_MATH_H
#define FREE_LOOK_MATH_H

#include <stdbool.h>
#include <stdint.h>

/* The arithmetic horizontal free look is made of, with no engine and no configuration in it, so
 * every one of these can be checked without the game running. Getting any of them wrong is
 * invisible until a camera is on screen:
 *
 *   * the OFFSET is the whole feature, the engine adds it to an interpolated heading, so it has
 *     to be built against exactly the heading the engine is about to use;
 *   * the INPUT ANGLE is where the signed-speed trap lives. The sideways walk builds its angle in
 *     the frame of the drive because backward is a negative speed along an unchanged facing. Free
 *     look turns the body to face its travel and always drives forward, so its angle must NOT be
 *     built that way and a lone backward key must come out as a half turn;
 *   * the GATE decides whether a scripted or authored camera is on screen, and a wrong answer
 *     there is the one defect that would reach a player as a permanently rotated cutscene.
 */

/* The camera object's first field. Anything but FOLLOW means a scripted or authored camera owns
 * the shot and the yaw offset must be left alone. */
#define BAPVIEW_STATE_FOLLOW 0

/* Camera region flags: bit 0 fixed look-at, bit 1 fixed heading, bit 2 CUT, bit 3 world-fixed.
 * Bit 4 (fast lag) is deliberately NOT in the mask; it only shortens the damper and appears on
 * follow-shaped regions, so releasing on it would cost a camera that is perfectly steerable.
 *
 * BIT 2 is in the mask even though it leaves the camera in its follow state, and that is not
 * belt and braces. Bits 0, 1 and 3 each pick a camera family and set the camera object's state; bit
 * 2 only asks for a snap instead of a blend, and the arm it selects assigns the yaw offset the
 * region's authored yaw outright, LATER in the same call than the recentre and later than anything
 * we could write in front of it. Staying armed through a cut region would therefore have the engine
 * overwrite our value every frame and us overwrite the engine's every frame, which is a flicker
 * rather than a compromise. Releasing hands the cut to the engine whole, which is what a cut is. */
#define CAMERA_REGION_FIXED_MASK 0x0Fu

/* The player module's own state word. Only 1 runs the phases, and the death arm is not 1, so this
 * one test covers both "no simulation" and "the player is dying". */
#define PLAYER_MODULE_PHASES_RUN 1

/* Everything the arming decision looks at, gathered into one structure so the decision itself is
 * arithmetic over plain numbers. */
typedef struct free_look_gate {
    bool     enabled;           /* the feature is installed and switched on                   */
    bool     record_valid;      /* the player record is there and readable                     */
    int32_t  module_state;      /* player record +0x04                                         */
    int32_t  mode_index;        /* the live mode, or -1 when it cannot be told apart           */
    bool     view_valid;        /* the camera object pointer is there and readable             */
    int32_t  view_state;        /* the camera object's first field                             */
    int32_t  camera_override;   /* non-zero: a script has forced a camera region               */
    int32_t  snap_countdown;    /* non-zero: the camera is snapping rather than blending       */
    bool     region_known;      /* the current-region pointer resolved and is readable         */
    uint32_t region_flags;      /* the region record's first field                             */
} free_look_gate_t;

/* Why the gate answers *WHICH* condition refused, and not a plain bool.
 *
 * A release is invisible from the outside. The feature simply stops writing; the engine's own
 * damper then pulls the camera back toward the region's authored yaw at four per cent a frame, and
 * the player sees the camera turn for a reason he cannot name. So the decision has to be able to
 * say what fired.
 *
 * It also has to say WHICH FAMILY fired, because the two families want opposite handling when the
 * gate opens again. A script or the engine owning the shot, a cutscene, a warp, a load, the death
 * arm, ends with the camera deliberately placed somewhere, and restoring a minute-old mouse angle
 * on top of that would undo the placement. An authored camera region under the player's own feet is
 * different: the player walked into it and will walk out of it a moment later, and the yaw he had
 * before he stepped in is still the yaw he is asking for.
 *
 * The order below is the order the conditions are tested in, and that order is load-bearing for the
 * classification rather than for the result: a script that forces a region drives the camera object
 * into the same non-follow state an authored region does, so the scripted test has to come first or
 * a cutscene would be classified as a region. */
typedef enum free_look_release {
    FREE_LOOK_ARMED = 0,                    /* nothing refused: the camera yaw is ours       */
    FREE_LOOK_RELEASE_SWITCHED_OFF,
    FREE_LOOK_RELEASE_NO_PLAYER_RECORD,
    FREE_LOOK_RELEASE_PHASES_NOT_RUNNING,   /* no level, a parked player, or the death arm   */
    FREE_LOOK_RELEASE_MODE_UNKNOWN,
    FREE_LOOK_RELEASE_SCRIPT_FORCED_REGION, /* dialogue, the director, a set piece, a turret */
    FREE_LOOK_RELEASE_SNAP_RUNNING,         /* level start, warp, new view, the frame after a load */
    FREE_LOOK_RELEASE_NO_CAMERA_OBJECT,
    FREE_LOOK_RELEASE_CAMERA_NOT_FOLLOW,    /* the author's camera family, seen on the object */
    FREE_LOOK_RELEASE_CAMERA_REGION,        /* the author's camera family, seen on the region */
    FREE_LOOK_RELEASE_NOT_A_NUMBER          /* produced at the store, never by the gate       */
} free_look_release_t;

/* FREE_LOOK_ARMED when every one of the conditions allows the camera yaw to be ours this frame,
 * otherwise the FIRST one that refused. */
free_look_release_t free_look_gate_refusal(const free_look_gate_t *gate);

/* True only when every one of the conditions allows the camera yaw to be ours this frame. */
bool free_look_gate_is_armed(const free_look_gate_t *gate);

/* A short English name for a log line. Never NULL, and never a bare number: a release nobody can
 * name is a release nobody can act on. */
const char *free_look_release_name(free_look_release_t reason);

/* True when the level author placed a camera under the player's feet, as opposed to a script, the
 * engine or this feature's own switch taking the shot away. Only these keep the wanted yaw. */
bool free_look_release_is_authored_region(free_look_release_t reason);

/* Whether the yaw the player had before an authored region took the camera should be taken back
 * now that the region has let go.
 *
 * The engine's recentre has been running throughout the release, so the camera it hands back sits
 * somewhere between where the player left it and the region's authored home, and exactly where
 * depends on how many frames the release lasted, which is what makes an untreated release read as
 * "the camera rotated at random". Taking the wanted yaw back undoes that, but only while the engine
 * has not turned the camera further than `limit_degrees`: past that the swing is no longer a stolen
 * few degrees but a genuine re-aim, and snapping over it would be a jump of its own.
 *
 * `limit_degrees <= 0` (and NaN) switches the recovery off and the engine's angle is always taken,
 * which is what the feature did before this existed. */
bool free_look_recovers_wanted_yaw(float wanted, float engine_yaw, float limit_degrees);

/* Into [0, 360) and into (-180, 180]. A value that is not a number passes through unchanged, so
 * the caller's finiteness gate sees it rather than a silently normalised zero. */
float free_look_wrap360(float degrees);
float free_look_wrap180(float degrees);

/* The engine's own formula for the heading the camera is about to be built from:
 * previous + wrap180(current, previous) * alpha. Deliberately NOT wrapped, because the engine
 * does not wrap it either and the sum is wrapped further down its own path. */
float free_look_interpolated_heading(float previous, float current, float alpha);

/* The direction the player is asking to travel, relative to the camera, in degrees and POSITIVE
 * TO THE LEFT, the direction increasing heading turns.
 *
 *   `strafe`   +1 right, -1 left, 0 none
 *   `forward`  +1 forward, -1 backward, 0 none
 *
 * Returns false when nothing is held, in which case `*out_degrees` is untouched. */
bool free_look_input_angle(float strafe, float forward, float *out_degrees);

/* There is deliberately no leash function here. Clamping the camera against the body made the
 * camera a function of the body, which is the one thing free look exists to undo. */

/* The number the engine adds to its interpolated heading to get the camera yaw. */
float free_look_offset(float camera_yaw, float interpolated_heading);

#endif /* FREE_LOOK_MATH_H */
