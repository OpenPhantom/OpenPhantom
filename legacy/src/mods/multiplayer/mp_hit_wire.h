/* mp_hit_wire.h: the two hit messages on the wire, and nothing else.
 *
 * A reported hit goes from a client to the host: that client's player struck an enemy the host
 * owns, and the host performs it. A player hit goes from the host to one client: the host's world
 * struck that client's player, and the client performs it on its own body. Why each carries what
 * it carries is written where the messages are made, in mp_hit_relay.h.
 *
 * Both name the enemy by its KEY (mp_wire.h), two bytes, because a copy an editor spawned is named
 * past 255. Until then they were built by hand inside the engine binding, where no test and no
 * fuzzer could reach them; this file is the pure half, as mp_death.h is for the death.
 *
 * SIZE NOTE: under 100 lines, no seam.
 */
#ifndef MULTIPLAYER_MP_HIT_WIRE_H
#define MULTIPLAYER_MP_HIT_WIRE_H

#include "mp_node_map.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Tag, the victim's key, the contact code, the impact code, the flags. */
#define MP_HIT_RELAY_BYTES 7u

/* Tag, the world slot the hit is for, the attacker's key, the contact code, the impact code and
 * the flags. */
#define MP_PLAYER_HIT_BYTES 8u

/* No replica of the attacker: an explosion, a mover, the level itself. The hit still travels,
 * and lands with no attacker, which the engine accepts on every arm that reads one. It is the
 * wire's key for nobody, so no placement and no copy can ever be read as it. */
#define MP_PLAYER_HIT_NO_ATTACKER 0xFFFFu

/* The flags byte, both messages. */
#define MP_HIT_FLAG_A      0x01u   /* the contact may be blocked */
#define MP_HIT_FLAG_B      0x02u   /* the attacker carried an armed node */
#define MP_HIT_FLAG_ARMED  0x04u   /* and its contact node was set, which two arms test */
#define MP_HIT_FLAGS_KNOWN (MP_HIT_FLAG_A | MP_HIT_FLAG_B | MP_HIT_FLAG_ARMED)

typedef struct mp_hit_note {
    uint16_t victim;    /* the key of the enemy the client's player struck */
    uint8_t  code;      /* the contact code the client saw */
    uint8_t  impact;    /* the impact code, which indexes the damage table */
    uint8_t  flags;     /* MP_HIT_FLAG_* */
    uint8_t  node;      /* the struck node as an engine name id, or MP_NODE_ID_NONE. See
                         * mp_node_map.h: an index into a model's own table would mean a
                         * different joint on a rig built from another file. */
} mp_hit_note_t;

typedef struct mp_player_hit_note {
    uint8_t  node;      /* the struck node, as in the note above */
    uint8_t  slot;      /* the world slot of the player who was struck */
    uint16_t attacker;  /* the key of the enemy that struck, or MP_PLAYER_HIT_NO_ATTACKER */
    uint8_t  code;
    uint8_t  impact;
    uint8_t  flags;
} mp_player_hit_note_t;

/* The recogniser tests the length and the tag together, as every other one on this channel does.
 * The coders refuse a key that names no enemy and a flag this build does not know; an encode
 * answers the bytes written, or 0. */
bool   mp_hit_note_is(const uint8_t *bytes, size_t length);
size_t mp_hit_note_encode(const mp_hit_note_t *note, uint8_t *out, size_t capacity);
bool   mp_hit_note_decode(const uint8_t *bytes, size_t length, mp_hit_note_t *out);

bool   mp_player_hit_is(const uint8_t *bytes, size_t length);
size_t mp_player_hit_encode(const mp_player_hit_note_t *note, uint8_t *out, size_t capacity);
bool   mp_player_hit_decode(const uint8_t *bytes, size_t length, mp_player_hit_note_t *out);

#endif /* MULTIPLAYER_MP_HIT_WIRE_H */
