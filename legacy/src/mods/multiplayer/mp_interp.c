/* mp_interp.c: the far body sampled out of its history at the timeline's render moment. */
#include "mp_interp.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The peer body is always stored in this slot of the one-body snapshots, whichever slot it
 * arrived in on the wire, so the sampling reads one place. */
#define INTERP_SLOT 1u

/* How far down the history the sample at or before the render tick is looked for: the whole
 * ring. How far up the sample after it is looked for: the lag, which is where the newest sample
 * sits; the ring is the cap for a lag that a stall has stretched. The history is thirty two
 * entries by tick modulo thirty two with the tick checked on fetch, and with a target lag of
 * three, a band to four and the resync at twelve, every tick the sampling asks for lies inside
 * it. */
#define PROBE_DOWN MP_SNAPSHOT_HISTORY
#define PROBE_UP   MP_SNAPSHOT_HISTORY

void mp_interp_init(mp_interp_t *interp, uint32_t target_lag)
{
    mp_timeline_init(&interp->timeline, target_lag);
    mp_snapshot_history_init(&interp->history);
    interp->underruns   = 0u;
    interp->world_holds = 0u;
}

void mp_interp_receive(mp_interp_t *interp, const mp_wire_body_t *body, uint32_t tick)
{
    mp_snapshot_t one;

    mp_snapshot_clear(&one);
    one.tick = tick;
    mp_snapshot_set_body(&one, INTERP_SLOT, body);
    mp_snapshot_history_store(&interp->history, &one);
    mp_timeline_note_received(&interp->timeline, tick);
}

bool mp_interp_newest(const mp_interp_t *interp, mp_wire_body_t *out, uint32_t *tick)
{
    const mp_snapshot_t *newest = interp != NULL ? mp_snapshot_history_newest(&interp->history)
                                                 : NULL;

    if (newest == NULL || !mp_snapshot_has_body(newest, INTERP_SLOT)) {
        return false;
    }
    if (out != NULL) {
        *out = newest->body[INTERP_SLOT];
    }
    if (tick != NULL) {
        *tick = newest->tick;
    }
    return true;
}

bool mp_interp_advance(mp_interp_t *interp)
{
    return mp_timeline_advance(&interp->timeline);
}

float mp_interp_blend_angle(float from, float to, float alpha)
{
    float delta = to - from;

    while (delta > 180.0f) {
        delta -= 360.0f;
    }
    while (delta < -180.0f) {
        delta += 360.0f;
    }
    return from + delta * alpha;
}

/* A heading into 0..360 as the wire carries it, and a twist into -180..180 as the node array
 * holds it; a blend along the shortest arc can leave either range by the width of the step. */
static float wrap360(float degrees)
{
    float wrapped = (float)fmod((double)degrees, 360.0);

    if (wrapped < 0.0f) {
        wrapped += 360.0f;
    }
    return wrapped >= 360.0f ? 0.0f : wrapped;
}

static float wrap180(float degrees)
{
    float wrapped = wrap360(degrees);

    return wrapped > 180.0f ? wrapped - 360.0f : wrapped;
}

/* The body stored at exactly this tick, or NULL. */
static const mp_wire_body_t *body_at(const mp_snapshot_history_t *history, uint32_t tick)
{
    const mp_snapshot_t *snapshot = mp_snapshot_history_get(history, tick);

    if (snapshot == NULL || !mp_snapshot_has_body(snapshot, INTERP_SLOT)) {
        return NULL;
    }
    return &snapshot->body[INTERP_SLOT];
}

/* The nearest stored sample at or before `tick`, probing down one tick at a time; NULL when a
 * whole ring holds none. */
static const mp_wire_body_t *sample_at_or_before(const mp_snapshot_history_t *history,
                                                 uint32_t tick, uint32_t *found_tick)
{
    uint32_t back;

    for (back = 0u; back < PROBE_DOWN; ++back) {
        const mp_wire_body_t *body = body_at(history, tick - back);

        if (body != NULL) {
            *found_tick = tick - back;
            return body;
        }
    }
    return NULL;
}

/* The nearest stored sample after `tick`, up to `limit` ticks ahead. */
static const mp_wire_body_t *sample_after(const mp_snapshot_history_t *history, uint32_t tick,
                                          uint32_t limit, uint32_t *found_tick)
{
    uint32_t ahead;

    if (limit > PROBE_UP) {
        limit = PROBE_UP;
    }
    for (ahead = 1u; ahead <= limit; ++ahead) {
        const mp_wire_body_t *body = body_at(history, tick + ahead);

        if (body != NULL) {
            *found_tick = tick + ahead;
            return body;
        }
    }
    return NULL;
}

static const mp_wire_twist_t *twist_of(const mp_wire_body_t *body, uint8_t node)
{
    size_t index;

    for (index = 0; index < body->twist_count && index < MP_WIRE_MAX_TWISTS; ++index) {
        if (body->twist[index].node == node) {
            return &body->twist[index];
        }
    }
    return NULL;
}

static void put_twist(mp_interp_pose_t *out, uint8_t node, float pitch, float yaw)
{
    if (out->twist_count >= MP_WIRE_MAX_TWISTS) {
        return;
    }
    out->twist[out->twist_count].node  = node;
    out->twist[out->twist_count].pitch = wrap180(pitch);
    out->twist[out->twist_count].yaw   = wrap180(yaw);
    ++out->twist_count;
}

/* The twists paired by node: a node in both blends between them, a node only in the earlier
 * sample blends toward zero, a node only in the later one blends up from zero. The engine's
 * writers of these angles, the flinch's timers, the aim ease and the steering lean, are all
 * linear in time, so the blend is exact to first order. */
static void blend_twists(const mp_wire_body_t *from, const mp_wire_body_t *to, float alpha,
                         mp_interp_pose_t *out)
{
    size_t index;

    out->twist_count = 0u;
    for (index = 0; index < from->twist_count && index < MP_WIRE_MAX_TWISTS; ++index) {
        const mp_wire_twist_t *a = &from->twist[index];
        const mp_wire_twist_t *b = twist_of(to, a->node);
        float                  pitch_to = b != NULL ? b->pitch : 0.0f;
        float                  yaw_to = b != NULL ? b->yaw : 0.0f;

        put_twist(out, a->node, mp_interp_blend_angle(a->pitch, pitch_to, alpha),
                  mp_interp_blend_angle(a->yaw, yaw_to, alpha));
    }
    for (index = 0; index < to->twist_count && index < MP_WIRE_MAX_TWISTS; ++index) {
        const mp_wire_twist_t *b = &to->twist[index];

        if (twist_of(from, b->node) == NULL) {
            put_twist(out, b->node, mp_interp_blend_angle(0.0f, b->pitch, alpha),
                      mp_interp_blend_angle(0.0f, b->yaw, alpha));
        }
    }
}

static void exact_sample(const mp_wire_body_t *from, mp_interp_pose_t *out)
{
    memcpy(out->position, from->position, sizeof out->position);
    out->yaw = from->orientation[1];
    memcpy(out->twist, from->twist, sizeof out->twist);
    out->twist_count = from->twist_count <= MP_WIRE_MAX_TWISTS ? from->twist_count
                                                                : MP_WIRE_MAX_TWISTS;
}

static void blended_sample(const mp_wire_body_t *from, const mp_wire_body_t *to, float alpha,
                           mp_interp_pose_t *out)
{
    int axis;

    for (axis = 0; axis < 3; ++axis) {
        out->position[axis] = from->position[axis] +
                              (to->position[axis] - from->position[axis]) * alpha;
    }
    out->yaw = wrap360(mp_interp_blend_angle(from->orientation[1], to->orientation[1], alpha));
    blend_twists(from, to, alpha, out);
}

bool mp_interp_resolve(mp_interp_t *interp, mp_interp_pose_t *out)
{
    const mp_timeline_t  *timeline = &interp->timeline;
    const mp_wire_body_t *from;
    const mp_wire_body_t *to = NULL;
    uint32_t              render;
    uint32_t              phase;
    uint32_t              from_tick = 0u;
    uint32_t              to_tick = 0u;
    int32_t               lag;

    if (out == NULL || !mp_timeline_render_known(timeline)) {
        return false;
    }
    render = mp_timeline_render_tick(timeline);
    phase  = mp_timeline_render_phase8(timeline);
    from   = sample_at_or_before(&interp->history, render, &from_tick);
    if (from == NULL) {
        /* Nothing is extrapolated: the far body's future depends on input this machine does not
         * have, and a body that guessed and was corrected would teleport, which reads worse than
         * one that waits. */
        ++interp->underruns;
        return false;
    }

    memset(out, 0, sizeof *out);
    out->tick       = render;
    out->state_tick = from_tick;
    out->state      = *from;

    /* The exact sample whenever the moment lies on it. Otherwise the moment lies between the
     * sample and the next one stored, which may be more than a tick on across a loss, and the
     * blend runs across that whole span: a lost sample is then a straight line through it
     * rather than a sample held and a jump. */
    lag = mp_timeline_lag(timeline);
    if (from_tick != render || phase != 0u) {
        to = sample_after(&interp->history, render, lag > 0 ? (uint32_t)lag : 0u, &to_tick);
    }
    if (to != NULL && to->world != from->world) {
        ++interp->world_holds;   /* the last sample of one world and the first of the next */
        to = NULL;
    }
    if (to == NULL) {
        exact_sample(from, out);
        return true;
    }
    {
        float span   = (float)(int32_t)(to_tick - from_tick);
        float moment = (float)(int32_t)(render - from_tick) + (float)phase / MP_TIMELINE_EIGHTHS;

        blended_sample(from, to, moment / span, out);
    }
    return true;
}

const mp_timeline_t *mp_interp_timeline(const mp_interp_t *interp)
{
    return &interp->timeline;
}

/* A fresh interpolator holds the lag it was built with, and the bridge builds one on every
 * arrival of a peer; the run's choice therefore has to be put back right there, or the
 * regulation is on for the first peer of a session and off for every one after it, which is
 * exactly the class of defect a reset produces and no test sees. */
void mp_interp_set_auto_lag(mp_interp_t *interp, bool enabled)
{
    if (interp != NULL) {
        mp_timeline_set_auto_lag(&interp->timeline, enabled);
    }
}

uint32_t mp_interp_underruns(const mp_interp_t *interp)
{
    return interp->underruns;
}

uint32_t mp_interp_world_holds(const mp_interp_t *interp)
{
    return interp->world_holds;
}
