/* world_pick.c: from the pointer into the world and back, on a camera of the test's own.
 *
 * The camera is built the way the engine builds one: a world matrix of the eye and its turn,
 * inverted into the world transform the camera keeps at +0x08, read as rows. A point projected and
 * the ray under its pixel then have to meet again, and the eye has to land on the camera's origin
 * under the reading used and away from it under the other, which is the check the field run makes
 * on the live camera until it can tell. Then the copy under the pointer: the nearest body the ray
 * enters, and none behind the wall the ray struck.
 */
#include "unittest.h"

#include "character_prop.h"
#include "spawn_place.h"
#include "world_pick.h"

#include <math.h>
#include <string.h>

#define DEG (3.14159265f / 180.0f)

/* The camera at `eye`, turned `yaw` degrees about z and pitched `pitch` degrees: its world matrix
 * has right, forward and up as rows, and the world transform is its inverse. */
static world_pick_camera_t camera_at(const float eye[3], float yaw, float pitch)
{
    world_pick_camera_t camera;
    float               world[12];
    float               cy = cosf(yaw * DEG);
    float               sy = sinf(yaw * DEG);
    float               cp = cosf(pitch * DEG);
    float               sp = sinf(pitch * DEG);

    world[0]  = cy;
    world[1]  = sy;
    world[2]  = 0.0f;
    world[3]  = -sy * cp;
    world[4]  = cy * cp;
    world[5]  = sp;
    world[6]  = sy * sp;
    world[7]  = -cy * sp;
    world[8]  = cp;
    world[9]  = eye[0];
    world[10] = eye[1];
    world[11] = eye[2];
    (void)character_prop_mat_invert(camera.view, world);
    camera.focal    = 554.256f;
    camera.centre_x = 320.0f;
    camera.centre_y = 240.0f;
    return camera;
}

static float distance_to_ray(const float p[3], const float o[3], const float d[3])
{
    float v[3] = { p[0] - o[0], p[1] - o[1], p[2] - o[2] };
    float t    = v[0] * d[0] + v[1] * d[1] + v[2] * d[2];
    float x    = v[0] - d[0] * t;
    float y    = v[1] - d[1] * t;
    float z    = v[2] - d[2] * t;

    return sqrtf(x * x + y * y + z * z);
}

static void the_projection(void)
{
    const float         eye[3]    = { 10.0f, 20.0f, 3.0f };
    world_pick_camera_t cam       = camera_at(eye, 30.0f, -20.0f);
    const float         ahead[3]  = { 10.0f - 5.0f * sinf(30.0f * DEG),
                                      20.0f + 5.0f * cosf(30.0f * DEG), 3.0f };
    const float         behind[3] = { 10.0f + 5.0f * sinf(30.0f * DEG),
                                      20.0f - 5.0f * cosf(30.0f * DEG), 3.0f };
    const float         point[3]  = { 13.0f, 26.0f, 0.5f };
    float               sx = 0.0f;
    float               sy = 0.0f;
    float               depth = 0.0f;
    float               origin[3];
    float               dir[3];

    ut_section("the projection, there and back");
    ut_check(world_pick_project(&cam, point, &sx, &sy, &depth) && depth > 0.0f,
             "a point in front of the camera projects");
    ut_check(world_pick_ray(&cam, sx, sy, origin, dir), "the ray under its pixel is cast");
    ut_near(distance_to_ray(point, origin, dir), 0.0, 1e-3,
            "and passes through the point it came from");
    ut_near(origin[0], 10.0, 1e-3, "from the eye across");
    ut_near(origin[1], 20.0, 1e-3, "and along");
    ut_near(origin[2], 3.0, 1e-3, "and up");
    ut_near(sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]), 1.0, 1e-4,
            "of unit length");
    ut_check(!world_pick_project(&cam, behind, &sx, &sy, NULL),
             "a point behind the camera is refused, not mirrored onto the screen");
    ut_check(!world_pick_project(&cam, eye, &sx, &sy, NULL), "and so is the eye itself");
    {
        world_pick_camera_t level = camera_at(eye, 30.0f, 0.0f);

        ut_check(world_pick_project(&level, ahead, &sx, &sy, NULL) && fabsf(sx - 320.0f) < 1e-2f &&
                     fabsf(sy - 240.0f) < 1e-2f,
                 "a point straight ahead of a level camera lands on the centre");
    }
    {
        world_pick_camera_t level = camera_at(eye, 0.0f, 0.0f);
        const float         right[3] = { 11.0f, 25.0f, 3.0f };
        const float         up[3]    = { 10.0f, 25.0f, 4.0f };

        ut_check(world_pick_project(&level, right, &sx, &sy, NULL) && sx > 320.0f,
                 "a point to the right lands right of the centre: x is right");
        ut_check(world_pick_project(&level, up, &sx, &sy, NULL) && sy < 240.0f,
                 "a point above lands above the centre: z is up and the screen's y runs down");
    }
    {
        world_pick_camera_t skewed = cam;

        skewed.view[0] *= 1.5f;
        ut_check(!world_pick_ray(&skewed, 300.0f, 200.0f, origin, dir),
                 "a transform that is no clean rotation casts no ray");
        skewed = cam;
        skewed.focal = 0.0f;
        ut_check(!world_pick_ray(&skewed, 300.0f, 200.0f, origin, dir),
                 "nor does a camera without a focal length");
    }
}

static void the_reading(void)
{
    const float         eye[3] = { 7.0f, -4.0f, 2.5f };
    world_pick_camera_t cam    = camera_at(eye, 55.0f, -15.0f);

    ut_section("which way the nine floats are read");
    ut_near(world_pick_eye_residual(&cam, eye, false), 0.0, 1e-3,
            "read as rows, the engine's reading, the eye lands on the camera's origin");
    ut_check(world_pick_eye_residual(&cam, eye, true) > 1.0f,
             "read as columns it lands far from it, so a wrong reading cannot pass the check");
    ut_check(world_pick_reading_verdict(world_pick_eye_residual(&cam, eye, false),
                                        world_pick_eye_residual(&cam, eye, true), true) ==
                 WORLD_PICK_READING_AGREES,
             "and the verdict on the two is: the rows reading agrees");
    {
        const float         level_eye[3] = { 0.0f, 0.0f, 0.1f };
        world_pick_camera_t level        = camera_at(level_eye, 0.0f, 0.0f);

        ut_check(world_pick_reading_verdict(world_pick_eye_residual(&level, level_eye, false),
                                            world_pick_eye_residual(&level, level_eye, true),
                                            true) == WORLD_PICK_READING_UNDECIDED,
                 "a camera looking along a world axis from near the origin cannot tell the two "
                 "readings apart, and says it is undecided rather than that they agree");
    }
    ut_check(world_pick_reading_verdict(0.01f, 5.0f, false) == WORLD_PICK_READING_UNDECIDED,
             "a view record naming another camera decides nothing");
    ut_check(world_pick_reading_verdict(3.0f, 0.02f, true) == WORLD_PICK_READING_DISAGREES,
             "the eye far from the origin read as rows is a verdict: it does not agree");
    ut_check(world_pick_reading_verdict(-1.0f, 5.0f, true) == WORLD_PICK_READING_UNDECIDED,
             "a residual that could not be made decides nothing");
}

/* The pointer looks along plus y from the origin, a unit over the floor. The wall it strikes has
 * its face at y = 5.15, so the engine's sphere struck at 5.0. */
static void the_nearest(void)
{
    const float       o[3] = { 0.0f, 0.0f, 1.0f };
    const float       d[3] = { 0.0f, 1.0f, 0.0f };
    world_pick_body_t bodies[3];
    float             t = -1.0f;
    float             limit = spawn_place_hover_limit(5.0f);

    ut_section("the copy under the pointer");
    memset(bodies, 0, sizeof bodies);
    bodies[0].base[1] = 5.6f;   /* behind the wall, 0.15 past its face at its nearest */
    bodies[0].radius  = 0.3f;
    bodies[0].height  = 2.0f;
    ut_check(world_pick_nearest(o, d, bodies, 1u, limit, &t) == -1,
             "a copy standing behind the wall the ray struck is not under the pointer");
    bodies[1].base[1] = 3.0f;
    bodies[1].radius  = 0.3f;
    bodies[1].height  = 2.0f;
    ut_check(world_pick_nearest(o, d, bodies, 2u, limit, &t) == 1 && fabsf(t - 2.7f) < 1e-4f,
             "one in front of the wall is, entered 2.7 along the ray");
    bodies[2].base[1] = 1.5f;
    bodies[2].radius  = 0.3f;
    bodies[2].height  = 2.0f;
    ut_check(world_pick_nearest(o, d, bodies, 3u, limit, &t) == 2,
             "of two in front the nearer wins");
    bodies[0].base[1] = 5.0f;
    ut_check(world_pick_nearest(o, d, bodies, 1u, limit, &t) == 0,
             "a copy standing where the ray struck, against the wall's face, is under it");
    ut_check(world_pick_nearest(o, d, NULL, 3u, limit, &t) == -1 &&
                 world_pick_nearest(o, d, bodies, 0u, limit, &t) == -1,
             "no bodies, no copy");
}

static void the_cylinder(void)
{
    const float base[3] = { 0.0f, 10.0f, 0.0f };
    float       o[3];
    float       d[3];
    float       t = -1.0f;

    ut_section("the ray against a body's cylinder");
    o[0] = 0.0f;
    o[1] = 0.0f;
    o[2] = 1.0f;
    d[0] = 0.0f;
    d[1] = 1.0f;
    d[2] = 0.0f;
    ut_check(world_pick_cylinder(o, d, base, 0.5f, 2.0f, &t) && fabsf(t - 9.5f) < 1e-4f,
             "straight at it, the ray enters the wall half a unit short of the centre");
    o[2] = 3.0f;
    ut_check(!world_pick_cylinder(o, d, base, 0.5f, 2.0f, &t),
             "over its head the ray misses");
    o[2] = -0.5f;
    ut_check(!world_pick_cylinder(o, d, base, 0.5f, 2.0f, &t),
             "under its feet it misses");
    o[0] = 0.7f;
    o[2] = 1.0f;
    ut_check(!world_pick_cylinder(o, d, base, 0.5f, 2.0f, &t),
             "beside it the ray misses");
    o[0] = 0.0f;
    o[1] = 10.0f;
    o[2] = 10.0f;
    d[1] = 0.0f;
    d[2] = -1.0f;
    ut_check(world_pick_cylinder(o, d, base, 0.5f, 2.0f, &t) && fabsf(t - 8.0f) < 1e-4f,
             "straight down onto it, the ray meets the top at the head");
    o[0] = 0.0f;
    o[1] = 20.0f;
    o[2] = 1.0f;
    d[1] = 1.0f;
    d[2] = 0.0f;
    ut_check(!world_pick_cylinder(o, d, base, 0.5f, 2.0f, &t),
             "a body behind the ray's start is not met");
    o[1] = 10.1f;
    ut_check(world_pick_cylinder(o, d, base, 0.5f, 2.0f, &t) && t == 0.0f,
             "a ray that starts inside meets it at once");
    o[0] = 0.0f;
    o[1] = 0.0f;
    o[2] = 4.0f;
    d[0] = 0.0f;
    d[1] = 0.98058f;
    d[2] = -0.19612f;
    ut_check(world_pick_cylinder(o, d, base, 0.5f, 2.0f, &t) && t > 0.0f &&
                 fabsf(o[2] + d[2] * t - 2.0f) < 0.4f,
             "a ray slanting down meets it near the head");
    ut_check(!world_pick_cylinder(o, d, base, 0.0f, 2.0f, &t) &&
                 !world_pick_cylinder(o, d, base, 0.5f, 0.0f, &t),
             "a cylinder without width or height is never met");
}

int main(void)
{
    the_projection();
    the_reading();
    the_cylinder();
    the_nearest();
    return ut_summary("world pick");
}
