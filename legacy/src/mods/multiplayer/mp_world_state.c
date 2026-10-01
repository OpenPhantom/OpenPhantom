/* mp_world_state.c: the host's description of the movers that are away from home.
 *
 * The header carries why this exists beside the digest and what travels in it. What is here is the
 * codec and the three decisions, and the decisions are the part worth reading twice, because each
 * of them is a direct transcription of one arm of the engine's own integrator and a transcription
 * that reads one arm wrongly is a mover corrected in the middle of its travel.
 */
#include "mp_world_state.h"

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The engine's mover types, by the arms of its own integrator. Only the two this module corrects
 * and the one it must refuse are named. */
#define MOVER_TYPE_DOOR   2u
#define MOVER_TYPE_BUTTON 6u

/* The directions of a door, in the order its arm walks them. Closed and away is where the level
 * begins; opening drives the pose up; latched holds it at the far end while the dwell runs; and
 * closing drives it back down and falls to closed when it reaches zero. */
#define DOOR_DIR_CLOSED  0u
#define DOOR_DIR_OPENING 1u
#define DOOR_DIR_LATCHED 2u
#define DOOR_DIR_CLOSING 3u

/* The three one-shot movers: an elevator, a lift, a drawbridge. They run once, latch at the far
 * end and stay there, and 204 of the 528 movers in the eleven shipped levels are one.
 *
 * They matter because a JOINING player gets none of them. The host is standing on the elevator at
 * the top; the joiner's map has it at the bottom, because the map was authored that way and
 * nothing on this side ever ran it. That is not a cosmetic difference, it is a floor the second
 * player cannot reach. */
#define MOVER_TYPE_ONE_SHOT_LATCH 3u   /* runs to the end, latches, and stops being a mover face */
#define MOVER_TYPE_ONE_SHOT_HOLD  4u   /* runs to the end and keeps its own active flag set */
#define MOVER_TYPE_ONE_SHOT_ARMED 5u   /* the engine's savegame restore has an arm for this one */

/* The direction of a free running mover, which never stops and never needs to travel. */
#define MOVER_TYPE_FREE_RUNNING   0u

/* The directions of a button. Six of them: closed, rising, held with the first dwell running, held
 * with nothing running, falling, and closed with the second dwell running. */
#define BUTTON_DIR_CLOSED    0u
#define BUTTON_DIR_RISING    1u
#define BUTTON_DIR_HELD_BUSY 2u
#define BUTTON_DIR_HELD      3u
#define BUTTON_DIR_FALLING   4u
#define BUTTON_DIR_SHUT_BUSY 5u

bool mp_world_state_is_one_shot(uint32_t type)
{
    return type == MOVER_TYPE_ONE_SHOT_LATCH || type == MOVER_TYPE_ONE_SHOT_HOLD ||
           type == MOVER_TYPE_ONE_SHOT_ARMED;
}

/* The one of the three the engine's OWN savegame restore has a special arm for: it forces the
 * running direction and a zero time base before integrating, and the clamp inside the integrator
 * then produces the pose, the direction and the active flag in one pass. Named as a question
 * rather than as a number so the applier does not have to carry the engine's type codes. */
bool mp_world_state_is_restore_armed(uint32_t type)
{
    return type == MOVER_TYPE_ONE_SHOT_ARMED;
}

/* The predicate this replaced was list membership alone, `active != 0`, and the integrator's own
 * arms say what that misses:
 *
 *     case 3:  if (dir == 2) { *mover = 0; bapmap_disableMoverFaces(mover); return; }
 *     case 5:  if (pose >= length) { pose = length; dir = 2; *mover = 0; }
 *
 * Both clear the membership when the mover finishes and neither puts the pose back, so a type 3
 * or 5 that has run sits at active 0, direction 2, pose at the end, permanently. There are 49 of
 * type 3 and 13 of type 5 across the eleven levels, 62 records a joining client would have had in
 * the wrong place for the rest of the level. The other types keep their membership: a door in
 * direction 2 was put on the list by the opener and only direction 0 takes it off, and a type 4
 * latches and returns before the clearing tail runs, so it keeps active set for good. */
bool mp_world_state_disturbed(uint32_t type, uint32_t active, uint32_t dir)
{
    /* 43 push blocks in the shipped levels, pose always zero, and the direction field is a
     * pointer to the landing floor rather than a direction. */
    if (type >= MP_WORLD_MOVER_TYPES || type == MP_WORLD_TYPE_PUSH_BLOCK) {
        return false;
    }
    /* A FREE RUNNING mover is never disturbed, and asking whether it is active is asking the
     * wrong question: the engine's own level opener sets the flag on every one of them and no arm
     * anywhere ever clears it, so all 59 in the eleven shipped levels answered yes to this from
     * the first frame of every level. They rode every note ever sent, up to 131 bytes a note in
     * BIGCITY, to say that a fan is turning, which it is on both machines anyway, identically,
     * because nobody ever started it. */
    if (type == MOVER_TYPE_FREE_RUNNING) {
        return false;
    }
    /* And a one-shot that has not fired is in the same position for the same reason: the opener
     * sets its active flag too. What says it has fired is the DIRECTION, which starts at zero and
     * is the only field a run moves off it. */
    if (type == MOVER_TYPE_ONE_SHOT_ARMED) {
        return dir != 0u;
    }
    return active != 0u || dir != 0u;
}

/* Read arm by arm out of the integrator at 0x00409170. The door's four:
 *
 *     dir 0:  pose = 0;      active = 0;                 still, and off the list
 *     dir 1:  pose += speed * dt;                        TRAVELLING, upward
 *     dir 2:  pose = length; dwell += dt;                still, pinned at the end
 *     dir 3:  pose -= speed * dt;                        TRAVELLING, downward
 *
 * Direction 3 is the one worth spelling out, because a latched door and a closing door look alike
 * from outside and the first description of it listed 0, 2 and 3 as the standing set.
 * The arm for 3 jumps to the label that reads the pose DOWN by a substep of travel every tick,
 * the same label 2 falls into once the dwell reaches the hold, and a pose written into such a
 * door would fight the local integrator once a substep. The button's six:
 *
 *     dir 0:  pose = 0;      active = 0;                 still, and off the list
 *     dir 1:  pose += speed * dt;                        TRAVELLING, upward
 *     dir 2:  pose = length; dwell += dt; 3 at the hold  still, but a phase that ends by itself
 *     dir 3:  pose = length;                             still, nothing pending
 *     dir 4:  pose -= speed * dt;                        TRAVELLING, downward
 *     dir 5:  pose = 0;      dwell += dt; 0 at the hold  still, but a phase that ends by itself
 *
 * Directions 2 and 5 hold the pose still and are refused anyway: a description of a phase that
 * ends by itself is out of date before it can be applied. */
/* The note's selection, plus the free runners.
 *
 * The two questions were one for four days and it cost the phase controller its whole input. The
 * note's selection grew a rule that free runners are never disturbed, which is true and is the
 * reason they are not worth a note: the engine's level opener sets their active flag and nothing
 * ever clears it, so all fifty nine in the shipped levels rode every note to say that a fan is
 * turning. The digest then inherited that rule, and the controller that exists only for free
 * runners never saw one again: every client log since says `0 debt(s) taken`, where the last run
 * before it had three and nudged seventy four integrations.
 *
 * A free runner has no anchor: nothing starts it, so nothing puts it back in step, and its phase
 * is its own machine's level clock. Measuring it is the only way anybody learns that the two maps
 * have drifted, which is what the digest is for. */
bool mp_world_state_in_digest(uint32_t type, uint32_t active, uint32_t dir)
{
    if (type >= MP_WORLD_MOVER_TYPES || type == MP_WORLD_TYPE_PUSH_BLOCK) {
        return false;
    }
    if (type == MOVER_TYPE_FREE_RUNNING) {
        return true;
    }
    return mp_world_state_disturbed(type, active, dir);
}

bool mp_world_state_at_rest(uint32_t type, uint32_t dir)
{
    if (type == MOVER_TYPE_DOOR) {
        return dir == DOOR_DIR_CLOSED || dir == DOOR_DIR_LATCHED;
    }
    if (type == MOVER_TYPE_BUTTON) {
        return dir == BUTTON_DIR_CLOSED || dir == BUTTON_DIR_HELD;
    }
    /* All three one-shots rest in the same two places, and it is the door's pair: at home, where
     * the level put them, or latched at the far end where a run left them. Direction one is the
     * run itself and is the one state that must never be copied, because a pose taken out of the
     * middle of somebody else's run is a jump under the feet of whoever is riding it. */
    if (mp_world_state_is_one_shot(type)) {
        return dir == DOOR_DIR_CLOSED || dir == DOOR_DIR_LATCHED;
    }
    return false;
}

bool mp_world_state_carries(uint32_t type)
{
    return type == MOVER_TYPE_DOOR || type == MOVER_TYPE_BUTTON ||
           mp_world_state_is_one_shot(type);
}

/* Where a one-shot may be put, in world units along its own travel.
 *
 * It is not put at the end, and that is the whole trick. The engine composes a mover's geometry
 * only on the tail of its integrator, and the integrator turns back in its first lines for a
 * mover that is already latched, so a pose written straight to the far end produces a record
 * that says "at the top" and a lift that is still visibly at the bottom. What is written instead
 * is a pose a hair SHORT of the end, in the running arm, so that the one integration this module
 * runs reaches the tail, composes the geometry, evaluates the keyframes and clamps to the end
 * itself. The mover arrives where the host has it, by the same route the host got there.
 *
 * The margin has to cover the travel of that integration, and it has a floor of one substep of
 * this mover's own speed. The engine's own settling step grows with the level clock, and at an
 * hour of level time it is a thousand times what it is at the start; the floor is what makes the
 * margin big enough at the beginning without making it visible at the end. */
/* The margin, in world units. Both the placement and the threshold are built out of THIS, so
 * that neither can be widened without the other: they were written separately once, and the
 * threshold then covered the margin only while the substep floor was the larger of the two, which
 * stops being true after about two hours of level time. Past that the two disagreed permanently
 * and the correction fired again on every note. */
static float one_shot_guard(float length, float speed, float settle_dt)
{
    float guard = speed * settle_dt * 4.0f;

    if (guard < speed / 32.0f) {
        guard = speed / 32.0f;
    }
    if (guard > length) {
        guard = length;   /* a mover slower than its own travel; leave it at home, not behind it */
    }
    return guard;
}

float mp_world_state_one_shot_pose(float wire_pose, float length, float speed, float settle_dt)
{
    float guard;
    float pose;

    if (!(length > 0.0f) || !(speed > 0.0f)) {
        return wire_pose * length;   /* nothing to guard; the caller's fault handling has it */
    }
    guard = one_shot_guard(length, speed, settle_dt);
    pose = wire_pose * length;
    if (pose > length - guard) {
        pose = length - guard;
    }
    if (pose < 0.0f) {
        pose = 0.0f;
    }
    return pose;
}

/* How close is close enough, as a fraction of the travel, for THIS type.
 *
 * A door is a thousandth: it is written to the pose the host has and lands there. A one-shot is
 * left a guard short of the end on purpose, so it is systematically up to one substep of travel
 * away from what the host reports, and on the fastest mover in the shipped levels that is 12.9%
 * of the whole travel. Held to the door's threshold it would disagree forever, and the client
 * would re-apply the same correction every second, firing the last keyframe edges each time. */
float mp_world_state_tolerance(uint32_t type, float length, float speed, float settle_dt)
{
    if (!mp_world_state_is_one_shot(type)) {
        return MP_WORLD_STATE_SAME;
    }
    if (!(length > 0.0f) || !(speed > 0.0f)) {
        return MP_WORLD_STATE_SAME;
    }
    /* THE MARGIN ITSELF, as a fraction of the travel, and never tighter than a door's. Computed
     * from the same function that places the mover, so a placement this module has just made
     * agrees with the host on the next note by construction rather than by two sums happening to
     * match. */
    return one_shot_guard(length, speed, settle_dt) / length + MP_WORLD_STATE_SAME;
}

bool mp_world_state_agrees_within(const mp_world_entry_t *wire, const mp_world_entry_t *local,
                                  float tolerance)
{
    float delta;

    if (wire == NULL || local == NULL) {
        return false;
    }
    if (wire->type != local->type || wire->dir != local->dir ||
        (wire->active != 0u) != (local->active != 0u)) {
        return false;
    }
    delta = wire->pose - local->pose;
    if (delta < 0.0f) {
        delta = -delta;
    }
    return delta < tolerance;
}


size_t mp_world_state_bytes(size_t count)
{
    return MP_WORLD_STATE_HEADER_BYTES + count * MP_WORLD_STATE_ENTRY_BYTES;
}

/* The bound is the level rather than the traffic, since the predicate can select every mover
 * that is not a push block. The largest shipped level has 100 movers, 6 of them push blocks, so
 * its whole note is 94 entries and 670 bytes against a reliable message maximum of 1170: one
 * message, no delta needed, and the cap of 96 entries is 684 bytes. The packet reserves its
 * payload first and seats reliable messages in what is left, so where the actor block is large
 * there may not be 670 free bytes for several packets in a row; that is why a refused note is
 * counted rather than kept, the next one is a second away and describes the map as it will be. */
bool mp_world_state_fits(size_t count)
{
    return count <= MP_WORLD_STATE_MAX_ENTRIES;
}

void mp_world_state_note_init(mp_world_state_note_t *note, uint32_t tick, uint16_t level,
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

mp_world_add_t mp_world_state_note_add(mp_world_state_note_t *note, uint32_t id, uint32_t type,
                                       uint32_t dir, uint32_t active, float pose, float length,
                                       float dwell)
{
    mp_world_entry_t *entry;
    float             fraction;

    if (note == NULL || id > 0xFFFFu || type >= MP_WORLD_MOVER_TYPES || dir > MOVER_DIR_MAX) {
        return MP_WORLD_ADD_REFUSED;
    }
    /* A positive test, so a length that is not a number is refused with the rest. The engine's own
     * wrap loop subtracts the length from the pose until the pose is no larger, so a mover with no
     * length is one the engine itself cannot integrate. */
    if (!(length > 0.0f) || pose != pose) {
        return MP_WORLD_ADD_REFUSED;
    }
    if (note->count >= MP_WORLD_STATE_MAX_ENTRIES) {
        return MP_WORLD_ADD_FULL;
    }
    fraction = pose / length;
    if (fraction < 0.0f) {
        fraction = 0.0f;
    }
    if (fraction > 1.0f) {
        fraction = 1.0f;
    }
    entry         = &note->entry[note->count];
    entry->id     = (uint16_t)id;
    entry->type   = (uint8_t)type;
    entry->dir    = (uint8_t)dir;
    entry->active = (uint8_t)(active != 0u ? 1u : 0u);
    entry->pose   = fraction;
    entry->dwell  = dwell;
    ++note->count;
    return MP_WORLD_ADD_OK;
}

size_t mp_world_state_encode(const mp_world_state_note_t *note, uint8_t *buffer, size_t capacity)
{
    mp_wire_writer_t writer;
    size_t           index;

    if (note == NULL || buffer == NULL || note->count > MP_WORLD_STATE_MAX_ENTRIES) {
        return 0u;
    }
    mp_wire_writer_init(&writer, buffer, capacity);
    mp_wire_put_u8(&writer, MP_WORLD_STATE_TAG);
    mp_wire_put_u32(&writer, note->tick);
    mp_wire_put_u16(&writer, note->level);
    mp_wire_put_u32(&writer, note->generation);
    mp_wire_put_u8(&writer, note->count);
    for (index = 0; index < note->count; ++index) {
        const mp_world_entry_t *entry = &note->entry[index];

        if (entry->type >= MP_WORLD_MOVER_TYPES || entry->dir > MOVER_DIR_MAX ||
            entry->pose != entry->pose) {
            return 0u;
        }
        mp_world_entry_put(&writer, entry);
    }
    return writer.overflowed ? 0u : writer.at;
}

/* The note is a length family, 12 + 7n, and so is the digest next door at 8 + 7n. Neither ladder
 * lands on 1 or 4, the lengths of the two messages on this channel that carry no tag at all, which
 * are the only lengths a message can be confused with outright. 12 + 7 * 4 is exactly 40, the
 * appearance event's length, and that is the first collision of a family with a message that is
 * not one; the tags are the whole of what separates them, and the unit test pins both facts. */
bool mp_world_state_is_note(const uint8_t *buffer, size_t bytes)
{
    size_t body;

    if (buffer == NULL || bytes < MP_WORLD_STATE_HEADER_BYTES ||
        buffer[0] != MP_WORLD_STATE_TAG) {
        return false;
    }
    body = bytes - MP_WORLD_STATE_HEADER_BYTES;
    if ((body % MP_WORLD_STATE_ENTRY_BYTES) != 0u) {
        return false;
    }
    /* The count byte and the length say the same thing twice, and a message where they disagree is
     * a truncated one that would otherwise read as a shorter, valid note. */
    return (body / MP_WORLD_STATE_ENTRY_BYTES) == (size_t)buffer[11] &&
           buffer[11] <= MP_WORLD_STATE_MAX_ENTRIES;
}

bool mp_world_state_decode(const uint8_t *buffer, size_t bytes, mp_world_state_note_t *out)
{
    mp_wire_reader_t reader;
    uint8_t          tag = 0;
    uint8_t          count = 0;
    size_t           index;

    if (out == NULL || !mp_world_state_is_note(buffer, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&reader, buffer, bytes);
    mp_wire_get_u8(&reader, &tag);
    mp_wire_get_u32(&reader, &out->tick);
    mp_wire_get_u16(&reader, &out->level);
    mp_wire_get_u32(&reader, &out->generation);
    mp_wire_get_u8(&reader, &count);
    for (index = 0; index < count; ++index) {
        mp_world_entry_get(&reader, &out->entry[index]);
    }
    if (reader.overran) {
        return false;
    }
    out->count = count;
    return true;
}

bool mp_world_state_generation_current(uint32_t theirs, uint32_t newest_seen)
{
    return theirs >= newest_seen;
}
