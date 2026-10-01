/* mp_enemy_dead_watch_rule.c: when a dead enemy stood too long, and when a corpse was moved. See
 * the header. */
#include "mp_enemy_dead_watch_rule.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

void mp_dead_watch_begin(mp_dead_watch_life_t *life, uint8_t generation)
{
    memset(life, 0, sizeof *life);
    life->known      = true;
    life->generation = generation;
}

bool mp_dead_watch_needs_body(const mp_dead_watch_life_t *life, int32_t health)
{
    if (health > 0) {
        return false;
    }
    return life->standing || (life->was_up && !life->fell);
}

mp_dead_watch_end_t mp_dead_watch_down(const mp_dead_watch_sample_t *sample)
{
    if (!sample->by_body && sample->state >= MP_DEAD_WATCH_STATE_DEATH_FIRST &&
        sample->state <= MP_DEAD_WATCH_STATE_CORPSE) {
        return MP_DEAD_WATCH_DEATH_STATE;
    }
    if (!sample->drawn) {
        return MP_DEAD_WATCH_HIDDEN;
    }
    if (!sample->solid) {
        return MP_DEAD_WATCH_UNSOLID;
    }
    return MP_DEAD_WATCH_STILL_STANDING;
}

mp_dead_watch_phase_t mp_dead_watch_phase(uint8_t state, bool script_death)
{
    if (state == MP_DEAD_WATCH_STATE_LANDING) {
        return MP_DEAD_WATCH_PHASE_LANDING;
    }
    if (state == MP_DEAD_WATCH_STATE_SCRIPT) {
        return script_death ? MP_DEAD_WATCH_PHASE_SCRIPTED : MP_DEAD_WATCH_PHASE_UNDEAD;
    }
    return MP_DEAD_WATCH_PHASE_OTHER;
}

/* Written as NOTs so that a span that is not a number lands in the first bucket, the one that
 * means nothing went wrong, rather than in the last. */
uint32_t mp_dead_watch_bucket(float seconds)
{
    if (!(seconds > 2.0f)) {
        return 0u;
    }
    if (!(seconds > 5.0f)) {
        return 1u;
    }
    if (!(seconds > MP_DEAD_WATCH_LIMIT_SECONDS)) {
        return 2u;
    }
    return 3u;
}

/* A clock that went back, a game restored over the level, gives a negative span, which is no
 * time at all. */
float mp_dead_watch_stood_seconds(const mp_dead_watch_life_t *life)
{
    float span;

    if (!life->fell) {
        return 0.0f;
    }
    span = life->stood_at - life->fell_at;
    return span > 0.0f ? span : 0.0f;
}

uint32_t mp_dead_watch_stood_censuses(const mp_dead_watch_life_t *life)
{
    return life->fell ? life->stood_census - life->fell_census : 0u;
}

static void close_run(mp_dead_watch_life_t *life, mp_dead_watch_end_t end)
{
    life->standing = false;
    life->end      = (uint8_t)end;
}

/* The corpse's clip is fixed at its second sample in state 14 and compared from its third on. */
static uint32_t watch_corpse(mp_dead_watch_life_t *life, const mp_dead_watch_sample_t *sample)
{
    if (sample->by_body || sample->state != MP_DEAD_WATCH_STATE_CORPSE) {
        return 0u;
    }
    if (life->corpse_samples == 0u) {
        life->corpse_samples = 1u;
        life->corpse_at      = sample->now;
        return 0u;
    }
    if (life->corpse_samples == 1u) {
        life->corpse_samples = 2u;
        life->corpse_clip    = sample->clip;
        return MP_DEAD_WATCH_CORPSE_HELD;
    }
    if (life->corpse_moved || sample->clip == life->corpse_clip) {
        return 0u;
    }
    life->corpse_moved    = true;
    life->corpse_moved_to = sample->clip;
    life->corpse_moved_at = sample->now;
    return MP_DEAD_WATCH_CORPSE_MOVED;
}

uint32_t mp_dead_watch_step(mp_dead_watch_life_t *life, const mp_dead_watch_sample_t *sample)
{
    uint32_t            events = 0u;
    mp_dead_watch_end_t down;

    if (life->sampled && sample->state != life->state) {
        life->state_before = life->state;
    }
    life->state       = sample->state;
    life->sampled     = true;
    life->last_census = sample->census;

    if (sample->health > 0) {
        life->was_up = true;
        if (life->standing) {
            close_run(life, MP_DEAD_WATCH_REVIVED);
            events |= MP_DEAD_WATCH_CLOSED;
        }
    } else if (life->was_up && !life->fell) {
        life->fell         = true;
        life->standing     = true;
        life->fell_at      = sample->now;
        life->fell_census  = sample->census;
        life->stood_at     = sample->now;
        life->stood_census = sample->census;
        events |= MP_DEAD_WATCH_FELL;
    }
    if (life->standing) {
        down = mp_dead_watch_down(sample);
        if (down != MP_DEAD_WATCH_STILL_STANDING) {
            close_run(life, down);
            events |= MP_DEAD_WATCH_CLOSED;
        } else {
            life->stood_at     = sample->now;
            life->stood_census = sample->census;
            if (!life->over && mp_dead_watch_stood_seconds(life) > MP_DEAD_WATCH_LIMIT_SECONDS) {
                life->over = true;
                events |= MP_DEAD_WATCH_PASSED_LIMIT;
            }
        }
    }
    return events | watch_corpse(life, sample);
}

uint32_t mp_dead_watch_gone(mp_dead_watch_life_t *life)
{
    if (!life->standing) {
        return 0u;
    }
    close_run(life, MP_DEAD_WATCH_REMOVED);
    return MP_DEAD_WATCH_CLOSED;
}
