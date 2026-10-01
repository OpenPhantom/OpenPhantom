/* mp_enemy_wire.h: one enemy as it travels, and the table that describes it.
 *
 * Layer 1, pure logic. No engine, no address, no socket: values arrive already quantised and leave
 * the same way, which is what lets every property below be proven in a test.
 *
 * The field list is the byte census of 2026-09-05 and nothing else. Each row earned its place by a
 * reader or a writer in the engine, and the three that are NOT here earned their absence the same
 * way:
 *
 *   `actualYaw` is a pure copy of `heading` and both of its writers assign it from there, so
 *   sending it would be a second angle carrying the first one's value.
 *
 *   The clip START MODE does not travel. Every site in the NPC code passes a constant 4 and only
 *   the spawner passes 2, so the receiver knows it without being told.
 *
 *   The ground block, the matrix and the node spheres are recomputed from position and angles by
 *   the engine's own pose pass, so they are derivations rather than state.
 *
 * ============================== Why a change mask and not a struct ============================
 *
 * The measured peak is 37 actors alive at once and the packet budget is 1187 bytes. A fixed record
 * of the full set would not fit; a record that carries only what moved does, because most of an
 * enemy stands still most of the time. The mask belongs to `mp_fieldset`, and this file is that
 * module's first customer.
 *
 * ================================ Presence is not the same as change ==========================
 *
 * The mask says a field CHANGED. Whether an optional part of an enemy EXISTS at all is a different
 * question and is answered by bits inside the state word: the overlay channel, the flyer pose, the
 * playhead, the hidden nodes and the hidden meshes. A receiver that finds a presence bit clear
 * leaves the matching fields alone; they keep whatever the baseline held, which costs nothing on
 * the wire because an unused field stops changing and its mask bit clears with it.
 *
 * ===================================== State, never an event ==================================
 *
 * Every field here is a state: what the actor is now, which a later record repeats and a lost one
 * heals. What happens to an actor once, an emitter a script starts, a clang, a limb taken off,
 * travels in the world events in front of the records (mp_world_event_rule.h), each with a number
 * of its own. A field has one place, and two of them in one substep lost the first.
 *
 * A receiver does not extrapolate an enemy, it interpolates between the host's records, as Source
 * does. There is no velocity and no turn ramp here for that reason, and none was ever read.
 *
 * ==================================== The rotations are a tail ================================
 *
 * A variable number of node rotations is not a field, because a table with variable length rows is
 * a parser and a parser is what the field set exists to avoid. They are appended after the record
 * and read back by this file's own two calls, and the count rides in a field so the reader knows
 * how many to expect before it starts. They travel every substep and are never deltaed: the node
 * relax at 004360C0 pulls every rotation back towards zero in each pre-tick, so a held rotation is
 * rewritten constantly and there is no such thing as one that stands still. A still fighter with
 * no rotation costs the mask and the rotation count, which is always written, four bytes; one
 * holding two rotations costs fourteen.
 */
#ifndef MULTIPLAYER_MP_ENEMY_WIRE_H
#define MULTIPLAYER_MP_ENEMY_WIRE_H

#include "mp_fieldset.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The fields, in wire order. The enum is the index into the value array the encoder takes, so a
 * row added in the middle moves every caller's index and the compiler says so. */
typedef enum mp_enemy_field {
    MP_ENEMY_F_INDEX = 0,   /* the placement index; one writer in the engine, stable for a life */
    MP_ENEMY_F_GENERATION,  /* which life this is, because a placement respawns, see below */
    MP_ENEMY_F_POS_X,
    MP_ENEMY_F_POS_Y,
    MP_ENEMY_F_POS_Z,
    MP_ENEMY_F_HEADING,
    MP_ENEMY_F_STATE,       /* reaction state, the volatile flags and the presence bits */
    MP_ENEMY_F_HEALTH,      /* biased: the engine's is signed and goes negative */
    MP_ENEMY_F_CLIP,        /* base channel clip ordinal; ordinals are per MODEL, never global */
    MP_ENEMY_F_HEAD,        /* base channel playhead, sixteenths of a frame */
    MP_ENEMY_F_OVERLAY_CLIP,
    MP_ENEMY_F_OVERLAY_HEAD,
    MP_ENEMY_F_PITCH,       /* flyer pose only */
    MP_ENEMY_F_ROLL,        /* flyer pose only */
    MP_ENEMY_F_TWISTS,      /* how many rotations follow the record */
    MP_ENEMY_F_SHIELD,      /* the droideka's shield, a state: mp_enemy_shield_rule.h */
    MP_ENEMY_F_BODY,        /* how the body ends: drawn, solid, shadow, alpha, dissolve; below */
    MP_ENEMY_F_NODES_LO,    /* the hidden nodes of the drawn thing, nodes 0 to 31 */
    MP_ENEMY_F_NODES_HI,    /* and 32 to 63, the engine's own two words (mp_enemy_nodes.h) */
    MP_ENEMY_F_MESHES_LO,   /* the hidden meshes, in the same two words */
    MP_ENEMY_F_MESHES_HI,
    MP_ENEMY_FIELD_COUNT
} mp_enemy_field_t;

/* The body field. Byte 0 says whether the actor has a body, whether it is drawn, whether it
 * collides and whether it casts a shadow; byte 1 is its alpha, byte 2 how far it has dissolved,
 * and byte 3 counts the starts of its base clip. The module that fills the field names the whole
 * layout; these are the two bits the interest rule reads, because a death that lays a body down
 * is a change a watcher must not wait for. */
#define MP_ENEMY_BODY_IS_DRAWN 0x02u
#define MP_ENEMY_BODY_IS_SOLID 0x04u

/* The state word. Five bits of reaction state, five volatile flags that decide what a watcher
 * sees, and the bits that say which optional parts follow.
 *
 * The reaction layer has SEVENTEEN values, not sixteen, which is why the state occupies five bits
 * and not four; a four bit field would fold the seventeenth onto the first and put a dying actor
 * into an idle pose. */
#define MP_ENEMY_STATE_MASK        0x001Fu   /* bits 0..4 */
#define MP_ENEMY_STATE_VALUES      17u

#define MP_ENEMY_FLAG_JUMP_UP      0x0020u   /* the vertical jump ramp has finished */
#define MP_ENEMY_FLAG_JUMP_FWD     0x0040u   /* the horizontal jump impulse is set */
#define MP_ENEMY_FLAG_BLOCKING     0x0080u   /* blocking right now, which a watcher sees */
#define MP_ENEMY_FLAG_IMPULSE      0x0100u   /* took a foreign impulse this tick */
#define MP_ENEMY_FLAG_FORCE_THROWN 0x0200u   /* thrown by a force push */

/* The overlay channel travels because NPC scripts do use it, which was once recorded as unproven:
 * the arm at 0042D9F8 picks a block or a parry out of ten variants on that channel, and 0042DDCE
 * is a second arm. Without it a far enemy blocks with its whole body instead of its arms. */
#define MP_ENEMY_HAS_OVERLAY       0x0400u
#define MP_ENEMY_HAS_FLYER_POSE    0x0800u

/* Whether the playhead field means anything.
 *
 * It exists because a playhead of zero is a real value and an absent playhead is not, and without
 * this bit the two are the same sixteen zero bits. The sender leaves the field at zero when the
 * body has no base track to read, and a receiver that cannot tell the difference reads that as
 * "the animation is at its first frame", pulls the local playhead back to zero, and does it again
 * on every packet. The result is a body pinned to the first frame of its clip, which is neither
 * what the sender said nor anything the sender could have said. */
#define MP_ENEMY_HAS_HEAD          0x2000u

/* Whether the node words and the mesh words mean anything, one bit each, as the engine's own save
 * flags each of its two arrays. Four words of zero are also what a sender writes for an actor it
 * leaves out or a thing it could not read, and a receiver that took those for "nothing hidden"
 * would show every node the host has hidden. A sabre put away is one of those nodes. */
#define MP_ENEMY_HAS_NODES         0x4000u
#define MP_ENEMY_HAS_MESHES        0x8000u

/* The reaction states from the death clip to the corpse, 11 to 14: death, fade, shatter and
 * corpse. An actor in any of them is dead on the machine that owns it, and each has an arm in the
 * engine's enemy tick. What those arms do to the body travels in the body field, so only the ends
 * of the range are named. */
#define MP_ENEMY_STATE_DEATH       11u
#define MP_ENEMY_STATE_CORPSE      14u

/* Health is signed in the engine and goes negative, and a wire field is unsigned, so it travels
 * biased. The bias is larger than any damage one hit can do below zero and the range still covers
 * the 999 hit points the placement census found, which a byte would have turned into 231.
 *
 * There is a living actor with zero hit points: 00432F89 sets health 0 and state 1 together.
 * Anything that reads "dead" out of health at or below zero kills it on the far machine, which is
 * why the state word travels beside the health rather than being derived from it. */
#define MP_ENEMY_HEALTH_BIAS       1024
#define MP_ENEMY_HEALTH_MIN        (-MP_ENEMY_HEALTH_BIAS)
#define MP_ENEMY_HEALTH_MAX        (65535 - MP_ENEMY_HEALTH_BIAS)

/* How many node rotations one enemy may carry. Two is the measured case, waist and neck; four
 * leaves room for a model that turns more without letting a corrupt count run a reader off the
 * end of a packet. */
#define MP_ENEMY_MAX_TWISTS        4u

/* One node rotation: which node, and its two angles. Five bytes on the wire.
 *
 * The node travels as a RAW INDEX into the model's own node table, and that is safe here for a
 * reason that does not hold everywhere: an enemy's asset is authored in the level data, so both
 * machines built this actor from the same file and index seven means the same joint on both. The
 * PLAYER's puppet cannot do the same, because a player may be wearing a swapped character or a
 * swapped model and the two rigs carry between 24 and 48 nodes in different orders; there the
 * node has to travel as a name rather than as a position. */
typedef struct mp_enemy_twist {
    uint8_t  node;    /* index into the model's node table */
    uint16_t pitch;   /* biased into unsigned by the caller, like every other angle here */
    uint16_t yaw;
} mp_enemy_twist_t;

#define MP_ENEMY_TWIST_BYTES 5u

/* One enemy, ready to encode or freshly decoded. The values array is the field set's own shape so
 * that a caller can hold a baseline without this file inventing a second one. */
typedef struct mp_enemy_record {
    uint32_t         value[MP_ENEMY_FIELD_COUNT];
    mp_enemy_twist_t twist[MP_ENEMY_MAX_TWISTS];
} mp_enemy_record_t;

/* The table itself. Walked by the encoder, the decoder, the report and the test. */
const mp_fieldset_t *mp_enemy_wire_set(void);

/* The largest one enemy can be: the field set's own maximum plus a full tail. The budget
 * arithmetic for a packet starts here rather than in a comment. It is 70 bytes: 3 of mask, 47 of
 * fields and 20 of rotations. The events left for their own part and the dead reckoning that was
 * never built went with them, and the body and the four mask words came in; a record whole, with
 * no rotation, is 50 (the unit test asserts all of it). */
size_t mp_enemy_wire_max_bytes(void);

/* Encode one enemy against a baseline. A NULL baseline means AGAINST ZERO, which is what a first
 * record for a newly spawned actor is, and it is deliberately not "write every field".
 *
 * That distinction was measured rather than reasoned. A fighting ground actor with an overlay and
 * two node rotations costs 60 bytes when every field is spelled out, because a whole record writes
 * a flyer pose, a shield, a body and four mask words the actor does not have; against zero the
 * same actor costs 34, because a field whose value is zero is simply absent from the mask.
 *
 * Thirty seven of NEITHER fit one packet: 60 comes to 2220 and 34 to 1258, against 1187. What
 * fits is the STEADY STATE, 24 bytes for an actor that walks and swings, and introducing a level's
 * population is therefore a sweep across packets either way. Making NULL mean the cheap form does
 * not change that; it shortens the sweep by nearly half, and the expensive form is reached only by
 * asking for it by name, below. The unit test asserts all three numbers, so that a field added to
 * the table trips a test rather than a packet.
 *
 * The enemy sender no longer sends this form for a record it cannot delta, it sends the whole one
 * below. A receiver that holds nothing for a key cannot tell a record against zero from a delta
 * against a base it never got, and the second one read against nothing sets every field it leaves
 * out to zero, the life included; so a receiver takes a record read against nothing only when it
 * is whole (mp_enemy_wire_is_whole).
 *
 * The tail follows the record and carries exactly the number of rotations the count field names,
 * which is refused rather than clamped when it exceeds the maximum: a count that does not match
 * the tail is a caller error, and clamping it would send a body with somebody else's neck. */
bool mp_enemy_wire_encode(const mp_enemy_record_t *record, const mp_enemy_record_t *baseline,
                          uint8_t *out, size_t capacity, size_t *bytes);

/* Encode one enemy with EVERY field in the mask, whatever the field holds.
 *
 * This is the expensive form the NULL baseline above was made not to be. A receiver reads a field
 * the mask leaves out from whatever it holds itself, so a sender may only leave a field out when
 * it knows what that is. Against zero is right for a receiver that holds nothing; a receiver that
 * still holds an older description of the same life reads every field that has since fallen to
 * zero as its old value.
 *
 * The enemy sender therefore uses this for every record it cannot delta: while its receiver has
 * confirmed nothing, for a key it opened again after a loss, and for a life it has not described
 * yet. The record reads the same against any baseline or none, and introducing a level costs the
 * longer of the two sweeps the paragraph above measures. */
bool mp_enemy_wire_encode_whole(const mp_enemy_record_t *record, uint8_t *out, size_t capacity,
                                size_t *bytes);

/* Decode one enemy. `baseline` supplies every field the mask says did not change; the tail is
 * always read whole out of the record and never inherited, because a held rotation changes every
 * substep anyway. False on a truncated or malformed record, never a partial read. */
bool mp_enemy_wire_decode(const uint8_t *buffer, size_t available,
                          const mp_enemy_record_t *baseline, mp_enemy_record_t *out,
                          size_t *bytes);

/* Whether an encoded record carries every field in its mask, which is the only form a receiver with
 * nothing to read it against may believe. False for a buffer too short to hold the mask. */
bool mp_enemy_wire_is_whole(const uint8_t *buffer, size_t available);

/* Whether an encoded record names all three position fields in its mask. Read against nothing, a
 * field the mask leaves out is zero, and a position of zero is the bottom of the range, below every
 * shipped level. A receiver used to take a record read against nothing when this held; it now asks
 * mp_enemy_wire_is_whole, and counts with this how many it refused would once have been taken with
 * every other field left at zero. False as well for a buffer too short to hold the mask. */
bool mp_enemy_wire_names_position(const uint8_t *buffer, size_t available);

/* The two questions a caller asks about a decoded record before it acts on one. Kept here rather
 * than at every call site, because a presence bit read by hand is a presence bit read wrongly
 * somewhere. */
bool mp_enemy_wire_has(const mp_enemy_record_t *record, uint16_t presence_bit);
int32_t mp_enemy_wire_health(const mp_enemy_record_t *record);

/* Whether the sender reported this actor in a death state, from the death clip to the corpse.
 * That is the only thing that lays a replica down, because it is the only thing that lays the
 * sender's body down: its enemy tick takes the class and the shadow away in those arms and
 * nowhere else. A health at or below zero is not a death state. A death the script owns keeps
 * the actor active while its clip plays, and three shipped placements are authored with no hit
 * points at all and live for the whole level. */
bool mp_enemy_wire_reports_death(const mp_enemy_record_t *record);

/* Whether the health field holds a health. A field of zero on the wire is not one: it is what a
 * record decoded against nothing holds for a field that never arrived, and it reads as -1024. */
bool mp_enemy_wire_health_known(const mp_enemy_record_t *record);

/* Whether this record is where a death begins, for a report that follows deaths: a death state,
 * or a health that falls to zero or below in a life that was reported above zero before. A life
 * that never had a health above zero has nothing to lose, so its zero is not a death. */
bool mp_enemy_wire_death_begins(const mp_enemy_record_t *record, bool health_was_up);

/* A position on this record, and the one quantisation here that rests on a census rather than on
 * an argument.
 *
 * Every world coordinate in the eleven shipped levels was measured, over 2250 enemy placements and
 * their waypoints: x from 0.5 to 240.0, y from -2.1 to 176.7, z from 11.0 to 97.0. The largest
 * magnitude anywhere is 240 units and the largest span is 240. The z floor was first written as
 * 1.0, and that figure came from a working copy of a level lying beside the eleven, not from one
 * of them; nothing here turns on it, since both are far inside the range, but a number that
 * rests on a census has to be about the right set of files.
 *
 * So a u16 carries a position with room to spare, at a resolution far finer than a body needs, and
 * the two numbers below follow from the measurement rather than from a guess. The bias exists
 * because the census found NEGATIVE ground: a y of -2.1 in the swamp, which an unbiased u16 could
 * not represent at all, and which would have arrived on the far side as a body 512 units away.
 *
 * The range covers -128 to +383.99, which is 143 units of headroom above the highest coordinate
 * any shipped level uses. A custom level beyond that is refused rather than wrapped. */
#define MP_ENEMY_POS_SCALE 128.0f    /* 1/128 of a world unit, about a hundredth of a body */
#define MP_ENEMY_POS_BIAS  128.0f    /* what the measured negative ground needs */
#define MP_ENEMY_POS_MIN   (-MP_ENEMY_POS_BIAS)
#define MP_ENEMY_POS_MAX   (65535.0f / MP_ENEMY_POS_SCALE - MP_ENEMY_POS_BIAS)

/* False when the coordinate is not finite or lies outside that range, and the caller must not send
 * the record: a refused position is a level this build does not describe, and clamping one would
 * put a body against a wall it is not standing at. */
bool     mp_enemy_wire_put_position(float world, uint32_t *out);
float    mp_enemy_wire_get_position(uint32_t wire);

/* The conversion that owns the bias, so a caller never writes it out. Values outside the range are
 * clamped HERE and only here, which is the one place a clamp is right: the alternative is a field
 * that silently wraps and puts an actor at the far side of the level. */
uint32_t mp_enemy_wire_put_health(int32_t health);

#endif /* MULTIPLAYER_MP_ENEMY_WIRE_H */
