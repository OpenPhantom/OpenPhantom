/* mp_npc_copies_host.c: the host's table of copy keys, its decision and its throttle.
 *
 * What would be silent if it were wrong: a key handed out while an actor still carries it, which
 * is two copies to every reader of the key; a copy the player rides that falls out of the table and
 * out of the cap; a grant that never ends, so its k is lost for the level; a wish refused for the
 * wrong reason; and a throttle that stops the host's own restore after a load.
 *
 * SIZE NOTE: over 600 lines. One table, one fixture: every check below grants, walks and answers
 * through the same few helpers, and the checks that tell a mutated line from the right one sit
 * beside the ones they sharpen.
 */
#include "unittest.h"

#include "mp_npc_copies.h"
#include "mp_npc_copy_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_npc_copies_t s_copies;

static void desc_bytes(uint8_t *out)
{
    npc_spawn_note_desc_t desc;

    memset(&desc, 0, sizeof desc);
    desc.source      = 3u;
    desc.position[0] = 10.0f;
    desc.position[1] = 20.0f;
    desc.position[2] = 5.0f;
    memcpy(desc.file, "trooper.baf", 11u);
    (void)mp_npc_copy_desc_put(&desc, out);
}

/* A census in which the copies listed are there, alive or not. */
static void census(bool complete, const uint32_t *live, size_t live_count, const uint32_t *dead,
                   size_t dead_count, uint32_t chain)
{
    size_t i;

    mp_npc_copies_census_begin(&s_copies);
    for (i = 0; i < live_count; ++i) {
        mp_npc_copies_census_saw(&s_copies, live[i], true);
    }
    for (i = 0; i < dead_count; ++i) {
        mp_npc_copies_census_saw(&s_copies, dead[i], false);
    }
    mp_npc_copies_census_end(&s_copies, complete, chain, 128u);
}

static void empty_census(void)
{
    census(true, NULL, 0u, NULL, 0u, 20u);
}

static mp_npc_copy_ask_t a_spawn(bool from_wire)
{
    mp_npc_copy_ask_t ask;

    memset(&ask, 0, sizeof ask);
    ask.kind              = NPC_SPAWN_WISH_SPAWN;
    ask.from_wire         = from_wire;
    ask.bucket_ok         = true;
    ask.wish_epoch        = 4u;
    ask.epoch             = 4u;
    ask.in_level          = true;
    ask.wish_level        = 99u;
    ask.level             = 99u;
    ask.wish_world        = 3u;
    ask.world             = 3u;
    ask.has_builder       = true;
    ask.description_sound = true;
    ask.cap               = 16u;
    return ask;
}

static mp_npc_copy_who_t from_client(uint8_t slot, uint64_t connection, uint32_t wish)
{
    mp_npc_copy_who_t who;

    memset(&who, 0, sizeof who);
    who.owner            = slot;
    who.owner_connection = connection;
    who.asker            = slot;
    who.asker_connection = connection;
    who.wish             = wish;
    return who;
}

static uint8_t grant_one(uint32_t *k, const mp_npc_copy_who_t *who)
{
    mp_npc_copy_ask_t     ask = a_spawn(!who->asker_own);
    mp_npc_copy_verdict_t verdict;
    uint8_t               desc[MP_NPC_COPY_DESC_BYTES];

    desc_bytes(desc);
    mp_npc_copies_decide(&s_copies, &ask, &verdict);
    if (verdict.outcome != MP_NPC_COPY_GRANT) {
        return 0u;
    }
    *k = verdict.k;
    return mp_npc_copies_grant(&s_copies, verdict.k, who, desc);
}

static void fresh(void)
{
    mp_npc_copies_init(&s_copies, 0u);
    empty_census();
}

static void check_the_decision(void)
{
    mp_npc_copy_ask_t     ask;
    mp_npc_copy_verdict_t v;

    ut_section("a wish is decided in the order a player can follow");
    fresh();

    ask            = a_spawn(false);
    ask.wish_epoch = 3u;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.outcome == MP_NPC_COPY_STALE && s_copies.counters.stale == 1u,
             "the host's own wish from another epoch gets no answer, and is counted stale");
    ask           = a_spawn(false);
    ask.bucket_ok = false;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.outcome == MP_NPC_COPY_GRANT,
             "the host's own wishes are never throttled: a restore after a load goes through");
    ask           = a_spawn(true);
    ask.bucket_ok = false;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.outcome == MP_NPC_COPY_REFUSE && v.reason == NPC_SPAWN_REFUSED_TOO_FAST,
             "a client's wish without a token is too fast");
    ask            = a_spawn(true);
    ask.wish_world = 4u;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.reason == NPC_SPAWN_REFUSED_NO_LEVEL, "a client's wish from another world");
    ask            = a_spawn(true);
    ask.wish_level = 98u;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.reason == NPC_SPAWN_REFUSED_NO_LEVEL, "or another level");
    ask            = a_spawn(false);
    ask.wish_level = 1u;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.outcome == MP_NPC_COPY_GRANT,
             "while the host's own wish is judged by its epoch, not by a level it never sent");
    ask          = a_spawn(true);
    ask.in_level = false;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.reason == NPC_SPAWN_REFUSED_NO_LEVEL, "the lobby is no level");
    ask                   = a_spawn(true);
    ask.has_builder       = false;
    ask.description_sound = false;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.reason == NPC_SPAWN_REFUSED_NO_BUILDER,
             "no builder is said before a description the wire cannot carry");
    ask                   = a_spawn(true);
    ask.description_sound = false;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.reason == NPC_SPAWN_REFUSED_UNSOUND, "then the description");
    ask     = a_spawn(true);
    ask.cap = 0u;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.reason == NPC_SPAWN_REFUSED_CAP, "then the cap");
    census(true, NULL, 0u, NULL, 0u, 112u);
    ask = a_spawn(true);
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.reason == NPC_SPAWN_REFUSED_POOL, "then the pool: 112 of 128 leaves the reserve");
    census(true, NULL, 0u, NULL, 0u, 111u);
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.outcome == MP_NPC_COPY_GRANT && v.k == 0u, "111 does not, and k 0 is first");
    {
        mp_npc_copy_who_t who = from_client(1u, 1u, 1u);
        uint8_t           desc[MP_NPC_COPY_DESC_BYTES];

        desc_bytes(desc);
        (void)mp_npc_copies_grant(&s_copies, v.k, &who, desc);
        mp_npc_copies_decide(&s_copies, &ask, &v);
        ut_check(v.reason == NPC_SPAWN_REFUSED_POOL,
                 "and a grant not yet built takes its slot: 111 and one open is 112");
        census(true, NULL, 0u, NULL, 0u, 110u);
    }
    ut_check(s_copies.counters.refused[NPC_SPAWN_REFUSED_NO_LEVEL] == 3u &&
                 s_copies.counters.throttled == 1u && s_copies.counters.from_wire == 10u,
             "each refusal counted under its reason");

    mp_npc_copies_init(&s_copies, 0u);
    ask = a_spawn(true);
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.reason == NPC_SPAWN_REFUSED_POOL,
             "before any complete census the pool is unknown, and unknown is not room");

    fresh();
    ask      = a_spawn(true);
    ask.kind = NPC_SPAWN_WISH_REMOVE_OWN;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.outcome == MP_NPC_COPY_REMOVE, "a removal passes on to be carried out");
    ask.in_level = false;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.reason == NPC_SPAWN_REFUSED_NO_LEVEL, "but not in the lobby");
    ask.in_level    = true;
    ask.has_builder = false;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.outcome == MP_NPC_COPY_REMOVE, "and needs no builder: nothing is built");
    ask      = a_spawn(false);
    ask.kind = NPC_SPAWN_WISH_REMOVE_ALL;
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.outcome == MP_NPC_COPY_REMOVE, "removing them all is a removal as well");
}

static void check_a_life(void)
{
    mp_npc_copy_who_t who = from_client(2u, 0xAAAA0001ull, 77u);
    npc_spawn_grant_t grant;
    mp_npc_copy_entry_t entry;
    uint32_t          k = 99u;
    uint32_t          duty_k;
    uint8_t           duty = 0;
    uint32_t          live[1];

    ut_section("a copy's life on the host: granted, seen, gone, resting, free");
    fresh();

    ut_check(grant_one(&k, &who) == 1u && s_copies.row[k].state == MP_NPC_COPY_GRANTED,
             "the first life of k 0 is generation 1");
    ut_check(mp_npc_copies_next_duty(&s_copies, 0xFFu, &duty_k, &duty) && duty_k == k &&
                 duty == MP_NPC_COPY_DUTY_GRANT_NOTE,
             "it owes its grant to the host's overlay first");
    ut_check(mp_npc_copies_note_for(&s_copies, k, duty, &grant) &&
                 grant.kind == NPC_SPAWN_GRANT_BUILD && grant.key == 256u &&
                 grant.generation == 1u &&
                 grant.owner == 2u && grant.wish == 0u && npc_spawn_note_grant_is_sound(&grant),
             "a sound build for key 256, owned by slot 2, answering no wish of the host's");
    mp_npc_copies_duty_done(&s_copies, k, duty, 41u);
    ut_check(!mp_npc_copies_entry_for(&s_copies, k, MP_NPC_COPY_DUTY_ANNOUNCE, 99u, 3u, &entry),
             "nobody is told of it before the census sees it alive");

    live[0] = k;
    census(true, live, 1u, NULL, 0u, 21u);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_LIVE &&
                 mp_npc_copies_next_duty(&s_copies, 0xFFu, &duty_k, &duty) &&
                 duty == MP_NPC_COPY_DUTY_ANNOUNCE,
             "seen alive it is live and owes its announcement");
    ut_check(mp_npc_copies_entry_for(&s_copies, k, duty, 99u, 3u, &entry) &&
                 entry.kind == NPC_SPAWN_GRANT_BUILD && entry.k == 0u && entry.generation == 1u &&
                 entry.owner == 2u && entry.world == 3u && entry.desc.source == 3u,
             "a build entry with the level and world it is sent under");
    mp_npc_copies_duty_done(&s_copies, k, duty, 0u);
    mp_npc_copies_take_answer(&s_copies, 41u, NPC_SPAWN_ANSWER_DONE);

    census(false, NULL, 0u, NULL, 0u, 21u);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_LIVE,
             "a census that could not walk the pool concludes nothing from not seeing it");
    census(true, NULL, 0u, live, 1u, 21u);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_ENDING &&
                 s_copies.counters.gone == 1u,
             "seen dead it is ending");
    census(true, NULL, 0u, live, 1u, 21u);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_ENDING &&
                 mp_npc_copies_count(&s_copies) == 0u,
             "and stays so while its corpse carries the key, counted against nothing");
    census(false, NULL, 0u, NULL, 0u, 21u);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_ENDING,
             "a short walk that missed the corpse does not call it gone");
    empty_census();
    ut_check(s_copies.row[k].state == MP_NPC_COPY_RESTING,
             "the corpse gone, it rests");
    ut_check(grant_one(&k, &who) == 1u && k == 1u,
             "a resting k is not handed out: the next wish gets k 1");
    for (duty_k = 0; duty_k + 1u < MP_NPC_COPIES_REST; ++duty_k) {
        empty_census();
    }
    ut_check(s_copies.row[0u].state == MP_NPC_COPY_RESTING,
             "a census short of the whole rest it still rests");
    empty_census();
    ut_check(s_copies.row[0u].state == MP_NPC_COPY_FREE,
             "after the rest k 0 is free again");
    ut_check(s_copies.row[1u].state == MP_NPC_COPY_GRANTED,
             "while k 1, granted and not seen, still has its deadline to run");
    for (duty_k = 0; duty_k <= MP_NPC_COPIES_SEEN_DEADLINE; ++duty_k) {
        empty_census();
    }
    ut_check(s_copies.row[1u].state == MP_NPC_COPY_ENDING &&
                 s_copies.counters.never_seen == 1u,
             "and k 1, never seen alive in three seconds, ended");
    ut_check(mp_npc_copies_next_duty(&s_copies, 0xFFu, &duty_k, &duty) && duty_k == 1u &&
                 duty == MP_NPC_COPY_DUTY_REFUSE_ASKER,
             "with no cancel, since its grant never reached the overlay, but a refusal owed");
    ut_check(mp_npc_copies_entry_for(&s_copies, 1u, duty, 99u, 3u, &entry) &&
                 entry.kind == NPC_SPAWN_GRANT_REFUSED && entry.serial == 77u &&
                 entry.reason == NPC_SPAWN_REFUSED_NOT_BUILT && entry.owner == 2u,
             "to the asker, for its wish 77, not built");
}

static void check_answers_and_the_ride(void)
{
    mp_npc_copy_who_t own;
    mp_npc_copy_who_t who = from_client(1u, 5u, 12u);
    uint32_t          k   = 0;
    uint32_t          live[1];
    uint32_t          duty_k;
    uint8_t           duty = 0;

    ut_section("the overlay's answers, and the copy a player rides");
    fresh();
    memset(&own, 0, sizeof own);
    own.asker_own = true;
    own.wish      = 500u;

    (void)grant_one(&k, &own);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 7u);
    mp_npc_copies_take_answer(&s_copies, 7u, NPC_SPAWN_ANSWER_REFUSED);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_ENDING &&
                 !mp_npc_copies_next_duty(&s_copies, 0xFFu, &duty_k, &duty) &&
                 s_copies.counters.not_built == 1u,
             "a grant the host's overlay refused ends, and its own wish needs no refusal");
    mp_npc_copies_take_answer(&s_copies, 7u, NPC_SPAWN_ANSWER_DONE);
    ut_check(s_copies.counters.answers_unmatched == 1u, "a second answer to it is counted, idle");

    (void)grant_one(&k, &who);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 8u);
    live[0] = k;
    census(true, live, 1u, NULL, 0u, 21u);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_ANNOUNCE, 0u);
    ut_check(mp_npc_copies_cancel_owned(&s_copies, 1u, 6u) == 0u &&
                 s_copies.counters.removals_empty == 1u,
             "removing slot 1's copies on another connection removes none");
    ut_check(mp_npc_copies_cancel_owned(&s_copies, 1u, 5u) == 1u &&
                 s_copies.row[k].state == MP_NPC_COPY_ENDING &&
                 mp_npc_copies_next_duty(&s_copies, 0xFFu, &duty_k, &duty) &&
                 duty == MP_NPC_COPY_DUTY_CANCEL_NOTE,
             "on its own connection the copy ends and owes its cancel");
    mp_npc_copies_duty_done(&s_copies, k, duty, 9u);
    census(true, live, 1u, NULL, 0u, 21u);
    ut_check(mp_npc_copies_count(&s_copies) == 1u,
             "alive while the cancel is out, it still counts");
    mp_npc_copies_take_answer(&s_copies, 9u, NPC_SPAWN_ANSWER_REFUSED);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_LIVE &&
                 s_copies.counters.ridden == 1u && mp_npc_copies_count(&s_copies) == 1u,
             "the cancel refused, the ridden copy is live again and counts against the cap");
    ut_check(mp_npc_copies_cancel_all(&s_copies) == 1u, "and a later removal finds it");

    fresh();
    (void)grant_one(&k, &who);
    (void)grant_one(&duty_k, &who);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 20u);
    mp_npc_copies_duty_done(&s_copies, duty_k, MP_NPC_COPY_DUTY_GRANT_NOTE, 21u);
    mp_npc_copies_take_answer(&s_copies, 21u, NPC_SPAWN_ANSWER_REFUSED);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_GRANTED &&
                 s_copies.row[duty_k].state == MP_NPC_COPY_ENDING,
             "an answer goes to the row that waits for its serial, not the first that waits");
}

static void check_owners_and_foreign_keys(void)
{
    mp_npc_copy_who_t who        = from_client(3u, 0x100000000ull, 1u);
    uint64_t          present[NPC_SPAWN_WORLD_SLOTS];
    uint32_t          k = 0;
    uint32_t          second = 0;
    uint32_t          live[2];
    uint8_t           owner = 9u;
    uint8_t           duty = 0;
    uint32_t          duty_k;

    ut_section("a copy whose owner left goes to the host; an unknown actor keeps its key");
    fresh();
    memset(present, 0, sizeof present);
    present[3] = 0x100000000ull;

    (void)grant_one(&k, &who);
    (void)grant_one(&second, &who);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 1u);
    mp_npc_copies_duty_done(&s_copies, second, MP_NPC_COPY_DUTY_GRANT_NOTE, 2u);
    live[0] = k;
    census(true, live, 1u, NULL, 0u, 22u);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_ANNOUNCE, 0u);
    mp_npc_copies_owners(&s_copies, present);
    ut_check(s_copies.counters.owners_changed == 0u,
             "the owner on the same 64-bit connection keeps its copies");
    present[3] = 0x200000000ull;
    mp_npc_copies_owners(&s_copies, present);
    ut_check(mp_npc_copies_owner(&s_copies, k, &owner) && owner == 0u &&
                 s_copies.row[k].duties ==
                     (MP_NPC_COPY_DUTY_OWNER_NOTE | MP_NPC_COPY_DUTY_ANNOUNCE),
             "a replacement on the same slot is somebody else: the live copy is the host's, told");
    ut_check(s_copies.row[second].duties == MP_NPC_COPY_DUTY_OWNER_NOTE,
             "while a granted one tells only the overlay; its first announcement names the host");
    ut_check(!mp_npc_copies_asker_present(&s_copies, second, present),
             "and a refusal owed to the one who left would not go to the one who came");

    fresh();
    live[0] = 0u;
    live[1] = 0u;
    census(true, live, 2u, NULL, 0u, 22u);
    ut_check(s_copies.row[0u].state == MP_NPC_COPY_FOREIGN &&
                 s_copies.counters.doubles == 1u && s_copies.counters.foreign == 1u,
             "an actor under a key nobody handed out makes it foreign; two are counted double");
    ut_check(grant_one(&k, &who) != 0u && k == 1u, "and k 0 is not handed out");
    ut_check(!mp_npc_copies_describable(&s_copies, 0u) && mp_npc_copies_describable(&s_copies, 1u),
             "nor described in a block");
    empty_census();
    ut_check(s_copies.row[0u].state == MP_NPC_COPY_RESTING,
             "once it is gone the key rests before anybody gets it");

    fresh();
    (void)grant_one(&k, &who);
    live[0] = k;
    census(true, live, 1u, NULL, 0u, 22u);
    mp_npc_copies_clear(&s_copies);
    ut_check(grant_one(&k, &who) == 0u, "a clear forgets the pool: nothing is granted blind");
    ut_check(s_copies.row[0].generation == 1u, "the clear keeps the generation it handed out");
    census(true, live, 1u, NULL, 0u, 22u);
    ut_check(s_copies.row[0u].state == MP_NPC_COPY_FOREIGN &&
                 grant_one(&k, &who) == 1u && k == 1u,
             "a copy still standing after the clear is foreign, and k 1 is handed out instead");
    ut_check(mp_npc_copies_next_duty(&s_copies, 0xFFu, &duty_k, &duty) && duty_k == 1u,
             "the new grant is the one owed");
}

static void check_the_corpse_sweep_and_generations(void)
{
    mp_npc_copy_who_t who = from_client(1u, 1u, 1u);
    uint32_t          k   = 0;
    uint32_t          dead[1];
    uint32_t          i;
    uint8_t           g = 0;

    ut_section("a copy's corpse is swept only when asked; generations wrap past 0");
    fresh();
    s_copies.corpse_censuses = 5u;
    (void)grant_one(&k, &who);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 3u);
    mp_npc_copies_take_answer(&s_copies, 3u, NPC_SPAWN_ANSWER_DONE);
    dead[0] = k;
    census(true, dead, 1u, NULL, 0u, 21u);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_ANNOUNCE, 0u);
    census(true, NULL, 0u, dead, 1u, 21u);
    for (i = 0; i < 4u; ++i) {
        census(true, NULL, 0u, dead, 1u, 21u);
    }
    ut_check(s_copies.counters.corpses_swept == 0u, "four censuses of a corpse are not five");
    census(true, NULL, 0u, dead, 1u, 21u);
    ut_check((s_copies.row[k].duties & MP_NPC_COPY_DUTY_CANCEL_NOTE) != 0u &&
                 s_copies.counters.corpses_swept == 1u,
             "after the censuses asked for, the corpse owes a cancel, once");

    fresh();
    s_copies.row[0].generation = 255u;
    ut_check(grant_one(&k, &who) == 1u && k == 0u, "after 255 comes 1, never 0");
    ut_check(mp_npc_copies_generation(&s_copies, 0u, &g) && g == 1u,
             "and that is the life the block and the relay read");
}

static void check_the_throttle(void)
{
    uint32_t now = 0xFFFFF000u;
    unsigned taken = 0;
    unsigned i;

    ut_section("four wishes at once, then one a second, across the millisecond wrap");
    mp_npc_copies_init(&s_copies, 0u);
    for (i = 0; i < 6u; ++i) {
        taken += mp_npc_copies_throttle(&s_copies, 2u, 7u, now) ? 1u : 0u;
    }
    ut_check(taken == 4u, "six at once, four through");
    ut_check(!mp_npc_copies_throttle(&s_copies, 2u, 7u, now + 999u) &&
                 mp_npc_copies_throttle(&s_copies, 2u, 7u, now + 1000u),
             "the fifth a second later, not a millisecond sooner, across the wrap");
    ut_check(mp_npc_copies_throttle(&s_copies, 2u, 8u, now + 1001u),
             "a new connection on the slot starts full");
    ut_check(mp_npc_copies_throttle(&s_copies, 3u, 7u, now + 1001u),
             "and another slot has its own bucket");
    ut_check(!mp_npc_copies_throttle(&s_copies, NPC_SPAWN_WORLD_SLOTS, 7u, now),
             "a slot past the session has none");
    taken = 0;
    for (i = 0; i < 6u; ++i) {
        taken += mp_npc_copies_throttle(&s_copies, 3u, 7u, now + 60000u) ? 1u : 0u;
    }
    ut_check(taken == 4u, "a minute of quiet saves no more than the burst");
}

static void check_what_a_walk_can_tell(void)
{
    mp_npc_copy_who_t     who = from_client(1u, 5u, 12u);
    mp_npc_copy_ask_t     ask;
    mp_npc_copy_verdict_t v;
    uint32_t              k = 0;
    uint32_t              one[1];
    uint32_t              i;

    ut_section("a short walk tells what it saw, and only a whole one what it did not");
    fresh();
    (void)grant_one(&k, &who);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 1u);
    one[0] = k;
    census(false, NULL, 0u, one, 1u, 21u);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_GRANTED &&
                 (s_copies.row[k].duties & MP_NPC_COPY_DUTY_ANNOUNCE) == 0u,
             "a granted copy seen dead is not built yet: nobody is told of a corpse");
    census(true, one, 1u, NULL, 0u, 21u);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_ANNOUNCE, 0u);
    mp_npc_copies_take_answer(&s_copies, 1u, NPC_SPAWN_ANSWER_DONE);
    census(false, NULL, 0u, one, 1u, 21u);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_ENDING &&
                 mp_npc_copies_count(&s_copies) == 0u,
             "a short walk that sees it dead ends it, and it stops counting at once");

    fresh();
    one[0] = 5u;
    census(true, one, 1u, NULL, 0u, 21u);
    census(false, NULL, 0u, NULL, 0u, 21u);
    ut_check(s_copies.row[5u].state == MP_NPC_COPY_FOREIGN,
             "a foreign key a short walk missed is still foreign");

    fresh();
    (void)grant_one(&k, &who);
    for (i = 0; i <= MP_NPC_COPIES_SEEN_DEADLINE; ++i) {
        census(false, NULL, 0u, NULL, 0u, 21u);
    }
    ut_check(s_copies.row[k].state == MP_NPC_COPY_GRANTED,
             "a grant past its deadline is not ended by walks that could not see the pool");
    empty_census();
    ut_check(s_copies.row[k].state == MP_NPC_COPY_ENDING,
             "the first whole walk ends it");

    fresh();
    (void)grant_one(&k, &who);
    one[0] = k;
    census(true, NULL, 0u, one, 1u, 111u);
    ask = a_spawn(true);
    mp_npc_copies_decide(&s_copies, &ask, &v);
    ut_check(v.outcome == MP_NPC_COPY_GRANT,
             "a grant the walk already found in the chain is not counted twice: 111 is room");

    fresh();
    (void)grant_one(&k, &who);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 2u);
    mp_npc_copies_take_answer(&s_copies, 2u, NPC_SPAWN_ANSWER_DONE);
    census(true, one, 1u, NULL, 0u, 21u);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_ANNOUNCE, 0u);
    (void)mp_npc_copies_cancel_all(&s_copies);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_CANCEL_NOTE, 3u);
    census(true, one, 1u, NULL, 0u, 21u);
    ut_check((s_copies.row[k].duties & MP_NPC_COPY_DUTY_CANCEL_NOTE) == 0u,
             "a copy alive while its cancel waits for an answer is not cancelled again");
    mp_npc_copies_take_answer(&s_copies, 3u, NPC_SPAWN_ANSWER_DONE);
    census(true, one, 1u, NULL, 0u, 21u);
    ut_check((s_copies.row[k].duties & MP_NPC_COPY_DUTY_CANCEL_NOTE) != 0u &&
                 s_copies.counters.recancelled == 1u,
             "alive after its cancel was done, it is cancelled once more");
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_CANCEL_NOTE, 4u);
    mp_npc_copies_take_answer(&s_copies, 4u, NPC_SPAWN_ANSWER_DONE);
    census(true, one, 1u, NULL, 0u, 21u);
    ut_check(s_copies.counters.recancelled == 1u && s_copies.counters.stuck == 1u &&
                 (s_copies.row[k].duties & MP_NPC_COPY_DUTY_CANCEL_NOTE) == 0u,
             "and alive after that as well it is counted stuck, not cancelled for ever");
}

static void check_grants_notes_and_late_answers(void)
{
    mp_npc_copy_who_t   who = from_client(3u, 7u, 55u);
    mp_npc_copy_entry_t entry;
    npc_spawn_grant_t   grant;
    uint64_t            present[NPC_SPAWN_WORLD_SLOTS];
    uint8_t             desc[MP_NPC_COPY_DESC_BYTES];
    uint32_t            k = 0;
    uint32_t            one[1];
    uint32_t            unmatched;
    uint8_t             owner = 9u;

    ut_section("a key is granted once, a note only when owed, a late answer read as written");
    fresh();
    desc_bytes(desc);
    memset(present, 0, sizeof present);
    present[3] = 7u;
    (void)grant_one(&k, &who);
    ut_check(mp_npc_copies_grant(&s_copies, k, &who, desc) == 0u,
             "a key granted once is not granted again");
    ut_check(!mp_npc_copies_note_for(&s_copies, k, MP_NPC_COPY_DUTY_CANCEL_NOTE, &grant) &&
                 !mp_npc_copies_entry_for(&s_copies, k, MP_NPC_COPY_DUTY_REFUSE_ASKER, 99u, 3u,
                                          &entry),
             "nor is a note or an entry made for a duty it does not owe");
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 9u);
    present[3] = 8u;
    mp_npc_copies_owners(&s_copies, present);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_OWNER_NOTE, 10u);
    mp_npc_copies_take_answer(&s_copies, 9u, NPC_SPAWN_ANSWER_REFUSED);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_ENDING,
             "an owner note written after the grant does not hide the grant's answer");
    unmatched = s_copies.counters.answers_unmatched;
    mp_npc_copies_take_answer(&s_copies, 10u, NPC_SPAWN_ANSWER_DONE);
    ut_check(s_copies.counters.answers_unmatched == unmatched &&
                 s_copies.row[k].owner_waiting == 0u,
             "and the owner note's own answer is expected");
    ut_check(mp_npc_copies_entry_for(&s_copies, k, MP_NPC_COPY_DUTY_REFUSE_ASKER, 99u, 3u,
                                     &entry) &&
                 entry.owner == 3u,
             "the refusal goes to the one who asked, though the copy is the host's by now");

    fresh();
    (void)grant_one(&k, &who);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 11u);
    mp_npc_copies_take_answer(&s_copies, 11u, NPC_SPAWN_ANSWER_LOST);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_ENDING &&
                 s_copies.counters.not_built == 1u,
             "a lost answer to a grant is no build: the census clears up if there was one");

    fresh();
    (void)grant_one(&k, &who);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 12u);
    one[0] = k;
    census(true, one, 1u, NULL, 0u, 21u);
    mp_npc_copies_take_answer(&s_copies, 12u, NPC_SPAWN_ANSWER_REFUSED);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_LIVE,
             "a late refusal of a grant the census has seen alive leaves the copy live");

    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_ANNOUNCE, 0u);
    present[3] = 7u;
    (void)mp_npc_copies_cancel_all(&s_copies);
    present[3] = 0u;
    mp_npc_copies_owners(&s_copies, present);
    ut_check(mp_npc_copies_owner(&s_copies, k, &owner) && owner == 0u,
             "a copy on its way out goes to the host as well when its owner leaves");
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_CANCEL_NOTE, 13u);
    census(true, NULL, 0u, one, 1u, 21u);
    mp_npc_copies_take_answer(&s_copies, 13u, NPC_SPAWN_ANSWER_REFUSED);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_ENDING,
             "a cancel refused for a corpse brings nothing back: only a live copy is ridden");

    fresh();
    (void)grant_one(&k, &who);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 14u);
    census(true, one, 1u, NULL, 0u, 21u);
    (void)mp_npc_copies_cancel_all(&s_copies);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_CANCEL_NOTE, 15u);
    census(true, one, 1u, NULL, 0u, 21u);
    mp_npc_copies_take_answer(&s_copies, 15u, NPC_SPAWN_ANSWER_REFUSED);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_LIVE &&
                 s_copies.row[k].duties ==
                     (MP_NPC_COPY_DUTY_OWNER_NOTE | MP_NPC_COPY_DUTY_ANNOUNCE),
             "a ridden copy nobody was told of is announced, and its asker not refused");
}

static void check_answers_against_what_was_written(void)
{
    mp_npc_copy_who_t   who = from_client(1u, 5u, 12u);
    mp_npc_copy_who_t   own;
    mp_npc_copy_wish_t  wish;
    mp_npc_copy_entry_t entry;
    npc_spawn_grant_t   grant;
    uint32_t            k      = 0;
    uint32_t            other  = 0;
    uint32_t            cursor = 0;
    uint32_t            found  = 0;
    uint32_t            live[1];
    uint8_t             duty = 0;

    ut_section("an answer is read against what was written, and every asker hears back");
    fresh();
    (void)grant_one(&k, &who);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 30u);
    (void)mp_npc_copies_cancel_owned(&s_copies, 1u, 5u);
    mp_npc_copies_take_answer(&s_copies, 30u, NPC_SPAWN_ANSWER_REFUSED);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_ENDING &&
                 mp_npc_copies_count(&s_copies) == 0u &&
                 !mp_npc_copies_next_live(&s_copies, &cursor, &found) &&
                 (s_copies.row[k].duties & MP_NPC_COPY_DUTY_CANCEL_NOTE) != 0u &&
                 s_copies.counters.not_built == 1u,
             "a grant refused under a removal leaves the row ending: no live row without a copy");

    fresh();
    (void)grant_one(&k, &who);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 60u);
    live[0] = k;
    census(true, live, 1u, NULL, 0u, 21u);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_ANNOUNCE, 0u);
    (void)mp_npc_copies_cancel_owned(&s_copies, 1u, 5u);
    mp_npc_copies_take_answer(&s_copies, 60u, NPC_SPAWN_ANSWER_LOST);
    ut_check(s_copies.row[k].state == MP_NPC_COPY_ENDING &&
                 (s_copies.row[k].duties & MP_NPC_COPY_DUTY_CANCEL_NOTE) != 0u,
             "and a lost answer to the grant of a live copy does not undo its owed cancel");

    fresh();
    memset(&own, 0, sizeof own);
    own.asker_own = true;
    own.wish      = 44u;
    (void)grant_one(&k, &own);
    (void)mp_npc_copies_cancel_all(&s_copies);
    ut_check(mp_npc_copies_next_duty(&s_copies, 0xFFu, &found, &duty) &&
                 duty == MP_NPC_COPY_DUTY_REFUSE_OWN &&
                 mp_npc_copies_note_for(&s_copies, found, duty, &grant) &&
                 grant.kind == NPC_SPAWN_GRANT_REFUSED && grant.wish == 44u &&
                 grant.reason == NPC_SPAWN_REFUSED_NOT_BUILT &&
                 npc_spawn_note_grant_is_sound(&grant),
             "an own grant dropped before it was written tells the overlay its wish was refused");

    fresh();
    who.wish = 0u;
    (void)grant_one(&k, &who);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_GRANT_NOTE, 31u);
    live[0] = k;
    census(true, live, 1u, NULL, 0u, 21u);
    census(true, NULL, 0u, live, 1u, 21u);
    ut_check(s_copies.counters.died_unannounced == 1u &&
                 (s_copies.row[k].duties & MP_NPC_COPY_DUTY_REFUSE_ASKER) != 0u &&
                 (s_copies.row[k].duties & MP_NPC_COPY_DUTY_ANNOUNCE) == 0u,
             "a copy dead before its first announcement owes its asker a refusal, serial 0 too");

    fresh();
    who.wish = 12u;
    (void)grant_one(&k, &who);
    (void)grant_one(&other, &who);
    mp_npc_copies_duty_done(&s_copies, other, MP_NPC_COPY_DUTY_GRANT_NOTE, 50u);
    live[0] = other;
    census(true, live, 1u, NULL, 0u, 22u);
    ut_check(mp_npc_copies_next_duty(&s_copies, MP_NPC_COPY_DUTIES_WIRE, &found, &duty) &&
                 found == other && duty == MP_NPC_COPY_DUTY_ANNOUNCE &&
                 mp_npc_copies_next_duty(&s_copies, MP_NPC_COPY_DUTIES_NOTE, &found, &duty) &&
                 found == k && duty == MP_NPC_COPY_DUTY_GRANT_NOTE,
             "a full note does not hold up the wire, nor a full channel the note");
    mp_npc_copies_take_answer(&s_copies, 50u, NPC_SPAWN_ANSWER_OPEN);
    mp_npc_copies_duty_done(&s_copies, k, MP_NPC_COPY_DUTY_CANCEL_NOTE, 51u);
    ut_check(s_copies.counters.answers_open == 1u && s_copies.row[other].waiting == 50u &&
                 s_copies.counters.duties_unowed == 1u && s_copies.row[k].waiting == 0u,
             "no answer is no answer, and a duty not owed is counted and changes nothing");

    memset(&wish, 0, sizeof wish);
    wish.serial = 0xABCDu;
    wish.level  = 7u;
    wish.world  = 9u;
    ut_check(mp_npc_copies_refusal_for(&wish, 3u, NPC_SPAWN_REFUSED_CAP, &entry) &&
                 entry.kind == NPC_SPAWN_GRANT_REFUSED && entry.serial == 0xABCDu &&
                 entry.owner == 3u && entry.level == 7u && entry.world == 9u &&
                 entry.reason == NPC_SPAWN_REFUSED_CAP,
             "a refusal of a client's wish carries the level and world the wish came with");
    ut_check(!mp_npc_copies_refusal_for(&wish, 3u, 0u, &entry) &&
                 !mp_npc_copies_refusal_for(&wish, NPC_SPAWN_WORLD_SLOTS, 1u, &entry),
             "and is not made without a reason or for a slot past the session");

    mp_npc_copies_init(&s_copies, 0u);
    ut_check(mp_npc_copies_throttle_answer(&s_copies, 2u, 1000u) &&
                 !mp_npc_copies_throttle_answer(&s_copies, 2u, 1999u) &&
                 mp_npc_copies_throttle_answer(&s_copies, 2u, 2000u),
             "a flood is told it is too fast once a second, not once a wish");
}

/* A grant of a copy with `behaviour`, owned by `who`. */
static uint8_t grant_behaving(uint32_t *k, const mp_npc_copy_who_t *who, uint8_t behaviour)
{
    mp_npc_copy_ask_t     ask = a_spawn(!who->asker_own);
    mp_npc_copy_verdict_t verdict;
    npc_spawn_note_desc_t desc;
    uint8_t               bytes[MP_NPC_COPY_DESC_BYTES];

    memset(&desc, 0, sizeof desc);
    desc.source      = 3u;
    desc.behaviour   = behaviour;
    desc.position[0] = 10.0f;
    memcpy(desc.file, "trooper.baf", 11u);
    (void)mp_npc_copy_desc_put(&desc, bytes);
    mp_npc_copies_decide(&s_copies, &ask, &verdict);
    if (verdict.outcome != MP_NPC_COPY_GRANT) {
        return 0u;
    }
    *k = verdict.k;
    return mp_npc_copies_grant(&s_copies, verdict.k, who, bytes);
}

static void check_whom_a_copy_goes_with(void)
{
    mp_npc_copy_who_t         client = from_client(3u, 0x300000001ull, 7u);
    mp_npc_copy_who_t         host;
    npc_spawn_anchor_record_t anchors;
    float                     point[NPC_SPAWN_WORLD_SLOTS][3];
    uint32_t                  follower = 0;
    uint32_t                  helper   = 0;
    uint32_t                  attacker = 0;
    uint32_t                  mine     = 0;
    uint8_t                   owner    = 99u;
    uint8_t                   s;

    ut_section("whom a copy goes with: its owner while the owner stands, else the host");
    fresh();
    memset(&host, 0, sizeof host);
    host.asker_own = true;
    (void)grant_behaving(&follower, &client, (uint8_t)NPC_SPAWN_BEHAVIOUR_FOLLOW);
    (void)grant_behaving(&helper, &client, (uint8_t)NPC_SPAWN_BEHAVIOUR_HELP);
    (void)grant_behaving(&attacker, &client, 2u);
    (void)grant_behaving(&mine, &host, (uint8_t)NPC_SPAWN_BEHAVIOUR_FOLLOW);
    ut_check(mp_npc_copies_follows(&s_copies, follower, &owner) && owner == 3u,
             "a following copy goes with a player, the one who asked for it");
    ut_check(mp_npc_copies_follows(&s_copies, helper, &owner) && owner == 3u,
             "and so does a helping one");
    owner = 99u;
    ut_check(!mp_npc_copies_follows(&s_copies, attacker, &owner) && owner == 99u,
             "an attacking copy goes with nobody, and is told nothing");
    ut_check(mp_npc_copies_follows(&s_copies, mine, &owner) && owner == 0u,
             "the host's own follows the host");
    ut_check(!mp_npc_copies_follows(&s_copies, 100u, &owner),
             "and a k with no life follows nobody");
    {
        mp_npc_copy_who_t handed = from_client(3u, 0x300000001ull, 8u);
        uint32_t          stander = 0;
        uint32_t          handed_k = 0;

        handed.owner = 5u;   /* asked for by slot 3, belonging to slot 5 */
        (void)grant_behaving(&handed_k, &handed, (uint8_t)NPC_SPAWN_BEHAVIOUR_FOLLOW);
        ut_check(mp_npc_copies_follows(&s_copies, handed_k, &owner) && owner == 5u,
                 "a copy goes with its owner, not with whoever asked for it");
        (void)grant_behaving(&stander, &client, 0u);
        ut_check(!mp_npc_copies_follows(&s_copies, stander, &owner),
                 "a standing copy goes with nobody");
    }
    {
        mp_npc_copy_who_t mover = from_client(2u, 0xAAAA0001ull, 77u);
        uint32_t          k     = 0;
        uint32_t          duty_k;
        uint8_t           duty  = 0;
        uint32_t          live[1];

        fresh();
        (void)grant_behaving(&k, &mover, (uint8_t)NPC_SPAWN_BEHAVIOUR_FOLLOW);
        (void)mp_npc_copies_next_duty(&s_copies, 0xFFu, &duty_k, &duty);
        mp_npc_copies_duty_done(&s_copies, k, duty, 41u);
        live[0] = k;
        census(true, live, 1u, NULL, 0u, 21u);
        (void)mp_npc_copies_next_duty(&s_copies, 0xFFu, &duty_k, &duty);
        mp_npc_copies_duty_done(&s_copies, k, duty, 0u);
        mp_npc_copies_take_answer(&s_copies, 41u, NPC_SPAWN_ANSWER_DONE);
        census(true, NULL, 0u, live, 1u, 21u);
        empty_census();
        ut_check(s_copies.row[k].state == MP_NPC_COPY_RESTING &&
                     !mp_npc_copies_follows(&s_copies, k, &owner),
                 "and a copy whose life is over, its row resting, follows nobody");
    }
    ut_check(mp_npc_copies_goes_with(3u, true) == 3u && mp_npc_copies_goes_with(3u, false) == 0u &&
                 mp_npc_copies_goes_with(0u, true) == 0u &&
                 mp_npc_copies_goes_with((uint8_t)NPC_SPAWN_WORLD_SLOTS, true) == 0u,
             "an owner who stands is gone with, one who does not hands it to the host, and a slot "
             "past the session is nobody's");

    ut_section("the flyers' anchors: each player's own point, the host's for one who is down");
    memset(point, 0, sizeof point);
    for (s = 0; s < NPC_SPAWN_WORLD_SLOTS; ++s) {
        point[s][0] = 100.0f * (float)s;
        point[s][2] = 1.0f;
    }
    mp_npc_copies_anchors((const float(*)[3])point, (uint16_t)((1u << 0) | (1u << 2)), 6u,
                          &anchors);
    ut_check(anchors.epoch == 6u && anchors.valid == 0xFFFFu,
             "with the host standing every slot has a point, under the grant record's epoch");
    ut_check(anchors.point[2][0] == 200.0f && anchors.point[0][0] == 0.0f &&
                 anchors.point[5][0] == 0.0f && anchors.point[5][2] == 1.0f,
             "a standing player's is their own, anybody else's is the host's");
    mp_npc_copies_anchors((const float(*)[3])point, (uint16_t)(1u << 2), 6u, &anchors);
    ut_check(anchors.valid == (1u << 2) && anchors.point[0][0] == 0.0f &&
                 anchors.point[0][2] == 0.0f && npc_spawn_note_publish_anchors(&anchors),
             "with no host point only a standing player's slot is valid, the rest zero, and the "
             "record is one the note carries");
}

int main(void)
{
    check_the_decision();
    check_a_life();
    check_answers_and_the_ride();
    check_owners_and_foreign_keys();
    check_the_corpse_sweep_and_generations();
    check_the_throttle();
    check_answers_against_what_was_written();
    check_what_a_walk_can_tell();
    check_grants_notes_and_late_answers();
    check_whom_a_copy_goes_with();
    return ut_summary("mp_npc_copies_host");
}
