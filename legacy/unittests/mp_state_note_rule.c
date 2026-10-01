/* mp_state_note_rule.c: which notes are states, and what one says without its clock.
 *
 * The scene and the crate note are built by their own encoders into buffers of the size their
 * senders use, so the bytes the key leaves out are the ones those encoders put the clock in, not
 * the ones this file believes they do.
 */
#include "unittest.h"

#include "mp_crate_wire.h"
#include "mp_host_settings_rule.h"
#include "mp_scene_note.h"
#include "mp_state_note_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The byte of a crate note that says whole (1) or a change (0), after the tag, the host's tick,
 * the level and the generation. */
#define CRATE_WHOLE_BYTE 11u

static void check_the_ten(void)
{
    static const uint8_t states[] = { 0x8Fu, 0x92u, 0x86u, 0x96u, 0xA3u, 0x9Au, 0x8Cu, 0xA4u,
                                      0xA5u, 0xABu };
    static const uint8_t events[] = { 0x81u, 0x85u, 0x88u, 0x8Bu, 0x90u, 0x91u, 0x95u, 0xA0u,
                                      0xA6u, 0xA7u };
    uint8_t              note[16];
    uint8_t              kind = 0;
    uint32_t             key = 0;
    size_t               index;
    bool                 all_states = true;
    bool                 no_events = true;
    bool                 walk_agrees = true;

    ut_section("the ten states are recognised, and nothing else");
    memset(note, 0x11, sizeof note);
    note[CRATE_WHOLE_BYTE] = 1u;   /* a crate note is a state only when it is whole */
    for (index = 0; index < sizeof states; ++index) {
        note[0] = states[index];
        all_states = all_states && mp_state_note_classify(note, sizeof note, &kind, &key) &&
                     kind == states[index];
    }
    for (index = 0; index < sizeof events; ++index) {
        note[0] = events[index];
        no_events = no_events && !mp_state_note_classify(note, sizeof note, &kind, &key);
    }
    for (index = 0; index < sizeof states; ++index) {
        walk_agrees = walk_agrees && mp_state_note_kind_at(index) == states[index];
    }
    ut_check(all_states, "the roster, the setup, the digest, the map note, the level state, the "
                         "story, the blackboard, the scene, the whole crate note and the host's "
                         "settings are states, each its own kind");
    ut_check(no_events, "the moments, the plates, a removal, a hit, the lobby line, the score, "
                        "a bolt, a push wish and a fall are not, and neither is the campaign "
                        "bank, a difference against the sender's mirror");
    ut_checkf(MP_STATE_NOTE_KINDS == sizeof states && walk_agrees &&
                  mp_state_note_kind_at(sizeof states) == 0u,
              "the report's walk names the ten in order and nothing past them (%u kinds)",
              (unsigned)MP_STATE_NOTE_KINDS);
    ut_check(strcmp(mp_state_note_name(0x8Fu), "the roster") == 0 &&
             strcmp(mp_state_note_name(0xA4u), "the scene") == 0 &&
             strcmp(mp_state_note_name(0xA5u), "the whole crate note") == 0 &&
             strcmp(mp_state_note_name(0xABu), "the host's settings") == 0 &&
             strcmp(mp_state_note_name(0x81u), "a state") == 0,
             "each has a name for the report");
    ut_check(!mp_state_note_classify(note, 0u, &kind, &key) &&
             !mp_state_note_classify(NULL, 4u, &kind, &key),
             "an empty note or none is nothing");
}

static uint32_t key_of(const uint8_t *note, size_t bytes)
{
    uint32_t key = 0;

    (void)mp_state_note_classify(note, bytes, NULL, &key);
    return key;
}

static void check_the_key(void)
{
    uint8_t  digest[15];
    uint8_t  roster[2u + 2u * 57u];
    uint32_t before;

    ut_section("the key is what a state says without its clock");
    memset(digest, 0, sizeof digest);
    digest[0] = 0x86u;
    digest[5] = 57u;
    before = key_of(digest, sizeof digest);
    digest[1] = 0x44u;   /* the sender's tick */
    digest[4] = 0x01u;
    ut_check(key_of(digest, sizeof digest) == before, "a digest whose tick moved has the same key");
    digest[8] = 3u;      /* an entry */
    ut_check(key_of(digest, sizeof digest) != before, "one whose movers moved has another");
    digest[0] = 0xA3u;
    before    = key_of(digest, sizeof digest);
    digest[2] = 0x99u;
    ut_check(key_of(digest, sizeof digest) == before, "so has a level state whose tick moved");

    memset(roster, 0, sizeof roster);
    roster[0] = 0x8Fu;
    roster[1] = 2u;
    before = key_of(roster, sizeof roster);
    roster[2 + 4]      = 40u;   /* the first player's round trip */
    roster[2 + 57 + 5] = 1u;    /* the second's, high byte */
    ut_check(key_of(roster, sizeof roster) == before,
             "a roster whose round trips moved has the same key");
    roster[2 + 57 + 1] = 1u;    /* the second player's team */
    ut_check(key_of(roster, sizeof roster) != before,
             "one where a player changed side has another");
    ut_check(key_of(roster, sizeof roster - 1u) != key_of(roster, sizeof roster),
             "and a note of another length is another note");
}

/* ==============================================================================================
 * The scene: the age is its clock, and the key agrees with the sender's own test for a change.
 * ============================================================================================ */

static void scene_of(mp_scene_note_t *note, uint8_t phase, uint16_t age_ms, uint8_t seats)
{
    uint8_t i;

    memset(note, 0, sizeof *note);
    note->serial       = 3u;
    note->generation   = 1u;
    note->phase        = phase;
    note->what         = (uint8_t)(MP_SCENE_WHAT_LOCK | MP_SCENE_WHAT_BARS);
    note->trigger_slot = 2u;
    note->anchor[0]    = 120.5f;
    note->anchor[1]    = -34.25f;
    note->anchor[2]    = 8.0f;
    note->heading      = 1.5f;
    note->age_ms       = age_ms;
    note->seats        = seats;
    for (i = 0u; i < seats; ++i) {
        note->seat[i].slot        = (uint8_t)(i + 1u);
        note->seat[i].position[0] = 121.0f + (float)i;
        note->seat[i].position[1] = -34.0f;
        note->seat[i].position[2] = 8.0f;
    }
}

static void check_the_scene(void)
{
    mp_scene_note_t note;
    uint8_t         a[MP_SCENE_NOTE_MAX_BYTES];
    uint8_t         b[MP_SCENE_NOTE_MAX_BYTES];
    size_t          a_bytes;
    size_t          b_bytes;
    uint8_t         kind = 0;
    uint32_t        round;
    bool            agrees = true;

    ut_section("the scene's key leaves its age out, as the sender's own test for a change does");
    scene_of(&note, (uint8_t)MP_SCENE_PHASE_GATHERING, 100u, 2u);
    a_bytes = mp_scene_note_encode(&note, a, sizeof a);
    ut_check(a_bytes != 0u && mp_state_note_classify(a, a_bytes, &kind, NULL) && kind == 0xA4u,
             "an encoded scene note is a state of its own kind");
    note.age_ms = 65535u;
    b_bytes = mp_scene_note_encode(&note, b, sizeof b);
    ut_check(key_of(a, a_bytes) == key_of(b, b_bytes),
             "one a minute older, and nothing else changed, has the same key");
    note.phase = (uint8_t)MP_SCENE_PHASE_RUNNING;
    b_bytes = mp_scene_note_encode(&note, b, sizeof b);
    ut_check(key_of(a, a_bytes) != key_of(b, b_bytes), "one that went on to run has another");

    /* One question with one answer: whether two notes say the same is asked by the host's sender
     * before it repeats and by the channel before it leaves a repeat out. */
    for (round = 0u; round < 64u; ++round) {
        mp_scene_note_t other;

        scene_of(&note, (uint8_t)(round % 4u), (uint16_t)(round * 997u), (uint8_t)(round % 3u));
        scene_of(&other, (uint8_t)((round / 4u) % 4u), (uint16_t)(round * 31u),
                 (uint8_t)((round / 3u) % 3u));
        a_bytes = mp_scene_note_encode(&note, a, sizeof a);
        b_bytes = mp_scene_note_encode(&other, b, sizeof b);
        agrees  = agrees && a_bytes != 0u && b_bytes != 0u &&
                 (key_of(a, a_bytes) == key_of(b, b_bytes)) ==
                     mp_scene_note_same(a, a_bytes, b, b_bytes);
    }
    ut_check(agrees, "over 64 pairs the key and mp_scene_note_same call the same pairs equal");
}

/* ==============================================================================================
 * The crate note: whole, it is a state whose clock is the host's tick; a change note is not one.
 * ============================================================================================ */

static void crates_of(mp_crate_note_t *note, bool whole, uint32_t tick, float x)
{
    memset(note, 0, sizeof *note);
    note->tick                 = tick;
    note->level                = 57u;
    note->generation           = 2u;
    note->whole                = whole;
    note->count                = 2u;
    note->entry[0].id          = 3u;
    note->entry[0].kind        = (uint8_t)MP_CRATE_KIND_BLOCK;
    note->entry[0].position[0] = x;
    note->entry[0].position[1] = 149.5f;
    note->entry[0].position[2] = 35.5f;
    note->entry[0].pusher      = (uint8_t)MP_CRATE_NOBODY;
    note->entry[0].verdict     = (uint8_t)MP_CRATE_VERDICT_REACHED;
    note->entry[1]             = note->entry[0];
    note->entry[1].id          = 9u;
    note->entry[1].position[0] = 12.0f;
}

static void check_the_crate_note(void)
{
    mp_crate_note_t note;
    uint8_t         a[MP_CRATE_NOTE_MAX_BYTES];
    uint8_t         b[MP_CRATE_NOTE_MAX_BYTES];
    size_t          a_bytes;
    size_t          b_bytes;
    uint8_t         kind = 0;

    ut_section("the whole crate note is a state whose tick is part of it; a change note is not");
    crates_of(&note, true, 1000u, 134.5f);
    a_bytes = mp_crate_note_encode(&note, a, sizeof a);
    ut_check(a_bytes != 0u && mp_state_note_classify(a, a_bytes, &kind, NULL) && kind == 0xA5u,
             "an encoded whole note is a state of its own kind");
    crates_of(&note, true, 0x7FFFFFFFu, 134.5f);
    b_bytes = mp_crate_note_encode(&note, b, sizeof b);
    kind = 0u;
    ut_check(mp_state_note_classify(b, b_bytes, &kind, NULL) && kind == 0xA5u &&
                 key_of(a, a_bytes) != key_of(b, b_bytes),
             "one from a later tick, every block where it was, is the same kind, so it replaces "
             "the older, and has another key, so it is never left out as a repeat of it: a change "
             "note may stand between the two");
    crates_of(&note, true, 1000u, 134.75f);
    b_bytes = mp_crate_note_encode(&note, b, sizeof b);
    ut_check(key_of(a, a_bytes) != key_of(b, b_bytes), "one where a block moved has another");

    crates_of(&note, false, 1000u, 134.5f);
    b_bytes = mp_crate_note_encode(&note, b, sizeof b);
    ut_check(b_bytes != 0u && !mp_state_note_classify(b, b_bytes, &kind, NULL),
             "a change note carries only what changed and is no state");
    b[CRATE_WHOLE_BYTE] = 2u;
    ut_check(!mp_state_note_classify(b, b_bytes, &kind, NULL),
             "and a note that says neither whole nor a change is none either");
    ut_check(!mp_state_note_classify(a, CRATE_WHOLE_BYTE, &kind, NULL),
             "nor is one that ends before the byte that would say so");
}

/* ==============================================================================================
 * The host's settings: no clock at all, so the whole note is its key.
 * ============================================================================================ */

static void check_the_host_settings(void)
{
    mp_host_settings_note_t note;
    uint8_t                 a[MP_HOST_SETTINGS_BYTES];
    uint8_t                 b[MP_HOST_SETTINGS_BYTES];
    size_t                  a_bytes;
    size_t                  b_bytes;
    uint8_t                 kind = 0;

    ut_section("the host's settings are a state, and a repeat of them is a repeat");
    memset(&note, 0, sizeof note);
    note.present = (uint16_t)(1u << HOST_SETTING_VIEW_RANGE_SCALE);
    note.values[HOST_SETTING_VIEW_RANGE_SCALE] = 1.5f;
    a_bytes = mp_host_settings_encode(&note, a, sizeof a);
    b_bytes = mp_host_settings_encode(&note, b, sizeof b);
    ut_check(a_bytes != 0u && mp_state_note_classify(a, a_bytes, &kind, NULL) && kind == 0xABu,
             "an encoded note of the host's settings is a state of its own kind");
    ut_check(key_of(a, a_bytes) == key_of(b, b_bytes),
             "the same settings said twice have the same key: the second is left out on its way");
    note.values[HOST_SETTING_VIEW_RANGE_SCALE] = 2.0f;
    b_bytes = mp_host_settings_encode(&note, b, sizeof b);
    ut_check(key_of(a, a_bytes) != key_of(b, b_bytes), "and another draw distance is news");
}

int main(void)
{
    check_the_ten();
    check_the_key();
    check_the_scene();
    check_the_crate_note();
    check_the_host_settings();
    return ut_summary("mp_state_note_rule");
}
