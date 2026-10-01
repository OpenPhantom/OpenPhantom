/* mp_session_peers_state.c: state notes, the addressed hit and the overflow packet, over the
 * three client harness (mp_peers_net.c).
 *
 * The state notes travel as their newest copy, a hit on a far player reaches that player's machine
 * alone, and a note too large to sit beside a full payload rides a second packet of the same
 * substep. Each is proven here with real sessions over the stepped network.
 *
 * SIZE NOTE: over 600 lines, because every check reads the client's side as well as the host's,
 * which is what lets it fail (a host's count of pending seats is the same with and without most of
 * these defects). The seam is the state notes against the rest: the addressed note and the overflow
 * packet share only the harness.
 */
#include "unittest.h"

#include "mp_peers_net.h"

#include "mp_channel.h"
#include "mp_crate_wire.h"
#include "mp_scene_note.h"
#include "mp_session.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * State notes: the newest copy is what counts.
 * ============================================================================================ */

#define ROSTER_NOTE_BYTES (2u + 57u)
#define DIGEST_NOTE_BYTES 15u

/* A roster of one player whose name carries `version` and whose round trip is `rtt`. */
static size_t make_roster(uint8_t version, uint16_t rtt, uint8_t *out)
{
    memset(out, 0, ROSTER_NOTE_BYTES);
    out[0] = 0x8Fu;   /* the roster's tag */
    out[1] = 1u;      /* one entry */
    out[2] = 1u;      /* its slot */
    out[6] = (uint8_t)(rtt & 0xFFu);
    out[7] = (uint8_t)(rtt >> 8);
    out[8] = 'P';
    out[9] = version;
    return ROSTER_NOTE_BYTES;
}

/* A map digest whose only change is the sender's tick, as a level where nothing moves sends it. */
static size_t make_digest(uint32_t tick, uint8_t *out)
{
    mp_wire_writer_t w;

    memset(out, 0, DIGEST_NOTE_BYTES);
    mp_wire_writer_init(&w, out, DIGEST_NOTE_BYTES);
    mp_wire_put_u8(&w, 0x86u);   /* the digest's tag */
    mp_wire_put_u32(&w, tick);
    mp_wire_put_u16(&w, 57u);
    mp_wire_put_u8(&w, 1u);
    return DIGEST_NOTE_BYTES;
}

typedef struct state_run {
    uint32_t round;
    bool     digest;      /* a digest with a new tick, or else a roster with a new name */
    uint32_t refused;
} state_run_t;

static void broadcast_state(void *context)
{
    state_run_t *run = (state_run_t *)context;
    uint8_t      note[ROSTER_NOTE_BYTES];
    size_t       bytes;

    bytes = run->digest ? make_digest(1000u + run->round, note)
                        : make_roster((uint8_t)run->round, (uint16_t)(40u + run->round % 7u), note);
    if (mp_session_broadcast_reliable(&s_host, note, bytes) == 0u) {
        ++run->refused;
    }
    ++run->round;
}

/* What the client read of the rosters: how many, whether each was newer than the one before, and
 * the last one's version. */
typedef struct roster_reader {
    uint32_t count;
    bool     newer_each_time;
    int      last;
} roster_reader_t;

static void read_rosters(size_t k, roster_reader_t *reader)
{
    uint8_t note[MP_CHANNEL_MESSAGE_BYTES];
    size_t  bytes = 0;

    while (mp_session_read_reliable(&s_client[k], 0, note, sizeof note, &bytes)) {
        if (bytes != ROSTER_NOTE_BYTES || note[0] != 0x8Fu) {
            continue;
        }
        if ((int)note[9] <= reader->last) {
            reader->newer_each_time = false;
        }
        reader->last = note[9];
        ++reader->count;
    }
}

/* A client silent for three seconds while the roster changes every substep. Every change used to
 * take a seat of its own until the sixty four were gone and the next was refused; now the channel
 * holds the copy in flight shrunk to nothing, the newest, and one more waiting outside it, and the
 * client that comes back reads only rosters newer than the last it read, ending on the newest. */
static void check_a_changing_state_takes_two_seats(void)
{
    state_run_t     run;
    roster_reader_t reader;
    int             round;

    ut_section("a state that changes every substep takes two seats of a silent client's channel");
    memset(&run, 0, sizeof run);
    memset(&reader, 0, sizeof reader);
    reader.newer_each_time = true;
    reader.last            = -1;
    ut_check(net_build(1u), "the host and one client connected");
    s_net.muted[1] = true;
    for (round = 0; round < 100; ++round) {
        net_round(1u, &broadcast_state, &run);
    }
    ut_checkf(run.refused == 0u && mp_session_reliable_pending(&s_host) <= 3u,
              "100 rosters later nothing was refused and %u are pending, the most three",
              (unsigned)mp_session_reliable_pending(&s_host));
    s_net.muted[1] = false;
    for (round = 0; round < 40; ++round) {
        net_round(1u, NULL, NULL);
        read_rosters(0u, &reader);
    }
    ut_checkf(reader.last == 99 && reader.newer_each_time,
              "the client read %u roster(s), each newer than the one before, the last the "
              "newest (%d)", (unsigned)reader.count, reader.last);
    ut_check(mp_session_reliable_pending(&s_host) == 0u, "and nothing is pending any more");
}

/* A copy that was lost on its way and shrunk before it came again arrives empty: the session
 * skips it before any reader sees it, counts it, and the reader gets the newer copy in order. */
static void check_an_empty_note_is_skipped(void)
{
    state_run_t     run;
    roster_reader_t reader;
    int             round;

    ut_section("a copy shrunk while it was lost arrives empty and is skipped, counted");
    memset(&run, 0, sizeof run);
    memset(&reader, 0, sizeof reader);
    reader.newer_each_time = true;
    reader.last            = -1;
    ut_check(net_build(1u), "the host and one client connected");
    s_net.deaf[1] = true;
    net_round(1u, &broadcast_state, &run);   /* roster 0 goes out and is lost */
    net_round(1u, &broadcast_state, &run);   /* roster 1 shrinks it and is lost too */
    s_net.deaf[1] = false;
    for (round = 0; round < 20; ++round) {
        net_round(1u, NULL, NULL);
        read_rosters(0u, &reader);
    }
    ut_checkf(reader.count == 1u && reader.last == 1,
              "the reader got one roster, the newer (%u read, the last %d)",
              (unsigned)reader.count, reader.last);
    ut_checkf(mp_session_empty_notes(&s_client[0]) == 1u,
              "and the empty copy in front of it was skipped and counted (%u)",
              (unsigned)mp_session_empty_notes(&s_client[0]));
}

/* The campaign bank looks like a state and is a difference against the sender's mirror: two of
 * them are two messages, and the client reads both whole. Taken for a state, the first, lost and
 * then shrunk by the second, would reach it empty and be skipped, and the difference it carried
 * would be missing there for good. Read on the client's side, because the host's count of pending
 * messages is two either way: one shrunk and one whole are two as well. */
static void check_the_bank_is_never_collapsed(void)
{
    uint8_t  note[40];
    uint8_t  message[MP_CHANNEL_MESSAGE_BYTES];
    size_t   bytes = 0;
    uint32_t whole = 0;
    int      last = 0;
    bool     in_order = true;
    int      round;

    ut_section("the campaign bank is a difference, and two of them stay two");
    ut_check(net_build(1u), "the host and one client connected");
    s_net.deaf[1] = true;   /* both go out, and both are lost */
    memset(note, 0, sizeof note);
    note[0] = 0x8Bu;   /* the bank's tag */
    for (round = 1; round <= 2; ++round) {
        note[5] = (uint8_t)round;
        (void)mp_session_broadcast_reliable(&s_host, note, sizeof note);
        net_round(1u, NULL, NULL);
    }
    ut_checkf(mp_session_reliable_pending(&s_host) == 2u,
              "both are pending as messages of their own (%u)",
              (unsigned)mp_session_reliable_pending(&s_host));
    s_net.deaf[1] = false;
    for (round = 0; round < 20; ++round) {
        net_round(1u, NULL, NULL);
        while (mp_session_read_reliable(&s_client[0], 0, message, sizeof message, &bytes)) {
            if (bytes != sizeof note || message[0] != 0x8Bu) {
                continue;
            }
            in_order = in_order && (int)message[5] == last + 1;
            last     = message[5];
            ++whole;
        }
    }
    ut_checkf(whole == 2u && last == 2 && in_order,
              "and the client read both whole, in order (%u whole, the last %d)", (unsigned)whole,
              last);
}

/* The same digest every substep, only its tick moving: while one is on its way, a repeat that says
 * nothing new is not sent at all. */
static void check_an_unchanged_state_is_not_repeated(void)
{
    state_run_t run;
    int         round;

    ut_section("an unchanged state with a new tick is not sent again while one is on its way");
    memset(&run, 0, sizeof run);
    run.digest = true;
    ut_check(net_build(1u), "the host and one client connected");
    s_net.muted[1] = true;
    for (round = 0; round < 50; ++round) {
        net_round(1u, &broadcast_state, &run);
    }
    ut_checkf(run.refused == 0u && mp_session_reliable_pending(&s_host) == 1u,
              "50 digests later one is pending (%u), none refused",
              (unsigned)mp_session_reliable_pending(&s_host));
}

/* ==============================================================================================
 * The scene and the whole crate note are states too; a crate change note is not.
 * ============================================================================================ */

typedef enum wave_note {
    WAVE_SCENE,
    WAVE_CRATES_WHOLE,
    WAVE_CRATES_CHANGE
} wave_note_t;

_Static_assert(MP_SCENE_NOTE_MAX_BYTES <= MP_CRATE_NOTE_MAX_BYTES,
               "one buffer of the crate sender's size holds either note");

/* One note for `round`, with its clock at `clock`, encoded with the capacity its own sender hands
 * the encoder. The round is where a reader finds it again: the scene's anchor, the first block's
 * place. Only the scene's age and the crate note's tick carry the clock. */
static size_t wave_note(wave_note_t which, uint32_t round, uint32_t clock, uint8_t *out)
{
    mp_scene_note_t scene;
    mp_crate_note_t crates;

    if (which == WAVE_SCENE) {
        memset(&scene, 0, sizeof scene);
        scene.serial       = 1u;
        scene.generation   = 1u;
        scene.phase        = (uint8_t)MP_SCENE_PHASE_RUNNING;
        scene.what         = (uint8_t)MP_SCENE_WHAT_LOCK;
        scene.trigger_slot = (uint8_t)MP_SCENE_TRIGGER_UNKNOWN;
        scene.anchor[0]    = (float)round;
        scene.age_ms       = mp_scene_note_age(clock * ROUND_MS);
        return mp_scene_note_encode(&scene, out, MP_SCENE_NOTE_MAX_BYTES);
    }
    memset(&crates, 0, sizeof crates);
    crates.tick                 = 1000u + clock;
    crates.level                = 57u;
    crates.generation           = 1u;
    crates.whole                = which == WAVE_CRATES_WHOLE;
    crates.count                = 1u;
    crates.entry[0].id          = 3u;
    crates.entry[0].kind        = (uint8_t)MP_CRATE_KIND_BLOCK;
    crates.entry[0].position[0] = (float)round;
    crates.entry[0].pusher      = (uint8_t)MP_CRATE_NOBODY;
    crates.entry[0].verdict     = (uint8_t)MP_CRATE_VERDICT_REACHED;
    return mp_crate_note_encode(&crates, out, MP_CRATE_NOTE_MAX_BYTES);
}

/* Broadcast by the host; 1 when no peer took it, for a count of refusals. */
static uint32_t wave_refused(wave_note_t which, uint32_t round, uint32_t clock)
{
    uint8_t note[MP_CRATE_NOTE_MAX_BYTES];
    size_t  bytes = wave_note(which, round, clock, note);

    return (bytes == 0u || mp_session_broadcast_reliable(&s_host, note, bytes) == 0u) ? 1u : 0u;
}

/* The seats the host's channel to its one client holds, a shrunk copy included. */
static size_t seats_to_client(void)
{
    return mp_channel_send_pending(&s_host.peers[0].channel);
}

/* What a client read of one kind: how many, the round of the last, and whether each was newer
 * than the one before. */
typedef struct wave_kind_read {
    uint32_t count;
    int      last;
    bool     newer_each_time;
} wave_kind_read_t;

typedef struct wave_reader {
    wave_kind_read_t scene;
    wave_kind_read_t whole;
    wave_kind_read_t change;
    uint32_t         torn;   /* a note of either tag that would not decode */
} wave_reader_t;

static void wave_reader_init(wave_reader_t *reader)
{
    memset(reader, 0, sizeof *reader);
    reader->scene.last  = -1;
    reader->whole.last  = -1;
    reader->change.last = -1;
    reader->scene.newer_each_time  = true;
    reader->whole.newer_each_time  = true;
    reader->change.newer_each_time = true;
}

static void wave_take(wave_kind_read_t *kind, int round)
{
    if (round <= kind->last) {
        kind->newer_each_time = false;
    }
    kind->last = round;
    ++kind->count;
}

static void read_wave_notes(size_t k, wave_reader_t *reader)
{
    uint8_t         note[MP_CHANNEL_MESSAGE_BYTES];
    size_t          bytes = 0;
    mp_scene_note_t scene;
    mp_crate_note_t crates;

    while (mp_session_read_reliable(&s_client[k], 0, note, sizeof note, &bytes)) {
        if (mp_scene_note_is_note(note, bytes)) {
            if (!mp_scene_note_decode(note, bytes, &scene)) {
                ++reader->torn;
                continue;
            }
            wave_take(&reader->scene, (int)scene.anchor[0]);
        } else if (mp_crate_is_note(note, bytes)) {
            if (!mp_crate_note_decode(note, bytes, &crates)) {
                ++reader->torn;
                continue;
            }
            wave_take(crates.whole ? &reader->whole : &reader->change,
                      (int)crates.entry[0].position[0]);
        }
    }
}

/* The host repeats its scene once a second and its whole crate note as often, and in between a
 * change goes at once. To a client whose acknowledgements do not come back, every one of them used
 * to take a seat of its own; now a second copy in the same substep overwrites the first before it
 * went out. A scene whose only news is its age is left out while the first is on its way. A whole
 * crate note is never such a repeat, because its tick is part of what it says (the check below
 * this one says why): it shrinks the copy on its way, and the next waits outside the channel, so
 * the kind still holds two seats however long the silence. */
static void check_two_in_a_row_take_one_seat(void)
{
    static const wave_note_t kinds[] = { WAVE_SCENE, WAVE_CRATES_WHOLE };
    static const char *const names[] = { "scene", "whole crate" };
    size_t                   i;
    uint32_t                 round;

    ut_section("two scene or whole crate notes in a row take one seat of a channel nobody answers");
    for (i = 0; i < sizeof kinds / sizeof kinds[0]; ++i) {
        uint32_t refused = 0;
        size_t   most_seats = 0;
        bool     scene = kinds[i] == WAVE_SCENE;

        ut_check(net_build(1u), "the host and one client connected");
        s_net.muted[1] = true;
        refused += wave_refused(kinds[i], 1u, 0u);
        refused += wave_refused(kinds[i], 2u, 0u);
        net_round(1u, NULL, NULL);
        ut_checkf(refused == 0u && seats_to_client() == 1u,
                  "two %s notes that differ, in one substep, take %u seat(s), one", names[i],
                  (unsigned)seats_to_client());
        for (round = 1u; round <= 40u; ++round) {
            refused += wave_refused(kinds[i], 2u, round);
            net_round(1u, NULL, NULL);
            most_seats = seats_to_client() > most_seats ? seats_to_client() : most_seats;
        }
        ut_checkf(refused == 0u && most_seats == (scene ? 1u : 2u) &&
                      mp_session_reliable_pending(&s_host) == (scene ? 1u : 3u),
                  "40 %s notes later whose clock alone moved, at the most %u seat(s) and %u "
                  "pending: %s", names[i], (unsigned)most_seats,
                  (unsigned)mp_session_reliable_pending(&s_host),
                  scene ? "one each, the age is no news"
                        : "two seats and one waiting, the tick is part of the state");
    }
}

/* A whole crate note, a change note, then a whole note again with every block where the first had
 * it and only its tick newer, to a client whose acknowledgements never come back. Keyed without its
 * tick, the third said the same as the first, which was still on its way, and was left out as a
 * repeat: the client read the first and the change and stood a block where the host no longer had
 * it, until a whole note went out after the first was acknowledged. The change note in between is
 * an event the client applies at its own tick, so an older copy on its way is no stand-in for the
 * newer one. */
static void check_whole_change_whole_to_a_silent_client(void)
{
    uint8_t         note[MP_CHANNEL_MESSAGE_BYTES];
    size_t          bytes = 0;
    mp_crate_note_t crates;
    int             place[4] = { 0, 0, 0, 0 };
    bool            whole[4] = { false, false, false, false };
    size_t          count = 0;
    uint32_t        refused = 0;
    int             round;

    ut_section("a whole crate note, a change and the first state again reach a silent client");
    ut_check(net_build(1u), "the host and one client connected");
    s_net.muted[1] = true;   /* the client hears, its acknowledgements are lost */
    refused += wave_refused(WAVE_CRATES_WHOLE, 1u, 0u);
    net_round(1u, NULL, NULL);
    refused += wave_refused(WAVE_CRATES_CHANGE, 2u, 1u);
    net_round(1u, NULL, NULL);
    refused += wave_refused(WAVE_CRATES_WHOLE, 1u, 2u);
    net_round(1u, NULL, NULL);
    for (round = 0; round < 20; ++round) {
        net_round(1u, NULL, NULL);
        while (mp_session_read_reliable(&s_client[0], 0, note, sizeof note, &bytes)) {
            if (!mp_crate_is_note(note, bytes) || !mp_crate_note_decode(note, bytes, &crates)) {
                continue;
            }
            if (count < 4u) {
                place[count] = (int)crates.entry[0].position[0];
                whole[count] = crates.whole;
            }
            ++count;
        }
    }
    ut_checkf(refused == 0u && count == 3u && whole[0] && place[0] == 1 && !whole[1] &&
                  place[1] == 2 && whole[2] && place[2] == 1,
              "the client read %u crate note(s): whole at %d, a change to %d, whole at %d again, "
              "and stands where the host has the block", (unsigned)count, place[0], place[1],
              place[2]);
}

/* Both change every substep while the client is silent: each kind holds its shrunk copy and its
 * newest, never more, and each waits outside the channel in a row of its own. */
static void check_a_changing_scene_and_crates_take_two_seats_each(void)
{
    wave_reader_t reader;
    uint32_t      refused = 0;
    size_t        most_seats = 0;
    uint32_t      round;

    ut_section("a scene and a whole crate note changing every substep take two seats each");
    wave_reader_init(&reader);
    ut_check(net_build(1u), "the host and one client connected");
    s_net.muted[1] = true;
    for (round = 0u; round < 100u; ++round) {
        refused += wave_refused(WAVE_SCENE, round, round);
        refused += wave_refused(WAVE_CRATES_WHOLE, round, round);
        net_round(1u, NULL, NULL);
        most_seats = seats_to_client() > most_seats ? seats_to_client() : most_seats;
    }
    ut_checkf(refused == 0u && most_seats <= 4u && mp_session_reliable_pending(&s_host) <= 6u,
              "100 of each later %u were refused, at the most %u seat(s) were taken (four: per "
              "kind one shrunk and one whole) and %u are pending, the most six",
              (unsigned)refused, (unsigned)most_seats,
              (unsigned)mp_session_reliable_pending(&s_host));
    s_net.muted[1] = false;
    for (round = 0u; round < 40u; ++round) {
        net_round(1u, NULL, NULL);
        read_wave_notes(0u, &reader);
    }
    ut_checkf(reader.scene.last == 99 && reader.whole.last == 99 &&
                  reader.scene.newer_each_time && reader.whole.newer_each_time &&
                  reader.torn == 0u,
              "the client read %u scene(s) and %u whole crate note(s), each newer than the one "
              "before, the last the newest (%d, %d)", (unsigned)reader.scene.count,
              (unsigned)reader.whole.count, reader.scene.last, reader.whole.last);
    ut_check(mp_session_reliable_pending(&s_host) == 0u, "and nothing is pending any more");
}

/* A change note carries only the blocks that changed, so it is an event however it is tagged:
 * neither a later change nor a whole note may overwrite or shrink it, and every one arrives whole
 * and in order. The whole notes beside it are the newest state as before. */
static void check_a_crate_change_note_is_never_replaced(void)
{
    wave_reader_t reader;
    uint32_t      refused = 0;
    int           round;

    ut_section("a crate change note is an event: never overwritten, never shrunk, read whole");
    wave_reader_init(&reader);
    ut_check(net_build(1u), "the host and one client connected");
    s_net.deaf[1] = true;   /* everything the host sends is lost until the client hears again */
    refused += wave_refused(WAVE_CRATES_CHANGE, 1u, 1u);
    refused += wave_refused(WAVE_CRATES_CHANGE, 2u, 1u);
    refused += wave_refused(WAVE_CRATES_WHOLE, 2u, 1u);
    refused += wave_refused(WAVE_CRATES_WHOLE, 3u, 1u);
    net_round(1u, NULL, NULL);
    ut_checkf(refused == 0u && seats_to_client() == 3u,
              "two changes and two whole notes in one substep take %u seat(s): both changes and "
              "the newer whole note", (unsigned)seats_to_client());
    refused += wave_refused(WAVE_CRATES_CHANGE, 4u, 2u);
    net_round(1u, NULL, NULL);
    refused += wave_refused(WAVE_CRATES_WHOLE, 4u, 3u);   /* shrinks the whole note on its way */
    net_round(1u, NULL, NULL);
    s_net.deaf[1] = false;
    for (round = 0; round < 20; ++round) {
        net_round(1u, NULL, NULL);
        read_wave_notes(0u, &reader);
    }
    ut_checkf(refused == 0u && reader.change.count == 3u && reader.change.last == 4 &&
                  reader.change.newer_each_time && reader.torn == 0u,
              "the client read all three change notes whole and in order (%u, the last %d)",
              (unsigned)reader.change.count, reader.change.last);
    ut_checkf(reader.whole.count == 1u && reader.whole.last == 4,
              "and of the whole notes only the newest, the one shrunk on its way skipped (%u, "
              "the last %d)", (unsigned)reader.whole.count, reader.whole.last);
}

/* ==============================================================================================
 * A note for one player's machine.
 * ============================================================================================ */

/* A hit on a far player concerns that player's machine: the slot it plays at names its peer by the
 * one rule the slot byte uses, and the note reaches that client and no other. */
static void check_a_note_reaches_the_slots_peer_alone(void)
{
    reader_t readers[CLIENTS];
    uint8_t  event[EVENT_BYTES];
    size_t   index = 99u;
    size_t   k;
    bool     inverse = true;
    int      round;

    ut_section("a note addressed to a slot reaches the peer at that slot and nobody else");
    for (k = 0; k < MP_SESSION_MAX_PEERS; ++k) {
        inverse = inverse && mp_session_peer_of_slot(mp_session_slot_of_peer(k), &index) &&
                  index == k;
    }
    ut_check(inverse, "every peer's slot names that peer back");
    ut_check(!mp_session_peer_of_slot(0u, &index) &&
             !mp_session_peer_of_slot((uint8_t)(MP_SESSION_MAX_PEERS + 1u), &index),
             "slot 0, the host's own player, and a slot past the peers name nobody");

    memset(readers, 0, sizeof readers);
    ut_check(net_build(CLIENTS), "a host and three clients connected");
    ut_check(mp_session_peer_of_slot(2u, &index) && index == 1u,
             "slot 2 is the second client's");
    make_event(7u, event);
    ut_check(mp_session_send_or_hold(&s_host, index, event, sizeof event),
             "the host sends the note to that peer alone");
    for (round = 0; round < 5; ++round) {
        net_round(CLIENTS, NULL, NULL);
        for (k = 0; k < CLIENTS; ++k) {
            read_all(k, &readers[k]);
        }
    }
    ut_checkf(readers[1].expected == 8u && readers[0].expected == 0u &&
              readers[2].expected == 0u && readers[0].others == 0u && readers[2].others == 0u,
              "the second client read note 7, the first and the third read nothing");
}

/* ==============================================================================================
 * More packets, not larger ones.
 * ============================================================================================ */

#define FULL_PAYLOAD_BYTES 1100u
#define LARGE_NOTE_BYTES   500u

typedef struct overflow_run {
    uint32_t rounds;
    uint32_t notes;          /* large notes the host queued */
    uint32_t host_packets;   /* packets the host sent in the rounds with a note */
    uint32_t most_a_round;   /* the most the host sent one client in one round */
} overflow_run_t;

/* Every substep a payload that nearly fills the packet, and every fourth a note of 500 bytes: the
 * note does not fit beside the payload, and nothing between substeps services the session here. */
static void payload_and_large_note(void *context)
{
    overflow_run_t *run = (overflow_run_t *)context;
    uint8_t         payload[FULL_PAYLOAD_BYTES];
    uint8_t         note[LARGE_NOTE_BYTES];

    memset(payload, 0x33, sizeof payload);
    (void)mp_session_set_payload(&s_host, 0u, payload, sizeof payload);
    if (run->rounds % 4u == 0u) {
        memset(note, 0x44, sizeof note);
        note[0] = (uint8_t)EVENT_TAG;
        if (mp_session_broadcast_reliable(&s_host, note, sizeof note) != 0u) {
            ++run->notes;
        }
    }
    ++run->rounds;
}

/* A note too large to sit beside a full payload used to wait for a packet without one, and in a
 * level, where every substep carries a payload, that packet only came from the frame pump between
 * substeps. Now the substep's own service sends a second packet with messages only, and the note
 * arrives in the substep it was sent; never more than two packets a substep. */
static void check_a_large_note_takes_an_overflow_packet(void)
{
    overflow_run_t run;
    uint8_t        note[MP_CHANNEL_MESSAGE_BYTES];
    size_t         bytes = 0;
    uint32_t       read = 0;
    uint32_t       round;

    ut_section("a note that cannot sit beside a full payload rides a second "
               "packet, the same substep");
    memset(&run, 0, sizeof run);
    ut_check(net_build(1u), "the host and one client connected");
    for (round = 0; round < 64u; ++round) {
        uint32_t before = s_net.sent[0];
        uint32_t sent;

        net_round(1u, &payload_and_large_note, &run);
        sent = s_net.sent[0] - before;
        run.most_a_round = sent > run.most_a_round ? sent : run.most_a_round;
        while (mp_session_read_reliable(&s_client[0], 0, note, sizeof note, &bytes)) {
            read += (bytes == LARGE_NOTE_BYTES) ? 1u : 0u;
        }
    }
    ut_checkf(run.notes == 16u && read == 16u,
              "all %u large notes arrived (%u read)", (unsigned)run.notes, (unsigned)read);
    ut_checkf(run.most_a_round <= 2u,
              "and the host never sent more than two packets in a substep (%u at the most)",
              (unsigned)run.most_a_round);
}

int main(void)
{
    check_a_changing_state_takes_two_seats();
    check_an_unchanged_state_is_not_repeated();
    check_two_in_a_row_take_one_seat();
    check_whole_change_whole_to_a_silent_client();
    check_a_changing_scene_and_crates_take_two_seats_each();
    check_a_crate_change_note_is_never_replaced();
    check_the_bank_is_never_collapsed();
    check_an_empty_note_is_skipped();
    check_a_note_reaches_the_slots_peer_alone();
    check_a_large_note_takes_an_overflow_packet();
    return ut_summary("mp_session_peers_state");
}
