/* The place of a scene: the way a body at it faces, whether a body stands at it, and whether the
 * host stands there already, so that he is not moved for a scene he is at.
 *
 * The rule of the place itself, which reading leads to which step, is walked in mp_scene_hold.c
 * beside the hold that waits for it.
 */
#include "unittest.h"

#include "mp_scene_room.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static const float PLACE[3] = { 10.0f, 10.0f, 0.0f };

static void check_the_heading_and_the_place(void)
{
    float to[3];

    ut_section("the heading of a body at the place, by the engine's formula");
    to[0] = PLACE[0];
    to[1] = PLACE[1] + 3.0f;
    to[2] = PLACE[2];
    ut_near((double)mp_scene_facing(PLACE, to), 0.0, 0.001, "north is heading 0");
    to[0] = PLACE[0] + 3.0f;
    to[1] = PLACE[1];
    ut_near((double)mp_scene_facing(PLACE, to), -90.0, 0.001, "east is heading -90");
    ut_near((double)mp_scene_facing(NULL, to), 0.0, 0.001, "no place to face from is heading 0");
    ut_near((double)mp_scene_facing(PLACE, NULL), 0.0, 0.001, "and so is nothing to face");

    ut_section("at the place: within a unit on the plane and two in height");
    to[0] = PLACE[0] + 0.9f;
    to[1] = PLACE[1];
    to[2] = PLACE[2] + 2.0f;
    ut_check(mp_scene_at_place(to, PLACE), "0.9 units off and 2.0 up: at it");
    to[0] = PLACE[0] + 1.0f;
    ut_check(!mp_scene_at_place(to, PLACE), "1.0 units off: not");
    to[0] = PLACE[0];
    to[2] = PLACE[2] + 2.1f;
    ut_check(!mp_scene_at_place(to, PLACE), "2.1 up: not");
    ut_check(!mp_scene_at_place(NULL, PLACE) && !mp_scene_at_place(PLACE, NULL),
             "no body or no place is at nothing");
}

/* The host at `dx`, `dy`, `dz` from the place, which is the trigger's body as well. */
static bool there(bool hero, float dx, float dy, float dz)
{
    float host[3];

    host[0] = PLACE[0] + dx;
    host[1] = PLACE[1] + dy;
    host[2] = PLACE[2] + dz;
    return mp_scene_host_is_there(hero, host, PLACE, PLACE);
}

static void check_the_host_there_already(void)
{
    static const float ELSEWHERE[3] = { 50.0f, 50.0f, 0.0f };
    float              host[3];
    float              nan = nanf("");

    ut_section("a hero scene: the box of the place and no more than half a unit of height");
    ut_check(there(true, 0.0f, 0.0f, 0.0f), "on the place: there");
    ut_check(there(true, 0.9f, 0.0f, 0.0f) && there(true, 0.0f, -0.9f, 0.0f),
             "0.9 units off on the plane: there");
    ut_check(!there(true, 1.0f, 0.0f, 0.0f), "a unit off: not, the host is moved");
    ut_check(there(true, 0.5f, 0.0f, MP_SCENE_NEAR_HERO_HEIGHT) &&
                 there(true, 0.5f, 0.0f, -MP_SCENE_NEAR_HERO_HEIGHT),
             "half a unit above or below: there, the step a hero's walk climbs");
    ut_check(!there(true, 0.5f, 0.0f, 0.6f) && !there(true, 0.5f, 0.0f, -0.6f),
             "0.6 above or below: not, though the place's own box reaches two units");
    host[0] = PLACE[0];
    host[1] = PLACE[1];
    host[2] = PLACE[2];
    ut_check(mp_scene_host_is_there(true, host, PLACE, ELSEWHERE),
             "a hero scene measures the place found, wherever the trigger's body is");
    ut_check(!mp_scene_host_is_there(true, host, NULL, PLACE),
             "and with no place found the host is at none");

    ut_section("a lock: two units on the plane and one in height of the body its script tested");
    ut_check(there(false, 1.9f, 0.0f, 0.0f) && there(false, 0.0f, -1.9f, 0.0f),
             "1.9 units off: there");
    ut_check(!there(false, 2.0f, 0.0f, 0.0f), "two units off: not");
    ut_check(there(false, 1.4f, 1.4f, 0.0f) && !there(false, 1.5f, 1.5f, 0.0f),
             "measured in a circle: 1.98 units off on the diagonal is there, 2.12 is not");
    ut_check(there(false, 1.0f, 0.0f, MP_SCENE_NEAR_LOCK_HEIGHT) &&
                 there(false, 1.0f, 0.0f, -MP_SCENE_NEAR_LOCK_HEIGHT),
             "a unit above or below: there");
    ut_check(!there(false, 1.0f, 0.0f, 1.1f) && !there(false, 1.0f, 0.0f, -1.1f),
             "1.1 above or below: not");
    ut_check(mp_scene_host_is_there(false, host, ELSEWHERE, PLACE),
             "a lock measures the trigger's body, wherever the place found is");
    ut_check(!mp_scene_host_is_there(false, host, PLACE, NULL),
             "and with no body read the host is at none");

    ut_section("a host that cannot be measured is not there");
    ut_check(!mp_scene_host_is_there(true, NULL, PLACE, PLACE) &&
                 !mp_scene_host_is_there(false, NULL, PLACE, PLACE),
             "no host position");
    host[0] = nan;
    ut_check(!mp_scene_host_is_there(true, host, PLACE, PLACE) &&
                 !mp_scene_host_is_there(false, host, PLACE, PLACE),
             "a position that is not a number");
    host[0] = PLACE[0];
    host[2] = nan;
    ut_check(!mp_scene_host_is_there(true, host, PLACE, PLACE) &&
                 !mp_scene_host_is_there(false, host, PLACE, PLACE),
             "a height that is not a number");
    host[2] = INFINITY;
    ut_check(!mp_scene_host_is_there(true, host, PLACE, PLACE) &&
                 !mp_scene_host_is_there(false, host, PLACE, PLACE),
             "an infinite height");
}

int main(void)
{
    check_the_heading_and_the_place();
    check_the_host_there_already();
    return ut_summary("the place of a scene");
}
