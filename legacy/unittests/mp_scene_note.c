/* The scene note, 0xA4: its bytes, what it refuses, and a round trip over every shape it can have.
 *
 * A codec that encodes something its own decoder refuses, or reads back something other than it
 * wrote, is found by writing many notes and reading each back, not by reading the code twice. */
#include "unittest.h"

#include "mp_scene_note.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_scene_note_t a_note(uint8_t seats)
{
    mp_scene_note_t note;
    uint8_t         i;

    memset(&note, 0, sizeof note);
    note.serial       = 7u;
    note.generation   = 3u;
    note.phase        = MP_SCENE_PHASE_GATHERING;
    note.what         = MP_SCENE_WHAT_LOCK | MP_SCENE_WHAT_BARS;
    note.trigger_slot = 2u;
    note.warp_serial  = 0u;
    note.anchor[0]    = 154.25f;
    note.anchor[1]    = 147.75f;
    note.anchor[2]    = 37.0f;
    note.heading      = 90.5f;
    note.age_ms       = 250u;
    note.seats        = seats;
    for (i = 0u; i < seats; ++i) {
        note.seat[i].slot        = (uint8_t)(i + 1u);
        note.seat[i].flags       = 0u;
        note.seat[i].position[0] = 150.0f + (float)i;
        note.seat[i].position[1] = 145.0f - (float)i;
        note.seat[i].position[2] = 37.0f;
        note.seat[i].heading     = 45.0f * (float)i;
    }
    return note;
}

static bool same_note(const mp_scene_note_t *a, const mp_scene_note_t *b)
{
    uint8_t i;

    if (a->serial != b->serial || a->generation != b->generation || a->phase != b->phase ||
        a->what != b->what || a->trigger_slot != b->trigger_slot ||
        a->warp_serial != b->warp_serial || a->age_ms != b->age_ms || a->seats != b->seats ||
        memcmp(a->anchor, b->anchor, sizeof a->anchor) != 0 || a->heading != b->heading) {
        return false;
    }
    for (i = 0u; i < a->seats; ++i) {
        if (a->seat[i].slot != b->seat[i].slot || a->seat[i].flags != b->seat[i].flags ||
            memcmp(a->seat[i].position, b->seat[i].position, sizeof a->seat[i].position) != 0 ||
            a->seat[i].heading != b->seat[i].heading) {
            return false;
        }
    }
    return true;
}

static void check_the_bytes(void)
{
    uint8_t         buffer[MP_SCENE_NOTE_MAX_BYTES + 8u];
    mp_scene_note_t note = a_note(0u);
    mp_scene_note_t back;
    uint8_t         seats;

    ut_section("twenty seven bytes and eighteen a seat, eighty one at most");
    ut_check(MP_SCENE_NOTE_HEADER_BYTES == 27u && MP_SCENE_NOTE_SEAT_BYTES == 18u &&
                 MP_SCENE_NOTE_MAX_BYTES == 81u,
             "the sizes the wire format fixes");
    for (seats = 0u; seats <= MP_SCENE_NOTE_MAX_SEATS; ++seats) {
        size_t bytes;

        note  = a_note(seats);
        bytes = mp_scene_note_encode(&note, buffer, sizeof buffer);
        ut_checkf(bytes == MP_SCENE_NOTE_HEADER_BYTES + (size_t)seats * MP_SCENE_NOTE_SEAT_BYTES &&
                      bytes == mp_scene_note_bytes(&note),
                  "%u seat(s) take %u bytes", (unsigned)seats, (unsigned)bytes);
        ut_checkf(buffer[0] == MP_SCENE_NOTE_TAG && mp_scene_note_is_note(buffer, bytes),
                  "%u seat(s): the first byte is the tag 0xA4", (unsigned)seats);
        memset(&back, 0xCD, sizeof back);
        ut_checkf(mp_scene_note_decode(buffer, bytes, &back) && same_note(&note, &back),
                  "%u seat(s): read back exactly as written", (unsigned)seats);
    }

    ut_section("a note is read whole or refused whole");
    note = a_note(2u);
    {
        size_t bytes = mp_scene_note_encode(&note, buffer, sizeof buffer);
        size_t cut;
        bool   any  = false;

        for (cut = 0u; cut < bytes; ++cut) {
            any = any || mp_scene_note_decode(buffer, cut, &back);
        }
        ut_check(!any, "every shorter length is torn, the header's own included");
        buffer[bytes] = 0u;
        ut_check(!mp_scene_note_decode(buffer, bytes + 1u, &back),
                 "and a byte more than the seats say is torn as well");
        buffer[0] = (uint8_t)(MP_SCENE_NOTE_TAG + 1u);
        ut_check(!mp_scene_note_is_note(buffer, bytes) &&
                     !mp_scene_note_decode(buffer, bytes, &back),
                 "a message under another tag is not this note");
    }
    ut_check(mp_scene_note_encode(&note, buffer, MP_SCENE_NOTE_HEADER_BYTES) == 0u,
             "a buffer too small for the seats is refused, not overrun");
}

static void check_what_is_refused(void)
{
    uint8_t         buffer[MP_SCENE_NOTE_MAX_BYTES];
    mp_scene_note_t note;

    ut_section("the encoder refuses a note this build would not send");
    note       = a_note(1u);
    note.phase = 4u;
    ut_check(mp_scene_note_encode(&note, buffer, sizeof buffer) == 0u, "a phase past over");
    note      = a_note(1u);
    note.what = 0x20u;
    ut_check(mp_scene_note_encode(&note, buffer, sizeof buffer) == 0u, "a bit nobody knows");
    note       = a_note(1u);
    note.seats = MP_SCENE_NOTE_MAX_SEATS + 1u;
    ut_check(mp_scene_note_encode(&note, buffer, sizeof buffer) == 0u, "a fourth seat");
    note           = a_note(1u);
    note.anchor[1] = (float)NAN;
    ut_check(mp_scene_note_encode(&note, buffer, sizeof buffer) == 0u,
             "an anchor that is not a number");
    note                    = a_note(1u);
    note.seat[0].position[2] = (float)INFINITY;
    ut_check(mp_scene_note_encode(&note, buffer, sizeof buffer) == 0u,
             "a seat at infinity");
    note          = a_note(1u);
    note.heading  = (float)-INFINITY;
    ut_check(mp_scene_note_encode(&note, buffer, sizeof buffer) == 0u, "a heading at infinity");
    note              = a_note(2u);
    note.seat[1].slot = note.seat[0].slot;
    ut_check(mp_scene_note_encode(&note, buffer, sizeof buffer) == 0u,
             "two seats for one player");
    note              = a_note(1u);
    note.seat[0].slot = MP_SCENE_SLOT_MAX + 1u;
    ut_check(mp_scene_note_encode(&note, buffer, sizeof buffer) == 0u, "a seat for no slot");
    note               = a_note(1u);
    note.seat[0].flags = 0x02u;
    ut_check(mp_scene_note_encode(&note, buffer, sizeof buffer) == 0u, "a seat flag nobody knows");
    note              = a_note(1u);
    note.trigger_slot = 0x40u;
    ut_check(mp_scene_note_encode(&note, buffer, sizeof buffer) == 0u,
             "a trigger that is neither a slot nor unknown");
    note              = a_note(1u);
    note.trigger_slot = MP_SCENE_TRIGGER_UNKNOWN;
    ut_check(mp_scene_note_encode(&note, buffer, sizeof buffer) != 0u,
             "and an unknown trigger is a note like any other");
    note        = a_note(1u);
    note.serial = 0u;
    ut_check(mp_scene_note_encode(&note, buffer, sizeof buffer) == 0u,
             "a scene numbered nought, which is the number of no scene at all");

    ut_section("the decoder refuses the same things off the wire");
    note = a_note(1u);
    {
        size_t  bytes = mp_scene_note_encode(&note, buffer, sizeof buffer);
        uint8_t keep;
        mp_scene_note_t back;

        keep      = buffer[4];
        buffer[4] = 9u;   /* the phase */
        ut_check(!mp_scene_note_decode(buffer, bytes, &back), "a phase past over");
        buffer[4] = keep;
        buffer[8] = 0x00u;   /* the anchor's x, its bits made a NaN */
        buffer[9] = 0x00u;
        buffer[10] = 0xC0u;
        buffer[11] = 0x7Fu;
        ut_check(!mp_scene_note_decode(buffer, bytes, &back), "an anchor that is not a number");
    }
}

static void check_the_small_rules(void)
{
    uint8_t         a[MP_SCENE_NOTE_MAX_BYTES];
    uint8_t         b[MP_SCENE_NOTE_MAX_BYTES];
    mp_scene_note_t note = a_note(2u);
    mp_scene_note_t other;
    size_t          a_bytes;
    size_t          b_bytes;

    ut_section("the age saturates, a change is a difference but the age");
    ut_check(mp_scene_note_age(0u) == 0u && mp_scene_note_age(65535u) == 65535u,
             "the ends of sixteen bits");
    ut_check(mp_scene_note_age(65536u) == 65535u && mp_scene_note_age(0xFFFFFFFFu) == 65535u,
             "and past them it holds, where a wrap would call a long scene a young one");

    a_bytes      = mp_scene_note_encode(&note, a, sizeof a);
    other        = note;
    other.age_ms = 1999u;
    b_bytes      = mp_scene_note_encode(&other, b, sizeof b);
    ut_check(mp_scene_note_same(a, a_bytes, b, b_bytes), "the same scene a moment older");
    other       = note;
    other.phase = MP_SCENE_PHASE_RUNNING;
    b_bytes     = mp_scene_note_encode(&other, b, sizeof b);
    ut_check(!mp_scene_note_same(a, a_bytes, b, b_bytes), "a new phase is a change");
    other       = note;
    other.seats = 1u;
    b_bytes     = mp_scene_note_encode(&other, b, sizeof b);
    ut_check(!mp_scene_note_same(a, a_bytes, b, b_bytes), "and so is a seat less");

    ut_section("a player's seat, and which scene came after which");
    ut_check(mp_scene_note_seat_of(&note, 2u) == &note.seat[1], "slot 2's seat is found");
    ut_check(mp_scene_note_seat_of(&note, 0u) == NULL,
             "the host has none: it seats itself");
    ut_check(mp_scene_serial_after(8u, 7u) && !mp_scene_serial_after(7u, 8u) &&
                 !mp_scene_serial_after(7u, 7u),
             "eight came after seven, and nothing after itself");
    ut_check(mp_scene_serial_after(1u, 0xFFFFu) && !mp_scene_serial_after(0xFFFFu, 1u),
             "across the wrap of sixteen bits");
}

/* A small generator of its own, so the run is the same on every machine. */
static uint32_t next(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state >> 8;
}

static float a_float(uint32_t *state)
{
    return (float)((int32_t)(next(state) % 200000u) - 100000) / 128.0f;
}

static void check_a_round_trip_over_every_shape(void)
{
    uint32_t state   = 0x5CE7E0A4u;
    unsigned rounds  = 0u;
    unsigned written = 0u;
    unsigned wrong   = 0u;

    ut_section("ten thousand notes of every shape, each read back as written");
    for (rounds = 0u; rounds < 10000u; ++rounds) {
        uint8_t         buffer[MP_SCENE_NOTE_MAX_BYTES];
        mp_scene_note_t note;
        mp_scene_note_t back;
        size_t          bytes;
        uint8_t         i;

        memset(&note, 0, sizeof note);
        note.serial       = (uint16_t)(1u + next(&state) % 0xFFFFu);
        note.generation   = (uint8_t)next(&state);
        note.phase        = (uint8_t)(next(&state) % 4u);
        note.what         = (uint8_t)(next(&state) & MP_SCENE_WHAT_ALL);
        note.trigger_slot = (next(&state) % 5u) == 0u ? (uint8_t)MP_SCENE_TRIGGER_UNKNOWN
                                                      : (uint8_t)(next(&state) % 16u);
        note.warp_serial  = (uint8_t)next(&state);
        note.anchor[0]    = a_float(&state);
        note.anchor[1]    = a_float(&state);
        note.anchor[2]    = a_float(&state);
        note.heading      = a_float(&state);
        note.age_ms       = (uint16_t)next(&state);
        note.seats        = (uint8_t)(next(&state) % (MP_SCENE_NOTE_MAX_SEATS + 1u));
        for (i = 0u; i < note.seats; ++i) {
            note.seat[i].slot        = (uint8_t)(i * 5u + next(&state) % 5u);
            note.seat[i].flags       = (uint8_t)(next(&state) & MP_SCENE_SEAT_F_ALL);
            note.seat[i].position[0] = a_float(&state);
            note.seat[i].position[1] = a_float(&state);
            note.seat[i].position[2] = a_float(&state);
            note.seat[i].heading     = a_float(&state);
        }
        bytes = mp_scene_note_encode(&note, buffer, sizeof buffer);
        if (bytes == 0u) {
            ++wrong;   /* every note built here is one the encoder has to take */
            continue;
        }
        ++written;
        memset(&back, 0, sizeof back);
        if (!mp_scene_note_decode(buffer, bytes, &back) || !same_note(&note, &back)) {
            ++wrong;
        }
    }
    ut_checkf(written == rounds && wrong == 0u,
              "%u written, %u read back wrong or refused", written, wrong);
}

int main(void)
{
    check_the_bytes();
    check_what_is_refused();
    check_the_small_rules();
    check_a_round_trip_over_every_shape();
    return ut_summary("the scene note");
}
