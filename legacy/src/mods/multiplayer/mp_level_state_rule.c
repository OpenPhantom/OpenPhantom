/* mp_level_state_rule.c: the level's state as a note, and a client's decisions. See the header. */
#include "mp_level_state_rule.h"

#include "mp_director_rule.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The director's two fog ramps: to pale green and back to black. The two edges are the other
 * half of its fog class. */
#define FOG_RAMP_GREEN 11
#define FOG_RAMP_BACK  12

#define PARTS_KNOWN                                                                               \
    (MP_LEVEL_STATE_PART_EMITTERS | MP_LEVEL_STATE_PART_LIGHTS | MP_LEVEL_STATE_PART_FOG |       \
     MP_LEVEL_STATE_PART_ACTOR_LOOPS | MP_LEVEL_STATE_PART_FOG_VIEWERS |                         \
     MP_LEVEL_STATE_PART_ESCORT | MP_LEVEL_STATE_PART_JOURNAL)

_Static_assert((PARTS_KNOWN & MP_LEVEL_STATE_PART_RESERVED) == 0u,
               "the eighth part bit is reserved, so a note that sets it is not one this build "
               "reads");
_Static_assert(MP_LEVEL_STATE_LOOPS_MAX <= 255u && MP_LEVEL_STATE_FOG_VIEWERS_MAX <= 255u &&
                   MP_LEVEL_STATE_JOURNAL_MAX <= 255u,
               "a part counts its entries in a byte");

void mp_level_state_note_init(mp_level_state_note_t *note, uint32_t tick, uint16_t level,
                              uint32_t generation)
{
    if (note == NULL) {
        return;
    }
    memset(note, 0, sizeof *note);
    note->tick       = tick;
    note->level      = level;
    note->generation = generation;
}

bool mp_level_state_light_on(const mp_level_state_note_t *note, size_t index)
{
    if (note == NULL || index >= note->lights || index >= MP_LEVEL_STATE_MAX_LIGHTS) {
        return false;
    }
    return (note->light[index >> 3] & (uint8_t)(1u << (index & 7u))) != 0u;
}

void mp_level_state_set_light(mp_level_state_note_t *note, size_t index, bool on)
{
    uint8_t bit;

    if (note == NULL || index >= MP_LEVEL_STATE_MAX_LIGHTS) {
        return;
    }
    bit = (uint8_t)(1u << (index & 7u));
    if (on) {
        note->light[index >> 3] |= bit;
    } else {
        note->light[index >> 3] &= (uint8_t)~bit;
    }
}

size_t mp_level_state_bytes(const mp_level_state_note_t *note)
{
    size_t bytes = MP_LEVEL_STATE_HEADER_BYTES;

    if (note == NULL) {
        return 0u;
    }
    if ((note->parts & MP_LEVEL_STATE_PART_EMITTERS) != 0u) {
        bytes += 1u + (2u * (size_t)note->emitters + 7u) / 8u;
    }
    if ((note->parts & MP_LEVEL_STATE_PART_LIGHTS) != 0u) {
        bytes += 2u + ((size_t)note->lights + 7u) / 8u;
    }
    if ((note->parts & MP_LEVEL_STATE_PART_FOG) != 0u) {
        bytes += MP_LEVEL_STATE_FOG_BYTES;
    }
    if ((note->parts & MP_LEVEL_STATE_PART_ACTOR_LOOPS) != 0u) {
        bytes += 1u + (size_t)note->loops * MP_LEVEL_STATE_LOOP_BYTES;
    }
    if ((note->parts & MP_LEVEL_STATE_PART_FOG_VIEWERS) != 0u) {
        bytes += 1u + (size_t)note->fog_viewers * MP_LEVEL_STATE_FOG_VIEWER_BYTES;
    }
    if ((note->parts & MP_LEVEL_STATE_PART_ESCORT) != 0u) {
        bytes += MP_LEVEL_STATE_ESCORT_BYTES;
    }
    if ((note->parts & MP_LEVEL_STATE_PART_JOURNAL) != 0u) {
        bytes += 3u + (size_t)note->journal_count * MP_LEVEL_STATE_JOURNAL_BYTES;
    }
    return bytes;
}

/* A journal entry this build can play: a kind it knows, a number, and for the fog one of the
 * director's fog commands. */
static bool journal_entry_is_sound(const mp_level_journal_entry_t *entry)
{
    if (entry->sequence == 0u) {
        return false;
    }
    switch (entry->kind) {
    case MP_LEVEL_JOURNAL_EMITTER:
    case MP_LEVEL_JOURNAL_LIGHT:
        return entry->a <= 1u;
    case MP_LEVEL_JOURNAL_FOG:
        return mp_level_state_fog_command((int32_t)entry->a);
    case MP_LEVEL_JOURNAL_CRAWL:
        return true;
    case MP_LEVEL_JOURNAL_ESCORT:
        return entry->a <= MP_LEVEL_STATE_ESCORT_HEALTH;
    case MP_LEVEL_JOURNAL_NONE:
    case MP_LEVEL_JOURNAL_KINDS:
    default:
        return false;
    }
}

/* The four parts that came with the second form of the note, which the first form's check below
 * does not know of. */
static bool later_parts_are_sound(const mp_level_state_note_t *note)
{
    size_t i;

    if (note->loops > MP_LEVEL_STATE_LOOPS_MAX ||
        note->fog_viewers > MP_LEVEL_STATE_FOG_VIEWERS_MAX ||
        note->journal_count > MP_LEVEL_STATE_JOURNAL_MAX ||
        note->escort_health > MP_LEVEL_STATE_ESCORT_HEALTH ||
        ((uint32_t)note->escort_flags & ~(uint32_t)MP_LEVEL_STATE_ESCORT_SHOWN) != 0u) {
        return false;
    }
    for (i = 0; i < note->loops; ++i) {
        if (note->loop[i].key >= MP_WIRE_KEY_COUNT) {
            return false;
        }
    }
    for (i = 0; i < note->fog_viewers; ++i) {
        if (note->fog_viewer[i].key >= MP_WIRE_KEY_COUNT ||
            ((uint32_t)note->fog_viewer[i].flags &
             ~(uint32_t)(MP_LEVEL_STATE_FOG_VIEWER_ACTIVE | MP_LEVEL_STATE_FOG_VIEWER_ENDED)) !=
                0u) {
            return false;
        }
    }
    for (i = 0; i < note->journal_count; ++i) {
        if (!journal_entry_is_sound(&note->journal[i])) {
            return false;
        }
    }
    return true;
}

/* The fog's state names no flag this build does not know, and a ramp that still runs has its
 * target and length set. */
static bool fog_is_sound(const mp_level_fog_state_t *fog)
{
    if (((uint32_t)fog->flags & ~(uint32_t)MP_LEVEL_FOG_FLAGS) != 0u) {
        return false;
    }
    return fog->left == 0u || (fog->flags & MP_LEVEL_FOG_RAMP) != 0u;
}

/* What the encoder refuses to write rather than write wrong: a count past what its field or this
 * build's arrays hold, a part this build does not know, and a fog state that says nothing whole. */
static bool note_is_sound(const mp_level_state_note_t *note)
{
    size_t i;

    if (((uint32_t)note->parts & ~(uint32_t)PARTS_KNOWN) != 0u ||
        note->emitters > MP_LEVEL_STATE_MAX_EMITTERS || note->lights > MP_LEVEL_STATE_MAX_LIGHTS) {
        return false;
    }
    for (i = 0; i < note->emitters; ++i) {
        if (note->emitter[i] > (uint8_t)MP_LEVEL_EMITTER_GONE) {
            return false;
        }
    }
    if ((note->parts & MP_LEVEL_STATE_PART_FOG) != 0u && !fog_is_sound(&note->fog)) {
        return false;
    }
    return later_parts_are_sound(note);
}

static void put_fog(mp_wire_writer_t *w, const mp_level_fog_state_t *fog)
{
    (void)mp_wire_put_u8(w, fog->flags);
    (void)mp_wire_put_u32(w, fog->cur);
    (void)mp_wire_put_u32(w, fog->end);
    (void)mp_wire_put_u32(w, fog->target);
    (void)mp_wire_put_u32(w, fog->span);
    (void)mp_wire_put_u16(w, fog->left);
    (void)mp_wire_put_u8(w, fog->colour[0]);
    (void)mp_wire_put_u8(w, fog->colour[1]);
    (void)mp_wire_put_u8(w, fog->colour[2]);
}

static void get_fog(mp_wire_reader_t *r, mp_level_fog_state_t *fog)
{
    (void)mp_wire_get_u8(r, &fog->flags);
    (void)mp_wire_get_u32(r, &fog->cur);
    (void)mp_wire_get_u32(r, &fog->end);
    (void)mp_wire_get_u32(r, &fog->target);
    (void)mp_wire_get_u32(r, &fog->span);
    (void)mp_wire_get_u16(r, &fog->left);
    (void)mp_wire_get_u8(r, &fog->colour[0]);
    (void)mp_wire_get_u8(r, &fog->colour[1]);
    (void)mp_wire_get_u8(r, &fog->colour[2]);
}

static void put_later_parts(mp_wire_writer_t *w, const mp_level_state_note_t *note)
{
    size_t i;
    size_t k;

    if ((note->parts & MP_LEVEL_STATE_PART_ACTOR_LOOPS) != 0u) {
        (void)mp_wire_put_u8(w, note->loops);
        for (i = 0; i < note->loops; ++i) {
            (void)mp_wire_put_u16(w, note->loop[i].key);
            (void)mp_wire_put_u8(w, note->loop[i].life);
            (void)mp_wire_put_u16(w, note->loop[i].call);
        }
    }
    if ((note->parts & MP_LEVEL_STATE_PART_FOG_VIEWERS) != 0u) {
        (void)mp_wire_put_u8(w, note->fog_viewers);
        for (i = 0; i < note->fog_viewers; ++i) {
            (void)mp_wire_put_u16(w, note->fog_viewer[i].key);
            (void)mp_wire_put_u8(w, note->fog_viewer[i].life);
            (void)mp_wire_put_u16(w, note->fog_viewer[i].script);
            (void)mp_wire_put_u8(w, note->fog_viewer[i].flags);
            for (k = 0; k < 3u; ++k) {
                (void)mp_wire_put_u16(w, note->fog_viewer[i].place[k]);
            }
        }
    }
    if ((note->parts & MP_LEVEL_STATE_PART_ESCORT) != 0u) {
        (void)mp_wire_put_u8(w, note->escort_health);
        (void)mp_wire_put_u8(w, note->escort_flags);
    }
    if ((note->parts & MP_LEVEL_STATE_PART_JOURNAL) != 0u) {
        (void)mp_wire_put_u16(w, note->journal_newest);
        (void)mp_wire_put_u8(w, note->journal_count);
        for (i = 0; i < note->journal_count; ++i) {
            (void)mp_wire_put_u16(w, note->journal[i].sequence);
            (void)mp_wire_put_u8(w, note->journal[i].kind);
            (void)mp_wire_put_u8(w, note->journal[i].a);
            (void)mp_wire_put_u16(w, note->journal[i].b);
            (void)mp_wire_put_u32(w, note->journal[i].c);
        }
    }
}

/* The same four, read; false for a count this build's arrays cannot hold. The rest of the checks
 * are the encoder's own, asked of the whole note once it is read. */
static bool get_later_parts(mp_wire_reader_t *r, mp_level_state_note_t *note)
{
    size_t i;
    size_t k;

    if ((note->parts & MP_LEVEL_STATE_PART_ACTOR_LOOPS) != 0u) {
        (void)mp_wire_get_u8(r, &note->loops);
        if (note->loops > MP_LEVEL_STATE_LOOPS_MAX) {
            return false;
        }
        for (i = 0; i < note->loops; ++i) {
            (void)mp_wire_get_u16(r, &note->loop[i].key);
            (void)mp_wire_get_u8(r, &note->loop[i].life);
            (void)mp_wire_get_u16(r, &note->loop[i].call);
        }
    }
    if ((note->parts & MP_LEVEL_STATE_PART_FOG_VIEWERS) != 0u) {
        (void)mp_wire_get_u8(r, &note->fog_viewers);
        if (note->fog_viewers > MP_LEVEL_STATE_FOG_VIEWERS_MAX) {
            return false;
        }
        for (i = 0; i < note->fog_viewers; ++i) {
            (void)mp_wire_get_u16(r, &note->fog_viewer[i].key);
            (void)mp_wire_get_u8(r, &note->fog_viewer[i].life);
            (void)mp_wire_get_u16(r, &note->fog_viewer[i].script);
            (void)mp_wire_get_u8(r, &note->fog_viewer[i].flags);
            for (k = 0; k < 3u; ++k) {
                (void)mp_wire_get_u16(r, &note->fog_viewer[i].place[k]);
            }
        }
    }
    if ((note->parts & MP_LEVEL_STATE_PART_ESCORT) != 0u) {
        (void)mp_wire_get_u8(r, &note->escort_health);
        (void)mp_wire_get_u8(r, &note->escort_flags);
    }
    if ((note->parts & MP_LEVEL_STATE_PART_JOURNAL) != 0u) {
        (void)mp_wire_get_u16(r, &note->journal_newest);
        (void)mp_wire_get_u8(r, &note->journal_count);
        if (note->journal_count > MP_LEVEL_STATE_JOURNAL_MAX) {
            return false;
        }
        for (i = 0; i < note->journal_count; ++i) {
            (void)mp_wire_get_u16(r, &note->journal[i].sequence);
            (void)mp_wire_get_u8(r, &note->journal[i].kind);
            (void)mp_wire_get_u8(r, &note->journal[i].a);
            (void)mp_wire_get_u16(r, &note->journal[i].b);
            (void)mp_wire_get_u32(r, &note->journal[i].c);
        }
    }
    return true;
}

size_t mp_level_state_encode(const mp_level_state_note_t *note, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t w;
    size_t           i;

    if (note == NULL || buffer == NULL || !note_is_sound(note) ||
        capacity < mp_level_state_bytes(note)) {
        return 0u;
    }
    mp_wire_writer_init(&w, buffer, capacity);
    (void)mp_wire_put_u8(&w, (uint8_t)MP_LEVEL_STATE_TAG);
    (void)mp_wire_put_u32(&w, note->tick);
    (void)mp_wire_put_u16(&w, note->level);
    (void)mp_wire_put_u32(&w, note->generation);
    (void)mp_wire_put_u8(&w, note->parts);
    if ((note->parts & MP_LEVEL_STATE_PART_EMITTERS) != 0u) {
        uint8_t packed = 0u;

        (void)mp_wire_put_u8(&w, (uint8_t)note->emitters);
        for (i = 0; i < note->emitters; ++i) {
            packed |= (uint8_t)((note->emitter[i] & 3u) << ((i & 3u) * 2u));
            if ((i & 3u) == 3u || i + 1u == note->emitters) {
                (void)mp_wire_put_u8(&w, packed);
                packed = 0u;
            }
        }
    }
    if ((note->parts & MP_LEVEL_STATE_PART_LIGHTS) != 0u) {
        (void)mp_wire_put_u16(&w, note->lights);
        for (i = 0; i < ((size_t)note->lights + 7u) / 8u; ++i) {
            uint8_t byte = note->light[i];

            /* Bits past the count are nothing, and a reader must not find a difference in them. */
            if (i == (size_t)note->lights / 8u && (note->lights & 7u) != 0u) {
                byte &= (uint8_t)((1u << (note->lights & 7u)) - 1u);
            }
            (void)mp_wire_put_u8(&w, byte);
        }
    }
    if ((note->parts & MP_LEVEL_STATE_PART_FOG) != 0u) {
        put_fog(&w, &note->fog);
    }
    put_later_parts(&w, note);
    return w.overflowed ? 0u : w.at;
}

bool mp_level_state_is_note(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes >= MP_LEVEL_STATE_HEADER_BYTES &&
           buffer[0] == (uint8_t)MP_LEVEL_STATE_TAG;
}

bool mp_level_state_decode(const uint8_t *buffer, size_t bytes, mp_level_state_note_t *out)
{
    mp_level_state_note_t note;
    mp_wire_reader_t      r;
    uint8_t               tag = 0;
    size_t                i;

    if (out == NULL || !mp_level_state_is_note(buffer, bytes)) {
        return false;
    }
    memset(&note, 0, sizeof note);
    mp_wire_reader_init(&r, buffer, bytes);
    (void)mp_wire_get_u8(&r, &tag);
    (void)mp_wire_get_u32(&r, &note.tick);
    (void)mp_wire_get_u16(&r, &note.level);
    (void)mp_wire_get_u32(&r, &note.generation);
    (void)mp_wire_get_u8(&r, &note.parts);
    if (((uint32_t)note.parts & ~(uint32_t)PARTS_KNOWN) != 0u) {
        return false;   /* the reserved bit among them: not a note this build can read */
    }
    if ((note.parts & MP_LEVEL_STATE_PART_EMITTERS) != 0u) {
        uint8_t count = 0;

        (void)mp_wire_get_u8(&r, &count);
        note.emitters = count;
        for (i = 0; i < note.emitters; i += 4u) {
            uint8_t packed = 0;
            size_t  k;

            (void)mp_wire_get_u8(&r, &packed);
            for (k = 0; k < 4u && i + k < note.emitters; ++k) {
                note.emitter[i + k] = (uint8_t)((packed >> (k * 2u)) & 3u);
            }
        }
    }
    if ((note.parts & MP_LEVEL_STATE_PART_LIGHTS) != 0u) {
        (void)mp_wire_get_u16(&r, &note.lights);
        if (note.lights > MP_LEVEL_STATE_MAX_LIGHTS) {
            return false;
        }
        for (i = 0; i < ((size_t)note.lights + 7u) / 8u; ++i) {
            (void)mp_wire_get_u8(&r, &note.light[i]);
        }
    }
    if ((note.parts & MP_LEVEL_STATE_PART_FOG) != 0u) {
        get_fog(&r, &note.fog);
    }
    if (!get_later_parts(&r, &note)) {
        return false;
    }
    /* Read to its last byte and no further, or not at all. */
    if (r.overran || r.at != bytes || !note_is_sound(&note)) {
        return false;
    }
    *out = note;
    return true;
}

bool mp_level_state_same(const uint8_t *a, size_t a_bytes, const uint8_t *b, size_t b_bytes)
{
    if (a == NULL || b == NULL || a_bytes != b_bytes || a_bytes < MP_LEVEL_STATE_HEADER_BYTES) {
        return false;
    }
    /* The tag, then everything behind the tick. */
    return a[0] == b[0] && memcmp(a + 5, b + 5, a_bytes - 5u) == 0;
}

bool mp_level_state_emitter_owned(uint32_t type, const char *name, const char *template_name,
                                  const float *position, const float *place_position)
{
    if (type == 0u || name == NULL || template_name == NULL || position == NULL ||
        place_position == NULL) {
        return false;
    }
    /* The allocator copies the name with no bound into sixteen bytes and the position unchanged,
     * so both compare exactly or the slot is somebody else's. */
    return strncmp(name, template_name, MP_LEVEL_EMITTER_NAME_BYTES) == 0 &&
           memcmp(position, place_position, 3u * sizeof(float)) == 0;
}

mp_level_emitter_t mp_level_state_emitter_seen(int32_t live_index, bool owned, bool disabled)
{
    if (live_index < 0) {
        return MP_LEVEL_EMITTER_NEVER;
    }
    if (!owned) {
        return MP_LEVEL_EMITTER_GONE;
    }
    return disabled ? MP_LEVEL_EMITTER_OFF : MP_LEVEL_EMITTER_ON;
}

mp_level_act_t mp_level_state_emitter_act(mp_level_emitter_t host, mp_level_emitter_t here)
{
    bool want;
    bool seen;

    if (host == MP_LEVEL_EMITTER_GONE || host > MP_LEVEL_EMITTER_GONE) {
        return MP_LEVEL_ACT_NONE;
    }
    want = (host == MP_LEVEL_EMITTER_ON);
    seen = (here == MP_LEVEL_EMITTER_ON);
    if (want == seen) {
        return MP_LEVEL_ACT_NONE;
    }
    if (here == MP_LEVEL_EMITTER_GONE) {
        return MP_LEVEL_ACT_NOT_OWNED;
    }
    return want ? MP_LEVEL_ACT_ON : MP_LEVEL_ACT_OFF;
}

bool mp_level_state_fog_command(int32_t command)
{
    return mp_director_class_of(command) == MP_DIRECTOR_FOG;
}

bool mp_level_state_fog_is_ramp(int32_t command)
{
    return command == FOG_RAMP_GREEN || command == FOG_RAMP_BACK;
}

bool mp_level_state_fog_fits(int32_t a1)
{
    return a1 >= 0 && a1 <= (int32_t)UINT16_MAX;
}

mp_level_send_t mp_level_state_due(bool changed, bool sent_before, uint32_t since_sent,
                                   bool change_before, uint32_t since_change)
{
    if (!sent_before) {
        return MP_LEVEL_SEND_CHANGE;
    }
    if (changed && (!change_before || since_change >= MP_LEVEL_STATE_CHANGE_TICKS)) {
        return MP_LEVEL_SEND_CHANGE;
    }
    if (since_sent >= MP_LEVEL_STATE_REPEAT_TICKS) {
        return MP_LEVEL_SEND_REPEAT;
    }
    return changed ? MP_LEVEL_SEND_HELD : MP_LEVEL_SEND_NONE;
}

mp_level_order_t mp_level_state_order(const mp_level_taken_t *taken, uint16_t here_level,
                                      uint16_t level, uint32_t generation, uint32_t tick,
                                      bool *first)
{
    bool same = false;

    if (first != NULL) {
        *first = false;
    }
    if (level != here_level) {
        return MP_LEVEL_ORDER_OTHER_LEVEL;
    }
    if (taken != NULL && taken->any && generation < taken->newest_generation) {
        return MP_LEVEL_ORDER_OLD_GENERATION;
    }
    same = taken != NULL && taken->any && taken->level == level &&
           taken->generation == generation;
    if (same && (int32_t)(tick - taken->tick) <= 0) {
        return MP_LEVEL_ORDER_OLD_TICK;
    }
    if (first != NULL) {
        *first = !same;
    }
    return MP_LEVEL_ORDER_TAKE;
}
