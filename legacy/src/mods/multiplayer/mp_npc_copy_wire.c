/* mp_npc_copy_wire.c: the NPC copies' two messages and their description. See the header. */
#include "mp_npc_copy_wire.h"

#include "mp_channel.h"
#include "mp_enemy_wire.h"
#include "mp_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

_Static_assert(MP_WIRE_KEY_COPY_BASE == NPC_SPAWN_KEY_FIRST,
               "the wire and the note name a copy by the same key");
_Static_assert(MP_WIRE_COPY_MAX == NPC_SPAWN_COPIES_MAX, "and bound the copies alike");
_Static_assert(MP_NPC_COPY_ENTRY_BYTES <= MP_CHANNEL_EAGER_BYTES &&
                   MP_NPC_COPY_WISH_BYTES <= MP_CHANNEL_EAGER_BYTES,
               "both messages ride every packet until they are acknowledged");

static bool bytes_are_zero(const uint8_t *bytes, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i) {
        if (bytes[i] != 0u) {
            return false;
        }
    }
    return true;
}

bool mp_npc_copy_desc_put(const npc_spawn_note_desc_t *desc, uint8_t *out)
{
    mp_wire_writer_t w;
    uint32_t         position[3];
    size_t           i;

    if (desc == NULL || out == NULL || !npc_spawn_note_desc_is_sound(desc)) {
        return false;
    }
    for (i = 0; i < 3u; ++i) {
        if (!mp_enemy_wire_put_position(desc->position[i], &position[i])) {
            return false;
        }
    }
    mp_wire_writer_init(&w, out, MP_NPC_COPY_DESC_BYTES);
    (void)mp_wire_put_u8(&w, desc->source);
    (void)mp_wire_put_u8(&w, desc->behaviour);
    (void)mp_wire_put_u8(&w, desc->flags);
    for (i = 0; i < 3u; ++i) {
        (void)mp_wire_put_u16(&w, (uint16_t)position[i]);
    }
    (void)mp_wire_put_angle(&w, desc->facing);
    for (i = 0; i < NPC_SPAWN_FILE_MAX; ++i) {
        (void)mp_wire_put_u8(&w, (uint8_t)desc->file[i]);
    }
    return !w.overflowed && w.at == MP_NPC_COPY_DESC_BYTES;
}

bool mp_npc_copy_desc_get(const uint8_t *in, npc_spawn_note_desc_t *desc)
{
    mp_wire_reader_t      r;
    npc_spawn_note_desc_t got;
    size_t                i;
    bool                  ok;

    if (in == NULL || desc == NULL) {
        return false;
    }
    memset(&got, 0, sizeof got);
    mp_wire_reader_init(&r, in, MP_NPC_COPY_DESC_BYTES);
    ok = mp_wire_get_u8(&r, &got.source) && mp_wire_get_u8(&r, &got.behaviour) &&
         mp_wire_get_u8(&r, &got.flags);
    for (i = 0; ok && i < 3u; ++i) {
        uint16_t wire = 0;

        ok = mp_wire_get_u16(&r, &wire);
        got.position[i] = mp_enemy_wire_get_position(wire);
    }
    ok = ok && mp_wire_get_angle(&r, &got.facing);
    for (i = 0; ok && i < NPC_SPAWN_FILE_MAX; ++i) {
        uint8_t c = 0;

        ok = mp_wire_get_u8(&r, &c);
        got.file[i] = (char)c;
    }
    if (!ok || !npc_spawn_note_desc_is_sound(&got)) {
        return false;
    }
    *desc = got;
    return true;
}

bool mp_npc_copy_desc_round(npc_spawn_note_desc_t *desc)
{
    uint8_t               bytes[MP_NPC_COPY_DESC_BYTES];
    npc_spawn_note_desc_t rounded;

    if (desc == NULL || !mp_npc_copy_desc_put(desc, bytes) ||
        !mp_npc_copy_desc_get(bytes, &rounded)) {
        return false;
    }
    *desc = rounded;
    return true;
}

/* A description's bytes for a form: a sound one put, or 23 zero bytes where the form carries none
 * and the description given is all zero. */
static bool put_desc_for(bool carries, const npc_spawn_note_desc_t *desc, uint8_t *out)
{
    static const npc_spawn_note_desc_t zero = {0};

    if (carries) {
        return mp_npc_copy_desc_put(desc, out);
    }
    memset(out, 0, MP_NPC_COPY_DESC_BYTES);
    return memcmp(desc, &zero, sizeof zero) == 0;
}

static bool get_desc_for(bool carries, const uint8_t *in, npc_spawn_note_desc_t *desc)
{
    if (carries) {
        return mp_npc_copy_desc_get(in, desc);
    }
    memset(desc, 0, sizeof *desc);
    return bytes_are_zero(in, MP_NPC_COPY_DESC_BYTES);
}

static bool wish_kind_travels(uint8_t kind)
{
    return kind == NPC_SPAWN_WISH_SPAWN || kind == NPC_SPAWN_WISH_REMOVE_OWN;
}

size_t mp_npc_copy_wish_encode(const mp_npc_copy_wish_t *wish, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t w;
    uint8_t          desc[MP_NPC_COPY_DESC_BYTES];
    size_t           i;

    if (wish == NULL || buffer == NULL || capacity < MP_NPC_COPY_WISH_BYTES ||
        !wish_kind_travels(wish->kind) ||
        !put_desc_for(wish->kind == NPC_SPAWN_WISH_SPAWN, &wish->desc, desc)) {
        return 0u;
    }
    mp_wire_writer_init(&w, buffer, capacity);
    (void)mp_wire_put_u8(&w, (uint8_t)MP_NPC_COPY_WISH_TAG);
    (void)mp_wire_put_u8(&w, wish->kind);
    (void)mp_wire_put_u16(&w, wish->serial);
    (void)mp_wire_put_u16(&w, wish->level);
    (void)mp_wire_put_u8(&w, wish->world);
    for (i = 0; i < sizeof desc; ++i) {
        (void)mp_wire_put_u8(&w, desc[i]);
    }
    return (w.overflowed || w.at != MP_NPC_COPY_WISH_BYTES) ? 0u : w.at;
}

bool mp_npc_copy_wish_is(const uint8_t *note, size_t bytes)
{
    return note != NULL && bytes == MP_NPC_COPY_WISH_BYTES &&
           note[0] == (uint8_t)MP_NPC_COPY_WISH_TAG;
}

bool mp_npc_copy_wish_decode(const uint8_t *note, size_t bytes, mp_npc_copy_wish_t *out)
{
    mp_wire_reader_t   r;
    mp_npc_copy_wish_t got;
    uint8_t            tag = 0;

    if (out == NULL || !mp_npc_copy_wish_is(note, bytes)) {
        return false;
    }
    memset(&got, 0, sizeof got);
    mp_wire_reader_init(&r, note, bytes);
    if (!mp_wire_get_u8(&r, &tag) || !mp_wire_get_u8(&r, &got.kind) ||
        !mp_wire_get_u16(&r, &got.serial) || !mp_wire_get_u16(&r, &got.level) ||
        !mp_wire_get_u8(&r, &got.world) || !wish_kind_travels(got.kind) ||
        !get_desc_for(got.kind == NPC_SPAWN_WISH_SPAWN, note + 7u, &got.desc)) {
        return false;
    }
    *out = got;
    return true;
}

static bool entry_is_sound(const mp_npc_copy_entry_t *entry)
{
    if (entry->owner >= NPC_SPAWN_WORLD_SLOTS) {
        return false;
    }
    if (entry->kind == NPC_SPAWN_GRANT_BUILD) {
        return entry->k < MP_WIRE_COPY_MAX && entry->generation != 0u;
    }
    return entry->kind == NPC_SPAWN_GRANT_REFUSED && entry->reason != 0u;
}

size_t mp_npc_copy_entry_encode(const mp_npc_copy_entry_t *entry, uint8_t *buffer,
                                size_t capacity)
{
    mp_wire_writer_t w;
    uint8_t          desc[MP_NPC_COPY_DESC_BYTES];
    bool             build;
    size_t           i;

    if (entry == NULL || buffer == NULL || capacity < MP_NPC_COPY_ENTRY_BYTES ||
        !entry_is_sound(entry)) {
        return 0u;
    }
    build = entry->kind == NPC_SPAWN_GRANT_BUILD;
    if (!put_desc_for(build, &entry->desc, desc)) {
        return 0u;
    }
    mp_wire_writer_init(&w, buffer, capacity);
    (void)mp_wire_put_u8(&w, (uint8_t)MP_NPC_COPY_ENTRY_TAG);
    (void)mp_wire_put_u8(&w, entry->kind);
    (void)mp_wire_put_u16(&w, build ? entry->k : entry->serial);
    (void)mp_wire_put_u8(&w, build ? entry->generation : entry->reason);
    (void)mp_wire_put_u8(&w, entry->owner);
    (void)mp_wire_put_u16(&w, entry->level);
    (void)mp_wire_put_u8(&w, entry->world);
    for (i = 0; i < sizeof desc; ++i) {
        (void)mp_wire_put_u8(&w, desc[i]);
    }
    return (w.overflowed || w.at != MP_NPC_COPY_ENTRY_BYTES) ? 0u : w.at;
}

bool mp_npc_copy_entry_is(const uint8_t *note, size_t bytes)
{
    return note != NULL && bytes == MP_NPC_COPY_ENTRY_BYTES &&
           note[0] == (uint8_t)MP_NPC_COPY_ENTRY_TAG;
}

bool mp_npc_copy_entry_decode(const uint8_t *note, size_t bytes, mp_npc_copy_entry_t *out)
{
    mp_wire_reader_t    r;
    mp_npc_copy_entry_t got;
    uint8_t             tag   = 0;
    uint16_t            first = 0;
    uint8_t             second = 0;
    bool                build;

    if (out == NULL || !mp_npc_copy_entry_is(note, bytes)) {
        return false;
    }
    memset(&got, 0, sizeof got);
    mp_wire_reader_init(&r, note, bytes);
    if (!mp_wire_get_u8(&r, &tag) || !mp_wire_get_u8(&r, &got.kind) ||
        !mp_wire_get_u16(&r, &first) || !mp_wire_get_u8(&r, &second) ||
        !mp_wire_get_u8(&r, &got.owner) || !mp_wire_get_u16(&r, &got.level) ||
        !mp_wire_get_u8(&r, &got.world)) {
        return false;
    }
    build = got.kind == NPC_SPAWN_GRANT_BUILD;
    if (build) {
        got.k          = first;
        got.generation = second;
    } else {
        got.serial = first;
        got.reason = second;
    }
    if (!entry_is_sound(&got) || !get_desc_for(build, note + 9u, &got.desc)) {
        return false;
    }
    *out = got;
    return true;
}
