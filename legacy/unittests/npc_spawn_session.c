/* npc_spawn_session.c: the panel's half of the spawn contract, with the engine faked.
 *
 * What would be silent if it were wrong: a grant answered out of order, or answered while a load
 * holds every grant, so the multiplayer reads "not built" for a copy that is built a frame later;
 * a late cancel that removes the newer copy under the same key; a restored copy raised at the
 * spawn point instead of where the save had it; a wish of the last world left standing in the
 * record after the epoch moved; and a client that keeps its host's copies after the session.
 *
 * SIZE NOTE: over 600 lines. One fixture, the grant record and the faked engine: every check
 * below writes grants and reads answers through the same few helpers, and the sharper checks
 * sit beside the ones they sharpen.
 */
#include "unittest.h"

#include "npc_spawn_session.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static npc_spawn_session_t      s_session;
static npc_spawn_grant_record_t s_grants;

typedef struct fake {
    npc_spawn_outcome_t build_says;
    npc_spawn_outcome_t remove_says;
    uint32_t            builds;
    uint32_t            removes;
    uint32_t            last_key;
    bool                last_saved;
    float               last_x;
    uint32_t            refusals;
    uint32_t            refused_wish;
    uint8_t             refused_kind;
    uint8_t             refused_reason;
} fake_t;

static fake_t s_fake;

static npc_spawn_outcome_t fake_build(void *user, const npc_spawn_desc_t *desc, uint32_t key,
                                      const npc_spawn_saved_t *saved)
{
    (void)user;
    ++s_fake.builds;
    s_fake.last_key   = key;
    s_fake.last_saved = saved != NULL;
    s_fake.last_x     = saved != NULL ? saved->position[0] : desc->position[0];
    return s_fake.build_says;
}

static npc_spawn_outcome_t fake_remove(void *user, uint32_t key)
{
    (void)user;
    ++s_fake.removes;
    s_fake.last_key = key;
    return s_fake.remove_says;
}

static void fake_refused(void *user, uint32_t wish, uint8_t kind, uint8_t reason)
{
    (void)user;
    ++s_fake.refusals;
    s_fake.refused_wish   = wish;
    s_fake.refused_kind   = kind;
    s_fake.refused_reason = reason;
}

static const npc_spawn_session_ops_t OPS = { &fake_build, &fake_remove, NULL, &fake_refused };

static npc_spawn_desc_t a_desc(float x)
{
    npc_spawn_desc_t desc;

    memset(&desc, 0, sizeof desc);
    desc.source      = 7u;
    desc.behaviour   = 1u;
    desc.position[0] = x;
    memcpy(desc.file, "ddroid.baf", 10u);
    return desc;
}

static void fresh(uint8_t own_slot)
{
    npc_spawn_session_init(&s_session, NPC_SPAWN_PANEL_BUILDER);
    memset(&s_grants, 0, sizeof s_grants);
    memset(&s_fake, 0, sizeof s_fake);
    s_grants.first    = 1u;
    s_grants.epoch    = 1u;
    s_grants.cap      = own_slot == 0u ? 16u : 0u;
    s_grants.own_slot = own_slot;
}

/* The multiplayer writes a grant of `kind` for k under life `generation`, answering `wish`. */
static uint32_t grant(uint8_t kind, uint32_t k, uint8_t generation, uint32_t wish)
{
    npc_spawn_grant_t g;
    uint32_t          serial;

    (void)npc_spawn_note_grants_drop(&s_grants, s_session.record.grants_done);
    serial = s_grants.first + s_grants.count;
    memset(&g, 0, sizeof g);
    g.kind = kind;
    g.wish = wish;
    if (kind == NPC_SPAWN_GRANT_REFUSED) {
        g.reason = NPC_SPAWN_REFUSED_CAP;
    } else {
        g.key        = (uint16_t)(NPC_SPAWN_KEY_FIRST + k);
        g.generation = generation;
    }
    if (kind == NPC_SPAWN_GRANT_BUILD) {
        npc_spawn_desc_t desc = a_desc(40.0f);

        (void)npc_spawn_session_to_note(&desc, &g.desc);
    }
    (void)npc_spawn_note_grants_append(&s_grants, serial, &g);
    return serial;
}

/* The same grant written behind the ones already answered, as the multiplayer does until it next
 * reads the panel's record: nothing is dropped first. */
static uint32_t grant_behind(uint8_t kind, uint32_t k, uint8_t generation)
{
    npc_spawn_grant_t g;
    uint32_t          serial = s_grants.first + s_grants.count;

    memset(&g, 0, sizeof g);
    g.kind       = kind;
    g.key        = (uint16_t)(NPC_SPAWN_KEY_FIRST + k);
    g.generation = generation;
    if (kind == NPC_SPAWN_GRANT_BUILD) {
        npc_spawn_desc_t desc = a_desc(40.0f);

        (void)npc_spawn_session_to_note(&desc, &g.desc);
    }
    (void)npc_spawn_note_grants_append(&s_grants, serial, &g);
    return serial;
}

/* A new epoch as the multiplayer writes one: the ring emptied, the serials going on. */
static void new_epoch(void)
{
    ++s_grants.epoch;
    npc_spawn_note_grants_restart(&s_grants);
}

static bool frame(bool now)
{
    return npc_spawn_session_frame(&s_session, &s_grants, now, &OPS);
}

static void check_the_descriptions(void)
{
    npc_spawn_desc_t      desc = a_desc(12.5f);
    npc_spawn_desc_t      back;
    npc_spawn_note_desc_t note;

    ut_section("the panel's description and the contract's, both ways");
    desc.archive = true;
    ut_check(npc_spawn_session_to_note(&desc, &note) && note.flags == NPC_SPAWN_DESC_ARCHIVE &&
                 npc_spawn_session_from_note(&note, &back) && back.archive &&
                 strcmp(back.file, "ddroid.baf") == 0 && back.position[0] == 12.5f &&
                 back.source == 7u && back.behaviour == 1u,
             "an archive kind goes out and comes back whole");
    memcpy(desc.file, "thirteenchars", 14u);
    ut_check(!npc_spawn_session_to_note(&desc, &note), "a name past twelve does not travel");
}

static void check_the_wishes(void)
{
    npc_spawn_desc_t desc = a_desc(10.0f);
    uint32_t         i;

    ut_section("wishes wait behind the six places, in order, stamped with the epoch read");
    fresh(0u);
    ut_check(!npc_spawn_session_active(&s_session) && s_session.changed &&
                 s_session.record.panel == NPC_SPAWN_PANEL_BUILDER,
             "before any grant record the copies are not run, and the record waits to go out");
    (void)frame(true);
    ut_check(npc_spawn_session_active(&s_session) && !npc_spawn_session_is_client(&s_session),
             "a host that names its cap runs them");
    for (i = 0; i < 8u; ++i) {
        (void)npc_spawn_session_wish(&s_session, NPC_SPAWN_WISH_SPAWN, &desc, NULL);
    }
    ut_check(s_session.record.count == NPC_SPAWN_WISH_SLOTS && s_session.queued == 2u &&
                 s_session.record.entry[0].epoch == 1u && s_session.record.first == 1u,
             "eight wishes: six in the record from serial 1, two behind it");
    s_grants.wishes_taken = 3u;
    (void)frame(true);
    ut_check(s_session.record.first == 4u && s_session.record.count == 5u &&
                 s_session.queued == 0u,
             "the three taken leave, and the two waiting take their places");
}

static void check_the_grants(void)
{
    uint32_t serial;

    ut_section("grants are answered in order, and a held frame answers none");
    fresh(0u);
    serial = grant(NPC_SPAWN_GRANT_BUILD, 3u, 2u, 0u);
    (void)frame(false);
    ut_check(s_fake.builds == 0u && s_session.record.grants_done == 0u &&
                 s_session.counters.held_back == 1u,
             "while a load holds the grants nothing is built and nothing answered");
    s_fake.build_says = NPC_SPAWN_OUTCOME_DONE;
    (void)frame(true);
    ut_check(s_fake.builds == 1u && s_fake.last_key == NPC_SPAWN_KEY_FIRST + 3u &&
                 npc_spawn_note_answer(&s_session.record, serial) == NPC_SPAWN_ANSWER_DONE &&
                 s_session.built[3] == 2u,
             "then built under the host's key, answered done, and its life remembered");
    serial = grant(NPC_SPAWN_GRANT_CANCEL, 3u, 1u, 0u);
    (void)frame(true);
    ut_check(s_fake.removes == 0u && s_session.counters.cancel_stale == 1u &&
                 npc_spawn_note_answer(&s_session.record, serial) == NPC_SPAWN_ANSWER_DONE,
             "a cancel of another life touches nothing and is done");
    s_fake.remove_says = NPC_SPAWN_OUTCOME_REFUSED;
    serial             = grant(NPC_SPAWN_GRANT_CANCEL, 3u, 2u, 0u);
    (void)frame(true);
    ut_check(s_fake.removes == 1u &&
                 npc_spawn_note_answer(&s_session.record, serial) == NPC_SPAWN_ANSWER_REFUSED &&
                 s_session.built[3] == 2u,
             "a cancel of the copy the player rides is refused, and the copy stays");
    s_fake.remove_says = NPC_SPAWN_OUTCOME_DONE;
    serial             = grant(NPC_SPAWN_GRANT_CANCEL, 3u, 2u, 0u);
    (void)frame(true);
    ut_check(s_fake.removes == 2u && s_session.built[3] == 0u &&
                 npc_spawn_note_answer(&s_session.record, serial) == NPC_SPAWN_ANSWER_DONE,
             "asked again once the player is off, it goes");
    s_fake.build_says = NPC_SPAWN_OUTCOME_NOT_NOW;
    serial            = grant(NPC_SPAWN_GRANT_BUILD, 4u, 1u, 0u);
    (void)grant(NPC_SPAWN_GRANT_OWNER, 3u, 2u, 0u);
    (void)frame(true);
    ut_check(npc_spawn_note_answer(&s_session.record, serial) == NPC_SPAWN_ANSWER_OPEN &&
                 s_session.counters.owners == 0u,
             "a build the engine cannot do this frame holds it and every grant behind it");
    s_fake.build_says = NPC_SPAWN_OUTCOME_REFUSED;
    (void)frame(true);
    ut_check(npc_spawn_note_answer(&s_session.record, serial) == NPC_SPAWN_ANSWER_REFUSED &&
                 s_session.counters.owners == 1u,
             "a build the engine refuses is answered refused, and the owner change after it done");
}

static void check_a_restore(void)
{
    npc_spawn_desc_t  desc = a_desc(10.0f);
    npc_spawn_saved_t saved;
    uint32_t          wish;

    ut_section("a restored copy is raised where the save had it");
    fresh(0u);
    (void)frame(true);
    memset(&saved, 0, sizeof saved);
    saved.desc        = desc;
    saved.position[0] = 77.0f;
    saved.health      = 40;
    ut_check(npc_spawn_session_wish(&s_session, NPC_SPAWN_WISH_RESTORE, &desc, &saved),
             "a load's copy on the host becomes a restore wish");
    wish              = s_session.record.first;
    s_fake.build_says = NPC_SPAWN_OUTCOME_DONE;
    (void)grant(NPC_SPAWN_GRANT_BUILD, 5u, 1u, wish);
    (void)frame(true);
    ut_check(s_fake.last_saved && s_fake.last_x == 77.0f &&
                 s_session.counters.restores_placed == 1u,
             "the grant answering it builds with how the copy stood");
    (void)grant(NPC_SPAWN_GRANT_BUILD, 6u, 1u, wish);
    (void)frame(true);
    ut_check(!s_fake.last_saved, "and the saved state is used once");
}

static void check_a_new_epoch(void)
{
    npc_spawn_desc_t desc = a_desc(10.0f);
    uint32_t         i;

    ut_section("a new epoch ends the old wishes, and a client leaves its host's copies behind");
    fresh(2u);
    (void)frame(true);
    ut_check(npc_spawn_session_active(&s_session) && npc_spawn_session_is_client(&s_session),
             "a socket client names its slot");
    s_fake.build_says = NPC_SPAWN_OUTCOME_DONE;
    (void)grant(NPC_SPAWN_GRANT_BUILD, 8u, 3u, 0u);
    (void)frame(true);
    for (i = 0; i < 8u; ++i) {
        (void)npc_spawn_session_wish(&s_session, NPC_SPAWN_WISH_SPAWN, &desc, NULL);
    }
    s_grants.epoch = 2u;
    npc_spawn_note_grants_restart(&s_grants);
    s_fake.removes     = 0u;
    s_fake.remove_says = NPC_SPAWN_OUTCOME_DONE;
    (void)frame(true);
    ut_check(s_session.record.count == 0u && s_session.queued == 0u &&
                 s_session.counters.ended == 8u,
             "every wish of the old epoch ends, sent or queued");
    ut_check(s_fake.removes == 1u && s_fake.last_key == NPC_SPAWN_KEY_FIRST + 8u &&
                 s_session.built[8] == 0u && s_session.counters.old_removed == 1u,
             "and the client's copy of the old world is removed");
    ut_check(npc_spawn_session_wish(&s_session, NPC_SPAWN_WISH_SPAWN, &desc, NULL) &&
                 s_session.record.entry[s_session.record.count - 1u].epoch == 2u,
             "a new wish carries the new epoch");
    s_grants.own_slot = 0u;
    s_grants.cap      = 0u;
    (void)frame(true);
    ut_check(!npc_spawn_session_active(&s_session),
             "and a record naming neither cap nor slot runs no copies: the panel is its own again");
}

static void check_what_k13_asked(void)
{
    npc_spawn_desc_t  desc = a_desc(10.0f);
    npc_spawn_saved_t saved;
    uint32_t          serial;
    uint32_t          wish;
    uint32_t          i;

    ut_section("the panel's edge cases, one by one");
    fresh(0u);
    (void)frame(true);
    s_fake.build_says = NPC_SPAWN_OUTCOME_DONE;
    (void)grant(NPC_SPAWN_GRANT_BUILD, 4u, 1u, 0u);
    (void)frame(true);
    new_epoch();
    (void)frame(true);
    ut_check(s_fake.removes == 0u && s_session.built[4] == 0u && !s_session.leftover[4],
             "the host keeps its copies at a new epoch, and holds no life of them any more");

    fresh(0u);
    (void)frame(true);
    s_fake.build_says = NPC_SPAWN_OUTCOME_DONE;
    (void)grant(NPC_SPAWN_GRANT_BUILD, 1u, 1u, 0u);
    (void)frame(true);
    serial = grant_behind(NPC_SPAWN_GRANT_BUILD, 2u, 1u);
    (void)frame(true);
    ut_check(s_fake.builds == 2u &&
                 npc_spawn_note_answer(&s_session.record, serial) == NPC_SPAWN_ANSWER_DONE,
             "a grant behind one already answered is answered, the answered one skipped");

    fresh(0u);
    (void)frame(true);
    memset(&saved, 0, sizeof saved);
    saved.desc        = desc;
    saved.position[0] = 77.0f;
    (void)npc_spawn_session_wish(&s_session, NPC_SPAWN_WISH_RESTORE, &desc, &saved);
    wish = s_session.record.first;
    (void)grant(NPC_SPAWN_GRANT_REFUSED, 0u, 0u, wish);
    (void)frame(true);
    ut_check(s_session.restores == 0u && s_fake.refusals == 1u &&
                 s_fake.refused_kind == NPC_SPAWN_WISH_RESTORE && s_fake.refused_wish == wish &&
                 s_fake.refused_reason == NPC_SPAWN_REFUSED_CAP,
             "a refused restore gives its saved state up, and the refusal is told with its kind");

    fresh(0u);
    (void)frame(true);
    (void)npc_spawn_session_wish(&s_session, NPC_SPAWN_WISH_RESTORE, &desc, &saved);
    s_grants.wishes_taken = s_session.record.first;
    (void)frame(true);
    new_epoch();
    (void)frame(true);
    ut_check(s_session.record.count == 0u && s_session.restores == 0u,
             "a restore the multiplayer took and a new epoch emptied is forgotten");

    fresh(2u);
    (void)frame(true);
    s_fake.build_says = NPC_SPAWN_OUTCOME_DONE;
    (void)grant(NPC_SPAWN_GRANT_BUILD, 8u, 3u, 0u);
    (void)frame(true);
    new_epoch();
    s_fake.removes = 0u;
    (void)frame(false);
    ut_check(s_fake.removes == 0u && s_session.leftover[8] && s_session.counters.old_found == 1u,
             "a client's copy of an old epoch, while the pool is held, is kept to remove");
    s_fake.remove_says = NPC_SPAWN_OUTCOME_REFUSED;
    (void)frame(true);
    ut_check(s_fake.removes == 1u && s_session.leftover[8],
             "asked again once it can be, and kept while the player rides it");
    s_fake.remove_says = NPC_SPAWN_OUTCOME_DONE;
    (void)frame(true);
    ut_check(s_fake.removes == 2u && !s_session.leftover[8] &&
                 s_session.counters.old_removed == 1u,
             "then removed");
    (void)grant(NPC_SPAWN_GRANT_BUILD, 9u, 1u, 0u);
    (void)frame(true);
    new_epoch();
    (void)frame(false);
    npc_spawn_session_set_world(&s_session, 5u);
    (void)frame(true);
    ut_check(!s_session.leftover[9] && s_session.counters.old_gone == 1u && s_fake.removes == 2u,
             "and one the world changed under is left to the level's end, never asked again");

    fresh(0u);
    s_grants.cap = 0u;
    (void)frame(true);
    ut_check(!npc_spawn_session_active(&s_session), "a client before its slot runs no copies");
    s_fake.build_says = NPC_SPAWN_OUTCOME_DONE;
    (void)grant(NPC_SPAWN_GRANT_BUILD, 6u, 1u, 0u);
    (void)frame(true);
    new_epoch();
    (void)frame(true);
    ut_check(s_fake.removes == 1u && s_fake.last_key == NPC_SPAWN_KEY_FIRST + 6u,
             "but a copy it built then is still its host's, and goes with the epoch");

    fresh(0u);
    (void)frame(true);
    serial = grant(NPC_SPAWN_GRANT_OWNER, 3u, 1u, 0u);
    (void)frame(true);
    ut_check(npc_spawn_note_answer(&s_session.record, serial) == NPC_SPAWN_ANSWER_DONE,
             "an owner change is answered done");
    npc_spawn_session_published(&s_session, false);
    ut_check(s_session.changed, "a record the channel refused is published again");
    (void)npc_spawn_session_frame(&s_session, NULL, true, &OPS);
    ut_check(s_session.changed && s_session.epoch == 1u && npc_spawn_session_active(&s_session),
             "and a frame with no reading changes neither that nor the session it last read");

    ut_section("wishes the contract cannot carry, and a queue with no room");
    fresh(0u);
    (void)frame(true);
    desc.behaviour = NPC_SPAWN_BEHAVIOURS;
    ut_check(!npc_spawn_session_wish(&s_session, NPC_SPAWN_WISH_SPAWN, &desc, NULL) &&
                 s_session.counters.unsound == 1u && s_session.queued == 0u &&
                 s_session.record.count == 0u,
             "a behaviour past the last is refused before it is queued, and counted");
    desc = a_desc(10.0f);
    for (i = 0; i < NPC_SPAWN_WISH_SLOTS + NPC_SPAWN_SESSION_QUEUE; ++i) {
        if (!npc_spawn_session_wish(&s_session, NPC_SPAWN_WISH_SPAWN, &desc, NULL)) {
            break;
        }
    }
    ut_check(i == NPC_SPAWN_WISH_SLOTS + NPC_SPAWN_SESSION_QUEUE &&
                 !npc_spawn_session_wish(&s_session, NPC_SPAWN_WISH_SPAWN, &desc, NULL) &&
                 s_session.counters.queue_full == 1u,
             "six in the record and the queue full, then the next is refused and counted");
}

static void check_what_the_probe_asked(void)
{
    npc_spawn_desc_t  desc = a_desc(10.0f);
    npc_spawn_saved_t saved;
    uint32_t          first;
    uint32_t          i;

    ut_section("the session rules' edge cases, one by one");
    fresh(0u);
    (void)frame(true);
    first = s_session.record.first;
    for (i = 0; i < NPC_SPAWN_SESSION_KINDS + 1u; ++i) {
        (void)npc_spawn_session_wish(&s_session, NPC_SPAWN_WISH_SPAWN, &desc, NULL);
        s_grants.wishes_taken = s_session.record.first + s_session.record.count - 1u;
        (void)frame(true);
    }
    (void)grant(NPC_SPAWN_GRANT_REFUSED, 0u, 0u, first);
    (void)frame(true);
    ut_check(s_fake.refusals == 1u && s_fake.refused_kind == 0u,
             "a refusal of a wish too old to remember is told without a kind");
    ut_check(s_session.counters.epochs == 0u, "and no epoch moved under all that");

    fresh(0u);
    (void)frame(true);
    memset(&saved, 0, sizeof saved);
    saved.desc = desc;
    for (i = 0; i < NPC_SPAWN_WISH_SLOTS + NPC_SPAWN_SESSION_QUEUE; ++i) {
        (void)npc_spawn_session_wish(&s_session, NPC_SPAWN_WISH_RESTORE, &desc, &saved);
        s_grants.wishes_taken = s_session.record.first + s_session.record.count - 1u;
        (void)frame(true);
    }
    ut_check(s_session.restores == NPC_SPAWN_SESSION_QUEUE &&
                 s_session.counters.restores_lost == NPC_SPAWN_WISH_SLOTS,
             "restores past the room kept are counted lost, and the kept ones stop at the room");
    new_epoch();
    (void)frame(true);
    ut_check(s_session.counters.epochs == 1u, "an epoch moved is counted");

    fresh(2u);
    (void)frame(true);
    s_fake.build_says = NPC_SPAWN_OUTCOME_DONE;
    (void)grant(NPC_SPAWN_GRANT_BUILD, 5u, 1u, 0u);
    (void)frame(true);
    new_epoch();
    (void)grant(NPC_SPAWN_GRANT_BUILD, 5u, 1u, 0u);
    s_fake.removes = 0u;
    (void)frame(true);
    ut_check(s_fake.removes == 1u && s_session.built[5] == 1u && !s_session.leftover[5],
             "the old copy goes before the new epoch's grant of its key is built");

    fresh(2u);
    (void)frame(true);
    (void)grant(NPC_SPAWN_GRANT_BUILD, 7u, 1u, 0u);
    s_fake.build_says = NPC_SPAWN_OUTCOME_DONE;
    (void)frame(true);
    new_epoch();
    s_fake.remove_says = NPC_SPAWN_OUTCOME_REFUSED;
    (void)frame(true);
    (void)grant(NPC_SPAWN_GRANT_BUILD, 7u, 1u, 0u);
    (void)frame(true);
    s_fake.remove_says = NPC_SPAWN_OUTCOME_DONE;
    s_fake.removes     = 0u;
    (void)frame(true);
    ut_check(!s_session.leftover[7] && s_fake.removes == 0u && s_session.built[7] == 1u,
             "a copy built on the key shows the old one gone, and is never taken for it");
}

/* A grant of `kind` for k under life `generation`, for the player in world slot `owner`. */
static void grant_owned(uint8_t kind, uint32_t k, uint8_t generation, uint8_t owner)
{
    npc_spawn_grant_t g;

    (void)npc_spawn_note_grants_drop(&s_grants, s_session.record.grants_done);
    memset(&g, 0, sizeof g);
    g.kind       = kind;
    g.key        = (uint16_t)(NPC_SPAWN_KEY_FIRST + k);
    g.generation = generation;
    g.owner      = owner;
    if (kind == NPC_SPAWN_GRANT_BUILD) {
        npc_spawn_desc_t desc = a_desc(40.0f);

        (void)npc_spawn_session_to_note(&desc, &g.desc);
    }
    (void)npc_spawn_note_grants_append(&s_grants, s_grants.first + s_grants.count, &g);
}

static void check_the_owners(void)
{
    uint8_t owner = 99u;

    ut_section("each copy's owner, for the flyer that flies to them");
    fresh(0u);
    (void)frame(true);
    s_fake.build_says = NPC_SPAWN_OUTCOME_DONE;
    ut_check(!npc_spawn_session_owner(&s_session, 4u, &owner) && owner == 99u,
             "a copy never built here has no owner here");
    grant_owned(NPC_SPAWN_GRANT_BUILD, 4u, 2u, 3u);
    (void)frame(true);
    ut_check(npc_spawn_session_owner(&s_session, 4u, &owner) && owner == 3u,
             "a built copy belongs to the player its grant names");
    grant_owned(NPC_SPAWN_GRANT_OWNER, 4u, 1u, 5u);
    (void)frame(true);
    ut_check(npc_spawn_session_owner(&s_session, 4u, &owner) && owner == 3u,
             "a change for a life gone leaves the one built alone");
    grant_owned(NPC_SPAWN_GRANT_OWNER, 4u, 2u, 0u);
    (void)frame(true);
    ut_check(npc_spawn_session_owner(&s_session, 4u, &owner) && owner == 0u,
             "and a change for its own life hands it on, here to the host");
    grant_owned(NPC_SPAWN_GRANT_CANCEL, 4u, 2u, 0u);
    s_fake.remove_says = NPC_SPAWN_OUTCOME_DONE;
    (void)frame(true);
    ut_check(!npc_spawn_session_owner(&s_session, 4u, &owner),
             "and once it is removed it is nobody's");

    ut_section("where a copy's flyer flies to");
    {
        npc_spawn_anchor_record_t anchors;
        float                     point[3] = { 0.0f, 0.0f, 0.0f };
        uint32_t                  key      = NPC_SPAWN_KEY_FIRST + 6u;

        fresh(0u);
        (void)frame(true);
        s_fake.build_says = NPC_SPAWN_OUTCOME_DONE;
        grant_owned(NPC_SPAWN_GRANT_BUILD, 6u, 1u, 2u);
        (void)frame(true);
        memset(&anchors, 0, sizeof anchors);
        anchors.epoch       = s_grants.epoch;
        anchors.valid       = (uint16_t)(1u << 2);
        anchors.point[2][0] = 42.0f;
        ut_check(npc_spawn_session_flyer(&s_session, &anchors, key, point) ==
                         NPC_SPAWN_FLYER_ANCHOR && point[0] == 42.0f,
                 "on the host it flies over its owner's anchor");
        ut_check(npc_spawn_session_flyer(&s_session, &anchors, NPC_SPAWN_KEY_FIRST + 7u, point) ==
                     NPC_SPAWN_FLYER_LOCAL,
                 "one of no grant flies over the local player, as it always did");
        anchors.valid = (uint16_t)(1u << 3);
        ut_check(npc_spawn_session_flyer(&s_session, &anchors, key, point) == NPC_SPAWN_FLYER_KEEP,
                 "with no anchor for its owner its record stays where it is");
        anchors.valid = (uint16_t)(1u << 2);
        anchors.epoch = (uint8_t)(s_grants.epoch + 1u);
        ut_check(npc_spawn_session_flyer(&s_session, &anchors, key, point) == NPC_SPAWN_FLYER_KEEP,
                 "and with an anchor of another epoch too");
        ut_check(npc_spawn_session_flyer(&s_session, NULL, key, point) == NPC_SPAWN_FLYER_KEEP,
                 "and with none read at all");
        s_grants.cap      = 0u;
        s_grants.own_slot = 2u;
        (void)frame(true);
        anchors.epoch = s_grants.epoch;
        ut_check(npc_spawn_session_flyer(&s_session, &anchors, key, point) ==
                     NPC_SPAWN_FLYER_LOCAL,
                 "on a client, whose copies are parked, it is left to the local player");
        s_grants.own_slot = 0u;
        (void)frame(true);
        ut_check(npc_spawn_session_flyer(&s_session, &anchors, key, point) ==
                     NPC_SPAWN_FLYER_LOCAL,
                 "and outside a session that runs the copies as well");
    }
}

int main(void)
{
    check_the_descriptions();
    check_the_wishes();
    check_the_grants();
    check_a_restore();
    check_a_new_epoch();
    check_what_k13_asked();
    check_what_the_probe_asked();
    check_the_owners();
    return ut_summary("npc_spawn_session");
}
