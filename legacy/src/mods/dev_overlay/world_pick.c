/* world_pick.c: see world_pick.h. */
#include "world_pick.h"

#include "character_prop.h"

#include <math.h>
#include <string.h>

static bool finite3(const float v[3])
{
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

bool world_pick_project(const world_pick_camera_t *camera, const float world[3], float *sx,
                        float *sy, float *depth)
{
    float cam[3];
    float s;

    if (camera == NULL || world == NULL || sx == NULL || sy == NULL) {
        return false;
    }
    character_prop_mat_point(cam, world, camera->view);
    if (!finite3(cam) || !(cam[1] > WORLD_PICK_NEAR)) {
        return false;
    }
    s   = camera->focal / cam[1];
    *sx = cam[0] * s + camera->centre_x;
    *sy = -cam[2] * s + camera->centre_y;
    if (depth != NULL) {
        *depth = cam[1];
    }
    return true;
}

bool world_pick_ray(const world_pick_camera_t *camera, float sx, float sy, float origin[3],
                    float direction[3])
{
    static const float ZERO[3] = { 0.0f, 0.0f, 0.0f };
    float inverse[12];
    float along[3];
    float through[3];
    float length;

    if (camera == NULL || origin == NULL || direction == NULL || !(camera->focal > 0.0f) ||
        !isfinite(sx) || !isfinite(sy) || !character_prop_mat_invert(inverse, camera->view)) {
        return false;
    }
    /* The eye is where the camera's origin goes, and the pixel's direction is carried by the same
     * inverse and taken back off it, which leaves the rotation alone. */
    character_prop_mat_point(origin, ZERO, inverse);
    along[0] = sx - camera->centre_x;
    along[1] = camera->focal;
    along[2] = camera->centre_y - sy;
    character_prop_mat_point(through, along, inverse);
    direction[0] = through[0] - origin[0];
    direction[1] = through[1] - origin[1];
    direction[2] = through[2] - origin[2];
    length = sqrtf(direction[0] * direction[0] + direction[1] * direction[1] +
                   direction[2] * direction[2]);
    if (!(length > 0.0f) || !finite3(origin)) {
        return false;
    }
    direction[0] /= length;
    direction[1] /= length;
    direction[2] /= length;
    return true;
}

/* Where the ray crosses the height `z`, if it is ahead and inside the circle. */
static bool through_cap(const float origin[3], const float direction[3], const float base[3],
                        float radius, float z, float *t)
{
    float along;
    float x;
    float y;

    if (direction[2] == 0.0f) {
        return false;
    }
    along = (z - origin[2]) / direction[2];
    if (along < 0.0f) {
        return false;
    }
    x = origin[0] + direction[0] * along - base[0];
    y = origin[1] + direction[1] * along - base[1];
    if (x * x + y * y > radius * radius) {
        return false;
    }
    *t = along;
    return true;
}

bool world_pick_cylinder(const float origin[3], const float direction[3], const float base[3],
                         float radius, float height, float *t)
{
    float ox;
    float oy;
    float a;
    float b;
    float c;
    float best = -1.0f;
    float cap;

    if (origin == NULL || direction == NULL || base == NULL || t == NULL ||
        !(radius > 0.0f) || !(height > 0.0f)) {
        return false;
    }
    ox = origin[0] - base[0];
    oy = origin[1] - base[1];
    c  = ox * ox + oy * oy - radius * radius;
    if (c <= 0.0f && origin[2] >= base[2] && origin[2] <= base[2] + height) {
        *t = 0.0f;   /* the ray starts inside */
        return true;
    }
    /* The side: where the ray's footprint crosses the circle, the nearer crossing ahead whose
     * height lies on the wall. */
    a = direction[0] * direction[0] + direction[1] * direction[1];
    b = 2.0f * (ox * direction[0] + oy * direction[1]);
    if (a > 0.0f) {
        float disc = b * b - 4.0f * a * c;

        if (disc >= 0.0f) {
            float root = sqrtf(disc);
            float near = (-b - root) / (2.0f * a);
            float far  = (-b + root) / (2.0f * a);
            float hit  = (near >= 0.0f) ? near : far;
            float z    = origin[2] + direction[2] * hit;

            if (hit >= 0.0f && z >= base[2] && z <= base[2] + height) {
                best = hit;
            }
        }
    }
    /* The two ends, for a ray from above or below. */
    if (through_cap(origin, direction, base, radius, base[2] + height, &cap) &&
        (best < 0.0f || cap < best)) {
        best = cap;
    }
    if (through_cap(origin, direction, base, radius, base[2], &cap) &&
        (best < 0.0f || cap < best)) {
        best = cap;
    }
    if (best < 0.0f) {
        return false;
    }
    *t = best;
    return true;
}

int32_t world_pick_nearest(const float origin[3], const float direction[3],
                           const world_pick_body_t *bodies, uint32_t count, float limit, float *t)
{
    int32_t  nearest = -1;
    float    best    = limit;
    uint32_t i;

    if (bodies == NULL) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        float entry;

        if (world_pick_cylinder(origin, direction, bodies[i].base, bodies[i].radius,
                                bodies[i].height, &entry) &&
            entry < best) {
            best    = entry;
            nearest = (int32_t)i;
        }
    }
    if (nearest >= 0 && t != NULL) {
        *t = best;
    }
    return nearest;
}

world_pick_reading_t world_pick_reading_verdict(float rows, float columns, bool same_camera)
{
    if (!(rows >= 0.0f) || !(columns >= 0.0f) || !same_camera) {
        return WORLD_PICK_READING_UNDECIDED;
    }
    if (rows > WORLD_PICK_READING_AGREES_WITHIN) {
        return WORLD_PICK_READING_DISAGREES;
    }
    return (columns >= WORLD_PICK_READING_APART_BY) ? WORLD_PICK_READING_AGREES
                                                     : WORLD_PICK_READING_UNDECIDED;
}

float world_pick_eye_residual(const world_pick_camera_t *camera, const float eye[3],
                              bool transposed)
{
    float read[12];
    float cam[3];

    if (camera == NULL || eye == NULL) {
        return -1.0f;
    }
    memcpy(read, camera->view, sizeof read);
    if (transposed) {
        read[1] = camera->view[3];
        read[2] = camera->view[6];
        read[3] = camera->view[1];
        read[5] = camera->view[7];
        read[6] = camera->view[2];
        read[7] = camera->view[5];
    }
    character_prop_mat_point(cam, eye, read);
    return sqrtf(cam[0] * cam[0] + cam[1] * cam[1] + cam[2] * cam[2]);
}
