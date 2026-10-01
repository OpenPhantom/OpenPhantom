/* mp_scene_note.c: the scene note's bytes. See the header for the layout and why it repeats. */
#include "mp_scene_note.h"

#include "mp_wire.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(MP_SCENE_NOTE_HEADER_BYTES == 1u + 2u + 1u + 1u + 1u + 1u + 1u + 16u + 2u + 1u,
               "the header is the fields the layout names, one byte for the tag in front");
_Static_assert(MP_SCENE_NOTE_SEAT_BYTES == 1u + 1u + 16u, "a seat is its slot, flags and four f32");
/* The age follows the tag, serial, generation, phase, what, trigger, warp serial and the anchor's
 * sixteen, and it is a u16. */
_Static_assert(MP_SCENE_NOTE_AGE_AT == 1u + 2u + 1u + 1u + 1u + 1u + 1u + 16u,
               "the age follows the anchor");
_Static_assert(MP_SCENE_NOTE_AGE_BYTES == sizeof(uint16_t), "the age is sixteen bits");

static uint32_t bits_of(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static float float_of(uint32_t bits)
{
    float value;

    memcpy(&value, &bits, sizeof value);
    return value;
}

static bool finite4(const float *values)
{
    return isfinite(values[0]) && isfinite(values[1]) && isfinite(values[2]) &&
           isfinite(values[3]);
}

/* The rules both directions apply, so the encoder cannot write what the decoder refuses. */
static bool sound(const mp_scene_note_t *note)
{
    float   anchor[4];
    uint8_t i;
    uint8_t j;

    if (note->serial == 0u || note->phase > (uint8_t)MP_SCENE_PHASE_OVER ||
        ((uint32_t)note->what & ~(uint32_t)MP_SCENE_WHAT_ALL) != 0u ||
        (note->trigger_slot > MP_SCENE_SLOT_MAX &&
         note->trigger_slot != MP_SCENE_TRIGGER_UNKNOWN) ||
        note->seats > MP_SCENE_NOTE_MAX_SEATS) {
        return false;
    }
    memcpy(anchor, note->anchor, sizeof note->anchor);
    anchor[3] = note->heading;
    if (!finite4(anchor)) {
        return false;
    }
    for (i = 0u; i < note->seats; ++i) {
        const mp_scene_seat_t *seat = &note->seat[i];
        float                  place[4];

        memcpy(place, seat->position, sizeof seat->position);
        place[3] = seat->heading;
        if (seat->slot > MP_SCENE_SLOT_MAX ||
            ((uint32_t)seat->flags & ~(uint32_t)MP_SCENE_SEAT_F_ALL) != 0u ||
            !finite4(place)) {
            return false;
        }
        for (j = 0u; j < i; ++j) {
            if (note->seat[j].slot == seat->slot) {
                return false;
            }
        }
    }
    return true;
}

size_t mp_scene_note_bytes(const mp_scene_note_t *note)
{
    if (note == NULL || !sound(note)) {
        return 0u;
    }
    return MP_SCENE_NOTE_HEADER_BYTES + (size_t)note->seats * MP_SCENE_NOTE_SEAT_BYTES;
}

size_t mp_scene_note_encode(const mp_scene_note_t *note, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t w;
    size_t           bytes = mp_scene_note_bytes(note);
    uint8_t          i;

    if (bytes == 0u || buffer == NULL || capacity < bytes) {
        return 0u;
    }
    mp_wire_writer_init(&w, buffer, capacity);
    (void)mp_wire_put_u8(&w, (uint8_t)MP_SCENE_NOTE_TAG);
    (void)mp_wire_put_u16(&w, note->serial);
    (void)mp_wire_put_u8(&w, note->generation);
    (void)mp_wire_put_u8(&w, note->phase);
    (void)mp_wire_put_u8(&w, note->what);
    (void)mp_wire_put_u8(&w, note->trigger_slot);
    (void)mp_wire_put_u8(&w, note->warp_serial);
    for (i = 0u; i < 3u; ++i) {
        (void)mp_wire_put_u32(&w, bits_of(note->anchor[i]));
    }
    (void)mp_wire_put_u32(&w, bits_of(note->heading));
    (void)mp_wire_put_u16(&w, note->age_ms);
    (void)mp_wire_put_u8(&w, note->seats);
    for (i = 0u; i < note->seats; ++i) {
        const mp_scene_seat_t *seat = &note->seat[i];

        (void)mp_wire_put_u8(&w, seat->slot);
        (void)mp_wire_put_u8(&w, seat->flags);
        (void)mp_wire_put_u32(&w, bits_of(seat->position[0]));
        (void)mp_wire_put_u32(&w, bits_of(seat->position[1]));
        (void)mp_wire_put_u32(&w, bits_of(seat->position[2]));
        (void)mp_wire_put_u32(&w, bits_of(seat->heading));
    }
    return (w.overflowed || w.at != bytes) ? 0u : bytes;
}

bool mp_scene_note_is_note(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes >= 1u && buffer[0] == (uint8_t)MP_SCENE_NOTE_TAG;
}

static float get_float(mp_wire_reader_t *r)
{
    uint32_t bits = 0u;

    (void)mp_wire_get_u32(r, &bits);
    return float_of(bits);
}

bool mp_scene_note_decode(const uint8_t *buffer, size_t bytes, mp_scene_note_t *out)
{
    mp_wire_reader_t r;
    mp_scene_note_t  note;
    uint8_t          tag = 0u;
    uint8_t          i;

    if (out == NULL || !mp_scene_note_is_note(buffer, bytes) ||
        bytes < MP_SCENE_NOTE_HEADER_BYTES) {
        return false;
    }
    memset(&note, 0, sizeof note);
    mp_wire_reader_init(&r, buffer, bytes);
    (void)mp_wire_get_u8(&r, &tag);
    (void)mp_wire_get_u16(&r, &note.serial);
    (void)mp_wire_get_u8(&r, &note.generation);
    (void)mp_wire_get_u8(&r, &note.phase);
    (void)mp_wire_get_u8(&r, &note.what);
    (void)mp_wire_get_u8(&r, &note.trigger_slot);
    (void)mp_wire_get_u8(&r, &note.warp_serial);
    for (i = 0u; i < 3u; ++i) {
        note.anchor[i] = get_float(&r);
    }
    note.heading = get_float(&r);
    (void)mp_wire_get_u16(&r, &note.age_ms);
    (void)mp_wire_get_u8(&r, &note.seats);
    /* The length is the seats' own count and nothing else: a byte short or long is a torn note. */
    if (note.seats > MP_SCENE_NOTE_MAX_SEATS ||
        bytes != MP_SCENE_NOTE_HEADER_BYTES + (size_t)note.seats * MP_SCENE_NOTE_SEAT_BYTES) {
        return false;
    }
    for (i = 0u; i < note.seats; ++i) {
        mp_scene_seat_t *seat = &note.seat[i];

        (void)mp_wire_get_u8(&r, &seat->slot);
        (void)mp_wire_get_u8(&r, &seat->flags);
        seat->position[0] = get_float(&r);
        seat->position[1] = get_float(&r);
        seat->position[2] = get_float(&r);
        seat->heading     = get_float(&r);
    }
    if (r.overran || r.at != bytes || !sound(&note)) {
        return false;
    }
    *out = note;
    return true;
}

bool mp_scene_note_same(const uint8_t *a, size_t a_bytes, const uint8_t *b, size_t b_bytes)
{
    if (a == NULL || b == NULL || a_bytes != b_bytes || a_bytes < MP_SCENE_NOTE_HEADER_BYTES) {
        return false;
    }
    return memcmp(a, b, MP_SCENE_NOTE_AGE_AT) == 0 &&
           memcmp(a + MP_SCENE_NOTE_AGE_AT + MP_SCENE_NOTE_AGE_BYTES,
                  b + MP_SCENE_NOTE_AGE_AT + MP_SCENE_NOTE_AGE_BYTES,
                  a_bytes - MP_SCENE_NOTE_AGE_AT - MP_SCENE_NOTE_AGE_BYTES) == 0;
}

uint16_t mp_scene_note_age(uint32_t ms)
{
    return ms > 0xFFFFu ? (uint16_t)0xFFFFu : (uint16_t)ms;
}

const mp_scene_seat_t *mp_scene_note_seat_of(const mp_scene_note_t *note, uint8_t slot)
{
    uint8_t i;

    if (note == NULL) {
        return NULL;
    }
    for (i = 0u; i < note->seats && i < MP_SCENE_NOTE_MAX_SEATS; ++i) {
        if (note->seat[i].slot == slot) {
            return &note->seat[i];
        }
    }
    return NULL;
}

bool mp_scene_serial_after(uint16_t a, uint16_t b)
{
    return (int16_t)(uint16_t)(a - b) > 0;
}
