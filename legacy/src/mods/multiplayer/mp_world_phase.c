/* mp_world_phase.c: how far apart two free runners are, and how much of that to pay off now.
 *
 * Pure arithmetic over a handful of numbers. Nothing here reads or writes the engine; the caller
 * brings the poses in and takes a time back out.
 */
#include "mp_world_phase.h"

#include <string.h>

void mp_world_phase_reset(mp_world_phase_t *phase)
{
    if (phase == NULL) {
        return;
    }
    memset(phase, 0, sizeof *phase);
}

/* A free runner has no ends, so 0.99 and 0.01 are a hair apart rather than the whole travel. The
 * sign says which way: positive means the local mover is ahead and has to lose time. */
float mp_world_phase_delta(float local_fraction, float wire_fraction)
{
    float delta;

    if (local_fraction != local_fraction || wire_fraction != wire_fraction) {
        return 0.0f;   /* one side has no number, so there is nothing to conclude */
    }
    delta = local_fraction - wire_fraction;
    while (delta > 0.5f) {
        delta -= 1.0f;
    }
    while (delta <= -0.5f) {
        delta += 1.0f;
    }
    return delta;
}

float mp_world_phase_dead_band(float speed, float length)
{
    if (!(speed > 0.0f) || !(length > 0.0f)) {
        return 1.0f;   /* nothing about such a record can be measured, so nothing is a difference */
    }
    return 2.0f * MP_WORLD_PHASE_SUBSTEP * speed / length;
}

static mp_world_phase_slot_t *find(mp_world_phase_t *phase, uint16_t id)
{
    size_t index;

    for (index = 0; index < MP_WORLD_PHASE_SLOTS; ++index) {
        if (phase->slot[index].used && phase->slot[index].id == id) {
            return &phase->slot[index];
        }
    }
    return NULL;
}

static mp_world_phase_slot_t *claim(mp_world_phase_t *phase, uint16_t id)
{
    size_t index;

    for (index = 0; index < MP_WORLD_PHASE_SLOTS; ++index) {
        if (!phase->slot[index].used) {
            mp_world_phase_slot_t *slot = &phase->slot[index];

            memset(slot, 0, sizeof *slot);
            slot->used = true;
            slot->id   = id;
            return slot;
        }
    }
    return NULL;
}

static void release(mp_world_phase_slot_t *slot)
{
    memset(slot, 0, sizeof *slot);
}

bool mp_world_phase_measure(mp_world_phase_t *phase, uint16_t id, float delta, float speed,
                            float length)
{
    mp_world_phase_slot_t *slot;
    float                  magnitude;
    uint32_t               milli;

    if (phase == NULL || !(speed > 0.0f) || !(length > 0.0f) || delta != delta) {
        return false;
    }

    magnitude = delta < 0.0f ? -delta : delta;
    slot      = find(phase, id);

    if (magnitude <= mp_world_phase_dead_band(speed, length)) {
        if (slot != NULL) {
            ++phase->settled;
            release(slot);
        }
        return false;
    }

    if (slot == NULL) {
        slot = claim(phase, id);
        if (slot == NULL) {
            ++phase->full;
            return false;
        }
    }

    /* The newest measurement replaces the debt rather than adding to it: what is left of an older
     * one is already inside the difference this one measured. */
    slot->owed      = delta * length / speed;
    slot->have_pose = false;
    ++phase->taken;

    milli = (uint32_t)(magnitude * 1000.0f + 0.5f);
    if (milli > phase->worst_milli) {
        phase->worst_milli = milli;
    }
    return true;
}

float mp_world_phase_bite(mp_world_phase_t *phase, uint16_t id, float pose, float speed,
                          float length)
{
    mp_world_phase_slot_t *slot;
    float                  cap;
    float                  bite;
    float                  advance;

    if (phase == NULL || !(speed > 0.0f) || !(length > 0.0f) || pose != pose) {
        return 0.0f;
    }
    slot = find(phase, id);
    if (slot == NULL) {
        return 0.0f;
    }

    /* The first look after a measurement only seeds the reference. There is no way to tell an
     * integration from a still frame without one, and paying into a mover nobody is ticking would
     * stack biases that the next integration takes all at once. */
    if (!slot->have_pose) {
        slot->have_pose = true;
        slot->last_pose = pose;
        return 0.0f;
    }

    advance = pose - slot->last_pose;
    if (advance < 0.0f) {
        advance += length;   /* the ring wrapped between the two looks */
    }
    slot->last_pose = pose;

    /* The engine integrates movers once per rendered frame and this runs once per substep. Above
     * 32 frames a second those are the same thing, because the integrator returns at once when
     * the level clock has not moved; below it the mover integrates once per frame with a dt worth
     * several substeps while this comes past several times in between. Paying only when the pose
     * moved is the only thing between a low frame rate and a stack of biases taken all at once. */
    if (advance <= 0.0f) {
        return 0.0f;         /* nobody integrated this mover since the last look */
    }
    if (advance > speed * MP_WORLD_PHASE_JUMP_SECONDS) {
        /* Further than the engine's own frame time cap allows in one step, so this was not the
         * integrator: the level prime and a savegame load both rewrite timeBase for every mover.
         * The debt was measured against a world that is gone. */
        ++phase->discarded;
        release(slot);
        return 0.0f;
    }

    cap  = MP_WORLD_PHASE_BITE * MP_WORLD_PHASE_SUBSTEP;
    bite = slot->owed;
    if (bite > cap) {
        bite = cap;
    } else if (bite < -cap) {
        bite = -cap;
    }
    if (bite == 0.0f) {
        release(slot);
        ++phase->settled;
        return 0.0f;
    }

    slot->owed -= bite;
    ++phase->nudges;

    /* Paid off. The next measurement decides whether it stays that way. */
    if ((slot->owed > 0.0f ? slot->owed : -slot->owed) < 1e-6f) {
        release(slot);
        ++phase->settled;
    }
    return bite;
}

uint32_t mp_world_phase_owing(const mp_world_phase_t *phase)
{
    uint32_t owing = 0;
    size_t   index;

    if (phase == NULL) {
        return 0u;
    }
    for (index = 0; index < MP_WORLD_PHASE_SLOTS; ++index) {
        if (phase->slot[index].used) {
            ++owing;
        }
    }
    return owing;
}
