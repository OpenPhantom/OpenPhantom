/* mp_hit_wire.c: the two hit messages, encoded and decoded. See the header for why they are not
 * in the engine binding any more. */
#include "mp_hit_wire.h"

#include "mp_events.h"
#include "mp_node_map.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static bool names_an_enemy(uint32_t key)
{
    return mp_wire_key_is_placement(key) || mp_wire_key_is_copy(key);
}

static bool flags_are_known(uint8_t flags)
{
    return ((uint32_t)flags & ~(uint32_t)MP_HIT_FLAGS_KNOWN) == 0u;
}

/* A node id is either one of the engine's own names or the word for none. Anything else is a
 * sender this side does not understand, and a note it sent is refused rather than resolved
 * against whatever that number happens to name here. */
static bool node_is_known(uint8_t node)
{
    return node == MP_NODE_ID_NONE || node < MP_NODE_NAME_COUNT;
}

bool mp_hit_note_is(const uint8_t *bytes, size_t length)
{
    return bytes != NULL && length == MP_HIT_RELAY_BYTES && bytes[0] == (uint8_t)MP_EVENT_HIT;
}

size_t mp_hit_note_encode(const mp_hit_note_t *note, uint8_t *out, size_t capacity)
{
    mp_wire_writer_t w;

    if (note == NULL || out == NULL || capacity < MP_HIT_RELAY_BYTES ||
        !names_an_enemy(note->victim) || !flags_are_known(note->flags)) {
        return 0u;
    }
    mp_wire_writer_init(&w, out, MP_HIT_RELAY_BYTES);
    if (!mp_wire_put_u8(&w, (uint8_t)MP_EVENT_HIT) || !mp_wire_put_u16(&w, note->victim) ||
        !mp_wire_put_u8(&w, note->code) || !mp_wire_put_u8(&w, note->impact) ||
        !mp_wire_put_u8(&w, note->flags) || !mp_wire_put_u8(&w, note->node)) {
        return 0u;
    }
    return MP_HIT_RELAY_BYTES;
}

bool mp_hit_note_decode(const uint8_t *bytes, size_t length, mp_hit_note_t *out)
{
    mp_wire_reader_t r;
    uint8_t          tag = 0;
    mp_hit_note_t    note;

    if (out == NULL || !mp_hit_note_is(bytes, length)) {
        return false;
    }
    mp_wire_reader_init(&r, bytes, length);
    if (!mp_wire_get_u8(&r, &tag) || !mp_wire_get_u16(&r, &note.victim) ||
        !mp_wire_get_u8(&r, &note.code) || !mp_wire_get_u8(&r, &note.impact) ||
        !mp_wire_get_u8(&r, &note.flags) || !mp_wire_get_u8(&r, &note.node)) {
        return false;
    }
    if (!names_an_enemy(note.victim) || !flags_are_known(note.flags) ||
        !node_is_known(note.node)) {
        return false;
    }
    *out = note;
    return true;
}

bool mp_player_hit_is(const uint8_t *bytes, size_t length)
{
    return bytes != NULL && length == MP_PLAYER_HIT_BYTES &&
           bytes[0] == (uint8_t)MP_EVENT_PLAYER_HIT;
}

size_t mp_player_hit_encode(const mp_player_hit_note_t *note, uint8_t *out, size_t capacity)
{
    mp_wire_writer_t w;

    if (note == NULL || out == NULL || capacity < MP_PLAYER_HIT_BYTES ||
        (note->attacker != (uint16_t)MP_PLAYER_HIT_NO_ATTACKER &&
         !names_an_enemy(note->attacker)) ||
        !flags_are_known(note->flags)) {
        return 0u;
    }
    mp_wire_writer_init(&w, out, MP_PLAYER_HIT_BYTES);
    if (!mp_wire_put_u8(&w, (uint8_t)MP_EVENT_PLAYER_HIT) || !mp_wire_put_u8(&w, note->slot) ||
        !mp_wire_put_u16(&w, note->attacker) || !mp_wire_put_u8(&w, note->code) ||
        !mp_wire_put_u8(&w, note->impact) || !mp_wire_put_u8(&w, note->flags) ||
        !mp_wire_put_u8(&w, note->node)) {
        return 0u;
    }
    return MP_PLAYER_HIT_BYTES;
}

bool mp_player_hit_decode(const uint8_t *bytes, size_t length, mp_player_hit_note_t *out)
{
    mp_wire_reader_t     r;
    uint8_t              tag = 0;
    mp_player_hit_note_t note;

    if (out == NULL || !mp_player_hit_is(bytes, length)) {
        return false;
    }
    mp_wire_reader_init(&r, bytes, length);
    if (!mp_wire_get_u8(&r, &tag) || !mp_wire_get_u8(&r, &note.slot) ||
        !mp_wire_get_u16(&r, &note.attacker) || !mp_wire_get_u8(&r, &note.code) ||
        !mp_wire_get_u8(&r, &note.impact) || !mp_wire_get_u8(&r, &note.flags) ||
        !mp_wire_get_u8(&r, &note.node)) {
        return false;
    }
    if ((note.attacker != (uint16_t)MP_PLAYER_HIT_NO_ATTACKER && !names_an_enemy(note.attacker)) ||
        !flags_are_known(note.flags) || !node_is_known(note.node)) {
        return false;
    }
    *out = note;
    return true;
}
