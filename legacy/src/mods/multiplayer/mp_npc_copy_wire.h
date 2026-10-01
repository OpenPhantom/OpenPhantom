/* mp_npc_copy_wire.h: the two messages of the NPC copies an editor spawns, and the description
 * they carry.
 *
 * A client asks its host for a copy with a wish (0xA1); the host tells everybody of a copy to
 * build, or tells a client its wish is refused, with an entry (0xA2). Both are small enough to
 * ride every packet until they are acknowledged. The description is what the one who asks
 * decides, rounded the way the wire rounds it, so that every machine builds from the same bits.
 *
 * Pure: codecs and nothing else. The rules for when a message is sent are mp_npc_copies.
 */
#ifndef MULTIPLAYER_MP_NPC_COPY_WIRE_H
#define MULTIPLAYER_MP_NPC_COPY_WIRE_H

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_NPC_COPY_WISH_TAG  0xA1u
#define MP_NPC_COPY_ENTRY_TAG 0xA2u

/* source, behaviour, flags, three positions of two bytes, a facing of two, the file's twelve */
#define MP_NPC_COPY_DESC_BYTES  23u
/* tag, kind, serial, level, world, the description */
#define MP_NPC_COPY_WISH_BYTES  30u
/* tag, kind, k or serial, generation or reason, owner or asker, level, world, the description */
#define MP_NPC_COPY_ENTRY_BYTES 32u

/* A wish from a client: to spawn, or to remove every copy its player owns. The other two kinds
 * never travel; they come from the host's own overlay. */
typedef struct mp_npc_copy_wish {
    uint8_t               kind;     /* NPC_SPAWN_WISH_SPAWN or _REMOVE_OWN */
    uint16_t              serial;   /* the low sixteen bits of the wish's serial in the note */
    uint16_t              level;    /* the client's level identity when it sent */
    uint8_t               world;    /* the world generation the client acted on */
    npc_spawn_note_desc_t desc;     /* rounded as the wire rounds it; zero for a removal */
} mp_npc_copy_wish_t;

/* An entry from the host: a copy to build, or a client's wish refused. An owner change is the same
 * build entry again with the new owner. A refusal carries the level and world of the wish it
 * answers, so that the refusal that says "another world" is not dropped for coming from one. */
typedef struct mp_npc_copy_entry {
    uint8_t               kind;         /* NPC_SPAWN_GRANT_BUILD or _REFUSED */
    uint16_t              k;            /* a build: the copy's k, below MP_WIRE_COPY_MAX */
    uint8_t               generation;   /* a build: its life, never 0 */
    uint8_t               owner;        /* a build: the owner's slot; a refusal: the asker's */
    uint16_t              serial;       /* a refusal: the wish's wire serial */
    uint8_t               reason;       /* a refusal: NPC_SPAWN_REFUSED_* */
    uint16_t              level;
    uint8_t               world;
    npc_spawn_note_desc_t desc;         /* a build: rounded; zero for a refusal */
} mp_npc_copy_entry_t;

/* A description in its 23 wire bytes and back. Putting refuses one the contract calls unsound or
 * one standing outside the positions the wire reaches; getting refuses bytes that do not come back
 * as a sound description. */
bool mp_npc_copy_desc_put(const npc_spawn_note_desc_t *desc, uint8_t *out);
bool mp_npc_copy_desc_get(const uint8_t *in, npc_spawn_note_desc_t *desc);

/* Rounds a description the way the wire does, in place, so that the host's own overlay builds from
 * the same bits a client will. Doing it twice changes nothing. False, and the description left
 * alone, for one the wire cannot carry. */
bool mp_npc_copy_desc_round(npc_spawn_note_desc_t *desc);

/* The codecs. Encoding answers the length, 0 for a form the rules below refuse; `is` recognises
 * by length and tag together; decoding refuses every form encoding would. */
size_t mp_npc_copy_wish_encode(const mp_npc_copy_wish_t *wish, uint8_t *buffer, size_t capacity);
bool   mp_npc_copy_wish_is(const uint8_t *note, size_t bytes);
bool   mp_npc_copy_wish_decode(const uint8_t *note, size_t bytes, mp_npc_copy_wish_t *out);
size_t mp_npc_copy_entry_encode(const mp_npc_copy_entry_t *entry, uint8_t *buffer,
                                size_t capacity);
bool   mp_npc_copy_entry_is(const uint8_t *note, size_t bytes);
bool   mp_npc_copy_entry_decode(const uint8_t *note, size_t bytes, mp_npc_copy_entry_t *out);

#endif /* MULTIPLAYER_MP_NPC_COPY_WIRE_H */
