/* spawn_place.c: the placement mode's state, its turn and its click lock, and where the pointer
 * puts the entity, over a stood-up world of probe answers.
 *
 * What would be silent if it were wrong:
 *
 *   a mode that stays on across a new world, which draws a ghost of a model that is gone;
 *
 *   a click from one life of the mode that places in the next, or a double click that places two;
 *
 *   a turn that leaves [0, 360), which the wire rounds to a facing nobody chose, or a Ctrl turn
 *   that goes ninety degrees from an odd angle and never lines up;
 *
 *   a place taken without a floor under it, or a floor looked for at the height of the strike
 *   rather than just above it, which misses a step struck on its edge;
 *
 *   a place at the foot of a wall taken as it is, which stands the body half in the wall;
 *
 *   a world that stays held after a copy is placed, which draws the copy between the world's
 *   origin and its place, or one that is never held again;
 *
 *   a mode that stays on over a dead player, a copy behind a wall taken for the one under the
 *   pointer, or a client's right click that deletes.
 */
#include "unittest.h"

#include "spawn_place.h"

#include <math.h>
#include <string.h>

static spawn_place_facts_t facts(bool open, bool available, bool player, uint32_t world)
{
    spawn_place_facts_t f;

    f.panel_open  = open;
    f.available   = available;
    f.player      = player;
    f.player_dead = false;
    f.world       = world;
    return f;
}

static void the_state(void)
{
    spawn_place_t       p;
    spawn_place_facts_t f;
    spawn_place_step_t  s;

    ut_section("the mode's state");
    memset(&p, 0, sizeof p);
    f = facts(true, true, true, 7u);
    s = spawn_place_frame(&p, &f);
    ut_check(!p.on && !s.entered && !s.refused && s.left == SPAWN_PLACE_STILL_ON,
             "nothing asked, nothing happens");
    spawn_place_ask(&p, true);
    s = spawn_place_frame(&p, &f);
    ut_check(p.on && s.entered && p.world == 7u, "asked on with the panel open, it comes on");
    s = spawn_place_frame(&p, &f);
    ut_check(p.on && !s.entered, "and stays on, the ask taken once");

    f = facts(true, true, true, 8u);
    s = spawn_place_frame(&p, &f);
    ut_check(!p.on && s.left == SPAWN_PLACE_OFF_WORLD, "a new world turns it off");

    spawn_place_ask(&p, true);
    f = facts(false, true, true, 8u);
    s = spawn_place_frame(&p, &f);
    ut_check(!p.on && s.refused, "asked with the panel shut, it is refused");
    f = facts(true, true, true, 8u);
    s = spawn_place_frame(&p, &f);
    ut_check(!p.on && !s.entered, "and a refused ask is not remembered for later");

    spawn_place_ask(&p, true);
    f = facts(true, false, true, 8u);
    s = spawn_place_frame(&p, &f);
    ut_check(!p.on && s.refused, "asked with the engine side unable, it is refused");

    spawn_place_ask(&p, true);
    f = facts(true, true, true, 8u);
    (void)spawn_place_frame(&p, &f);
    f = facts(false, true, true, 8u);
    s = spawn_place_frame(&p, &f);
    ut_check(!p.on && s.left == SPAWN_PLACE_OFF_PANEL, "the panel shut turns it off");

    spawn_place_ask(&p, true);
    f = facts(true, true, true, 8u);
    (void)spawn_place_frame(&p, &f);
    f = facts(true, true, false, 8u);
    s = spawn_place_frame(&p, &f);
    ut_check(!p.on && s.left == SPAWN_PLACE_OFF_PLAYER, "no player turns it off");

    spawn_place_ask(&p, true);
    f = facts(true, true, true, 8u);
    (void)spawn_place_frame(&p, &f);
    f = facts(true, false, true, 8u);
    s = spawn_place_frame(&p, &f);
    ut_check(!p.on && s.left == SPAWN_PLACE_OFF_UNAVAILABLE,
             "the engine side failing turns it off");

    spawn_place_ask(&p, true);
    f = facts(true, true, true, 8u);
    (void)spawn_place_frame(&p, &f);
    spawn_place_ask(&p, false);
    s = spawn_place_frame(&p, &f);
    ut_check(!p.on && s.left == SPAWN_PLACE_OFF_ASKED, "asked off, it goes off");
    ut_check(p.available, "the last frame's availability is kept for the panel row");

    spawn_place_ask(&p, true);
    f = facts(true, true, true, 8u);
    (void)spawn_place_frame(&p, &f);
    f.player_dead = true;
    s = spawn_place_frame(&p, &f);
    ut_check(!p.on && s.left == SPAWN_PLACE_OFF_DIED,
             "the player's death turns it off, and says so apart from no player");
    spawn_place_ask(&p, true);
    s = spawn_place_frame(&p, &f);
    ut_check(!p.on && s.refused, "and over a dead player it does not come on");
}

static void the_settle(void)
{
    spawn_place_t       p;
    spawn_place_facts_t f = facts(true, true, true, 1u);
    uint32_t            ran = 99u;

    ut_section("the settle");
    memset(&p, 0, sizeof p);
    spawn_place_settle_begin(&p, true, 100u, 5000u);
    ut_check(!p.settling, "with the mode off, a placement starts no settle");
    spawn_place_ask(&p, true);
    (void)spawn_place_frame(&p, &f);
    ut_check(spawn_place_settle_tick(&p, true, 100u, 5000u, &ran) == SPAWN_SETTLE_IDLE &&
                 ran == 0u,
             "on and nothing placed, the world is held: no settle runs");
    spawn_place_settle_begin(&p, true, 100u, 5000u);
    ut_check(p.settling, "a copy placed lets the world run");
    ut_check(spawn_place_settle_tick(&p, true, 101u, 5010u, &ran) == SPAWN_SETTLE_RUNNING &&
                 ran == 1u && p.settling,
             "one substep is not enough: the spawn's zero previous position is still in the pair");
    ut_check(spawn_place_settle_tick(&p, true, 102u, 5020u, &ran) == SPAWN_SETTLE_DONE &&
                 ran == 2u && !p.settling,
             "two substeps and the world is held again, the copy drawn where it stands");
    ut_check(spawn_place_settle_tick(&p, true, 110u, 5030u, &ran) == SPAWN_SETTLE_IDLE,
             "and it stays held");

    spawn_place_settle_begin(&p, true, 0xFFFFFFFFu, 6000u);
    ut_check(spawn_place_settle_tick(&p, true, 1u, 6010u, &ran) == SPAWN_SETTLE_DONE &&
                 ran == 2u,
             "the substep count is taken across its wrap");

    spawn_place_settle_begin(&p, true, 200u, 7000u);
    ut_check(spawn_place_settle_tick(&p, true, 200u, 7000u + SPAWN_PLACE_SETTLE_MAX_MS - 1u,
                                     &ran) == SPAWN_SETTLE_RUNNING,
             "a world that runs no substep keeps running until the time is up");
    ut_check(spawn_place_settle_tick(&p, true, 200u, 7000u + SPAWN_PLACE_SETTLE_MAX_MS,
                                     &ran) == SPAWN_SETTLE_TIMED_OUT && !p.settling,
             "and is held again when it is");

    spawn_place_settle_begin(&p, false, 0u, 8000u);
    ut_check(spawn_place_settle_tick(&p, false, 50u, 8100u, &ran) == SPAWN_SETTLE_RUNNING &&
                 ran == 0u,
             "without a count only the time ends it");
    ut_check(spawn_place_settle_tick(&p, false, 50u, 8000u + SPAWN_PLACE_SETTLE_MAX_MS, &ran) ==
                 SPAWN_SETTLE_TIMED_OUT,
             "and it does");

    spawn_place_settle_begin(&p, true, 300u, 9000u);
    (void)spawn_place_settle_tick(&p, true, 301u, 9010u, &ran);
    spawn_place_settle_begin(&p, true, 301u, 9020u);
    ut_check(spawn_place_settle_tick(&p, true, 302u, 9030u, &ran) == SPAWN_SETTLE_RUNNING,
             "a second copy placed inside the settle starts it again");

    spawn_place_ask(&p, false);
    (void)spawn_place_frame(&p, &f);
    ut_check(!p.settling, "the mode going off ends the settle with it");
}

static void the_right_click(void)
{
    ut_section("the right click and the pointer's reach");
    ut_check(spawn_place_remove_verdict(false, false, false) == SPAWN_REMOVE_NOTHING &&
                 spawn_place_remove_verdict(false, true, true) == SPAWN_REMOVE_NOTHING,
             "nothing under the pointer removes nothing, whoever clicks");
    ut_check(spawn_place_remove_verdict(true, true, false) == SPAWN_REMOVE_CLIENT &&
                 spawn_place_remove_verdict(true, true, true) == SPAWN_REMOVE_CLIENT,
             "a client removes no single copy, its own or another's, ridden or not");
    ut_check(spawn_place_remove_verdict(true, false, true) == SPAWN_REMOVE_RIDDEN,
             "a copy the player rides is skipped");
    ut_check(spawn_place_remove_verdict(true, false, false) == SPAWN_REMOVE_DELETE,
             "in a single player game and on the host the copy under the pointer goes");
    ut_near(spawn_place_hover_limit(5.0f), 5.0 + SPAWN_PLACE_HOVER_SLACK, 1e-6,
            "a copy may be entered up to a quarter unit past the strike");
    ut_near(spawn_place_hover_limit(-1.0f), SPAWN_SPOT_REACH + SPAWN_PLACE_HOVER_SLACK, 1e-6,
            "and without a strike, up to the reach");
    ut_check(spawn_place_cap_reached(false, 0u, 16u, 16u) &&
                 !spawn_place_cap_reached(false, 0u, 15u, 16u),
             "single player: the panel's own cap");
    ut_check(spawn_place_cap_reached(true, 4u, 4u, 16u) &&
                 !spawn_place_cap_reached(true, 4u, 3u, 16u) &&
                 !spawn_place_cap_reached(true, 0u, 40u, 16u),
             "a session: the host's cap, and a client that does not know it leaves it to the host");
    {
        const float at[3]     = { 0.0f, 0.0f, 0.0f };
        const float near[3]   = { 0.5f, 0.0f, 1.0f };
        const float beside[3] = { 0.6f, 0.0f, 0.0f };
        const float above[3]  = { 0.0f, 0.0f, 2.0f };

        ut_check(spawn_place_crowds(at, near) && !spawn_place_crowds(at, beside) &&
                     !spawn_place_crowds(at, above),
                 "another body takes a place nearer than 0.6 across and 2 up or down");
    }
}

static void the_clicks(void)
{
    spawn_place_t       p;
    spawn_place_facts_t f = facts(true, true, true, 1u);

    ut_section("the clicks");
    memset(&p, 0, sizeof p);
    spawn_place_ask_click(&p);
    ut_check(!p.click_asked, "a click with the mode off asks for nothing");
    spawn_place_ask(&p, true);
    (void)spawn_place_frame(&p, &f);
    spawn_place_ask_click(&p);
    ut_check(spawn_place_take_click(&p, 1000u), "a click in the mode places");
    ut_check(!spawn_place_take_click(&p, 1001u), "and is taken once");
    spawn_place_ask_click(&p);
    ut_check(!spawn_place_take_click(&p, 1000u + SPAWN_PLACE_CLICK_LOCK_MS - 1u),
             "a second click inside the lock places nothing");
    spawn_place_ask_click(&p);
    ut_check(spawn_place_take_click(&p, 1000u + SPAWN_PLACE_CLICK_LOCK_MS),
             "a click at the end of the lock places again");
    p.last_click_ms = 0xFFFFFFF0u;
    spawn_place_ask_click(&p);
    ut_check(!spawn_place_take_click(&p, 0x00000010u),
             "the lock holds across the millisecond clock's wrap");
    spawn_place_ask_click(&p);
    spawn_place_ask_remove(&p);
    spawn_place_ask(&p, false);
    (void)spawn_place_frame(&p, &f);
    spawn_place_ask(&p, true);
    (void)spawn_place_frame(&p, &f);
    ut_check(!spawn_place_take_click(&p, 5000u) && !spawn_place_take_remove(&p),
             "clicks asked in one life of the mode do not act in the next");
    spawn_place_ask_remove(&p);
    ut_check(spawn_place_take_remove(&p) && !spawn_place_take_remove(&p),
             "a right click is taken once");
}

static void the_turn(void)
{
    spawn_place_t p;
    float         from[2] = { 0.0f, 0.0f };
    float         north[2] = { 0.0f, 5.0f };
    float         west[2] = { -5.0f, 0.0f };

    ut_section("the turn");
    ut_near(spawn_place_turned(0.0f, 1, false, false), 15.0, 1e-4, "a notch is 15 degrees");
    ut_near(spawn_place_turned(0.0f, 1, true, false), 1.0, 1e-4, "with Shift 1 degree");
    ut_near(spawn_place_turned(350.0f, 2, false, false), 20.0, 1e-4,
            "past 360 it wraps round to 20");
    ut_near(spawn_place_turned(10.0f, -1, false, false), 355.0, 1e-4,
            "below 0 it wraps round to 355");
    ut_near(spawn_place_turned(37.0f, 1, false, true), 90.0, 1e-4,
            "with Ctrl from 37 the first notch lands on 90: onto the nearest quarter, then one");
    ut_near(spawn_place_turned(50.0f, 0, false, true), 90.0, 1e-4,
            "and 50 snaps to the nearer quarter, 90, with no notch at all");
    ut_near(spawn_place_turned(300.0f, 1, false, true), 0.0, 1e-4,
            "Ctrl from 300 goes to 270 and one quarter on, 0, not 360");
    ut_near(spawn_place_turned(0.0f, 24, false, false), 0.0, 1e-4, "24 notches are a full turn");
    ut_near(spawn_place_wrap(-720.0f), 0.0, 1e-4, "minus two turns is 0");
    ut_near(spawn_place_wrap(NAN), 0.0, 1e-9, "a NaN never becomes a facing");
    ut_near(spawn_place_yaw_toward(from, north), 0.0, 1e-3,
            "looking at a point ahead along y is yaw 0, the engine's forward (-sin, cos)");
    ut_near(spawn_place_yaw_toward(from, west), 90.0, 1e-3,
            "looking at a point along minus x is yaw 90");

    memset(&p, 0, sizeof p);
    ut_near(spawn_place_facing(&p, 123.0f), 123.0, 1e-4, "untouched, it faces the player");
    spawn_place_turn(&p, 1, false, false, 123.0f);
    ut_near(spawn_place_facing(&p, 10.0f), 138.0, 1e-4,
            "the first turn starts from where it faced and the facing then stays put");
    spawn_place_turn(&p, 2, false, false, 999.0f);
    ut_near(spawn_place_facing(&p, 10.0f), 168.0, 1e-4, "later turns start from the kept facing");
    spawn_place_turn(&p, 0, false, false, 0.0f);
    ut_near(spawn_place_facing(&p, 10.0f), 168.0, 1e-4, "no notch changes nothing");
    spawn_place_face_player(&p);
    ut_near(spawn_place_facing(&p, 10.0f), 10.0, 1e-4, "reset, it faces the player again");
}

/* ============================================================================================ */

typedef struct world {
    bool               strikes;
    float              distance;
    spawn_spot_floor_t floor;
    float              offset;
    bool               mover;
    bool               low;
    bool               crowded;
    float              floor_asked_z;
    uint32_t           floors;
    uint32_t           headrooms;
    uint32_t           crowds;
    uint32_t           sides;
    bool               wall_ahead;   /* a wall facing minus y at y = wall_y */
    float              wall_y;
    bool               wall_behind;  /* and one facing plus y at y = back_y */
    float              back_y;
    bool               flat;         /* a level floor at floor_z, answered from where it is asked */
    float              floor_z;
} world_t;

static bool w_hit(void *user, const float from[3], const float to[3], float *distance)
{
    world_t *w = (world_t *)user;

    (void)from;
    (void)to;
    if (!w->strikes) {
        return false;
    }
    *distance = w->distance;
    return true;
}

static spawn_spot_floor_t w_floor(void *user, const float at[3], float *offset, bool *mover)
{
    world_t *w = (world_t *)user;

    ++w->floors;
    w->floor_asked_z = at[2];
    *offset = w->flat ? w->floor_z - at[2] : w->offset;
    *mover  = w->mover;
    return w->floor;
}

static bool w_headroom(void *user, const float at[3])
{
    world_t *w = (world_t *)user;

    (void)at;
    ++w->headrooms;
    return !w->low;
}

static bool w_crowded(void *user, const float at[3])
{
    world_t *w = (world_t *)user;

    (void)at;
    ++w->crowds;
    return w->crowded;
}

/* The engine's ray is a sphere of SPAWN_SPOT_RAY_RADIUS: along y it strikes a wall when its centre
 * comes that close to the wall's face. Only rays along y are asked here. */
static bool w_side(void *user, const float from[3], const float to[3], float *distance)
{
    world_t *w      = (world_t *)user;
    float    dy     = to[1] - from[1];
    float    length = fabsf(dy);
    float    gap;

    ++w->sides;
    if (length < 1e-6f) {
        return false;
    }
    if (dy > 0.0f && w->wall_ahead) {
        gap = w->wall_y - SPAWN_SPOT_RAY_RADIUS - from[1];
    } else if (dy < 0.0f && w->wall_behind) {
        gap = from[1] - (w->back_y + SPAWN_SPOT_RAY_RADIUS);
    } else {
        return false;
    }
    if (gap > length) {
        return false;
    }
    *distance = (gap > 0.0f) ? gap : 0.0f;
    return true;
}

static void fresh(world_t *w)
{
    memset(w, 0, sizeof *w);
    w->strikes  = true;
    w->distance = 10.0f;
    w->floor    = SPAWN_SPOT_FLOOR_FOUND;
    w->offset   = -0.6f;
}

static void the_spot(void)
{
    static const spawn_spot_probes_t PROBES = { &w_hit, &w_floor, &w_headroom, &w_crowded };
    const float          eye[3]  = { 0.0f, 0.0f, 5.0f };
    const float          down[3] = { 0.0f, 0.6f, -0.8f };
    world_t              w;
    spawn_spot_found_t   found;
    spawn_spot_verdict_t v;

    ut_section("where the pointer puts the entity");
    fresh(&w);
    v = spawn_place_spot(&PROBES, &w, eye, down, 0.0f, false, &found);
    ut_check(v == SPAWN_SPOT_OK, "a floor struck from above, room over it, nobody on it: placed");
    ut_near(w.floor_asked_z, 5.0 - 8.0 + SPAWN_SPOT_LIFT, 1e-4,
            "the floor is looked for half a unit above the strike, not at it");
    ut_near(found.at[0], 0.0, 1e-4, "the place is under the strike across");
    ut_near(found.at[1], 6.0, 1e-4, "and along");
    ut_near(found.at[2], 5.0 - 8.0 + SPAWN_SPOT_LIFT - 0.6, 1e-4,
            "and at the height of the floor the probe found, its own sign added");

    fresh(&w);
    w.strikes = false;
    v = spawn_place_spot(&PROBES, &w, eye, down, 0.0f, false, &found);
    ut_check(v == SPAWN_SPOT_NOTHING_HIT && w.floors == 0u,
             "nothing struck is refused, and no floor is asked about");
    ut_near(found.at[1], 0.6 * SPAWN_SPOT_MISS, 1e-4, "and the place is shown in front of the eye");

    fresh(&w);
    w.floor = SPAWN_SPOT_FLOOR_NONE;
    v = spawn_place_spot(&PROBES, &w, eye, down, 0.0f, false, &found);
    ut_check(v == SPAWN_SPOT_NO_FLOOR && w.headrooms == 0u,
             "no floor is refused, nothing more asked");
    fresh(&w);
    w.floor = SPAWN_SPOT_FLOOR_UNAVAILABLE;
    ut_check(spawn_place_spot(&PROBES, &w, eye, down, 0.0f, false, &found) == SPAWN_SPOT_NO_FLOOR,
             "a floor probe that cannot be asked is a refusal, never a guess");
    fresh(&w);
    w.offset = -(SPAWN_SPOT_LIFT + SPAWN_SPOT_FLOOR_REACH) - 0.01f;
    ut_check(spawn_place_spot(&PROBES, &w, eye, down, 0.0f, false, &found) == SPAWN_SPOT_NO_FLOOR,
             "a floor just out of reach under the strike is no floor");
    fresh(&w);
    w.offset = -(SPAWN_SPOT_LIFT + SPAWN_SPOT_FLOOR_REACH);
    ut_check(spawn_place_spot(&PROBES, &w, eye, down, 0.0f, false, &found) == SPAWN_SPOT_OK,
             "and one at the very end of the reach is still the floor");
    fresh(&w);
    w.offset = 0.4f;
    ut_check(spawn_place_spot(&PROBES, &w, eye, down, 0.0f, false, &found) == SPAWN_SPOT_OK &&
                 fabsf(found.at[2] - (5.0f - 8.0f + SPAWN_SPOT_LIFT + 0.4f)) < 1e-4f,
             "a step up is climbed onto: the offset is added as the engine signs it");

    fresh(&w);
    w.mover = true;
    v = spawn_place_spot(&PROBES, &w, eye, down, 0.0f, false, &found);
    ut_check(v == SPAWN_SPOT_ON_MOVER && w.headrooms == 0u, "a moving platform is refused");
    fresh(&w);
    w.low = true;
    v = spawn_place_spot(&PROBES, &w, eye, down, 0.0f, false, &found);
    ut_check(v == SPAWN_SPOT_NO_HEADROOM && w.crowds == 0u, "no room over it is refused");
    fresh(&w);
    w.crowded = true;
    ut_check(spawn_place_spot(&PROBES, &w, eye, down, 0.0f, false, &found) == SPAWN_SPOT_CROWDED,
             "a copy on it is refused");
    fresh(&w);
    ut_check(spawn_place_spot(&PROBES, &w, eye, down, 0.0f, true, &found) == SPAWN_SPOT_CAP,
             "a good place with the cap reached is refused for the cap");
    fresh(&w);
    {
        spawn_spot_probes_t fewer = PROBES;

        fewer.headroom = NULL;
        fewer.crowded  = NULL;
        w.low          = true;
        w.crowded      = true;
        ut_check(spawn_place_spot(&fewer, &w, eye, down, 0.0f, false, &found) == SPAWN_SPOT_OK,
                 "a question this build cannot put is not asked, and does not refuse");
    }
    ut_check(strcmp(spawn_place_spot_word(SPAWN_SPOT_NO_FLOOR), "no floor") == 0 &&
                 strcmp(spawn_place_spot_word(SPAWN_SPOT_OK), "click places it") == 0 &&
                 strcmp(spawn_place_spot_word(SPAWN_SPOT_WALLED), "too tight between walls") ==
                     0,
             "every verdict has its few words");
    fresh(&w);
    v = spawn_place_spot(&PROBES, &w, eye, down, 0.0f, false, &found);
    ut_near(found.strike, 10.0, 1e-4, "the look says how far the ray struck");
    w.strikes = false;
    v = spawn_place_spot(&PROBES, &w, eye, down, 0.0f, false, &found);
    ut_near(found.strike, SPAWN_SPOT_REACH, 1e-4, "and the reach when it struck nothing");
}

/* The pointer on the floor at the foot of a wall: the ray from (0, 0, 5) along (0, 0.6, -0.8)
 * strikes at ten units, (0, 6, -3), and the wall's face stands 0.15 further on, where the engine's
 * sphere would have met it. */
static void the_wall(void)
{
    static const spawn_spot_probes_t PROBES = { &w_hit, &w_floor, &w_headroom, &w_crowded,
                                                &w_side };
    const float          eye[3]  = { 0.0f, 0.0f, 5.0f };
    const float          down[3] = { 0.0f, 0.6f, -0.8f };
    const float          radius  = 0.5f;
    world_t              w;
    spawn_spot_found_t   found;
    spawn_spot_verdict_t v;

    ut_section("a wall at the pointer");
    fresh(&w);
    w.flat       = true;
    w.floor_z    = -3.1f;
    w.wall_ahead = true;
    w.wall_y     = 6.15f;
    v = spawn_place_spot(&PROBES, &w, eye, down, radius, false, &found);
    ut_check(v == SPAWN_SPOT_OK, "a place at the foot of a wall is still a place");
    ut_near(w.wall_y - found.at[1], radius + SPAWN_SPOT_WALL_MARGIN, 1e-4,
            "pushed back until the body's radius and the margin clear the wall's face");
    ut_near(found.moved, radius + SPAWN_SPOT_WALL_MARGIN - SPAWN_SPOT_RAY_RADIUS, 1e-4,
            "and the look says how far it was pushed");
    ut_check(w.floors == 2u, "the floor is found again where the place was pushed to");
    ut_near(found.at[2], -3.1, 1e-4, "and the place stands on it");

    fresh(&w);
    w.wall_ahead = true;
    w.wall_y     = 9.0f;
    v = spawn_place_spot(&PROBES, &w, eye, down, radius, false, &found);
    ut_check(v == SPAWN_SPOT_OK && found.moved == 0.0f && fabsf(found.at[1] - 6.0f) < 1e-4f,
             "a wall out of reach of the body moves nothing");

    fresh(&w);
    w.wall_ahead  = true;
    w.wall_y      = 6.15f;
    w.wall_behind = true;
    w.back_y      = 5.5f;
    v = spawn_place_spot(&PROBES, &w, eye, down, radius, false, &found);
    ut_check(v == SPAWN_SPOT_WALLED && w.headrooms == 0u,
             "between two walls closer than the body is wide it is refused, nothing more asked");

    fresh(&w);
    w.wall_ahead = true;
    w.wall_y     = 6.15f;
    v = spawn_place_spot(&PROBES, &w, eye, down, 0.0f, false, &found);
    ut_check(v == SPAWN_SPOT_OK && w.sides == 0u && found.moved == 0.0f,
             "a radius of nothing keeps nothing off the walls and casts no ray");

    fresh(&w);
    w.wall_ahead = true;
    w.wall_y     = 6.15f;
    w.floor      = SPAWN_SPOT_FLOOR_FOUND;
    w.mover      = false;
    {
        spawn_spot_probes_t no_side = PROBES;

        no_side.side = NULL;
        v = spawn_place_spot(&no_side, &w, eye, down, radius, false, &found);
        ut_check(v == SPAWN_SPOT_OK && fabsf(found.at[1] - 6.0f) < 1e-4f,
                 "a build without the ray keeps the place as it was, not guessed");
    }
}

int main(void)
{
    the_state();
    the_clicks();
    the_settle();
    the_right_click();
    the_turn();
    the_spot();
    the_wall();
    return ut_summary("spawn place");
}
