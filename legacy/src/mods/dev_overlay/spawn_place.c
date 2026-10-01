/* spawn_place.c: see spawn_place.h. */
#include "spawn_place.h"

#include <math.h>
#include <string.h>

#define RADIANS_TO_DEGREES (180.0f / 3.14159265f)

static spawn_place_t place_state;

spawn_place_t *spawn_place_state(void)
{
    return &place_state;
}

void spawn_place_ask(spawn_place_t *place, bool on)
{
    if (place == NULL) {
        return;
    }
    place->asked_on  = on;
    place->asked_off = !on;
}

/* The asks that belong to one life of the mode go with it, so a click made in the last frame of one
 * life cannot place in the first frame of the next. */
static void forget_the_asks(spawn_place_t *place)
{
    place->click_asked  = false;
    place->remove_asked = false;
}

static void leave(spawn_place_t *place, spawn_place_step_t *step, spawn_place_off_t why)
{
    place->on       = false;
    place->settling = false;   /* the mode's world hold goes with it, and the settle with that */
    step->left      = why;
    forget_the_asks(place);
}

spawn_place_step_t spawn_place_frame(spawn_place_t *place, const spawn_place_facts_t *facts)
{
    spawn_place_step_t step;

    memset(&step, 0, sizeof step);
    if (place == NULL || facts == NULL) {
        return step;
    }
    place->available   = facts->available;
    place->unavailable = facts->available ? NULL : facts->unavailable;
    if (place->on) {
        if (place->asked_off) {
            leave(place, &step, SPAWN_PLACE_OFF_ASKED);
        } else if (!facts->panel_open) {
            leave(place, &step, SPAWN_PLACE_OFF_PANEL);
        } else if (facts->world != place->world) {
            leave(place, &step, SPAWN_PLACE_OFF_WORLD);
        } else if (!facts->player) {
            leave(place, &step, SPAWN_PLACE_OFF_PLAYER);
        } else if (facts->player_dead) {
            leave(place, &step, SPAWN_PLACE_OFF_DIED);
        } else if (!facts->available) {
            leave(place, &step, SPAWN_PLACE_OFF_UNAVAILABLE);
        }
    } else if (place->asked_on) {
        if (facts->panel_open && facts->player && !facts->player_dead && facts->available) {
            place->on    = true;
            place->world = facts->world;
            forget_the_asks(place);
            step.entered = true;
        } else {
            step.refused = true;
        }
    }
    place->asked_on  = false;
    place->asked_off = false;
    return step;
}

void spawn_place_ask_click(spawn_place_t *place)
{
    if (place != NULL && place->on) {
        place->click_asked = true;
    }
}

void spawn_place_ask_remove(spawn_place_t *place)
{
    if (place != NULL && place->on) {
        place->remove_asked = true;
    }
}

bool spawn_place_take_click(spawn_place_t *place, uint32_t now_ms)
{
    if (place == NULL || !place->click_asked) {
        return false;
    }
    place->click_asked = false;
    /* The difference is taken unsigned, so a clock that wrapped between the two clicks still reads
     * as the short interval it was. */
    if (place->clicked_once &&
        (uint32_t)(now_ms - place->last_click_ms) < SPAWN_PLACE_CLICK_LOCK_MS) {
        return false;
    }
    place->clicked_once  = true;
    place->last_click_ms = now_ms;
    return true;
}

bool spawn_place_take_remove(spawn_place_t *place)
{
    bool asked;

    if (place == NULL) {
        return false;
    }
    asked               = place->remove_asked;
    place->remove_asked = false;
    return asked;
}

void spawn_place_settle_begin(spawn_place_t *place, bool counted, uint32_t step, uint32_t now_ms)
{
    if (place == NULL || !place->on) {
        return;
    }
    place->settling       = true;
    place->settle_counted = counted;
    place->settle_step    = step;
    place->settle_ms      = now_ms;
}

spawn_settle_t spawn_place_settle_tick(spawn_place_t *place, bool counted, uint32_t step,
                                       uint32_t now_ms, uint32_t *ran)
{
    uint32_t steps = 0;

    if (ran != NULL) {
        *ran = 0;
    }
    if (place == NULL || !place->settling) {
        return SPAWN_SETTLE_IDLE;
    }
    /* Differences, taken unsigned, so a counter or a clock that wrapped still reads right. A count
     * that could be read at the start and not now leaves the time to end it. */
    if (place->settle_counted && counted) {
        steps = step - place->settle_step;
    }
    if (ran != NULL) {
        *ran = steps;
    }
    if (steps >= SPAWN_PLACE_SETTLE_SUBSTEPS) {
        place->settling = false;
        return SPAWN_SETTLE_DONE;
    }
    if ((uint32_t)(now_ms - place->settle_ms) >= SPAWN_PLACE_SETTLE_MAX_MS) {
        place->settling = false;
        return SPAWN_SETTLE_TIMED_OUT;
    }
    return SPAWN_SETTLE_RUNNING;
}

spawn_remove_t spawn_place_remove_verdict(bool hovered, bool client, bool ridden)
{
    if (!hovered) {
        return SPAWN_REMOVE_NOTHING;
    }
    if (client) {
        return SPAWN_REMOVE_CLIENT;
    }
    if (ridden) {
        return SPAWN_REMOVE_RIDDEN;
    }
    return SPAWN_REMOVE_DELETE;
}

float spawn_place_hover_limit(float strike)
{
    if (!(strike >= 0.0f) || strike > SPAWN_SPOT_REACH) {
        strike = SPAWN_SPOT_REACH;
    }
    return strike + SPAWN_PLACE_HOVER_SLACK;
}

bool spawn_place_cap_reached(bool session, uint32_t session_cap, uint32_t alive,
                             uint32_t single_cap)
{
    if (!session) {
        return alive >= single_cap;
    }
    return session_cap != 0u && alive >= session_cap;
}

float spawn_place_wrap(float degrees)
{
    if (!(degrees == degrees) || degrees > 1.0e6f || degrees < -1.0e6f) {
        return 0.0f;   /* nothing a wheel could have produced; a facing is never lost to a NaN */
    }
    degrees = fmodf(degrees, 360.0f);
    if (degrees < 0.0f) {
        degrees += 360.0f;
    }
    return (degrees >= 360.0f) ? 0.0f : degrees;
}

float spawn_place_turned(float facing, int32_t notches, bool fine, bool coarse)
{
    if (coarse) {
        /* Onto the nearest quarter first, so the first notch lines the entity up with the grid of
         * the level rather than turning it ninety degrees from an odd angle. */
        float quarter = floorf(spawn_place_wrap(facing) / SPAWN_PLACE_TURN_COARSE + 0.5f);

        return spawn_place_wrap((quarter + (float)notches) * SPAWN_PLACE_TURN_COARSE);
    }
    return spawn_place_wrap(facing + (float)notches * (fine ? SPAWN_PLACE_TURN_FINE
                                                            : SPAWN_PLACE_TURN_STEP));
}

void spawn_place_turn(spawn_place_t *place, int32_t notches, bool fine, bool coarse,
                      float current)
{
    if (place == NULL || notches == 0) {
        return;
    }
    place->facing = spawn_place_turned(place->fixed ? place->facing : current, notches, fine,
                                       coarse);
    place->fixed  = true;
}

void spawn_place_face_player(spawn_place_t *place)
{
    if (place != NULL) {
        place->fixed = false;
    }
}

float spawn_place_facing(const spawn_place_t *place, float toward_player)
{
    if (place != NULL && place->fixed) {
        return place->facing;
    }
    return spawn_place_wrap(toward_player);
}

float spawn_place_yaw_toward(const float from[2], const float to[2])
{
    float dx = to[0] - from[0];
    float dy = to[1] - from[1];

    if (dx == 0.0f && dy == 0.0f) {
        return 0.0f;
    }
    return spawn_place_wrap(atan2f(-dx, dy) * RADIANS_TO_DEGREES);
}

/* The floor under `spot`, looked for just above it, and the place moved onto it. */
static spawn_spot_verdict_t onto_the_floor(const spawn_spot_probes_t *probes, void *user,
                                           float spot[3])
{
    float              at[3];
    float              offset   = 0.0f;
    bool               on_mover = false;
    spawn_spot_floor_t floor;

    at[0] = spot[0];
    at[1] = spot[1];
    at[2] = spot[2] + SPAWN_SPOT_LIFT;
    floor = (probes->floor != NULL) ? probes->floor(user, at, &offset, &on_mover)
                                    : SPAWN_SPOT_FLOOR_UNAVAILABLE;
    if (floor != SPAWN_SPOT_FLOOR_FOUND ||
        !(offset >= -(SPAWN_SPOT_LIFT + SPAWN_SPOT_FLOOR_REACH))) {
        return SPAWN_SPOT_NO_FLOOR;
    }
    spot[2] = at[2] + offset;   /* the engine's own sign: the offset is the snap */
    return on_mover ? SPAWN_SPOT_ON_MOVER : SPAWN_SPOT_OK;
}

/* Ahead along the pointer's heading, back, and the two sides. A ray straight down has no heading
 * and takes the world's own y for one. */
static void side_headings(const float direction[3], float out[4][2])
{
    float x      = direction[0];
    float y      = direction[1];
    float length = sqrtf(x * x + y * y);

    if (!(length > 1.0e-3f)) {
        x = 0.0f;
        y = 1.0f;
    } else {
        x /= length;
        y /= length;
    }
    out[0][0] = x;
    out[0][1] = y;
    out[1][0] = -x;
    out[1][1] = -y;
    out[2][0] = -y;
    out[2][1] = x;
    out[3][0] = y;
    out[3][1] = -x;
}

/* Casts the four rays of `length` from a body's height over `spot` and sums, in `push`, how far
 * each one that struck says to move away from what it struck. */
static bool sides_struck(const spawn_spot_probes_t *probes, void *user, const float spot[3],
                         float headings[4][2], float length, float push[2])
{
    float    from[3];
    bool     struck = false;
    uint32_t k;

    from[0] = spot[0];
    from[1] = spot[1];
    from[2] = spot[2] + SPAWN_SPOT_SIDE_HEIGHT;
    push[0] = 0.0f;
    push[1] = 0.0f;
    for (k = 0; k < 4u; ++k) {
        float to[3];
        float distance = 0.0f;

        to[0] = from[0] + headings[k][0] * length;
        to[1] = from[1] + headings[k][1] * length;
        to[2] = from[2];
        if (probes->side(user, from, to, &distance) && distance < length) {
            float short_by = length - ((distance > 0.0f) ? distance : 0.0f);

            push[0] -= headings[k][0] * short_by;
            push[1] -= headings[k][1] * short_by;
            struck = true;
        }
    }
    return struck;
}

static spawn_spot_verdict_t off_the_walls(const spawn_spot_probes_t *probes, void *user,
                                          const float direction[3], float radius, float spot[3],
                                          float *moved)
{
    float                headings[4][2];
    float                push[2];
    float                length = radius + SPAWN_SPOT_WALL_MARGIN - SPAWN_SPOT_RAY_RADIUS;
    spawn_spot_verdict_t verdict;

    *moved = 0.0f;
    if (probes->side == NULL || !(length > SPAWN_SPOT_RECAST_SLACK)) {
        return SPAWN_SPOT_OK;
    }
    side_headings(direction, headings);
    if (!sides_struck(probes, user, spot, headings, length, push)) {
        return SPAWN_SPOT_OK;
    }
    spot[0] += push[0];
    spot[1] += push[1];
    *moved  = sqrtf(push[0] * push[0] + push[1] * push[1]);
    verdict = onto_the_floor(probes, user, spot);
    if (verdict != SPAWN_SPOT_OK) {
        return verdict;
    }
    if (sides_struck(probes, user, spot, headings, length - SPAWN_SPOT_RECAST_SLACK, push)) {
        return SPAWN_SPOT_WALLED;
    }
    return SPAWN_SPOT_OK;
}

spawn_spot_verdict_t spawn_place_spot(const spawn_spot_probes_t *probes, void *user,
                                      const float origin[3], const float direction[3],
                                      float radius, bool cap_reached, spawn_spot_found_t *found)
{
    float                end[3];
    float                distance = 0.0f;
    float               *spot;
    spawn_spot_verdict_t verdict;
    uint32_t             k;

    if (found == NULL || origin == NULL || direction == NULL) {
        return SPAWN_SPOT_NOTHING_HIT;
    }
    spot          = found->at;
    found->strike = SPAWN_SPOT_REACH;
    found->moved  = 0.0f;
    for (k = 0; k < 3u; ++k) {
        end[k]  = origin[k] + direction[k] * SPAWN_SPOT_REACH;
        spot[k] = origin[k] + direction[k] * SPAWN_SPOT_MISS;
    }
    if (probes == NULL || probes->hit == NULL || !probes->hit(user, origin, end, &distance)) {
        return SPAWN_SPOT_NOTHING_HIT;
    }
    found->strike = distance;
    for (k = 0; k < 3u; ++k) {
        spot[k] = origin[k] + direction[k] * distance;
    }
    verdict = onto_the_floor(probes, user, spot);
    if (verdict == SPAWN_SPOT_OK) {
        verdict = off_the_walls(probes, user, direction, radius, spot, &found->moved);
    }
    if (verdict != SPAWN_SPOT_OK) {
        return verdict;
    }
    if (probes->headroom != NULL && !probes->headroom(user, spot)) {
        return SPAWN_SPOT_NO_HEADROOM;
    }
    if (probes->crowded != NULL && probes->crowded(user, spot)) {
        return SPAWN_SPOT_CROWDED;
    }
    return cap_reached ? SPAWN_SPOT_CAP : SPAWN_SPOT_OK;
}

bool spawn_place_crowds(const float at[3], const float other[3])
{
    float dx = other[0] - at[0];
    float dy = other[1] - at[1];

    return dx * dx + dy * dy < SPAWN_SPOT_CROWD_UNITS * SPAWN_SPOT_CROWD_UNITS &&
           fabsf(other[2] - at[2]) < SPAWN_SPOT_CROWD_HEIGHT;
}

const char *spawn_place_spot_word(spawn_spot_verdict_t verdict)
{
    switch (verdict) {
    case SPAWN_SPOT_OK:          return "click places it";
    case SPAWN_SPOT_NOTHING_HIT: return "nothing under the pointer";
    case SPAWN_SPOT_NO_FLOOR:    return "no floor";
    case SPAWN_SPOT_ON_MOVER:    return "on a moving platform";
    case SPAWN_SPOT_WALLED:      return "too tight between walls";
    case SPAWN_SPOT_NO_HEADROOM: return "no headroom";
    case SPAWN_SPOT_CROWDED:     return "someone stands there";
    case SPAWN_SPOT_CAP:         return "the cap is reached";
    default:                     return "?";
    }
}
