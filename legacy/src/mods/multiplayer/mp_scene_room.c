/* mp_scene_room.c: the place of a scene, pure. See the header. */
#include "mp_scene_room.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Degrees in a radian, for the engine's heading. */
#define DEGREES_A_RADIAN 57.29577951308232

mp_scene_place_step_t mp_scene_place_rule(mp_scene_place_read_t read, bool hero, uint32_t waited)
{
    switch (read) {
    case MP_SCENE_PLACE_READ_FOUND:
        return MP_SCENE_PLACE_TAKE;
    case MP_SCENE_PLACE_READ_MOVING:
        /* He lands or climbs out within a second, mostly; after it the actor's side is asked. */
        if (waited < MP_SCENE_PLACE_WAIT_SUBSTEPS) {
            return MP_SCENE_PLACE_READ_AGAIN;
        }
        return hero ? MP_SCENE_PLACE_BESIDE_ACTOR : MP_SCENE_PLACE_NOBODY;
    case MP_SCENE_PLACE_READ_NONE_FREE:
    case MP_SCENE_PLACE_READ_UNREAD:
        return hero ? MP_SCENE_PLACE_BESIDE_ACTOR : MP_SCENE_PLACE_NOBODY;
    case MP_SCENE_PLACE_READ_MOVER:
        /* A lift somebody else rides is his ride: the host is not put on it, and the actor's side
         * would leave the mover without the host all the same. */
    case MP_SCENE_PLACE_READ_NO_PROBES:
    case MP_SCENE_PLACE_READS:
    default:
        return MP_SCENE_PLACE_NOBODY;
    }
}

const char *mp_scene_place_text(mp_scene_place_read_t read)
{
    switch (read) {
    case MP_SCENE_PLACE_READ_FOUND:     return "it stands";
    case MP_SCENE_PLACE_READ_NONE_FREE: return "nothing on it or around it is free";
    case MP_SCENE_PLACE_READ_MOVING:    return "in the air, swimming or over a drop";
    case MP_SCENE_PLACE_READ_MOVER:     return "on a mover";
    case MP_SCENE_PLACE_READ_UNREAD:    return "its body did not read";
    case MP_SCENE_PLACE_READ_NO_PROBES:
    case MP_SCENE_PLACE_READS:
    default:                            return "no level or no probes to search with";
    }
}

static float plane_distance_sq(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];

    return dx * dx + dy * dy;
}

bool mp_scene_at_place(const float body[3], const float place[3])
{
    return body != NULL && place != NULL && plane_distance_sq(body, place) < 1.0f &&
           fabsf(body[2] - place[2]) <= MP_SCENE_ROOM_HEIGHT;
}

/* The two boxes, by the engine's own numbers. The hero's walk refuses a point whose floor lies
 * more than half a unit above it (move_canStandAt 0x0042A5B5, `probe.dist > 0.5f`), so a host
 * further off the place's height than that is not at a place the hero can walk from. The facing
 * test of a conversation, enemy_isFacingTarget 0x00435656, passes within one unit of height and
 * two units along each axis of the plane; a lock is measured in a circle of that radius. A
 * position that is not a number is at no place. */
bool mp_scene_host_is_there(bool hero, const float host[3], const float place[3],
                            const float trigger[3])
{
    if (host == NULL) {
        return false;
    }
    if (hero) {
        return place != NULL && mp_scene_at_place(host, place) &&
               fabsf(host[2] - place[2]) <= MP_SCENE_NEAR_HERO_HEIGHT;
    }
    return trigger != NULL &&
           plane_distance_sq(host, trigger) <
               MP_SCENE_NEAR_LOCK_PLANE * MP_SCENE_NEAR_LOCK_PLANE &&
           fabsf(host[2] - trigger[2]) <= MP_SCENE_NEAR_LOCK_HEIGHT;
}

float mp_scene_facing(const float from[3], const float toward[3])
{
    double dx;
    double dy;

    if (from == NULL || toward == NULL) {
        return 0.0f;
    }
    dx = (double)toward[0] - (double)from[0];
    dy = (double)toward[1] - (double)from[1];
    return (float)(atan2(-dx, dy) * DEGREES_A_RADIAN);
}
