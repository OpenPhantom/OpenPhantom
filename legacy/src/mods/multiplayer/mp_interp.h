/* mp_interp.h: the far body's state at the render moment, sampled out of the history.
 *
 * Layer 1, pure logic. No engine, no socket, no wall clock: a received body goes in with its
 * tick, the timeline says which tick to show, and this module answers with that tick's sample.
 *
 * The one rule. Nothing is latched at arrival. A body received at tick T is stored under T and
 * nothing else happens; every channel of the far body, the position and heading, the clips and
 * their playheads, the live bits, the weapon, the health and the node twists, is read out of the
 * history at the render moment. The first build latched the discrete channels to the newest
 * packet and blended only the position a few ticks behind, so the far body's clips, weapon and
 * twists ran ahead of its movement by the whole lag: it took off before it rose, landed in the
 * air, and its twists jumped by two steps across every lost packet. Sampling all channels at one
 * moment is what makes the far body one body.
 *
 * The render moment is a whole tick and eighths of the next. At a whole tick the answer is that
 * tick's sample, exactly as the sender had it. Between two samples, or across a lost one, the
 * position and heading are blended and so are the twists, whose engine writers are linear in
 * time; the discrete state is the earlier sample's, because a change recorded at a tick belongs
 * to that tick's moment and not before it.
 *
 * Two samples of two worlds are never blended. Every body names the world its sender stood in, and
 * a point between the last pose of one level and the first of the next is a point in neither: a
 * reader that took it looked for a floor fifteen units from where anybody stood. Across a change of
 * world the earlier sample is held whole until the later one is due, as a lost sample is not.
 */
#ifndef MULTIPLAYER_MP_INTERP_H
#define MULTIPLAYER_MP_INTERP_H

#include "mp_snapshot.h"
#include "mp_snapshot_history.h"
#include "mp_timeline.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The default lag, three ticks: one lost sample stays invisible. */
#define MP_INTERP_DEFAULT_LAG MP_TIMELINE_TARGET_DEFAULT

typedef struct mp_interp {
    mp_timeline_t         timeline;
    mp_snapshot_history_t history;
    uint32_t              underruns;   /* render moments with no sample at or before them */
    uint32_t              world_holds; /* moments between two worlds, shown as the earlier sample */
} mp_interp_t;

/* What the far body is at the render moment: the tick shown, the tick of the sample the
 * discrete state came from, the blended position and heading, that sample's whole record, and
 * the twists blended between the two samples around the moment. */
typedef struct mp_interp_pose {
    uint32_t        tick;         /* the render tick */
    uint32_t        state_tick;   /* the sample `state` is; the render tick unless that was lost */
    float           position[3];
    float           yaw;
    mp_wire_body_t  state;        /* clips, playheads, bits, weapon, hero, health, dead, alive */
    mp_wire_twist_t twist[MP_WIRE_MAX_TWISTS];
    size_t          twist_count;
} mp_interp_pose_t;

/* An empty interpolator with the timeline's target lag, clamped by the timeline. */
void mp_interp_init(mp_interp_t *interp, uint32_t target_lag);

/* A received body at its own tick. Stored, and the tick noted to the timeline; nothing else.
 * An older tick arriving late fills its place in the history and moves the timeline not at all. */
void mp_interp_receive(mp_interp_t *interp, const mp_wire_body_t *body, uint32_t tick);

/* Once per own substep, after the substep's arrivals: the timeline's advance. */
bool mp_interp_advance(mp_interp_t *interp);

/* The far body at the render moment. False before the timeline has a render tick, and false on
 * an underrun (counted), which is a render tick with no sample at or up to a whole history
 * before it; then the caller holds what it last showed. */
bool mp_interp_resolve(mp_interp_t *interp, mp_interp_pose_t *out);

/* The newest sample the history holds, exactly as its sender sent it, and its tick. False with
 * none. For a host relaying one player's own state to the others: the render moment is this
 * machine's view of that body, and relaying it would add this machine's lag to theirs. */
bool mp_interp_newest(const mp_interp_t *interp, mp_wire_body_t *out, uint32_t *tick);

/* Blend two angles in degrees along the shortest arc; exposed so the unit test can pin the wrap. */
float mp_interp_blend_angle(float from, float to, float alpha);

const mp_timeline_t *mp_interp_timeline(const mp_interp_t *interp);

/* Hand the timeline's own regulation on or off. The timeline starts with the lag it was given,
 * which is what its unit tests pin; whether the lag is measured instead is a decision of the run,
 * so it is made here rather than at the module's own initialisation. */
void mp_interp_set_auto_lag(mp_interp_t *interp, bool enabled);
uint32_t             mp_interp_underruns(const mp_interp_t *interp);
uint32_t             mp_interp_world_holds(const mp_interp_t *interp);

#endif /* MULTIPLAYER_MP_INTERP_H */
