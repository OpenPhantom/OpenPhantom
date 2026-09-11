/* object_track.c: see object_track.h. */
#include "object_track.h"

#include <math.h>
#include <string.h>

typedef struct track_entry {
    uintptr_t key;            /* 0 means the slot has never been used */
    uint32_t  frame;          /* the frame this entry was last asked about */
    uint32_t  stamp;          /* the simulation step `current` was taken on */
    uint32_t  gap;            /* how many steps back `previous` was taken, never zero */
    bool      have_previous;
    float     previous[3];
    float     current[3];
} track_entry_t;

static track_entry_t track_table[OBJECT_TRACK_SLOTS];
static uint32_t      track_frame;

/* The object pointer with its high bits folded down. Allocations are aligned, so the bottom bits
 * of a pointer carry almost no information; using the value raw would pile every object into a
 * fraction of the table and turn the probe below into a walk. */
static size_t slot_for(uintptr_t key)
{
    uint32_t mixed = (uint32_t)key;

    mixed ^= mixed >> 4;
    mixed ^= mixed >> 12;
    return (size_t)(mixed % OBJECT_TRACK_SLOTS);
}

static bool finite3(const float *v)
{
    return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

static bool entry_is_stale(const track_entry_t *entry)
{
    /* Unsigned, so a frame counter that has wrapped still gives a small difference for a recent
     * entry rather than an enormous one. */
    return (uint32_t)(track_frame - entry->frame) > OBJECT_TRACK_STALE_FRAMES;
}

void object_track_reset(void)
{
    memset(track_table, 0, sizeof track_table);
    track_frame = 0;
}

void object_track_frame(void)
{
    ++track_frame;
}

bool object_track_sample(uintptr_t key, uint32_t stamp, const float *position,
                         float *out_previous, uint32_t *out_gap)
{
    size_t         start;
    size_t         probe;
    track_entry_t *entry = NULL;

    if (key == 0 || position == NULL || out_previous == NULL || out_gap == NULL) {
        return false;
    }

    /* Linear probing from the object's own slot, stopping at the first slot never used: a key that
     * had been stored would have been found before reaching one. */
    start = slot_for(key);
    for (probe = 0; probe < OBJECT_TRACK_SLOTS; ++probe) {
        track_entry_t *candidate = &track_table[(start + probe) % OBJECT_TRACK_SLOTS];

        if (candidate->key == key || candidate->key == 0) {
            entry = candidate;
            break;
        }
        if (entry == NULL && entry_is_stale(candidate)) {
            /* Remembered but not taken yet. A slot further along may still hold this very object,
             * and claiming this one first would leave the same object in the table twice. */
            entry = candidate;
        }
    }

    if (entry == NULL) {
        return false;       /* every slot holds a live object; the engine's own pair still works */
    }

    if (entry->key != key) {
        entry->key = key;
        entry->stamp = stamp;
        entry->gap = 1u;
        entry->have_previous = false;
        memcpy(entry->current, position, sizeof entry->current);
    } else if (entry->stamp != stamp) {
        /* A new step, so wherever it was is now where it was a step ago. This runs whether or
         * not the object moved, which is the point: a stationary object settles to previous
         * equals current and is drawn still, instead of swinging between where it last walked
         * and where it stands. */
        /* Unsigned, so a step counter that has wrapped still gives the true distance. A frame
         * slow enough to span several steps lands here with a gap above one, which is the whole
         * reason the gap is kept: the two samples are then further apart than the blend's own
         * weight assumes. */
        entry->gap = stamp - entry->stamp;
        memcpy(entry->previous, entry->current, sizeof entry->previous);
        memcpy(entry->current, position, sizeof entry->current);
        entry->stamp = stamp;
        entry->have_previous = true;
    }
    entry->frame = track_frame;

    if (!entry->have_previous) {
        return false;
    }
    memcpy(out_previous, entry->previous, sizeof entry->previous);
    /* The gap of the step that last moved the record forward, held for every frame inside that
     * step: the weight has to stay the same for all of them or the object jumps between frames
     * that are meant to divide one step evenly. */
    *out_gap = entry->gap;
    return true;
}

float object_track_weight(float alpha, uint32_t gap)
{
    if (gap <= 1u) {
        return alpha;            /* the engine's own weight, and the case at every usable rate */
    }
    if (!isfinite(alpha)) {
        /* The blend refuses this below and draws the object where it is. Inventing a number here
         * would hide that from the one place set up to catch it. */
        return alpha;
    }
    return (alpha + (float)(gap - 1u)) / (float)gap;
}

bool object_track_blend(const float *previous, const float *current, float weight, float limit,
                        float *out_position)
{
    float dx;
    float dy;
    float dz;

    if (previous == NULL || current == NULL || out_position == NULL) {
        return false;
    }

    /* The limit below is a distance test, and every comparison against a value that is not a
     * number is false, so a NaN would sail through the guard that exists to catch a bad one and
     * be drawn. The finiteness test therefore comes FIRST and is written as a NOT, so that the
     * awkward values fail it rather than pass it.
     *
     * The answer is the current position, as the engine drew before any of this existed. A
 * current position that is itself not a number is passed through rather than
     * replaced with something invented: this decides what to draw between two numbers the
     * simulation produced, and it is not the place to paper over one of them being broken. */
    if (!finite3(previous) || !finite3(current) || !isfinite(weight)) {
        memcpy(out_position, current, 3u * sizeof(float));
        return false;
    }

    dx = current[0] - previous[0];
    dy = current[1] - previous[1];
    dz = current[2] - previous[2];

    if (limit > 0.0f && (dx * dx + dy * dy + dz * dz) > (limit * limit)) {
        memcpy(out_position, current, 3u * sizeof(float));
        return false;
    }

    out_position[0] = previous[0] + dx * weight;
    out_position[1] = previous[1] + dy * weight;
    out_position[2] = previous[2] + dz * weight;
    return true;
}
