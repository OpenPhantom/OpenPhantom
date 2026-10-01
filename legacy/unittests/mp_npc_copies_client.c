/* mp_npc_copies_client.c: the copies a client holds for its host, and the wishes it sent.
 *
 * What would be silent if it were wrong: a copy built twice under one key, because a new life was
 * handed while the old one still stood or its cancel was still owed; a copy the overlay refused,
 * handed to it again every time the host repeats it; a copy given up and then rebuilt because the
 * host's block still names its corpse, or because a life that waited behind it was forgotten when
 * the host removed it; and a wish that is never answered, or answered twice.
 */
#include "unittest.h"

#include "mp_npc_copies_client.h"
#include "mp_npc_copy_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define ME 1u

static mp_npc_copies_client_t s_client;

static npc_spawn_note_desc_t a_desc(float x)
{
    npc_spawn_note_desc_t desc;

    memset(&desc, 0, sizeof desc);
    desc.source      = 5u;
    desc.position[0] = x;
    desc.position[1] = 1.0f;
    desc.position[2] = 2.0f;
    memcpy(desc.file, "gungan.baf", 10u);
    (void)mp_npc_copy_desc_round(&desc);
    return desc;
}

static mp_npc_copy_entry_t build(uint32_t k, uint8_t generation, uint8_t owner, float x)
{
    mp_npc_copy_entry_t entry;

    memset(&entry, 0, sizeof entry);
    entry.kind       = NPC_SPAWN_GRANT_BUILD;
    entry.k          = (uint16_t)k;
    entry.generation = generation;
    entry.owner      = owner;
    entry.desc       = a_desc(x);
    return entry;
}

static void census(const uint32_t *present, size_t count)
{
    size_t i;

    mp_npc_copies_client_census_begin(&s_client);
    for (i = 0; i < count; ++i) {
        mp_npc_copies_client_census_saw(&s_client, present[i]);
    }
    mp_npc_copies_client_census_end(&s_client, true);
}

/* Writes the duty the table owes for k, and answers it with `answer` as the overlay would; with
 * ANSWER_OPEN it is written and left unanswered. */
static uint8_t serve(uint32_t k, uint32_t serial, npc_spawn_answer_t answer)
{
    uint32_t          duty_k = 0;
    uint8_t           duty   = 0;
    npc_spawn_grant_t grant;

    if (!mp_npc_copies_client_next_duty(&s_client, &duty_k, &duty) || duty_k != k ||
        !mp_npc_copies_client_note_for(&s_client, k, duty, &grant) ||
        !npc_spawn_note_grant_is_sound(&grant)) {
        return 0u;
    }
    mp_npc_copies_client_duty_done(&s_client, k, duty, serial);
    if (duty != MP_NPC_HELD_DUTY_OWNER && answer != NPC_SPAWN_ANSWER_OPEN) {
        mp_npc_copies_client_take_answer(&s_client, serial, answer);
    }
    return duty;
}

static void check_a_copy_arrives(void)
{
    mp_npc_copy_entry_t e          = build(4u, 1u, 2u, 10.0f);
    uint32_t            present[1] = {4u};

    ut_section("a build entry is handed to the overlay once, and built");
    mp_npc_copies_client_init(&s_client, 30u);

    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    ut_check(mp_npc_copies_client_state(&s_client, 4u) == MP_NPC_HELD_HANDED &&
                 s_client.held[4].wish == 0u,
             "handed, answering no wish of this machine: slot 2 owns it");
    ut_check(serve(4u, 10u, NPC_SPAWN_ANSWER_DONE) == MP_NPC_HELD_DUTY_HAND &&
                 mp_npc_copies_client_state(&s_client, 4u) == MP_NPC_HELD_BUILT,
             "the overlay built it");
    mp_npc_copies_client_parked(&s_client, 4u, 0x1234u);
    census(present, 1u);
    ut_check(mp_npc_copies_client_replica(&s_client, 4u, 1u) == 0x1234u &&
                 mp_npc_copies_client_replica(&s_client, 4u, 2u) == 0u &&
                 s_client.counters.parked == 1u,
             "its replica answers for its own life and no other");
    ut_check(mp_npc_copies_client_record(&s_client, 4u, 1u),
             "a record of its life in a block is applied to it");
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    ut_check(s_client.counters.repeats == 1u && s_client.counters.handed == 1u,
             "the repetition is counted, not handed again");
    e.owner = 0u;
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    ut_check(serve(4u, 11u, NPC_SPAWN_ANSWER_DONE) == MP_NPC_HELD_DUTY_OWNER,
             "the same life with another owner tells the overlay the owner");
    mp_npc_copies_client_take_answer(&s_client, 11u, NPC_SPAWN_ANSWER_DONE);
    ut_check(s_client.counters.answers_unmatched == 0u, "and its answer is expected");
    ut_check(!mp_npc_copies_client_record(&s_client, 4u, 2u) &&
                 mp_npc_copies_client_state(&s_client, 4u) == MP_NPC_HELD_CANCELLING &&
                 s_client.counters.given_up[MP_NPC_GIVE_UP_GENERATION] == 1u,
             "a block naming another life of k gives this one up");
}

static void check_a_new_life_waits(void)
{
    mp_npc_copy_entry_t first      = build(7u, 1u, 2u, 10.0f);
    mp_npc_copy_entry_t second     = build(7u, 2u, 2u, 30.0f);
    uint32_t            present[1] = {7u};

    ut_section("a new life of k waits until the old one is gone from this machine");
    mp_npc_copies_client_init(&s_client, 30u);

    mp_npc_copies_client_take_entry(&s_client, &first, ME);
    (void)serve(7u, 20u, NPC_SPAWN_ANSWER_DONE);
    census(present, 1u);
    mp_npc_copies_client_take_entry(&s_client, &second, ME);
    ut_check(mp_npc_copies_client_state(&s_client, 7u) == MP_NPC_HELD_CANCELLING &&
                 s_client.counters.waited == 1u,
             "the built old life is cancelled, the new one waits");
    ut_check(serve(7u, 21u, NPC_SPAWN_ANSWER_DONE) == MP_NPC_HELD_DUTY_CANCEL &&
                 mp_npc_copies_client_state(&s_client, 7u) == MP_NPC_HELD_GIVEN_UP,
             "the overlay dropped it");
    census(present, 1u);
    ut_check(mp_npc_copies_client_state(&s_client, 7u) == MP_NPC_HELD_GIVEN_UP,
             "and while an actor still carries the key, nothing new is handed");
    census(NULL, 0u);
    ut_check(mp_npc_copies_client_state(&s_client, 7u) == MP_NPC_HELD_HANDED &&
                 s_client.held[7].generation == 2u,
             "once none does, the new life is handed");

    mp_npc_copies_client_init(&s_client, 30u);
    mp_npc_copies_client_take_entry(&s_client, &first, ME);
    (void)serve(7u, 30u, NPC_SPAWN_ANSWER_OPEN);
    mp_npc_copies_client_take_entry(&s_client, &second, ME);
    mp_npc_copies_client_take_answer(&s_client, 30u, NPC_SPAWN_ANSWER_DONE);
    ut_check(mp_npc_copies_client_state(&s_client, 7u) == MP_NPC_HELD_CANCELLING &&
                 (s_client.held[7].duties & MP_NPC_HELD_DUTY_CANCEL) != 0u,
             "the answer to the build is not read as the answer to the cancel still owed");
    census(NULL, 0u);
    ut_check(mp_npc_copies_client_state(&s_client, 7u) == MP_NPC_HELD_CANCELLING,
             "and no census starts the new life over the old one's owed cancel");

    mp_npc_copies_client_init(&s_client, 30u);
    mp_npc_copies_client_take_entry(&s_client, &first, ME);
    mp_npc_copies_client_take_entry(&s_client, &second, ME);
    ut_check(mp_npc_copies_client_state(&s_client, 7u) == MP_NPC_HELD_GIVEN_UP &&
                 s_client.held[7].waiting,
             "an old life the overlay never heard of is simply dropped, the new one waits");
    census(NULL, 0u);
    ut_check(mp_npc_copies_client_state(&s_client, 7u) == MP_NPC_HELD_HANDED &&
                 s_client.held[7].generation == 2u,
             "for a complete census to find the key free");

    mp_npc_copies_client_init(&s_client, 30u);
    census(present, 1u);
    mp_npc_copies_client_take_entry(&s_client, &first, ME);
    ut_check(mp_npc_copies_client_state(&s_client, 7u) == MP_NPC_HELD_NONE &&
                 s_client.held[7].waiting,
             "a key an unknown actor carries keeps even a first life waiting");
}

static void check_giving_up(void)
{
    mp_npc_copy_entry_t e          = build(9u, 3u, 2u, 10.0f);
    mp_npc_copy_entry_t next       = build(9u, 4u, 2u, 11.0f);
    uint32_t            present[1] = {9u};
    uint32_t            i;

    ut_section("a life refused or given up is never handed again, nor one that waited behind it");
    mp_npc_copies_client_init(&s_client, 30u);

    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    (void)serve(9u, 30u, NPC_SPAWN_ANSWER_REFUSED);
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    ut_check(mp_npc_copies_client_state(&s_client, 9u) == MP_NPC_HELD_REFUSED &&
                 s_client.counters.not_built == 1u && s_client.counters.handed == 1u,
             "refused, and the repetition does not hand it again");

    mp_npc_copies_client_init(&s_client, 30u);
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    (void)serve(9u, 31u, NPC_SPAWN_ANSWER_DONE);
    census(present, 1u);
    mp_npc_copies_client_give_up(&s_client, 9u, 2u, MP_NPC_GIVE_UP_DESPAWN);
    ut_check(mp_npc_copies_client_state(&s_client, 9u) == MP_NPC_HELD_BUILT,
             "a removal of another life leaves this one");
    mp_npc_copies_client_take_entry(&s_client, &next, ME);
    mp_npc_copies_client_give_up(&s_client, 9u, 4u, MP_NPC_GIVE_UP_DESPAWN);
    ut_check(!s_client.held[9].waiting && s_client.counters.waiting_dropped == 1u,
             "a removal of the life that waits behind it drops that one");
    ut_check(serve(9u, 32u, NPC_SPAWN_ANSWER_REFUSED) == MP_NPC_HELD_DUTY_CANCEL,
             "while the old one's cancel goes out, and the player rides it");
    for (i = 0; i < MP_NPC_COPIES_CANCEL_RETRY_BLOCKS; ++i) {
        mp_npc_copies_client_block(&s_client, NULL, 0u);
    }
    ut_check(s_client.counters.cancel_retries == 1u &&
                 serve(9u, 33u, NPC_SPAWN_ANSWER_DONE) == MP_NPC_HELD_DUTY_CANCEL,
             "a cancel refused is asked again after a while");
    census(NULL, 0u);
    ut_check(mp_npc_copies_client_state(&s_client, 9u) == MP_NPC_HELD_GIVEN_UP,
             "and once it is gone nothing is handed: the waiting life went with the removal");

    mp_npc_copies_client_init(&s_client, 30u);
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    (void)serve(9u, 34u, NPC_SPAWN_ANSWER_DONE);
    census(NULL, 0u);
    ut_check(mp_npc_copies_client_state(&s_client, 9u) == MP_NPC_HELD_GIVEN_UP &&
                 s_client.counters.lost_here == 1u,
             "built, and then no actor carries the key: lost here, and not rebuilt");
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    ut_check(mp_npc_copies_client_state(&s_client, 9u) == MP_NPC_HELD_GIVEN_UP,
             "not even when the host names that life again");
}

static void check_orphans_and_release(void)
{
    mp_npc_copy_entry_t e          = build(12u, 1u, 2u, 10.0f);
    uint8_t             bitmap[16];
    uint32_t            present[1] = {12u};
    uint32_t            i;

    ut_section("a copy the host's blocks stop naming is an orphan; a release gives up all");
    mp_npc_copies_client_init(&s_client, 5u);
    memset(bitmap, 0, sizeof bitmap);
    bitmap[1] = 0x10u;   /* k 12 */

    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    (void)serve(12u, 40u, NPC_SPAWN_ANSWER_DONE);
    census(present, 1u);
    for (i = 0; i < 4u; ++i) {
        mp_npc_copies_client_block(&s_client, NULL, 0u);
    }
    mp_npc_copies_client_block(&s_client, bitmap, sizeof bitmap);
    for (i = 0; i < 4u; ++i) {
        mp_npc_copies_client_block(&s_client, bitmap, 1u);
    }
    ut_check(mp_npc_copies_client_state(&s_client, 12u) == MP_NPC_HELD_BUILT,
             "four blocks without it, then one naming it, start the count again");
    mp_npc_copies_client_block(&s_client, bitmap, 1u);
    ut_check(mp_npc_copies_client_state(&s_client, 12u) == MP_NPC_HELD_CANCELLING &&
                 s_client.counters.given_up[MP_NPC_GIVE_UP_ORPHAN] == 1u,
             "five in a row without it, and a bitmap too short to hold its bit is without it");

    mp_npc_copies_client_init(&s_client, 5u);
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    e.k = 13u;
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    (void)serve(12u, 41u, NPC_SPAWN_ANSWER_DONE);
    mp_npc_copies_client_release_all(&s_client);
    ut_check(mp_npc_copies_client_state(&s_client, 12u) == MP_NPC_HELD_CANCELLING &&
                 mp_npc_copies_client_state(&s_client, 13u) == MP_NPC_HELD_GIVEN_UP &&
                 s_client.counters.given_up[MP_NPC_GIVE_UP_RELEASE] == 2u,
             "a release cancels what was built and drops what the overlay never heard of");
    mp_npc_copies_client_clear(&s_client);
    ut_check(mp_npc_copies_client_state(&s_client, 12u) == MP_NPC_HELD_NONE,
             "an epoch clears the table");
    census(present, 1u);
    mp_npc_copies_client_clear(&s_client);
    e.k = 12u;
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    ut_check(mp_npc_copies_client_state(&s_client, 12u) == MP_NPC_HELD_NONE &&
                 s_client.held[12].waiting,
             "but not what the census saw: an actor still carrying the key keeps a new life "
             "waiting");
}

static void check_the_wishes(void)
{
    mp_npc_copy_entry_t   e    = build(20u, 1u, ME, 10.0f);
    mp_npc_copy_entry_t   refusal;
    npc_spawn_note_desc_t desc = a_desc(10.0f);
    uint8_t               bytes[MP_NPC_COPY_DESC_BYTES];
    npc_spawn_grant_t     grant;
    mp_npc_refusal_t      said;
    uint32_t              i;

    ut_section("every wish of this machine gets one answer, or one said here");
    mp_npc_copies_client_init(&s_client, 30u);
    (void)mp_npc_copy_desc_put(&desc, bytes);

    ut_check(mp_npc_copies_client_open_wish(&s_client, 0x10005u, NPC_SPAWN_WISH_SPAWN, bytes) ==
                 0x0005u,
             "a wish travels under the low sixteen bits of its serial");
    (void)mp_npc_copies_client_open_wish(&s_client, 0x10006u, NPC_SPAWN_WISH_SPAWN, bytes);
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    ut_check(mp_npc_copies_client_note_for(&s_client, 20u, MP_NPC_HELD_DUTY_HAND, &grant) &&
                 grant.wish == 0x10005u,
             "the first build of this machine's copy answers the oldest match, in its note");
    e = build(21u, 1u, 2u, 10.0f);
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    ut_check(s_client.held[21].wish == 0u, "another player's copy of the same answers nothing");
    e = build(22u, 1u, ME, 12.0f);
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    ut_check(s_client.held[22].wish == 0u,
             "nor does this machine's copy of another description");

    memset(&refusal, 0, sizeof refusal);
    refusal.kind   = NPC_SPAWN_GRANT_REFUSED;
    refusal.serial = 0x0006u;
    refusal.reason = NPC_SPAWN_REFUSED_CAP;
    refusal.owner  = 2u;
    ut_check(!mp_npc_copies_client_take_refusal(&s_client, &refusal, ME) &&
                 s_client.counters.refusals_not_mine == 1u,
             "a refusal for another slot is not this machine's");
    refusal.owner = ME;
    ut_check(mp_npc_copies_client_take_refusal(&s_client, &refusal, ME) &&
                 mp_npc_copies_client_next_refusal(&s_client, &said) && said.note == 0x10006u &&
                 said.reason == NPC_SPAWN_REFUSED_CAP,
             "one for this slot answers the wish with that wire serial, with its reason");
    ut_check(!mp_npc_copies_client_take_refusal(&s_client, &refusal, ME) &&
                 s_client.counters.refusals_unmatched == 1u,
             "and only once");

    mp_npc_copies_client_give_up(&s_client, 20u, 0u, MP_NPC_GIVE_UP_TOMBSTONE);
    ut_check(mp_npc_copies_client_next_refusal(&s_client, &said) && said.note == 0x10005u &&
                 said.reason == NPC_SPAWN_REFUSED_NOT_BUILT,
             "a build that answered a wish and is dropped unwritten answers it with a refusal");

    (void)mp_npc_copies_client_open_wish(&s_client, 7u, NPC_SPAWN_WISH_REMOVE_OWN, NULL);
    (void)mp_npc_copies_client_open_wish(&s_client, 8u, NPC_SPAWN_WISH_SPAWN, bytes);
    for (i = 0; i < MP_NPC_COPIES_WISH_DEADLINE; ++i) {
        mp_npc_copies_client_census_begin(&s_client);
        mp_npc_copies_client_census_end(&s_client, true);
    }
    ut_check(s_client.counters.wishes_expired == 2u &&
                 mp_npc_copies_client_next_refusal(&s_client, &said) && said.note == 8u &&
                 !mp_npc_copies_client_next_refusal(&s_client, &said),
             "a spawn that runs out is refused here; a removal, answered by being done, is not");
    for (i = 0; i < MP_NPC_COPIES_CLIENT_WISHES + 1u; ++i) {
        (void)mp_npc_copies_client_open_wish(&s_client, 100u + i, NPC_SPAWN_WISH_SPAWN, bytes);
    }
    ut_check(s_client.counters.wishes_pushed_out == 1u &&
                 mp_npc_copies_client_next_refusal(&s_client, &said) && said.note == 100u,
             "a seventeenth open wish pushes the oldest out, and it is refused here");

    mp_npc_copies_client_take_answer(&s_client, 999u, NPC_SPAWN_ANSWER_OPEN);
    mp_npc_copies_client_duty_done(&s_client, 50u, MP_NPC_HELD_DUTY_CANCEL, 998u);
    ut_check(s_client.counters.answers_open == 1u && s_client.counters.duties_unowed == 1u,
             "no answer is no answer, and a duty not owed is counted");
}

static void short_census(void)
{
    mp_npc_copies_client_census_begin(&s_client);
    mp_npc_copies_client_census_end(&s_client, false);
}

static mp_npc_copy_entry_t refusal_of(uint16_t serial)
{
    mp_npc_copy_entry_t entry;

    memset(&entry, 0, sizeof entry);
    entry.kind   = NPC_SPAWN_GRANT_REFUSED;
    entry.serial = serial;
    entry.reason = NPC_SPAWN_REFUSED_CAP;
    entry.owner  = ME;
    return entry;
}

static void check_the_answers_of_the_overlay(void)
{
    mp_npc_copy_entry_t e          = build(30u, 1u, 2u, 10.0f);
    mp_npc_copy_entry_t next       = build(30u, 2u, 2u, 11.0f);
    uint32_t            present[1] = {30u};
    uint32_t            i;

    ut_section("each answer of the overlay is read against what it answers");
    mp_npc_copies_client_init(&s_client, 30u);
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    (void)serve(30u, 60u, NPC_SPAWN_ANSWER_OPEN);
    mp_npc_copies_client_take_answer(&s_client, 60u, NPC_SPAWN_ANSWER_OPEN);
    ut_check(mp_npc_copies_client_state(&s_client, 30u) == MP_NPC_HELD_HANDED &&
                 !mp_npc_copies_client_record(&s_client, 30u, 1u),
             "no answer yet leaves it handed, and a handed life takes no record");
    mp_npc_copies_client_give_up(&s_client, 30u, 0u, MP_NPC_GIVE_UP_DESPAWN);
    mp_npc_copies_client_take_answer(&s_client, 60u, NPC_SPAWN_ANSWER_REFUSED);
    ut_check(mp_npc_copies_client_state(&s_client, 30u) == MP_NPC_HELD_GIVEN_UP &&
                 s_client.held[30].duties == 0u,
             "a build refused before its cancel was written needs no cancel");

    mp_npc_copies_client_init(&s_client, 30u);
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    (void)serve(30u, 61u, NPC_SPAWN_ANSWER_OPEN);
    mp_npc_copies_client_give_up(&s_client, 30u, 0u, MP_NPC_GIVE_UP_DESPAWN);
    (void)serve(30u, 62u, NPC_SPAWN_ANSWER_OPEN);
    mp_npc_copies_client_take_answer(&s_client, 61u, NPC_SPAWN_ANSWER_REFUSED);
    ut_check(mp_npc_copies_client_state(&s_client, 30u) == MP_NPC_HELD_CANCELLING,
             "once the cancel is written, it waits for the cancel's own answer");
    for (i = 0; i <= MP_NPC_COPIES_CANCEL_RETRY_BLOCKS; ++i) {
        mp_npc_copies_client_block(&s_client, NULL, 0u);
    }
    ut_check(s_client.counters.cancel_retries == 0u,
             "and a cancel not answered yet is not asked again");
    mp_npc_copies_client_take_answer(&s_client, 62u, NPC_SPAWN_ANSWER_LOST);
    ut_check(mp_npc_copies_client_state(&s_client, 30u) == MP_NPC_HELD_CANCELLING &&
                 s_client.held[30].cancel_refused,
             "a lost answer to a cancel is not a removal: it is asked again later");

    mp_npc_copies_client_init(&s_client, 30u);
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    (void)serve(30u, 63u, NPC_SPAWN_ANSWER_LOST);
    ut_check(mp_npc_copies_client_state(&s_client, 30u) == MP_NPC_HELD_BUILT,
             "a lost answer to a build counts as built: the census tells if it is not");
    mp_npc_copies_client_take_answer(&s_client, 63u, NPC_SPAWN_ANSWER_REFUSED);
    ut_check(mp_npc_copies_client_state(&s_client, 30u) == MP_NPC_HELD_BUILT &&
                 s_client.counters.answers_unmatched == 1u,
             "and a second answer to it changes nothing");
    census(present, 1u);
    mp_npc_copies_client_take_entry(&s_client, &next, ME);
    mp_npc_copies_client_take_entry(&s_client, &next, ME);
    ut_check(s_client.counters.waited == 1u && s_client.counters.repeats == 1u,
             "a life that waits, named again, is a repetition");
    mp_npc_copies_client_give_up(&s_client, 30u, 1u, MP_NPC_GIVE_UP_DESPAWN);
    ut_check(s_client.held[30].waiting,
             "a removal of the old life leaves the life that waits behind it");

    mp_npc_copies_client_init(&s_client, 30u);
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    (void)serve(30u, 64u, NPC_SPAWN_ANSWER_REFUSED);
    mp_npc_copies_client_take_entry(&s_client, &next, ME);
    ut_check(mp_npc_copies_client_state(&s_client, 30u) == MP_NPC_HELD_REFUSED &&
                 s_client.held[30].waiting,
             "a new life of a refused key waits for a whole census like any other");
    mp_npc_copies_client_give_up(&s_client, 30u, 1u, MP_NPC_GIVE_UP_TOMBSTONE);
    ut_check(mp_npc_copies_client_state(&s_client, 30u) == MP_NPC_HELD_GIVEN_UP &&
                 s_client.counters.given_up[MP_NPC_GIVE_UP_TOMBSTONE] == 1u,
             "and the refused life, given up, is counted under why");
}

static void check_short_walks_and_the_owner(void)
{
    mp_npc_copy_entry_t e          = build(40u, 1u, 2u, 10.0f);
    mp_npc_copy_entry_t other      = build(41u, 1u, 2u, 10.0f);
    uint32_t            present[1] = {40u};
    npc_spawn_grant_t   grant;

    ut_section("a short walk forgets nothing; an owner change rides the hand still owed");
    mp_npc_copies_client_init(&s_client, 30u);
    census(present, 1u);
    short_census();
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    ut_check(mp_npc_copies_client_state(&s_client, 40u) == MP_NPC_HELD_NONE &&
                 s_client.held[40].waiting,
             "an actor a short walk missed still holds its key");
    mp_npc_copies_client_take_entry(&s_client, &other, ME);
    (void)serve(41u, 70u, NPC_SPAWN_ANSWER_DONE);
    short_census();
    ut_check(mp_npc_copies_client_state(&s_client, 41u) == MP_NPC_HELD_BUILT,
             "and a copy built since the last whole census is not lost to a short one");

    mp_npc_copies_client_init(&s_client, 30u);
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    e.owner = 0u;
    mp_npc_copies_client_take_entry(&s_client, &e, ME);
    ut_check(s_client.held[40].duties == MP_NPC_HELD_DUTY_HAND &&
                 mp_npc_copies_client_note_for(&s_client, 40u, MP_NPC_HELD_DUTY_HAND, &grant) &&
                 grant.owner == 0u,
             "an owner change before the hand is written rides in the hand, not in a note");
}

static void check_the_queues(void)
{
    npc_spawn_note_desc_t desc = a_desc(10.0f);
    mp_npc_copy_entry_t   refusal;
    mp_npc_refusal_t      said;
    uint8_t               bytes[MP_NPC_COPY_DESC_BYTES];
    uint32_t              i;

    ut_section("the wishes and the refusals keep their order and their bounds");
    mp_npc_copies_client_init(&s_client, 30u);
    (void)mp_npc_copy_desc_put(&desc, bytes);
    (void)mp_npc_copies_client_open_wish(&s_client, 0x20007u, NPC_SPAWN_WISH_SPAWN, bytes);
    (void)mp_npc_copies_client_open_wish(&s_client, 0x20008u, NPC_SPAWN_WISH_SPAWN, bytes);
    refusal = refusal_of(7u);
    (void)mp_npc_copies_client_take_refusal(&s_client, &refusal, ME);
    refusal = refusal_of(8u);
    (void)mp_npc_copies_client_take_refusal(&s_client, &refusal, ME);
    ut_check(mp_npc_copies_client_next_refusal(&s_client, &said) && said.note == 0x20007u &&
                 mp_npc_copies_client_next_refusal(&s_client, &said) && said.note == 0x20008u,
             "refusals come out in the order they were owed, with the whole serial");

    for (i = 0; i <= MP_NPC_COPIES_CLIENT_REFUSALS; ++i) {
        (void)mp_npc_copies_client_open_wish(&s_client, 1000u + i, NPC_SPAWN_WISH_SPAWN, bytes);
        refusal = refusal_of((uint16_t)(1000u + i));
        (void)mp_npc_copies_client_take_refusal(&s_client, &refusal, ME);
    }
    ut_check(s_client.refusals == MP_NPC_COPIES_CLIENT_REFUSALS &&
                 s_client.counters.refusals_overflow == 1u,
             "a full queue counts the one it cannot hold");
    mp_npc_copies_client_clear(&s_client);
    ut_check(!mp_npc_copies_client_next_refusal(&s_client, &said),
             "and an epoch empties it");

    mp_npc_copies_client_init(&s_client, 30u);
    (void)mp_npc_copies_client_open_wish(&s_client, 200u, NPC_SPAWN_WISH_SPAWN, bytes);
    mp_npc_copies_client_census_begin(&s_client);
    mp_npc_copies_client_census_end(&s_client, true);
    ut_check(s_client.counters.wishes_expired == 0u, "a wish outlives a census");
    for (i = 1; i < MP_NPC_COPIES_CLIENT_WISHES; ++i) {
        (void)mp_npc_copies_client_open_wish(&s_client, 200u + i, NPC_SPAWN_WISH_SPAWN, bytes);
    }
    (void)mp_npc_copies_client_open_wish(&s_client, 300u, NPC_SPAWN_WISH_SPAWN, bytes);
    ut_check(mp_npc_copies_client_next_refusal(&s_client, &said) && said.note == 200u,
             "a full table pushes out the wish nearest its deadline, the oldest");
}

int main(void)
{
    check_a_copy_arrives();
    check_a_new_life_waits();
    check_giving_up();
    check_orphans_and_release();
    check_the_wishes();
    check_the_answers_of_the_overlay();
    check_short_walks_and_the_owner();
    check_the_queues();
    return ut_summary("mp_npc_copies_client");
}
