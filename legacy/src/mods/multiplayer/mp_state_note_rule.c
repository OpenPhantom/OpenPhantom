/* mp_state_note_rule.c: the nine state notes and their keys. See the header. */
#include "mp_state_note_rule.h"

#include "mp_crate_wire.h"
#include "mp_host_settings_rule.h"
#include "mp_level_state_rule.h"
#include "mp_lobby.h"
#include "mp_quest.h"
#include "mp_roster.h"
#include "mp_scratch_wire.h"
#include "mp_world.h"
#include "mp_world_state.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The sender's tick in the digest, the map note and the level state: the four bytes after the tag,
 * the same place in all three encoders. The crate note carries its tick there as well and keeps it
 * in its key (is_clock says why). */
#define TICK_AT    1u
#define TICK_BYTES 4u

/* The crate note's tick is followed by the level (2) and the generation (4), then whole or not. */
_Static_assert(MP_CRATE_NOTE_WHOLE_AT == TICK_AT + TICK_BYTES + 2u + 4u,
               "the crate note carries its tick where the other three do");

/* The roster: its tag and count, then entries of MP_ROSTER_ENTRY_BYTES whose round trip is the
 * two bytes after slot, team, ready and hero. */
#define ROSTER_HEAD_BYTES 2u
#define ROSTER_RTT_AT     4u
#define ROSTER_RTT_BYTES  2u

typedef struct state_kind {
    uint8_t     tag;
    const char *name;
} state_kind_t;

static const state_kind_t KINDS[MP_STATE_NOTE_KINDS] = {
    { (uint8_t)MP_ROSTER_TAG,        "the roster" },
    { (uint8_t)MP_LOBBY_SETUP_TAG,   "the setup" },
    { (uint8_t)MP_WORLD_DIGEST_TAG,  "the map digest" },
    { (uint8_t)MP_WORLD_STATE_TAG,   "the map note" },
    { (uint8_t)MP_LEVEL_STATE_TAG,   "the level state" },
    { (uint8_t)MP_QUEST_STATE_TAG,   "the shared story" },
    { (uint8_t)MP_SCRATCH_TAG_AI,    "the blackboard" },
    { (uint8_t)MP_CRATE_NOTE_TAG,    "the whole crate note" },
    { (uint8_t)MP_HOST_SETTINGS_TAG, "the host's settings" },
};

/* Whether a note with a state's tag is that state. Every tag in the table is, except the crate
 * note: only a whole one describes every block, and a change note carries just the blocks that
 * changed, so it may neither overwrite a whole note nor be overwritten by one. It stays an
 * event. */
static bool is_state(const uint8_t *note, size_t bytes)
{
    size_t index;

    for (index = 0; index < MP_STATE_NOTE_KINDS; ++index) {
        if (KINDS[index].tag == note[0]) {
            return note[0] != (uint8_t)MP_CRATE_NOTE_TAG ||
                   (bytes > MP_CRATE_NOTE_WHOLE_AT &&
                    note[MP_CRATE_NOTE_WHOLE_AT] == (uint8_t)MP_CRATE_NOTE_WHOLE);
        }
    }
    return false;
}

/* Whether byte `at` of a note of `kind` is part of its clock, which the key leaves out.
 *
 * Not the whole crate note's tick. A change note of the same tag is an event the client applies at
 * its own tick, and a block can come back byte for byte to where a whole note still on its way had
 * it: whole, change, whole again. Keyed without its tick the third was a repeat of the first and
 * was left out, and the client stood on the change while the host had the block back. With the
 * tick in the key a whole note is never a repeat; it still replaces an older whole note, because
 * the channel replaces a copy by its kind, and the kind is the tag alone, and it still holds two
 * seats of a silent peer at the most, one shrunk and the newest, with the next waiting outside. */
static bool is_clock(uint8_t kind, const uint8_t *note, size_t bytes, size_t at)
{
    if (kind == (uint8_t)MP_WORLD_DIGEST_TAG || kind == (uint8_t)MP_WORLD_STATE_TAG ||
        kind == (uint8_t)MP_LEVEL_STATE_TAG) {
        return at >= TICK_AT && at < TICK_AT + TICK_BYTES;
    }
    if (kind == (uint8_t)MP_ROSTER_TAG && bytes >= ROSTER_HEAD_BYTES && at >= ROSTER_HEAD_BYTES) {
        size_t within = (at - ROSTER_HEAD_BYTES) % MP_ROSTER_ENTRY_BYTES;
        size_t entry  = (at - ROSTER_HEAD_BYTES) / MP_ROSTER_ENTRY_BYTES;

        return entry < note[1] && within >= ROSTER_RTT_AT &&
               within < ROSTER_RTT_AT + ROSTER_RTT_BYTES;
    }
    return false;
}

bool mp_state_note_classify(const uint8_t *note, size_t bytes, uint8_t *kind, uint32_t *key)
{
    uint32_t hash = 2166136261u;   /* FNV-1a, which is enough to tell two states apart */
    size_t   at;

    if (note == NULL || bytes == 0u || !is_state(note, bytes)) {
        return false;
    }
    for (at = 0; at < bytes; ++at) {
        if (!is_clock(note[0], note, bytes, at)) {
            hash = (hash ^ note[at]) * 16777619u;
        }
    }
    hash = (hash ^ (uint32_t)bytes) * 16777619u;
    if (kind != NULL) {
        *kind = note[0];
    }
    if (key != NULL) {
        *key = hash;
    }
    return true;
}

uint8_t mp_state_note_kind_at(size_t index)
{
    return index < MP_STATE_NOTE_KINDS ? KINDS[index].tag : 0u;
}

const char *mp_state_note_name(uint8_t kind)
{
    size_t index;

    for (index = 0; index < MP_STATE_NOTE_KINDS; ++index) {
        if (KINDS[index].tag == kind) {
            return KINDS[index].name;
        }
    }
    return "a state";
}
