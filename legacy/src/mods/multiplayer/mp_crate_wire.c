/* mp_crate_wire.c: the three push block messages. See the header. */
#include "mp_crate_wire.h"

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The whole-or-change byte follows the tag, the tick, the level and the generation. */
_Static_assert(MP_CRATE_NOTE_WHOLE_AT == 1u + 4u + 2u + 4u,
               "whole or a change follows the generation");
_Static_assert(MP_CRATE_NOTE_WHOLE_AT < MP_CRATE_NOTE_HEADER_BYTES, "and it is in the header");

#define PUSH_FLAGS_KNOWN (MP_CRATE_PUSH_FORWARD | MP_CRATE_PUSH_PULL | MP_CRATE_PUSH_LET_GO)

/* ==============================================================================================
 * The rules one field at a time, shared by the encoders and the decoders so that the two refuse
 * the same things.
 * ============================================================================================ */

static bool kind_sound(uint8_t kind)
{
    return kind == MP_CRATE_KIND_BLOCK || kind == MP_CRATE_KIND_SUNK ||
           kind == MP_CRATE_KIND_SUNK_ALT;
}

static bool slot_sound(uint8_t slot)
{
    return slot < MP_CRATE_SLOT_LIMIT || slot == MP_CRATE_NOBODY;
}

static bool push_flags_sound(uint8_t flags)
{
    if ((flags & ~PUSH_FLAGS_KNOWN) != 0u || flags == 0u) {
        return false;
    }
    return (flags & (MP_CRATE_PUSH_FORWARD | MP_CRATE_PUSH_PULL)) !=
           (MP_CRATE_PUSH_FORWARD | MP_CRATE_PUSH_PULL);
}

static bool put_position(mp_wire_writer_t *writer, const float position[3])
{
    return mp_wire_put_position(writer, position[0]) &&
           mp_wire_put_position(writer, position[1]) &&
           mp_wire_put_position(writer, position[2]);
}

static bool get_position(mp_wire_reader_t *reader, float position[3])
{
    return mp_wire_get_position(reader, &position[0]) &&
           mp_wire_get_position(reader, &position[1]) &&
           mp_wire_get_position(reader, &position[2]);
}

/* The four fields every message starts with. */
static bool put_head(mp_wire_writer_t *writer, uint8_t tag, uint32_t tick, uint16_t level,
                     uint32_t generation)
{
    return mp_wire_put_u8(writer, tag) && mp_wire_put_u32(writer, tick) &&
           mp_wire_put_u16(writer, level) && mp_wire_put_u32(writer, generation);
}

static bool get_head(mp_wire_reader_t *reader, uint8_t tag, uint32_t *tick, uint16_t *level,
                     uint32_t *generation)
{
    uint8_t got = 0;

    return mp_wire_get_u8(reader, &got) && got == tag && mp_wire_get_u32(reader, tick) &&
           mp_wire_get_u16(reader, level) && mp_wire_get_u32(reader, generation);
}

/* ==============================================================================================
 * The note.
 * ============================================================================================ */

static bool put_entry(mp_wire_writer_t *writer, const mp_crate_entry_t *entry)
{
    uint8_t flags = (uint8_t)(entry->flags & MP_CRATE_FLAGS_TRAVEL);

    if (!kind_sound(entry->kind) || !slot_sound(entry->pusher) ||
        entry->verdict >= (uint8_t)MP_CRATE_VERDICT_COUNT) {
        return false;
    }
    if (!mp_wire_put_u8(writer, entry->id) || !mp_wire_put_u8(writer, entry->kind) ||
        !mp_wire_put_u8(writer, flags) || !put_position(writer, entry->position) ||
        !mp_wire_put_u8(writer, entry->pusher) || !mp_wire_put_u16(writer, entry->sequence) ||
        !mp_wire_put_u8(writer, entry->verdict)) {
        return false;
    }
    if ((flags & MP_CRATE_FLAG_CARRIED) != 0u &&
        (!mp_wire_put_u8(writer, entry->carrier) || !mp_wire_put_u8(writer, entry->carrier_part) ||
         !put_position(writer, entry->carry_offset))) {
        return false;
    }
    if ((flags & MP_CRATE_FLAG_CARRYING) != 0u && !mp_wire_put_u8(writer, entry->carrying)) {
        return false;
    }
    return true;
}

size_t mp_crate_note_encode(const mp_crate_note_t *note, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t writer;
    size_t           i;

    if (note == NULL || buffer == NULL || note->count > MP_CRATE_MAX) {
        return 0u;
    }
    mp_wire_writer_init(&writer, buffer, capacity);
    if (!put_head(&writer, (uint8_t)MP_CRATE_NOTE_TAG, note->tick, note->level,
                  note->generation) ||
        !mp_wire_put_u8(&writer, note->whole ? (uint8_t)MP_CRATE_NOTE_WHOLE
                                             : (uint8_t)MP_CRATE_NOTE_CHANGE) ||
        !mp_wire_put_u8(&writer, note->count)) {
        return 0u;
    }
    for (i = 0; i < note->count; ++i) {
        if (!put_entry(&writer, &note->entry[i])) {
            return 0u;
        }
    }
    return writer.overflowed ? 0u : writer.at;
}

bool mp_crate_is_note(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes >= MP_CRATE_NOTE_HEADER_BYTES &&
           buffer[0] == (uint8_t)MP_CRATE_NOTE_TAG;
}

/* What one entry takes on the wire: its attachments go with their flag and only then. */
static size_t entry_bytes(uint8_t flags)
{
    return MP_CRATE_ENTRY_BYTES +
           ((flags & MP_CRATE_FLAG_CARRIED) != 0u ? MP_CRATE_CARRIED_BYTES : 0u) +
           ((flags & MP_CRATE_FLAG_CARRYING) != 0u ? MP_CRATE_CARRYING_BYTES : 0u);
}

static bool get_entry(mp_wire_reader_t *reader, mp_crate_entry_t *entry)
{
    size_t from = reader->at;

    if (!mp_wire_get_u8(reader, &entry->id) || !mp_wire_get_u8(reader, &entry->kind) ||
        !mp_wire_get_u8(reader, &entry->flags) || !get_position(reader, entry->position) ||
        !mp_wire_get_u8(reader, &entry->pusher) || !mp_wire_get_u16(reader, &entry->sequence) ||
        !mp_wire_get_u8(reader, &entry->verdict)) {
        return false;
    }
    if ((entry->flags & ~MP_CRATE_FLAGS_TRAVEL) != 0u || !kind_sound(entry->kind) ||
        !slot_sound(entry->pusher) || entry->verdict >= (uint8_t)MP_CRATE_VERDICT_COUNT) {
        return false;
    }
    if ((entry->flags & MP_CRATE_FLAG_CARRIED) != 0u &&
        (!mp_wire_get_u8(reader, &entry->carrier) ||
         !mp_wire_get_u8(reader, &entry->carrier_part) ||
         !get_position(reader, entry->carry_offset))) {
        return false;
    }
    if ((entry->flags & MP_CRATE_FLAG_CARRYING) != 0u &&
        !mp_wire_get_u8(reader, &entry->carrying)) {
        return false;
    }
    /* The fields read have to be the size the header names, or the two have come apart. */
    return reader->at - from == entry_bytes(entry->flags);
}

bool mp_crate_note_decode(const uint8_t *buffer, size_t bytes, mp_crate_note_t *out)
{
    mp_wire_reader_t reader;
    uint8_t          whole = 0;
    size_t           i;

    if (out == NULL || !mp_crate_is_note(buffer, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&reader, buffer, bytes);
    if (!get_head(&reader, (uint8_t)MP_CRATE_NOTE_TAG, &out->tick, &out->level,
                  &out->generation) ||
        !mp_wire_get_u8(&reader, &whole) || !mp_wire_get_u8(&reader, &out->count) ||
        whole > MP_CRATE_NOTE_WHOLE || out->count > MP_CRATE_MAX) {
        return false;
    }
    out->whole = whole == MP_CRATE_NOTE_WHOLE;
    for (i = 0; i < out->count; ++i) {
        if (!get_entry(&reader, &out->entry[i])) {
            return false;
        }
    }
    /* Exactly what the entries need: a note with bytes left over is not the note it says it is. */
    return !reader.overran && reader.at == bytes;
}

/* ==============================================================================================
 * The wish and the fall.
 * ============================================================================================ */

size_t mp_crate_push_encode(const mp_crate_push_t *push, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t writer;

    if (push == NULL || buffer == NULL || !push_flags_sound(push->flags)) {
        return 0u;
    }
    mp_wire_writer_init(&writer, buffer, capacity);
    if (!put_head(&writer, (uint8_t)MP_CRATE_PUSH_TAG, push->tick, push->level,
                  push->generation) ||
        !mp_wire_put_u8(&writer, push->id) || !mp_wire_put_u16(&writer, push->sequence) ||
        !mp_wire_put_u8(&writer, push->flags) || !put_position(&writer, push->target)) {
        return 0u;
    }
    return writer.at == MP_CRATE_PUSH_BYTES ? writer.at : 0u;
}

bool mp_crate_is_push(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes == MP_CRATE_PUSH_BYTES &&
           buffer[0] == (uint8_t)MP_CRATE_PUSH_TAG;
}

bool mp_crate_push_decode(const uint8_t *buffer, size_t bytes, mp_crate_push_t *out)
{
    mp_wire_reader_t reader;

    if (out == NULL || !mp_crate_is_push(buffer, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&reader, buffer, bytes);
    return get_head(&reader, (uint8_t)MP_CRATE_PUSH_TAG, &out->tick, &out->level,
                    &out->generation) &&
           mp_wire_get_u8(&reader, &out->id) && mp_wire_get_u16(&reader, &out->sequence) &&
           mp_wire_get_u8(&reader, &out->flags) && get_position(&reader, out->target) &&
           push_flags_sound(out->flags) && reader.at == bytes;
}

size_t mp_crate_fall_encode(const mp_crate_fall_t *fall, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t writer;

    if (fall == NULL || buffer == NULL ||
        (fall->direction[0] == 0.0f && fall->direction[1] == 0.0f)) {
        return 0u;
    }
    mp_wire_writer_init(&writer, buffer, capacity);
    if (!put_head(&writer, (uint8_t)MP_CRATE_FALL_TAG, fall->tick, fall->level,
                  fall->generation) ||
        !mp_wire_put_u8(&writer, fall->id) || !put_position(&writer, fall->position) ||
        !mp_wire_put_position(&writer, fall->direction[0]) ||
        !mp_wire_put_position(&writer, fall->direction[1])) {
        return 0u;
    }
    return writer.at == MP_CRATE_FALL_BYTES ? writer.at : 0u;
}

bool mp_crate_is_fall(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes == MP_CRATE_FALL_BYTES &&
           buffer[0] == (uint8_t)MP_CRATE_FALL_TAG;
}

bool mp_crate_fall_decode(const uint8_t *buffer, size_t bytes, mp_crate_fall_t *out)
{
    mp_wire_reader_t reader;

    if (out == NULL || !mp_crate_is_fall(buffer, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&reader, buffer, bytes);
    return get_head(&reader, (uint8_t)MP_CRATE_FALL_TAG, &out->tick, &out->level,
                    &out->generation) &&
           mp_wire_get_u8(&reader, &out->id) && get_position(&reader, out->position) &&
           mp_wire_get_position(&reader, &out->direction[0]) &&
           mp_wire_get_position(&reader, &out->direction[1]) &&
           (out->direction[0] != 0.0f || out->direction[1] != 0.0f) && reader.at == bytes;
}
