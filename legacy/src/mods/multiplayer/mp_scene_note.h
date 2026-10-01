/* mp_scene_note.h: the host's scene as a note, 0xA4, and the byte layout that carries it.
 *
 * Layer 1, pure. No address and no game in the process: the host's state machine and its binding
 * are in mp_scene_host.c, a client's mirror in mp_scene_client.c.
 *
 * A scene on the host is STATE, not an event: it begins, it gathers, it runs and it is over, and a
 * client that missed a note, loads a level late or comes out of its own menu has to find out where
 * the scene stands from the next note it reads. So the note describes the whole state and the host
 * repeats it: on every change, at most once in four substeps, and once a second while a scene is
 * anything but none, and for two seconds after it is over. A client keeps the newest.
 *
 *   serial        u16  the scene's number, 1 upward, never 0; counted on over the host's worlds,
 *                      not started over at a new one, because the generation tells worlds apart
 *   generation    u8   the host's world generation; a note of another world is refused
 *   phase         u8   0 none, 1 gathering, 2 running, 3 over
 *   what          u8   bit 0 the lock, 1 the bars, 2 the hero as an actor, 3 a warp, 4 the camera
 *   trigger_slot  u8   the world slot of the player the script meant, 0xFF for none known
 *   warp_serial   u8   the host's warps so far, 1 upward, 0 before its first; counted on over its
 *                      worlds like the serial, and a client forgets the one it followed at an exit
 *   anchor        4 f32 where the players are gathered and which way that place faces; for a warp
 *                      the target the host is respawned on
 *   age_ms        u16  since the phase began, saturated rather than wrapped
 *   seats         u8   0 to 3
 *   per seat      u8 slot, u8 flags (bit 0: a warp's fade of one second), 4 f32 place and heading
 *
 * Every note of a scene carries the seats handed out for it, in every phase, not only while it
 * gathers. The channel keeps only the newest copy, so the note that says "gathering" may never
 * arrive, and a client that reads "running" first takes its seat from that one.
 *
 * Twenty seven bytes and eighteen a seat, eighty one at most. A float that is not finite is
 * refused at the encoder, where there is still somebody to tell, and at the decoder as well,
 * because a note off the wire is not this machine's arithmetic.
 */
#ifndef MULTIPLAYER_MP_SCENE_NOTE_H
#define MULTIPLAYER_MP_SCENE_NOTE_H

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MP_SCENE_NOTE_HEADER_BYTES 27u
#define MP_SCENE_NOTE_SEAT_BYTES   18u
#define MP_SCENE_NOTE_MAX_SEATS    3u
#define MP_SCENE_NOTE_MAX_BYTES \
    (MP_SCENE_NOTE_HEADER_BYTES + MP_SCENE_NOTE_MAX_SEATS * MP_SCENE_NOTE_SEAT_BYTES)

/* Where the age sits: the one field two notes of the same scene may differ in and still say the
 * same. Both questions whether two notes say the same read it from here, the host's sender before
 * it repeats (mp_scene_note_same) and the session before it leaves a repeat out of a channel
 * (mp_state_note_classify), so the two cannot come to call different notes equal. */
#define MP_SCENE_NOTE_AGE_AT    24u
#define MP_SCENE_NOTE_AGE_BYTES 2u

/* No player is known to have been meant. */
#define MP_SCENE_TRIGGER_UNKNOWN 0xFFu

/* The highest world slot a trigger or a seat may name: sixteen bodies in a snapshot. */
#define MP_SCENE_SLOT_MAX 15u

typedef enum mp_scene_phase {
    MP_SCENE_PHASE_NONE = 0,
    MP_SCENE_PHASE_GATHERING,
    MP_SCENE_PHASE_RUNNING,
    MP_SCENE_PHASE_OVER
} mp_scene_phase_t;

#define MP_SCENE_WHAT_LOCK   0x01u
#define MP_SCENE_WHAT_BARS   0x02u
#define MP_SCENE_WHAT_HERO   0x04u
#define MP_SCENE_WHAT_WARP   0x08u
/* Bit 4, the camera alone, is known to the format and sent by nobody: a camera alone gathers
 * nobody. A note that carries it is read, not refused. */
#define MP_SCENE_WHAT_ALL    0x1Fu

/* A seat whose player moves under the warp's own fade of a second, as the host's respawn does. */
#define MP_SCENE_SEAT_F_WARP_FADE 0x01u
#define MP_SCENE_SEAT_F_ALL       0x01u

typedef struct mp_scene_seat {
    uint8_t slot;
    uint8_t flags;
    float   position[3];
    float   heading;
} mp_scene_seat_t;

typedef struct mp_scene_note {
    uint16_t        serial;
    uint8_t         generation;
    uint8_t         phase;          /* mp_scene_phase_t */
    uint8_t         what;
    uint8_t         trigger_slot;
    uint8_t         warp_serial;
    float           anchor[3];
    float           heading;
    uint16_t        age_ms;
    uint8_t         seats;
    mp_scene_seat_t seat[MP_SCENE_NOTE_MAX_SEATS];
} mp_scene_note_t;

/* The size a note takes on the wire, 0 for one that could not be encoded at all. */
size_t mp_scene_note_bytes(const mp_scene_note_t *note);

/* 0 when the note is not one this build would send: a phase or a bit it does not know, more seats
 * than there is room for, a slot out of range, two seats for one slot, or a float that is not
 * finite. The buffer is left as it was. */
size_t mp_scene_note_encode(const mp_scene_note_t *note, uint8_t *buffer, size_t capacity);

/* Whether a message is this note by its tag, which is all the drain asks before it hands the bytes
 * on. */
bool mp_scene_note_is_note(const uint8_t *buffer, size_t bytes);

/* Read whole or refused whole, by the same rules the encoder applies. */
bool mp_scene_note_decode(const uint8_t *buffer, size_t bytes, mp_scene_note_t *out);

/* Whether two encoded notes say the same, the age aside: a change is a difference here. */
bool mp_scene_note_same(const uint8_t *a, size_t a_bytes, const uint8_t *b, size_t b_bytes);

/* Milliseconds as the note carries them: saturated at the largest u16, never wrapped. */
uint16_t mp_scene_note_age(uint32_t ms);

/* The seat a note hands the player of `slot`, or NULL. */
const mp_scene_seat_t *mp_scene_note_seat_of(const mp_scene_note_t *note, uint8_t slot);

/* Whether serial `a` came after `b`, across the wrap of sixteen bits. */
bool mp_scene_serial_after(uint16_t a, uint16_t b);

#endif /* MULTIPLAYER_MP_SCENE_NOTE_H */
