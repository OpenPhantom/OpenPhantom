/* mp_puppet.h: the far player's body on this machine, placed and dressed from samples.
 *
 * A puppet is not simulated. Its position, heading, clips, playheads, weapon and node twists come
 * from one sample per substep, and the engine's own functions dress the object: the clip players
 * and the track advance for the animation channels, the weapon setter, the force push starter,
 * and the timers the engine's own phase one runs for the blade and for the aux action that
 * commits a weapon change and releases a force bolt. Everything runs inside the bank window,
 * where the player pointer names the puppet's block, so every engine call acts on the puppet and
 * on nothing of the local player's.
 *
 * The sample is the far body's state at one sender tick, as the interpolator resolves it for the
 * render moment: every channel of it comes from the same moment, and the events the far player
 * had are performed when the render moment reaches the tick they were caught in, at the muzzle
 * of the puppet as it is drawn then.
 *
 * The weapon is the one thing that arrives twice, as an event at the moment of the change and as
 * the committed slot in every later sample. The event is what the change is started from, so the
 * puppet's draw clip runs in step with the far player's own; the state behind it is the
 * reconciliation, and it stands back until the wire agrees with the change the event began.
 *
 * One puppet per far bank. Each far bank shows one far player, and everything a puppet holds
 * between two windows, the sample, the weapon change in flight, the moments still due, the
 * vitals, belongs to that player alone; the window finds its own by the bank it runs in.
 */
#ifndef MULTIPLAYER_MP_PUPPET_H
#define MULTIPLAYER_MP_PUPPET_H

#include "mp_events.h"
#include "mp_puppet_anim.h"
#include "mp_puppet_sabre.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mp_puppet_sample {
    uint32_t        state_tick;   /* the sender tick `state` was sampled at */
    float           position[3];
    float           yaw;
    mp_wire_body_t  state;        /* clips, playheads, live bits, weapon, hero, health, dead */
    mp_wire_twist_t twist[MP_WIRE_MAX_TWISTS];
    size_t          twist_count;
} mp_puppet_sample_t;

/* Resolve the engine functions the puppet is dressed with and hand the phase module the puppet's
 * own phase one. Optional in the sense the bridge documents: a build where a function did not
 * resolve places the puppet and dresses it less, and says so once. */
void mp_puppet_resolve(void);

/* Forget what far bank `bank`'s puppet last applied, so its next apply starts every channel over.
 * Run on every arrival of a peer in front of that bank, because a restarted peer's body starts
 * from its own defaults, and the all form wherever the whole session starts over. */
void mp_puppet_reset(size_t bank);
void mp_puppet_reset_all(void);

/* How the far players' heroes and the bodies built for them ended up agreeing. Reported rather
 * than warned about, because the warning can only be read once per body and the interesting
 * number is how long the wire took to agree. */
void mp_puppet_hero_counts(uint32_t *agreed, uint32_t *apart, uint32_t *worst_wait);

/* The sample for bank `bank`'s coming window: everything at once, as the interpolator resolves
 * it. */
void mp_puppet_set_sample(size_t bank, const mp_puppet_sample_t *sample);

/* The render tick the coming window shows. Until it is known every queued event is due at
 * arrival; with it, an event waits for its tick. */
void mp_puppet_note_render_tick(size_t bank, uint32_t render_tick);

/* A moment the far player had (a shot, a push, a sabre action, the start of a weapon change), to
 * be performed inside the window in order when the render tick reaches the tick the event
 * carries. */
void mp_puppet_queue_event(size_t bank, const mp_event_t *event);

/* The bank-window callback: place and dress the block the window installed, run the puppet plan,
 * perform the events that are due, run the sabre fallback, then write the wire's health into
 * the banked status and act on the dead flag's edges. Only ever entered through
 * mp_bank_run_at, and it dresses the bank that call made active. */
void __cdecl mp_puppet_apply_window(void);

/* How many force pushes were started and shots spawned on the puppet, for the report. */
uint32_t mp_puppet_force_pushes(void);
uint32_t mp_puppet_shots(void);

/* Every counter the puppet keeps, for the report: the animation channels', the sabre's, the
 * events', and the vitals' (the health written from the wire, the deaths and revivals). */
typedef struct mp_puppet_counters {
    mp_puppet_anim_counters_t  anim;
    mp_puppet_sabre_counters_t sabre;
    uint32_t                   weapon_events;   /* changes started from an event, in step */
    uint32_t                   weapon_dropped;  /* an event that waited two seconds to start */
    uint32_t                   weapon_refused;  /* the setter did not take the change */
    uint32_t                   weapon_withheld; /* the state path held back: every track busy */
    uint32_t                   weapon_expired;  /* a started change the wire never confirmed */
    uint32_t                   force_pushes;
    uint32_t                   shots;
    uint32_t                   shots_unplaced;  /* the puppet's object did not read; not spawned */
    uint32_t                   pushes_dropped;  /* a push that waited two seconds for the aux */
    uint32_t                   events_dropped;  /* the ring was full */
    uint32_t                   events_late;     /* performed more than a tick after its tick */
    uint32_t                   events_forced;   /* performed at once, its tick too far ahead */
    uint32_t                   write_faults;    /* a placement write the patch layer refused */
    uint32_t                   health_writes;   /* the wire's health written into the bank */
    uint32_t                   health_refused;  /* windows with no placed status to write */
    uint32_t                   last_health;     /* the last value written */
    uint32_t                   died;            /* rising edges of the far player's dead flag */
    uint32_t                   revived;         /* falling edges: shadow and collision back */
    uint32_t                   shadow_faults;   /* the object's flag word did not read or write */
} mp_puppet_counters_t;

void mp_puppet_get_counters(mp_puppet_counters_t *out);

#endif /* MULTIPLAYER_MP_PUPPET_H */
