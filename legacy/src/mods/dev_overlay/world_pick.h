/* world_pick.h: from the pointer into the world and back, with the engine's own camera numbers.
 *
 * The engine has no probe for "what is under the pointer", but its projection is two lines and they
 * turn round exactly. A world point goes into the camera's space through the camera's world
 * transform (rdCamera +0x08, twelve floats: three rows and the translation), applied the way
 * mat34_transform applies every matrix of this engine, a row vector times the rows:
 *
 *     cam.x = m0 x + m3 y + m6 z + t.x      cam.y = m1 x + m4 y + m7 z + t.y
 *     cam.z = m2 x + m5 y + m8 z + t.z
 *
 * The world pass does exactly this for every vertex (bapvrt_transformWorld) and the candy trail
 * for every point it projects, both with the camera at +0x08. The camera's space is the engine's
 * own and not the usual one: y is the depth in front of the eye, x is right and z is up. Then
 *
 *     sx =  cam.x * focal / cam.y + centre_x      sy = -cam.z * focal / cam.y + centre_y
 *
 * which is bapvrt_projectVertex with its focal length and centre copied out of the camera once a
 * frame. Backwards, a pixel is the camera space direction whose x is sx less centre_x, whose y is
 * the focal length and whose z is centre_y less sy, and the inverse of the transform carries it
 * and the eye into the world.
 *
 * Which way the nine floats are read is the one thing here that can be wrong without anything
 * failing, because a camera looking level reads almost the same either way. It is settled by the
 * engine's own code above, and checked in the field: the eye the view record keeps must land on
 * the camera's origin under the reading used, and far from it under the other
 * (world_pick_eye_residual, world_pick_reading_verdict).
 *
 * Pure. Internal to dev_overlay.
 */
#ifndef DEV_OVERLAY_WORLD_PICK_H
#define DEV_OVERLAY_WORLD_PICK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct world_pick_camera {
    float view[12];    /* world to camera: three rows, then the translation */
    float focal;       /* pixels, rdCamera +0x3C, as the projection used it this frame */
    float centre_x;    /* the canvas centre, pixels */
    float centre_y;
} world_pick_camera_t;

/* Nearer than this, in front of the eye, a point is not projected: the engine clips its own near
 * plane at half a unit and divides by the depth. */
#define WORLD_PICK_NEAR 0.05f

/* A world point on the screen. False for a point behind the eye or on its plane, and then nothing
 * is written. `depth` may be NULL. */
bool world_pick_project(const world_pick_camera_t *camera, const float world[3], float *sx,
                        float *sy, float *depth);

/* The ray under a screen point: from the eye, of unit length. False when the transform is not a
 * clean rotation, whose transpose is then no inverse, or a number is not finite. */
bool world_pick_ray(const world_pick_camera_t *camera, float sx, float sy, float origin[3],
                    float direction[3]);

/* The ray against an upright cylinder standing on `base`, the shape a body collides as: `radius`
 * across, `height` up. The nearest distance along the ray at which it enters, in `t`, or 0 when the
 * ray starts inside. False when it misses or the cylinder is behind. `direction` is of unit length.
 */
bool world_pick_cylinder(const float origin[3], const float direction[3], const float base[3],
                         float radius, float height, float *t);

/* A body as the pointer meets it. */
typedef struct world_pick_body {
    float base[3];
    float radius;
    float height;
} world_pick_body_t;

/* Which of `count` bodies the ray enters first, nearer than `limit` along it: its index, and the
 * distance in `t` when `t` is not NULL; -1 when it enters none that near. */
int32_t world_pick_nearest(const float origin[3], const float direction[3],
                           const world_pick_body_t *bodies, uint32_t count, float limit, float *t);

/* How far `eye` lands from the camera's origin under the reading this file uses, or, `transposed`,
 * under the other one: the nine floats as columns. The first is near zero for the engine's own
 * camera and the second is not, unless the camera looks straight along a world axis. */
float world_pick_eye_residual(const world_pick_camera_t *camera, const float eye[3],
                              bool transposed);

/* What the two residuals say about the reading. It agrees when the eye lands within the first bound
 * of the origin read as rows while the columns put it at least the second bound away. When both
 * land near, the camera looks almost along a world axis or the eye stands near the world's origin,
 * the two readings cannot be told apart there, and the check is asked again later; so it is when
 * the view record names another camera than the one read. Rows landing far is a verdict: the
 * reading, or the record, does not agree. */
#define WORLD_PICK_READING_AGREES_WITHIN 0.25f
#define WORLD_PICK_READING_APART_BY      1.0f

typedef enum world_pick_reading {
    WORLD_PICK_READING_AGREES = 0,
    WORLD_PICK_READING_UNDECIDED,
    WORLD_PICK_READING_DISAGREES
} world_pick_reading_t;

world_pick_reading_t world_pick_reading_verdict(float rows, float columns, bool same_camera);

#endif /* DEV_OVERLAY_WORLD_PICK_H */
