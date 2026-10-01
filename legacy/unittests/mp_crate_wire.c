/* mp_crate_wire.c: the three push block messages, whole or refused whole.
 *
 * Every message is encoded into a buffer of exactly the size one message of the reliable channel
 * carries, which is what the sender hands the encoder, not into a comfortable one. The round trip
 * is fuzzed, because a codec that is asymmetric in one field says so only for values nobody wrote
 * a case for.
 */
#include "unittest.h"

#include "mp_channel.h"
#include "mp_crate_wire.h"
#include "mp_wire.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

static uint8_t s_buffer[MP_CHANNEL_MESSAGE_BYTES];

static uint32_t s_seed = 0x2545F491u;

static uint32_t next_random(void)
{
    s_seed ^= s_seed << 13;
    s_seed ^= s_seed >> 17;
    s_seed ^= s_seed << 5;
    return s_seed;
}

/* A coordinate the wire carries exactly: a whole number of 1/256 steps inside the shipped world. */
static float exact_coordinate(void)
{
    return (float)(int32_t)(next_random() % 65536u) / 256.0f;
}

static void entry_of(mp_crate_entry_t *entry, uint8_t id, uint8_t flags)
{
    memset(entry, 0, sizeof *entry);
    entry->id          = id;
    entry->kind        = (uint8_t)MP_CRATE_KIND_BLOCK;
    entry->flags       = flags;
    entry->position[0] = 134.5f;
    entry->position[1] = 149.5f;
    entry->position[2] = 35.5f;
    entry->pusher      = 2u;
    entry->sequence    = 0xFFFEu;
    entry->verdict     = (uint8_t)MP_CRATE_VERDICT_ON_THE_WAY;
    if ((flags & MP_CRATE_FLAG_CARRIED) != 0u) {
        entry->carrier         = 17u;
        entry->carrier_part    = 3u;
        entry->carry_offset[0] = 0.25f;
        entry->carry_offset[1] = -1.5f;
        entry->carry_offset[2] = 2.0f;
    }
    if ((flags & MP_CRATE_FLAG_CARRYING) != 0u) {
        entry->carrying = 41u;
    }
}

static bool same_entry(const mp_crate_entry_t *a, const mp_crate_entry_t *b)
{
    bool same = a->id == b->id && a->kind == b->kind && a->flags == b->flags &&
                a->pusher == b->pusher && a->sequence == b->sequence && a->verdict == b->verdict &&
                memcmp(a->position, b->position, sizeof a->position) == 0;

    if ((a->flags & MP_CRATE_FLAG_CARRIED) != 0u) {
        same = same && a->carrier == b->carrier && a->carrier_part == b->carrier_part &&
               memcmp(a->carry_offset, b->carry_offset, sizeof a->carry_offset) == 0;
    }
    if ((a->flags & MP_CRATE_FLAG_CARRYING) != 0u) {
        same = same && a->carrying == b->carrying;
    }
    return same;
}

/* The length of a note holding one entry with `flags`. */
static size_t one_entry_bytes(uint8_t flags)
{
    mp_crate_note_t note;

    memset(&note, 0, sizeof note);
    note.count = 1u;
    entry_of(&note.entry[0], 41u, flags);
    return mp_crate_note_encode(&note, s_buffer, sizeof s_buffer);
}

static void check_the_sizes(void)
{
    mp_crate_note_t note;
    size_t          i;

    ut_section("the sizes of the note, its entries, the push and the fall");
    ut_check(one_entry_bytes(0u) == 13u + 19u, "the header is 13 bytes and an entry 19");
    ut_check(one_entry_bytes(MP_CRATE_FLAG_CARRIED) == 13u + 33u, "a carried one 14 more");
    ut_check(one_entry_bytes(MP_CRATE_FLAG_CARRYING) == 13u + 20u, "a carrying one 1 more");

    memset(&note, 0, sizeof note);
    note.count = 8u;
    for (i = 0; i < note.count; ++i) {
        entry_of(&note.entry[i], (uint8_t)(30u + i), 0u);
    }
    ut_check(mp_crate_note_encode(&note, s_buffer, sizeof s_buffer) == 165u,
             "a whole note of BIGCITY's eight blocks is 13 + 8 x 19 = 165 bytes");
    ut_check(MP_CRATE_NOTE_MAX_BYTES <= MP_CHANNEL_MESSAGE_BYTES,
             "the largest note there can be fits one message of the reliable channel");
    ut_check(MP_CRATE_PUSH_BYTES == 27u && MP_CRATE_FALL_BYTES == 32u,
             "a wish is 27 bytes and a fall 32");
}

static void check_the_note(void)
{
    mp_crate_note_t note;
    mp_crate_note_t back;
    size_t          bytes;
    size_t          i;
    bool            same = true;

    ut_section("the note");
    memset(&note, 0, sizeof note);
    note.tick       = 123456u;
    note.level      = 78u;
    note.generation = 5u;
    note.whole      = true;
    note.count      = 4u;
    entry_of(&note.entry[0], 39u, 0u);
    entry_of(&note.entry[1], 41u, MP_CRATE_FLAG_CARRIED | MP_CRATE_FLAG_SINK);
    entry_of(&note.entry[2], 42u, MP_CRATE_FLAG_CARRYING);
    entry_of(&note.entry[3], 55u, MP_CRATE_FLAG_FALLING);
    note.entry[1].kind    = (uint8_t)MP_CRATE_KIND_SUNK;
    note.entry[2].pusher  = (uint8_t)MP_CRATE_NOBODY;
    note.entry[3].verdict = (uint8_t)MP_CRATE_VERDICT_STALE;

    bytes = mp_crate_note_encode(&note, s_buffer, sizeof s_buffer);
    ut_check(bytes == 13u + 19u + 33u + 20u + 19u,
             "four entries encode to the length the attachments add up to");
    ut_check(mp_crate_is_note(s_buffer, bytes), "and are recognised as a note");
    ut_check(mp_crate_note_decode(s_buffer, bytes, &back), "and read back");
    for (i = 0; i < note.count; ++i) {
        same = same && same_entry(&note.entry[i], &back.entry[i]);
    }
    ut_check(same && back.tick == note.tick && back.level == note.level &&
                 back.generation == note.generation && back.whole && back.count == 4u,
             "field for field");

    ut_check(!mp_crate_note_decode(s_buffer, bytes - 1u, &back),
             "one byte short is torn and refused whole");
    s_buffer[bytes] = 0u;
    ut_check(!mp_crate_note_decode(s_buffer, bytes + 1u, &back), "and one byte long as well");

    note.whole = false;
    note.count = 0u;
    bytes      = mp_crate_note_encode(&note, s_buffer, sizeof s_buffer);
    ut_check(bytes == 13u && mp_crate_note_decode(s_buffer, bytes, &back) && !back.whole &&
                 back.count == 0u,
             "a change note with nothing in it is the header alone");

    note.count = (uint8_t)(MP_CRATE_MAX + 1u);
    ut_check(mp_crate_note_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "more entries than a level may hold are refused at the encoder");
}

static void check_what_a_note_refuses(void)
{
    mp_crate_note_t note;
    mp_crate_note_t back;
    size_t          bytes;
    size_t          flags_at = 13u + 2u;   /* header, id, kind */

    ut_section("what a note refuses and what it masks");
    memset(&note, 0, sizeof note);
    note.level = 57u;
    note.whole = true;
    note.count = 1u;
    entry_of(&note.entry[0], 41u, 0u);

    note.entry[0].flags = 0xFFu;
    bytes = mp_crate_note_encode(&note, s_buffer, sizeof s_buffer);
    ut_check(bytes != 0u && mp_crate_note_decode(s_buffer, bytes, &back) &&
                 back.entry[0].flags == MP_CRATE_FLAGS_TRAVEL,
             "a flag byte with every bit set travels masked to 0x17, with both attachments");

    entry_of(&note.entry[0], 41u, 0u);
    bytes = mp_crate_note_encode(&note, s_buffer, sizeof s_buffer);
    s_buffer[flags_at] = 0x20u;
    ut_check(!mp_crate_note_decode(s_buffer, bytes, &back),
             "a flag the engine does not travel with is torn on the way in");

    note.entry[0].position[1] = NAN;
    ut_check(mp_crate_note_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "a position that is not a number is refused at the encoder");
    note.entry[0].position[1] = 40000.0f;
    ut_check(mp_crate_note_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "and one past the wire's range");

    entry_of(&note.entry[0], 41u, 0u);
    note.entry[0].kind = 5u;
    ut_check(mp_crate_note_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "a kind no push block has is refused");
    note.entry[0].kind    = (uint8_t)MP_CRATE_KIND_SUNK_ALT;
    note.entry[0].verdict = (uint8_t)MP_CRATE_VERDICT_COUNT;
    ut_check(mp_crate_note_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "and a verdict past the five");
    note.entry[0].verdict = 0u;
    note.entry[0].pusher  = (uint8_t)MP_CRATE_SLOT_LIMIT;
    ut_check(mp_crate_note_encode(&note, s_buffer, sizeof s_buffer) == 0u,
             "and a pusher that is no slot and not nobody");
    note.entry[0].pusher = (uint8_t)MP_CRATE_NOBODY;
    ut_check(mp_crate_note_encode(&note, s_buffer, sizeof s_buffer) != 0u,
             "while kind 3 and nobody pushing are fine");

    ut_check(mp_crate_note_encode(&note, s_buffer, 12u) == 0u,
             "a buffer too small for the header is refused");
    ut_check(!mp_crate_is_note(s_buffer, 12u), "and twelve bytes are no note");
}

static void check_the_push_and_the_fall(void)
{
    mp_crate_push_t push;
    mp_crate_push_t push_back;
    mp_crate_fall_t fall;
    mp_crate_fall_t fall_back;
    size_t          bytes;

    ut_section("the wish and the fall");
    memset(&push, 0, sizeof push);
    push.tick       = 77u;
    push.level      = 57u;
    push.generation = 2u;
    push.id         = 55u;
    push.sequence   = 65535u;
    push.flags      = MP_CRATE_PUSH_PULL | MP_CRATE_PUSH_LET_GO;
    push.target[0]  = 123.5f;
    push.target[1]  = 132.25f;
    push.target[2]  = 35.5f;
    bytes = mp_crate_push_encode(&push, s_buffer, sizeof s_buffer);
    ut_check(bytes == MP_CRATE_PUSH_BYTES && mp_crate_is_push(s_buffer, bytes) &&
                 !mp_crate_is_note(s_buffer, bytes) && !mp_crate_is_fall(s_buffer, bytes),
             "a wish is 27 bytes and nothing else");
    ut_check(mp_crate_push_decode(s_buffer, bytes, &push_back) &&
                 memcmp(&push, &push_back, sizeof push) == 0,
             "and reads back field for field");

    push.flags = 0u;
    ut_check(mp_crate_push_encode(&push, s_buffer, sizeof s_buffer) == 0u,
             "a wish that names nothing is refused");
    push.flags = MP_CRATE_PUSH_FORWARD | MP_CRATE_PUSH_PULL;
    ut_check(mp_crate_push_encode(&push, s_buffer, sizeof s_buffer) == 0u,
             "and one that pushes and pulls at once");
    push.flags = MP_CRATE_PUSH_FORWARD;
    bytes      = mp_crate_push_encode(&push, s_buffer, sizeof s_buffer);
    s_buffer[14] |= 0x80u;   /* tag, tick, level, generation, id and sequence come first */
    ut_check(!mp_crate_push_decode(s_buffer, bytes, &push_back),
             "a flag bit nobody defined is torn on the way in");

    memset(&fall, 0, sizeof fall);
    fall.tick         = 999u;
    fall.level        = 57u;
    fall.generation   = 2u;
    fall.id           = 41u;
    fall.position[0]  = 134.5f;
    fall.position[1]  = 150.0f;
    fall.position[2]  = 35.5f;
    fall.direction[0] = 0.0f;
    fall.direction[1] = 1.0f;
    bytes = mp_crate_fall_encode(&fall, s_buffer, sizeof s_buffer);
    ut_check(bytes == MP_CRATE_FALL_BYTES && mp_crate_is_fall(s_buffer, bytes) &&
                 !mp_crate_is_push(s_buffer, bytes),
             "a fall is 32 bytes");
    ut_check(mp_crate_fall_decode(s_buffer, bytes, &fall_back) &&
                 memcmp(&fall, &fall_back, sizeof fall) == 0,
             "and reads back field for field");
    fall.direction[1] = 0.0f;
    ut_check(mp_crate_fall_encode(&fall, s_buffer, sizeof s_buffer) == 0u,
             "a fall with no direction is refused: the engine's drop normalises it");
}

/* A thousand notes of random shape, encoded, decoded and encoded again: the two encodings are the
 * same bytes, and everything the first said the second says. */
static void check_the_round_trip(void)
{
    mp_crate_note_t note;
    mp_crate_note_t back;
    uint8_t         again[MP_CHANNEL_MESSAGE_BYTES];
    unsigned        round;
    unsigned        failed = 0u;
    unsigned        cut_refused = 0u;

    ut_section("a thousand random notes, there and back");
    for (round = 0; round < 1000u; ++round) {
        size_t bytes;
        size_t i;

        memset(&note, 0, sizeof note);
        note.tick       = next_random();
        note.level      = (uint16_t)next_random();
        note.generation = next_random();
        note.whole      = (next_random() & 1u) != 0u;
        note.count      = (uint8_t)(next_random() % (MP_CRATE_MAX + 1u));
        for (i = 0; i < note.count; ++i) {
            mp_crate_entry_t *e = &note.entry[i];

            entry_of(e, (uint8_t)next_random(), (uint8_t)(next_random() & MP_CRATE_FLAGS_TRAVEL));
            e->kind            = (next_random() & 1u) != 0u ? (uint8_t)MP_CRATE_KIND_BLOCK
                                                            : (uint8_t)MP_CRATE_KIND_SUNK;
            e->position[0]     = exact_coordinate();
            e->position[1]     = exact_coordinate();
            e->position[2]     = exact_coordinate();
            e->pusher          = (uint8_t)(next_random() % 17u);
            e->pusher          = e->pusher == 16u ? (uint8_t)MP_CRATE_NOBODY : e->pusher;
            e->sequence        = (uint16_t)next_random();
            e->verdict         = (uint8_t)(next_random() % MP_CRATE_VERDICT_COUNT);
            e->carry_offset[0] = exact_coordinate();
        }
        bytes = mp_crate_note_encode(&note, s_buffer, sizeof s_buffer);
        if (bytes == 0u || !mp_crate_note_decode(s_buffer, bytes, &back) ||
            mp_crate_note_encode(&back, again, sizeof again) != bytes ||
            memcmp(s_buffer, again, bytes) != 0 || back.count != note.count) {
            ++failed;
            continue;
        }
        for (i = 0; i < note.count; ++i) {
            if (!same_entry(&note.entry[i], &back.entry[i])) {
                ++failed;
                break;
            }
        }
        if (bytes > MP_CRATE_NOTE_HEADER_BYTES &&
            !mp_crate_note_decode(s_buffer, bytes - 1u - (next_random() % 5u), &back)) {
            ++cut_refused;
        }
    }
    ut_checkf(failed == 0u, "every one encodes, decodes and encodes to the same bytes: %u failed",
              failed);
    ut_checkf(cut_refused > 900u, "and cut short anywhere is refused: %u of 1000", cut_refused);
}

int main(void)
{
    check_the_sizes();
    check_the_note();
    check_what_a_note_refuses();
    check_the_push_and_the_fall();
    check_the_round_trip();
    return ut_summary("the push block messages");
}
