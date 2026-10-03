/* mp_scene_hero_rule.c: whether the hero a scene drives still gets anywhere. See the header. */
#include "mp_scene_hero_rule.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Above any distance a level has, so the first real goal's distance is always progress. */
#define FAR_AWAY 1.0e30f

static float plane_distance(const float a[3], const float b[3])
{
    float dx = a[0] - b[0];
    float dy = a[1] - b[1];

    return sqrtf(dx * dx + dy * dy);
}

static float space_distance(const float a[3], const float b[3])
{
    float dz = a[2] - b[2];
    float dp = plane_distance(a, b);

    return sqrtf(dp * dp + dz * dz);
}

bool mp_scene_hero_rule_wants_to_walk(const mp_scene_hero_reading_t *reading)
{
    return reading != NULL && reading->move_requested && reading->move_speed != 0.0f &&
           (reading->move_mode & 1) == 0;
}

void mp_scene_hero_rule_reset(mp_scene_hero_clock_t *clock)
{
    if (clock != NULL) {
        memset(clock, 0, sizeof *clock);
    }
}

/* The clock starts on the first real goal, and again on one that stands apart from it; a goal that
 * creeps by less than that is followed without starting the clock again. */
static void take_the_goal(mp_scene_hero_clock_t *clock, const float target[3])
{
    if (!clock->goal_known ||
        space_distance(target, clock->goal_start) > MP_SCENE_HERO_GOAL_MOVED) {
        memcpy(clock->goal_start, target, sizeof clock->goal_start);
        clock->goal_known = true;
        clock->best       = FAR_AWAY;
        clock->mark       = FAR_AWAY;
        clock->still      = 0u;
    }
    memcpy(clock->goal, target, sizeof clock->goal);
}

/* Which reading names the real goal, and what the reading says about the detour. */
static bool reads_the_goal(mp_scene_hero_clock_t *clock, const mp_scene_hero_reading_t *reading)
{
    bool real = !reading->detour && !clock->detour_before &&
                !(clock->detour_seen &&
                  space_distance(reading->target, clock->detour_point) <=
                      MP_SCENE_HERO_SAME_POINT);

    if (reading->detour) {
        memcpy(clock->detour_point, reading->target, sizeof clock->detour_point);
        clock->detour_seen = true;
        clock->detour_ran  = true;
    }
    clock->detour_before = reading->detour;
    return real;
}

mp_scene_hero_verdict_t mp_scene_hero_rule_step(mp_scene_hero_clock_t *clock, uint32_t now,
                                                const mp_scene_hero_reading_t *reading,
                                                float goal[3])
{
    if (clock == NULL) {
        return MP_SCENE_HERO_IDLE;
    }
    if (reading == NULL || reading->actor == 0u) {
        mp_scene_hero_rule_reset(clock);
        return MP_SCENE_HERO_IDLE;
    }
    if (reading->actor != clock->actor) {
        mp_scene_hero_rule_reset(clock);
        clock->actor    = reading->actor;
        clock->waypoint = reading->waypoint;
    }
    if (!mp_scene_hero_rule_wants_to_walk(reading)) {
        return MP_SCENE_HERO_IDLE;
    }
    clock->walked    = true;
    clock->last_walk = now;
    if (reading->waypoint != clock->waypoint) {
        /* The walk reached its goal and moved on; the goal it had is behind it now. */
        clock->waypoint    = reading->waypoint;
        clock->goal_known  = false;
        clock->detour_seen = false;
        clock->detour_ran  = false;
        clock->still       = 0u;
        clock->blind       = 0u;
    }
    if (reads_the_goal(clock, reading)) {
        take_the_goal(clock, reading->target);
    }
    if (!clock->goal_known) {
        ++clock->blind;
        if (clock->blind > clock->longest_blind) {
            clock->longest_blind = clock->blind;
        }
        if (clock->blind < MP_SCENE_HERO_STILL_SUBSTEPS) {
            return MP_SCENE_HERO_BLIND;
        }
        clock->blind = 0u;
        return MP_SCENE_HERO_LOST;
    }
    clock->blind = 0u;
    clock->distance = plane_distance(reading->position, clock->goal);
    if (clock->distance < clock->best) {
        clock->best = clock->distance;
    }
    if (clock->distance <= clock->mark - MP_SCENE_HERO_PROGRESS) {
        clock->mark  = clock->distance;
        clock->still = 0u;
        return MP_SCENE_HERO_WALKING;
    }
    ++clock->still;
    if (clock->still > clock->longest_still) {
        clock->longest_still = clock->still;
    }
    if (clock->still < MP_SCENE_HERO_STILL_SUBSTEPS) {
        return MP_SCENE_HERO_WALKING;
    }
    /* Put onto the goal the body stands at nought from it, so only a move away and back again
     * could count as progress; a walk that still does not arrive is put again after as long. */
    if (goal != NULL) {
        memcpy(goal, clock->goal, 3u * sizeof(float));
    }
    clock->still = 0u;
    clock->mark  = 0.0f;
    return MP_SCENE_HERO_PUT;
}

bool mp_scene_hero_rule_walked_lately(const mp_scene_hero_clock_t *clock, uint32_t now)
{
    return clock != NULL && clock->walked &&
           now - clock->last_walk < MP_SCENE_HERO_WALK_MEMORY;
}
