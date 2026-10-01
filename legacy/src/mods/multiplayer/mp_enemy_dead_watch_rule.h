/* mp_enemy_dead_watch_rule.h: when a dead enemy has stood too long, and when a corpse was moved.
 *
 * Layer 1, pure. It follows one life of one enemy key through the host's census, a sample a
 * substep, and says when something happened that the engine should not have let happen.
 *
 * The first is a dead enemy that stays up. A life whose health falls from above zero to zero or
 * below opens a run at that sample; the run stays open while the body is drawn and solid outside
 * the four death states, 11 to 14, and closes at the first sample where it is not. The engine's
 * own deaths close it within seconds, the slowest of them in under 11 s, so a run still open after
 * 12 s of world time is a death that did not finish. World time and not substeps, because a
 * substep is shorter at a higher rate. How long a run lasted is measured to its last sample
 * standing, so a stretch the census did not see counts for nothing.
 *
 * The second is a corpse whose clip changes. The engine never puts a clip on a body in state 14,
 * except in the first substep there, where a script's own death may still play its Face. So the
 * clip at the second sample in state 14 is the corpse's, and any other clip after it was put on by
 * another hand: a mod standing the corpse up.
 *
 * Neither changes anything. They are measurements, and their counters grow only when a death goes
 * wrong.
 *
 * A client follows the same rule over its replicas, with one difference it states in each sample:
 * a replica's reaction state is only the host's word for it, and a body that stands drawn and
 * solid in a death state is exactly what the client watch is there to see. So a sample judged by
 * its body alone ends a run only when the body lies down, disappears, lives again or goes, and the
 * corpse's clip is left to the host's own watch.
 */
#ifndef MULTIPLAYER_MP_ENEMY_DEAD_WATCH_RULE_H
#define MULTIPLAYER_MP_ENEMY_DEAD_WATCH_RULE_H

#include <stdbool.h>
#include <stdint.h>

/* World seconds a dead enemy may stand before the run counts as a failure. */
#define MP_DEAD_WATCH_LIMIT_SECONDS 12.0f

/* The reaction states the watch names. */
#define MP_DEAD_WATCH_STATE_SCRIPT       1u    /* the script runs */
#define MP_DEAD_WATCH_STATE_LANDING      7u    /* the landing after a throw */
#define MP_DEAD_WATCH_STATE_DEATH_FIRST  11u   /* dying, fading, shattering and the corpse */
#define MP_DEAD_WATCH_STATE_CORPSE       14u

/* One substep's look at a life. The four body fields are read only once the life has fallen,
 * and only while its run is open or about to open (mp_dead_watch_needs_body). */
typedef struct mp_dead_watch_sample {
    float    now;            /* world time, seconds since the level began */
    uint32_t census;         /* which census of the host this sample comes from */
    int32_t  health;
    uint8_t  state;          /* the reaction state */
    uint8_t  clip;           /* the base clip */
    bool     drawn;          /* bit 0 of the body's flags */
    bool     solid;          /* the body's class, not 0 */
    bool     script_death;   /* the script owns the death: stateFlags 0x10000 */
    bool     guard_fired;    /* the death guard has fired: the death latch is set */
    bool     by_body;        /* judged by the body alone: the state ends no run, no corpse watch */
} mp_dead_watch_sample_t;

/* How a run ended. */
typedef enum mp_dead_watch_end {
    MP_DEAD_WATCH_STILL_STANDING = 0,
    MP_DEAD_WATCH_DEATH_STATE,   /* it entered 11 to 14 */
    MP_DEAD_WATCH_HIDDEN,        /* it was no longer drawn: a burst, or a script hiding it */
    MP_DEAD_WATCH_UNSOLID,       /* it was no longer solid */
    MP_DEAD_WATCH_REVIVED,       /* its health rose above zero again */
    MP_DEAD_WATCH_REMOVED        /* it left the census */
} mp_dead_watch_end_t;

/* Where a run stood when it passed the limit. */
typedef enum mp_dead_watch_phase {
    MP_DEAD_WATCH_PHASE_LANDING = 0,   /* state 7, which has no health test of its own */
    MP_DEAD_WATCH_PHASE_UNDEAD,        /* state 1, and the script does not own the death */
    MP_DEAD_WATCH_PHASE_SCRIPTED,      /* state 1, the script owns the death and has not ended it */
    MP_DEAD_WATCH_PHASE_OTHER,
    MP_DEAD_WATCH_PHASES
} mp_dead_watch_phase_t;

/* The distribution of the runs that ended: within 2 s, 5 s, 12 s, and later. */
#define MP_DEAD_WATCH_BUCKETS 4u

typedef struct mp_dead_watch_life {
    bool     known;
    uint8_t  generation;
    bool     sampled;         /* one sample of this life has been taken */
    bool     was_up;          /* its health has been above zero */
    bool     fell;            /* and then at or below zero */
    bool     standing;        /* the run that began at the fall is open */
    bool     over;            /* the run has passed the limit */
    uint8_t  end;             /* mp_dead_watch_end_t, once the run has closed */
    uint8_t  state;           /* the last state seen */
    uint8_t  state_before;    /* the state before the last change */
    float    fell_at;
    uint32_t fell_census;
    float    stood_at;        /* the last sample at which the run was open */
    uint32_t stood_census;
    uint32_t last_census;     /* the last sample of this life */

    uint8_t  corpse_samples;  /* samples in state 14, held at 2 */
    uint8_t  corpse_clip;     /* its clip at the second of them */
    bool     corpse_moved;
    uint8_t  corpse_moved_to;
    float    corpse_at;       /* the first sample in state 14 */
    float    corpse_moved_at;
} mp_dead_watch_life_t;

/* What one step saw. */
#define MP_DEAD_WATCH_FELL          0x01u   /* this sample is the fall */
#define MP_DEAD_WATCH_PASSED_LIMIT  0x02u   /* the run has just passed the limit */
#define MP_DEAD_WATCH_CLOSED        0x04u   /* the run has just closed */
#define MP_DEAD_WATCH_CORPSE_MOVED  0x08u   /* the corpse's clip has just changed */
#define MP_DEAD_WATCH_CORPSE_HELD   0x10u   /* the corpse's clip is known from this sample on */

/* A fresh life of the key. */
void mp_dead_watch_begin(mp_dead_watch_life_t *life, uint8_t generation);

/* Whether the four body fields of the next sample are wanted: the life has fallen, or falls with
 * `health`, and its run is open or about to open. */
bool mp_dead_watch_needs_body(const mp_dead_watch_life_t *life, int32_t health);

/* One sample of the life. Answers the MP_DEAD_WATCH_* bits of what happened in it. */
uint32_t mp_dead_watch_step(mp_dead_watch_life_t *life, const mp_dead_watch_sample_t *sample);

/* The life has left the census, a removal or another life on its key. Closes an open run at its
 * last sample standing; answers MP_DEAD_WATCH_CLOSED when there was one. */
uint32_t mp_dead_watch_gone(mp_dead_watch_life_t *life);

/* Why a run open at `sample` ends there, MP_DEAD_WATCH_STILL_STANDING when it does not. */
mp_dead_watch_end_t mp_dead_watch_down(const mp_dead_watch_sample_t *sample);

/* Where a run stood when it passed the limit. */
mp_dead_watch_phase_t mp_dead_watch_phase(uint8_t state, bool script_death);

/* The bucket a run of `seconds` falls in: 0 within 2 s, 1 within 5 s, 2 within 12 s, 3 later. */
uint32_t mp_dead_watch_bucket(float seconds);

/* How long the run stood, from the fall to its last sample standing, in world seconds and in
 * censuses; 0 for a life that has not fallen. */
float    mp_dead_watch_stood_seconds(const mp_dead_watch_life_t *life);
uint32_t mp_dead_watch_stood_censuses(const mp_dead_watch_life_t *life);

#endif /* MULTIPLAYER_MP_ENEMY_DEAD_WATCH_RULE_H */
