/* mp_world_event_rule.c: the world's moments as a codec and as decisions. See the header. */
#include "mp_world_event_rule.h"

#include "mp_range_gate_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The memory has to span every number a host can have in flight inside the window, or an event
 * that rides a second block after a burst of others would be taken twice; and it has to stay under
 * half the circle of sixteen bits, or "after" and "before" stop meaning anything. */
_Static_assert(MP_WORLD_EVENT_MEMORY_BITS >=
                   MP_WORLD_EVENT_POSTS_PER_SUBSTEP * MP_WORLD_EVENT_WINDOW,
               "the client's memory is narrower than what a host can have in flight");
_Static_assert(MP_WORLD_EVENT_MEMORY_BITS < 32768u,
               "the client's memory reaches past half the circle of the numbers");
_Static_assert(MP_WORLD_EVENT_MEMORY_BITS % 8u == 0u, "the memory is whole bytes");
_Static_assert(MP_WORLD_EVENT_KINDS <= MP_WORLD_EVENT_KIND_BITS + 1u,
               "a kind would reach into the flags beside it");
_Static_assert(MP_WORLD_EVENT_PART_MAX <= 255u, "the part counts its events in a byte");
_Static_assert(MP_WORLD_EVENT_WINDOW <= 255u, "an age is a byte");
_Static_assert(MP_WORLD_EVENT_TAIL_MAX == sizeof(float),
               "the longest tail is the blast's pitch, one float");

/* One row per kind: how long its tail is, whether it can be performed at a place with no actor,
 * whether a peer is concerned by earshot rather than by holding the actor, whether it shows on
 * screen, and its name. The clang is seen: the engine hangs sparks and a flash on the blade with
 * its sound. A script's sound is the one kind that is only heard. */
typedef struct kind_row {
    uint8_t     kind;
    uint8_t     tail_bytes;
    bool        at_place;
    bool        heard;
    bool        seen;
    const char *name;
} kind_row_t;

static const kind_row_t KINDS[MP_WORLD_EVENT_KINDS] = {
    { MP_WORLD_EVENT_NONE,           0u, false, false, false, "none"    },
    { MP_WORLD_EVENT_SCRIPT_EMITTER, 0u, false, false, true,  "emitter" },
    { MP_WORLD_EVENT_NPC_CLANG,      0u, false, false, true,  "clang"   },
    { MP_WORLD_EVENT_LIMB_FLY,       0u, false, false, true,  "limb"    },
    { MP_WORLD_EVENT_SCRIPT_SOUND,   0u, true,  true,  false, "sound"   },
    { MP_WORLD_EVENT_EXPLODE_AT,     4u, true,  false, true,  "blast"   },
    { MP_WORLD_EVENT_ZAP_ARCS,       0u, false, false, true,  "zap"     },
};

bool mp_world_event_kind_known(uint8_t kind)
{
    return kind != (uint8_t)MP_WORLD_EVENT_NONE && kind < (uint8_t)MP_WORLD_EVENT_KINDS &&
           KINDS[kind].kind == kind;
}

bool mp_world_event_kind_heard(uint8_t kind)
{
    return mp_world_event_kind_known(kind) && KINDS[kind].heard;
}

bool mp_world_event_kind_seen(uint8_t kind)
{
    return mp_world_event_kind_known(kind) && KINDS[kind].seen;
}

size_t mp_world_event_tail_bytes(uint8_t kind)
{
    return mp_world_event_kind_known(kind) ? KINDS[kind].tail_bytes : 0u;
}

/* Two ranks: seen above heard. A kind this build does not know has none and takes no place. */
static uint32_t rank_of(uint8_t kind)
{
    if (!mp_world_event_kind_known(kind)) {
        return 0u;
    }
    return KINDS[kind].seen ? 2u : 1u;
}

bool mp_world_event_gives_way(uint8_t held, uint8_t coming)
{
    return rank_of(held) != 0u && rank_of(held) < rank_of(coming);
}

mp_world_event_unread_t mp_world_event_unread(bool in_pool, bool life_noted)
{
    if (in_pool) {
        return MP_WORLD_EVENT_PASSED_OVER;
    }
    return life_noted ? MP_WORLD_EVENT_ANCHORED : MP_WORLD_EVENT_UNCLAIMED;
}

const char *mp_world_event_kind_name(uint8_t kind)
{
    return mp_world_event_kind_known(kind) ? KINDS[kind].name : "unknown";
}

bool mp_world_event_concerns(uint8_t kind, bool at_actor, bool interest, bool has_place,
                             const float place[3], bool viewer_placed, const float viewer[3],
                             float radius)
{
    if (!mp_world_event_kind_known(kind)) {
        return false;
    }
    if (at_actor && !KINDS[kind].heard) {
        return interest;
    }
    if (has_place && viewer_placed && place != NULL && viewer != NULL) {
        return mp_range_gate_within(place, viewer, radius + MP_WORLD_EVENT_SOUND_MARGIN);
    }
    if (at_actor) {
        return interest;
    }
    return true;
}

mp_world_event_verdict_t mp_world_event_where(const mp_world_event_t *event, bool replica,
                                              uint32_t age_here)
{
    if (event == NULL || !mp_world_event_kind_known(event->kind)) {
        return MP_WORLD_EVENT_DROP;
    }
    if (event->at_actor && replica) {
        return MP_WORLD_EVENT_AT_REPLICA;
    }
    if (event->has_place && KINDS[event->kind].at_place) {
        return MP_WORLD_EVENT_AT_PLACE;
    }
    if (event->at_actor && age_here < MP_WORLD_EVENT_WINDOW) {
        return MP_WORLD_EVENT_WAIT;
    }
    return MP_WORLD_EVENT_DROP;
}

/* ==============================================================================================
 * The codec, little endian like the rest of the enemy block.
 * ============================================================================================ */

static void put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFu);
    out[1] = (uint8_t)(value >> 8);
}

static uint16_t get_u16(const uint8_t *in)
{
    return (uint16_t)(in[0] | ((uint16_t)in[1] << 8));
}

size_t mp_world_event_bytes(const mp_world_event_t *event)
{
    size_t bytes = MP_WORLD_EVENT_FIXED_BYTES;

    if (event == NULL || !mp_world_event_kind_known(event->kind) || event->sequence == 0u ||
        event->age >= MP_WORLD_EVENT_WINDOW ||
        event->tail_bytes != mp_world_event_tail_bytes(event->kind)) {
        return 0u;
    }
    bytes += event->has_place ? MP_WORLD_EVENT_PLACE_BYTES : 0u;
    bytes += event->at_actor ? MP_WORLD_EVENT_ACTOR_BYTES : 0u;
    bytes += event->tail_bytes;
    return bytes <= MP_WORLD_EVENT_MAX_BYTES ? bytes : 0u;
}

bool mp_world_event_put(const mp_world_event_t *event, uint8_t *out, size_t room, size_t *written)
{
    size_t bytes = mp_world_event_bytes(event);
    size_t at    = 0;
    size_t i;

    if (bytes == 0u || out == NULL || written == NULL || room < bytes) {
        return false;
    }
    put_u16(out, event->sequence);
    out[2] = event->age;
    out[3] = (uint8_t)(event->kind | (event->at_actor ? MP_WORLD_EVENT_AT_ACTOR : 0u) |
                       (event->has_place ? MP_WORLD_EVENT_HAS_PLACE : 0u));
    at = 4u;
    if (event->has_place) {
        for (i = 0; i < 3u; ++i) {
            put_u16(out + at, event->place[i]);
            at += 2u;
        }
    }
    put_u16(out + at, event->a);
    at += 2u;
    if (event->at_actor) {
        put_u16(out + at, event->key);
        out[at + 2u] = event->life;
        at += MP_WORLD_EVENT_ACTOR_BYTES;
    }
    out[at++] = event->tail_bytes;
    memcpy(out + at, event->tail, event->tail_bytes);
    *written = at + event->tail_bytes;
    return true;
}

mp_world_event_read_t mp_world_event_get(const uint8_t *in, size_t available,
                                         mp_world_event_t *out, size_t *read)
{
    mp_world_event_t event;
    size_t           at = 4u;
    size_t           want;
    size_t           i;
    uint8_t          len;

    if (in == NULL || out == NULL || read == NULL || available < MP_WORLD_EVENT_FIXED_BYTES) {
        return MP_WORLD_EVENT_READ_TORN;
    }
    memset(&event, 0, sizeof event);
    event.sequence  = get_u16(in);
    event.age       = in[2];
    event.kind      = (uint8_t)(in[3] & MP_WORLD_EVENT_KIND_BITS);
    event.at_actor  = (in[3] & MP_WORLD_EVENT_AT_ACTOR) != 0u;
    event.has_place = (in[3] & MP_WORLD_EVENT_HAS_PLACE) != 0u;
    want = MP_WORLD_EVENT_FIXED_BYTES + (event.has_place ? MP_WORLD_EVENT_PLACE_BYTES : 0u) +
           (event.at_actor ? MP_WORLD_EVENT_ACTOR_BYTES : 0u);
    if (available < want) {
        return MP_WORLD_EVENT_READ_TORN;
    }
    if (event.has_place) {
        for (i = 0; i < 3u; ++i) {
            event.place[i] = get_u16(in + at);
            at += 2u;
        }
    }
    event.a = get_u16(in + at);
    at += 2u;
    if (event.at_actor) {
        event.key  = get_u16(in + at);
        event.life = in[at + 2u];
        at += MP_WORLD_EVENT_ACTOR_BYTES;
    }
    len = in[at++];
    if (available - at < len || event.sequence == 0u) {
        return MP_WORLD_EVENT_READ_TORN;
    }
    *read = at + len;
    if (!mp_world_event_kind_known(event.kind)) {
        return MP_WORLD_EVENT_READ_SKIPPED;
    }
    /* A kind this build knows keeps the tail it knows of and steps over what a later build added;
     * one shorter than its table is torn, because every byte of it is read. */
    if (len < mp_world_event_tail_bytes(event.kind)) {
        return MP_WORLD_EVENT_READ_TORN;
    }
    event.tail_bytes = (uint8_t)mp_world_event_tail_bytes(event.kind);
    memcpy(event.tail, in + at, event.tail_bytes);
    *out = event;
    return MP_WORLD_EVENT_READ_OK;
}

void mp_world_event_put_head(const mp_world_event_head_t *head, uint8_t *out)
{
    out[0] = head->count;
    put_u16(out + 1u, head->music_state);
    put_u16(out + 3u, head->music_sequence);
}

void mp_world_event_get_head(const uint8_t *in, mp_world_event_head_t *head)
{
    head->count          = in[0];
    head->music_state    = get_u16(in + 1u);
    head->music_sequence = get_u16(in + 3u);
}

/* ==============================================================================================
 * The numbers.
 * ============================================================================================ */

uint16_t mp_world_event_sequence_next(uint16_t sequence)
{
    return sequence == 0xFFFFu ? 1u : (uint16_t)(sequence + 1u);
}

int32_t mp_world_event_sequence_distance(uint16_t a, uint16_t b)
{
    return (int32_t)(int16_t)(uint16_t)(a - b);
}

static bool memory_bit(const mp_world_event_memory_t *memory, uint32_t k)
{
    return (memory->seen[k >> 3] & (uint8_t)(1u << (k & 7u))) != 0u;
}

static void memory_set(mp_world_event_memory_t *memory, uint32_t k, bool on)
{
    if (on) {
        memory->seen[k >> 3] |= (uint8_t)(1u << (k & 7u));
    } else {
        memory->seen[k >> 3] &= (uint8_t)~(1u << (k & 7u));
    }
}

void mp_world_event_memory_reset(mp_world_event_memory_t *memory)
{
    if (memory != NULL) {
        memset(memory, 0, sizeof *memory);
    }
}

void mp_world_event_memory_block(mp_world_event_memory_t *memory, uint32_t tick)
{
    if (memory == NULL) {
        return;
    }
    if (memory->block_known && (uint32_t)(tick - memory->block_tick) >= MP_WORLD_EVENT_WINDOW) {
        memory->any = false;
        memset(memory->seen, 0, sizeof memory->seen);
    }
    memory->block_known = true;
    memory->block_tick  = tick;
}

mp_world_event_novelty_t mp_world_event_memory_note(mp_world_event_memory_t *memory,
                                                    uint16_t sequence)
{
    int32_t  ahead;
    uint32_t k;

    if (memory == NULL) {
        return MP_WORLD_EVENT_FIRST_TIME;
    }
    if (!memory->any) {
        memset(memory->seen, 0, sizeof memory->seen);
        memory->any     = true;
        memory->highest = sequence;
        memory_set(memory, 0u, true);
        return MP_WORLD_EVENT_FIRST_TIME;
    }
    ahead = mp_world_event_sequence_distance(sequence, memory->highest);
    if (ahead > 0) {
        /* Everything remembered moves back by how far the new highest lies ahead. */
        for (k = MP_WORLD_EVENT_MEMORY_BITS; k-- > 0u;) {
            memory_set(memory, k,
                       k >= (uint32_t)ahead ? memory_bit(memory, k - (uint32_t)ahead) : false);
        }
        memory->highest = sequence;
        memory_set(memory, 0u, true);
        return MP_WORLD_EVENT_FIRST_TIME;
    }
    k = (uint32_t)(-ahead);
    if (k >= MP_WORLD_EVENT_MEMORY_BITS) {
        return MP_WORLD_EVENT_PAST_MEMORY;
    }
    if (memory_bit(memory, k)) {
        return MP_WORLD_EVENT_SEEN_AGAIN;
    }
    memory_set(memory, k, true);
    return MP_WORLD_EVENT_FIRST_TIME;
}
