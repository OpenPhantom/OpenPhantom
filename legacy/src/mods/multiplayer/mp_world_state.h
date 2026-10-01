/* mp_world_state.h: what the host says about the movers that have left their authored position.
 *
 * Layer 1. Everything here is arithmetic over a byte buffer and three decisions about numbers;
 * there is no map, no address and no game in the process, which is what lets the unit tests take
 * every one of them.
 *
 * ==================================== Why a second message ====================================
 *
 * The digest next door is symmetric and it applies nothing: both sides describe their own movers
 * once a second and each holds the other's description against its own, which is how the drift
 * between two free running maps gets measured. That is an instrument.
 *
 * This is not an instrument. It travels one way, from the side that owns the map to the side that
 * does not, and the receiving side is allowed to act on it. A player who joins a running game, or
 * who loads a level while the host comes out of a savegame, has a map in its authored state and a
 * host whose doors have been opened, whose lifts have been ridden and whose buttons have been
 * pressed; nothing in the event stream tells him about any of that, because an event describes a
 * moment and every one of those moments is in the past.
 *
 * ================================= What travels, and what does not =============================
 *
 * Only movers that have LEFT the position the level was authored with. All 528 mover records in
 * the eleven shipped levels are authored with direction zero, and the level reset writes zero into
 * that field for every one of them, so a direction that is not zero is a mover that has been
 * somewhere. The engine's own list membership is the other half: a mover can be standing still and
 * still be away from home, which is what a door that has latched open is.
 *
 * A push block is the one type this cannot judge. It has no timeline at all, its pose is always
 * zero, and its direction field is a pointer store rather than a direction, so the test would
 * report agreement it has not earned. Push blocks are counted and left out.
 *
 * ===================================== The two identities ======================================
 *
 * A mover id is an index into one level's table and means nothing outside it, and a session
 * outlives a level load on purpose. So the note carries the level, as the mover count the world
 * holds, exactly as the digest does and for the same reason. It also carries a GENERATION, the
 * number of bank transitions the sender has seen, because two runs of the same level have the same
 * mover count and a note encoded before a restart must not be applied after one.
 */
#ifndef MULTIPLAYER_MP_WORLD_STATE_H
#define MULTIPLAYER_MP_WORLD_STATE_H

#include "mp_world.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Its own tag, and a header no other message on the reliable channel has. The next free tag when
 * this was written; the block in mp_events.h is where the spent ones are listed. */
#define MP_WORLD_STATE_TAG          0x96u
#define MP_WORLD_STATE_HEADER_BYTES 12u   /* tag, tick, level, generation, count */
#define MP_WORLD_STATE_ENTRY_BYTES  MP_WORLD_DIGEST_ENTRY_BYTES
#define MP_WORLD_STATE_MAX_ENTRIES  MP_WORLD_DIGEST_MAX_ENTRIES
#define MP_WORLD_STATE_MAX_BYTES \
    (MP_WORLD_STATE_HEADER_BYTES + MP_WORLD_STATE_MAX_ENTRIES * MP_WORLD_STATE_ENTRY_BYTES)

/* Substeps between notes. The substep rate is 32 a second, so this is one a second, and it is
 * repeated rather than sent once on a change: a peer that arrives between two of them would
 * otherwise wait for the next thing to move before it learned anything at all. */
#define MP_WORLD_STATE_TICKS 32u

typedef struct mp_world_state_note {
    uint32_t         tick;        /* the sender's substep the description was taken at */
    uint16_t         level;       /* the mover count of the world this describes */
    uint32_t         generation;  /* bank transitions the sender has seen */
    uint8_t          count;
    mp_world_entry_t entry[MP_WORLD_STATE_MAX_ENTRIES];
} mp_world_state_note_t;

/* ==============================================================================================
 * The three decisions. Every one of them is a question about numbers and nothing else.
 * ============================================================================================ */

/* Whether a mover of this type has left the position the level was authored with.
 *
 * Direction zero plus not being on the integrator's list is the authored state of every shipped
 * mover, so the test is the pair. A push block cannot be judged this way and answers false. */
bool mp_world_state_disturbed(uint32_t type, uint32_t active, uint32_t dir);

/* Which movers a DIGEST carries, which is not the same question as which movers a state note
 * carries. The note describes what a joiner has to be put right about, so a mover nobody has
 * touched is not in it. The digest is a measurement, and the one type that drifts without anybody
 * touching it is exactly the one the note leaves out. */
bool mp_world_state_in_digest(uint32_t type, uint32_t active, uint32_t dir);

/* Whether this type runs once and latches: an elevator, a lift, a drawbridge. Three of the eight,
 * and 204 of the 528 movers in the eleven shipped levels. */
bool mp_world_state_is_one_shot(uint32_t type);

/* Whether this is the one of the three that the engine's own savegame restore has an arm for, and
 * so the one that may be handed a zero time base instead of a pulled back pose. */
bool mp_world_state_is_restore_armed(uint32_t type);

/* Where a one-shot may be put, in world units, given what the wire says and what the record has.
 * Short of the end on purpose; the reason is in the .c and it is the reason the whole stage
 * works. `settle_dt` is the step the caller will integrate with. */
float mp_world_state_one_shot_pose(float wire_pose, float length, float speed, float settle_dt);

/* How close two poses must be, as a fraction of the travel, before the two sides are saying the
 * same thing about a mover of this type. `settle_dt` is the step the caller integrates with, and
 * it is here for the same reason it is in the call above: the threshold is built out of the very
 * margin that placement leaves, so the two cannot drift apart. */
float mp_world_state_tolerance(uint32_t type, float length, float speed, float settle_dt);

/* Below this fraction of the travel the two sides are saying the same thing about a DOOR: the
 * pose crosses in one part of 65535, so a thousandth is well above the quantiser and well below
 * anything a player can see. It is the floor of every threshold, not the whole of any: what a
 * given mover earns comes out of mp_world_state_tolerance. */
#define MP_WORLD_STATE_SAME 0.001f

/* The comparison, against the threshold the caller names. There is no wrapper that picks one for
 * you: which threshold a mover earns depends on its type AND on its own speed and travel, and a
 * default would have been right for doors and silently wrong for the 204 shipped one-shots. */
bool mp_world_state_agrees_within(const mp_world_entry_t *wire, const mp_world_entry_t *local,
                                  float tolerance);

/* Whether a mover of this type and direction is standing still rather than travelling.
 *
 * This is read off the engine's own integrator, arm by arm, and it is narrower than it looks. A
 * door has four directions and only two of them hold the pose still: closed and away, and latched
 * open with the dwell running. The other two drive the pose, one each way. A button has six, and
 * two of the standing ones are dwells that will move again by themselves; only the closed one and
 * the held open one are quiet with nothing pending, and those are the two this answers for.
 *
 * A type this module does not correct answers false, because there is no arm here that was read
 * for it and answering for one that was not read is how a guess becomes a fact. */
bool mp_world_state_at_rest(uint32_t type, uint32_t dir);

/* Whether this module corrects movers of this type at all: a door, a button, and the three
 * one-shots.
 *
 * The one-shots joined on 2026-09-07, and this comment said the opposite until they did. They
 * were being SENT the whole time and thrown away on arrival, so carrying them costs nothing on
 * the wire; what changed is that the applying side stopped discarding 204 of the 528 shipped
 * movers. A joining player used to find the elevator at the bottom while the host stood at the
 * top, which is not a cosmetic difference but a floor that player cannot reach.
 *
 * Still not the free runners: their phase is a debt the phase controller pays off over seconds,
 * and writing their pose would skip every keyframe edge in between. Still not the push blocks:
 * they have no timeline to describe. */
bool mp_world_state_carries(uint32_t type);

/* What a note of this many entries costs, and whether the channel would take it. */
size_t mp_world_state_bytes(size_t count);
bool   mp_world_state_fits(size_t count);

/* ==============================================================================================
 * The codec.
 * ============================================================================================ */

void mp_world_state_note_init(mp_world_state_note_t *note, uint32_t tick, uint16_t level,
                              uint32_t generation);

/* Adds one mover, with the same refusals the digest makes: a type, a direction or an id the wire
 * cannot carry, a pose that is not a number, and a length that is not positive, which is the one
 * the engine's own wrap loop hangs on. */
mp_world_add_t mp_world_state_note_add(mp_world_state_note_t *note, uint32_t id, uint32_t type,
                                       uint32_t dir, uint32_t active, float pose, float length,
                                       float dwell);

size_t mp_world_state_encode(const mp_world_state_note_t *note, uint8_t *buffer, size_t capacity);
bool   mp_world_state_is_note(const uint8_t *buffer, size_t bytes);
bool   mp_world_state_decode(const uint8_t *buffer, size_t bytes, mp_world_state_note_t *out);

/* Whether a note that names this generation may be applied by a side that has already seen that
 * one. It is a SEQUENCE and not a number the two machines share: each counts the transitions it
 * has seen and a peer that joined late has seen fewer, permanently. What has to be refused is a
 * note from BEFORE the newest generation this side has already been told about. */
bool mp_world_state_generation_current(uint32_t theirs, uint32_t newest_seen);

#endif /* MULTIPLAYER_MP_WORLD_STATE_H */
