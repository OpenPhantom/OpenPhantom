/* mp_snapshot.c: the delta encoding, and the change detection it rests on.
 *
 * The header is the current tick and the baseline tick. Then one state byte per slot: absent,
 * unchanged from the baseline, or a full body record following. The decoder refuses a packet whose
 * named baseline is not the one it was handed, and refuses a slot that claims to be unchanged when
 * the baseline has no body there, because both mean the two sides disagree about what the baseline
 * was and applying the delta anyway would invent a body.
 */
#include "mp_snapshot.h"

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    SLOT_ABSENT = 0,   /* no body here */
    SLOT_SAME = 1,     /* present and identical to the baseline's body in this slot */
    SLOT_FULL = 2      /* present, and a full record follows */
};

void mp_snapshot_clear(mp_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
}

void mp_snapshot_set_body(mp_snapshot_t *snapshot, size_t slot, const mp_wire_body_t *body)
{
    if (slot >= MP_SNAPSHOT_MAX_BODIES) {
        return;
    }
    snapshot->body[slot] = *body;
    snapshot->present_mask |= (uint16_t)(1u << slot);
}

void mp_snapshot_clear_body(mp_snapshot_t *snapshot, size_t slot)
{
    if (slot >= MP_SNAPSHOT_MAX_BODIES) {
        return;
    }
    memset(&snapshot->body[slot], 0, sizeof(snapshot->body[slot]));
    snapshot->present_mask &= (uint16_t)~(1u << slot);
}

bool mp_snapshot_has_body(const mp_snapshot_t *snapshot, size_t slot)
{
    return slot < MP_SNAPSHOT_MAX_BODIES && (snapshot->present_mask & (1u << slot)) != 0u;
}

/* Field by field rather than memcmp, because the record has padding a memcmp would read and two
 * equal bodies could then compare unequal on the strength of uninitialised bytes. */
static bool anim_equal(const mp_wire_anim_t *a, const mp_wire_anim_t *b)
{
    return a->clip[0] == b->clip[0] && a->clip[1] == b->clip[1] && a->track[0] == b->track[0] &&
           a->track[1] == b->track[1] && a->channel_mask == b->channel_mask;
}

static bool twist_equal(const mp_wire_body_t *a, const mp_wire_body_t *b)
{
    size_t index;

    if (a->twist_count != b->twist_count) {
        return false;
    }
    for (index = 0; index < a->twist_count && index < MP_WIRE_MAX_TWISTS; ++index) {
        if (a->twist[index].node != b->twist[index].node ||
            a->twist[index].pitch != b->twist[index].pitch ||
            a->twist[index].yaw != b->twist[index].yaw) {
            return false;
        }
    }
    return true;
}

static bool body_equal(const mp_wire_body_t *a, const mp_wire_body_t *b)
{
    int i;

    for (i = 0; i < 3; ++i) {
        if (a->position[i] != b->position[i] || a->orientation[i] != b->orientation[i]) {
            return false;
        }
    }
    /* Health is compared like every other field: a body that only lost a hit point is a changed
     * body, and a codec that called it "same" would hold the far side's display at the old value
     * until the body moved. */
    /* The locomotion state is compared like the rest. It changes rarely, because a player
     * walks for seconds at a time, so it costs a delta only when he starts or stops, and
     * that is exactly when the receiver has to know. */
    /* And the world: a body that stands where it stood but in the next world is a changed body,
     * or its receiver would read the new world's first pose as the old one's. */
    return a->alive == b->alive && a->dead == b->dead && a->health == b->health &&
           a->weapon == b->weapon && a->hero == b->hero && a->loco == b->loco &&
           a->world == b->world && anim_equal(&a->anim, &b->anim) && twist_equal(a, b);
}

bool mp_snapshot_encode(const mp_snapshot_t *current, const mp_snapshot_t *baseline,
                        uint8_t *buffer, size_t capacity, size_t *bytes)
{
    mp_wire_writer_t w;
    size_t           slot;

    mp_wire_writer_init(&w, buffer, capacity);
    mp_wire_put_u32(&w, current->tick);
    mp_wire_put_u32(&w, baseline != NULL ? baseline->tick : 0u);

    for (slot = 0; slot < MP_SNAPSHOT_MAX_BODIES; ++slot) {
        bool here      = (current->present_mask & (1u << slot)) != 0u;
        bool was_here  = baseline != NULL && (baseline->present_mask & (1u << slot)) != 0u;

        if (!here) {
            mp_wire_put_u8(&w, SLOT_ABSENT);
        } else if (was_here && body_equal(&current->body[slot], &baseline->body[slot])) {
            /* Rare for a living body: the playheads in the track words move every frame, so the
             * host's stream is in practice always full. The codec does not mind. */
            mp_wire_put_u8(&w, SLOT_SAME);
        } else {
            mp_wire_put_u8(&w, SLOT_FULL);
            mp_wire_put_body(&w, &current->body[slot]);
        }
    }

    if (w.overflowed) {
        return false;
    }
    if (bytes != NULL) {
        *bytes = w.at;
    }
    return true;
}

bool mp_snapshot_baseline_tick(const uint8_t *buffer, size_t bytes, uint32_t *baseline_tick)
{
    mp_wire_reader_t r;
    uint32_t         tick = 0;

    mp_wire_reader_init(&r, buffer, bytes);
    if (!mp_wire_get_u32(&r, &tick) || !mp_wire_get_u32(&r, baseline_tick)) {
        return false;
    }
    return true;
}

bool mp_snapshot_decode(const uint8_t *buffer, size_t bytes, const mp_snapshot_t *baseline,
                        mp_snapshot_t *out)
{
    mp_wire_reader_t r;
    uint32_t         baseline_tick = 0;
    size_t           slot;

    mp_snapshot_clear(out);

    mp_wire_reader_init(&r, buffer, bytes);
    if (!mp_wire_get_u32(&r, &out->tick) || !mp_wire_get_u32(&r, &baseline_tick)) {
        /* The tick was read straight into the output, so a packet cut between the two header
         * words, four to seven bytes long, would otherwise hand back a tick with no bodies,
         * which the caller cannot tell from an empty world at that tick. The hand written
         * refusal tests all cut the packet later than the header and never saw it; the fuzz
         * test that cuts at every length did. */
        mp_snapshot_clear(out);
        return false;
    }

    /* The packet names the baseline it was built against. If the receiver holds a different one,
     * or holds none where the packet expected one, the two disagree and the delta cannot be
     * applied without inventing state. */
    if (baseline_tick != 0u) {
        if (baseline == NULL || baseline->tick != baseline_tick) {
            mp_snapshot_clear(out);
            return false;
        }
    } else if (baseline != NULL && baseline->tick != 0u) {
        /* A full packet applied against a real baseline is fine to accept, but the mismatch is a
         * caller error worth refusing so a wrong baseline never passes silently. */
        mp_snapshot_clear(out);
        return false;
    }

    for (slot = 0; slot < MP_SNAPSHOT_MAX_BODIES; ++slot) {
        uint8_t state = 0;

        if (!mp_wire_get_u8(&r, &state)) {
            mp_snapshot_clear(out);
            return false;
        }
        if (state == SLOT_ABSENT) {
            continue;
        }
        if (state == SLOT_SAME) {
            if (baseline == NULL || (baseline->present_mask & (1u << slot)) == 0u) {
                mp_snapshot_clear(out);
                return false;   /* unchanged from a body the baseline does not have */
            }
            out->body[slot] = baseline->body[slot];
            out->present_mask |= (uint16_t)(1u << slot);
        } else if (state == SLOT_FULL) {
            if (!mp_wire_get_body(&r, &out->body[slot])) {
                mp_snapshot_clear(out);
                return false;
            }
            out->present_mask |= (uint16_t)(1u << slot);
        } else {
            mp_snapshot_clear(out);
            return false;   /* an unknown state byte is a corrupt or foreign packet */
        }
    }

    if (r.overran) {
        mp_snapshot_clear(out);
        return false;
    }
    return true;
}
