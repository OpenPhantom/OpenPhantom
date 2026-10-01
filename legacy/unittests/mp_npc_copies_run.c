/* mp_npc_copies_run.c: the NPC copies' protocol a substep at a time, host and client.
 *
 * What would be silent if it were wrong: a wish of the last world taken in the new one and built
 * at its old place; a wish decided in the lobby; a copy handed to a client's overlay that cannot
 * build it, filling the note until no cancel fits; an announcement counted as sent when nothing
 * went out, so its asker never hears of the copy; a refusal sent to everybody or to whoever sits
 * on the asker's slot by now; an answer to a refusal read as the answer to a grant; and a record
 * the publisher refuses, which stops both ways at once.
 *
 * SIZE NOTE: over 600 lines. One fixture drives both roles through the same few helpers, and the
 * checks that tell a mutated line from the right one sit beside the ones they sharpen.
 */
#include "unittest.h"

#include "mp_npc_copies_run.h"
#include "mp_npc_copy_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SENT_MAX 32u

typedef struct sent {
    uint8_t bytes[MP_NPC_COPY_ENTRY_BYTES];
    size_t  count;
    int     slot;   /* -1 for everybody */
} sent_t;

static mp_npc_copies_run_t     s_run;
static npc_spawn_wish_record_t s_panel;
static uint64_t                s_connection[NPC_SPAWN_WORLD_SLOTS];
static sent_t                  s_sent[SENT_MAX];
static size_t                  s_sent_count;
static bool                    s_channel_full;
static uint32_t                s_fail_next;   /* sends refused before the channel takes again */

static bool broadcast(void *user, const uint8_t *bytes, size_t count)
{
    (void)user;
    if (s_fail_next > 0u) {
        --s_fail_next;
        return false;
    }
    if (s_channel_full || s_sent_count >= SENT_MAX || count > MP_NPC_COPY_ENTRY_BYTES) {
        return false;
    }
    memcpy(s_sent[s_sent_count].bytes, bytes, count);
    s_sent[s_sent_count].count = count;
    s_sent[s_sent_count].slot  = -1;
    ++s_sent_count;
    return true;
}

static bool send_to(void *user, uint8_t slot, const uint8_t *bytes, size_t count)
{
    if (!broadcast(user, bytes, count)) {
        return false;
    }
    s_sent[s_sent_count - 1u].slot = slot;
    return true;
}

static mp_npc_copies_run_in_t an_input(bool joined, bool in_level)
{
    mp_npc_copies_run_in_t in;

    memset(&in, 0, sizeof in);
    in.joined        = joined;
    in.level_known   = in_level;
    in.level         = 77u;
    in.world         = 5u;
    in.own_slot      = 2u;
    in.can_park      = true;
    in.connection_of = s_connection;
    in.now_ms        = 1000u;
    in.wishes        = &s_panel;
    in.broadcast     = &broadcast;
    in.send_to       = &send_to;
    return in;
}

static npc_spawn_note_desc_t a_desc(void)
{
    npc_spawn_note_desc_t desc;

    memset(&desc, 0, sizeof desc);
    desc.source      = 4u;
    desc.position[0] = 30.0f;
    desc.position[1] = 2.0f;
    desc.position[2] = 12.0f;
    memcpy(desc.file, "droid.baf", 9u);
    return desc;
}

/* The overlay wishes `kind` in `epoch`; answers its serial. */
static uint32_t wish(uint8_t kind, uint8_t epoch)
{
    npc_spawn_wish_t w;
    uint32_t         serial = s_panel.first + s_panel.count;

    memset(&w, 0, sizeof w);
    w.kind  = kind;
    w.epoch = epoch;
    if (kind == NPC_SPAWN_WISH_SPAWN || kind == NPC_SPAWN_WISH_RESTORE) {
        w.desc = a_desc();
    }
    (void)npc_spawn_note_wishes_append(&s_panel, serial, &w);
    return serial;
}

/* The overlay deals with every grant up to `serial`, the last one as `refused` says. */
static void answer(uint32_t serial, bool refused)
{
    (void)npc_spawn_note_acknowledge(&s_panel, serial, refused);
}

static void fresh(mp_npc_run_role_t role)
{
    mp_npc_copies_run_init(&s_run);
    memset(&s_panel, 0, sizeof s_panel);
    s_panel.first = 1u;
    s_panel.panel = NPC_SPAWN_PANEL_BUILDER;
    memset(s_connection, 0, sizeof s_connection);
    s_connection[3] = 0x300000001ull;
    s_sent_count    = 0u;
    s_channel_full  = false;
    s_fail_next     = 0u;
    mp_npc_copies_run_arm(&s_run, role, 16u, 0u, MP_NPC_RUN_ORPHAN_BLOCKS);
}

static void walk(const uint32_t *live, size_t count)
{
    size_t i;

    mp_npc_copies_run_census_begin(&s_run);
    for (i = 0; i < count; ++i) {
        mp_npc_copies_run_census_saw(&s_run, live[i], true);
    }
    mp_npc_copies_run_census_end(&s_run, true, 20u, 128u);
}

static bool substep(const mp_npc_copies_run_in_t *in)
{
    bool changed = mp_npc_copies_run_substep(&s_run, in);

    (void)npc_spawn_note_wishes_drop(&s_panel, s_run.record.wishes_taken);
    if (changed) {
        mp_npc_copies_run_published(&s_run, npc_spawn_note_publish_grants(&s_run.record));
    }
    return changed;
}

static const npc_spawn_grant_t *last_entry(void)
{
    return s_run.record.count > 0u ? &s_run.record.entry[s_run.record.count - 1u] : NULL;
}

static bool sent_entry(size_t i, mp_npc_copy_entry_t *out)
{
    return i < s_sent_count && mp_npc_copy_entry_decode(s_sent[i].bytes, s_sent[i].count, out);
}

/* A client's wish on the wire, from slot 3 unless said otherwise. */
static void wire_wish(uint8_t kind, uint16_t serial, uint8_t world, uint32_t now_ms)
{
    mp_npc_copy_wish_t w;
    uint8_t            bytes[MP_NPC_COPY_WISH_BYTES];
    size_t             count;

    memset(&w, 0, sizeof w);
    w.kind   = kind;
    w.serial = serial;
    w.level  = 77u;
    w.world  = world;
    if (kind == NPC_SPAWN_WISH_SPAWN) {
        w.desc = a_desc();
    }
    count = mp_npc_copy_wish_encode(&w, bytes, sizeof bytes);
    (void)mp_npc_copies_run_take(&s_run, 3u, s_connection[3], bytes, count, now_ms);
}

static void check_nothing_runs_outside_a_session(void)
{
    mp_npc_copies_run_in_t in;
    uint8_t                bytes[MP_NPC_COPY_WISH_BYTES];

    ut_section("with no session nothing runs, and the arming says so at once");
    mp_npc_copies_run_init(&s_run);
    memset(&s_panel, 0, sizeof s_panel);
    in = an_input(true, true);
    ut_check(!mp_npc_copies_run_substep(&s_run, &in) && s_run.record.epoch == 0u,
             "a run that was never armed does nothing");
    memset(bytes, 0, sizeof bytes);
    bytes[0] = MP_NPC_COPY_WISH_TAG;
    ut_check(mp_npc_copies_run_take(&s_run, 3u, 1u, bytes, sizeof bytes, 0u) &&
                 s_run.counters.wire_wrong_role == 1u,
             "a wish it cannot take is still claimed, and counted");
    mp_npc_copies_run_new_world(&s_run);
    mp_npc_copies_run_disarm(&s_run);
    ut_check(s_run.record.epoch == 0u && !s_run.dirty, "and no world or disarming moves its epoch");

    fresh(MP_NPC_RUN_HOST);
    ut_check(s_run.record.epoch == 1u && s_run.dirty && s_run.record.cap == 16u &&
                 s_run.record.first == 1u && mp_npc_copies_run_host_table(&s_run) != NULL &&
                 mp_npc_copies_run_client_table(&s_run) == NULL,
             "an arming raises the epoch, names the cap and is to be published");
    mp_npc_copies_run_disarm(&s_run);
    ut_check(s_run.record.epoch == 2u && s_run.record.cap == 0u && s_run.dirty &&
                 mp_npc_copies_run_host_table(&s_run) == NULL,
             "and a disarming raises it again, with no cap and no table");
}

static void check_the_host(void)
{
    mp_npc_copies_run_in_t in = an_input(false, false);
    mp_npc_copy_entry_t    entry;
    uint32_t               serial;
    uint32_t               k = 99u;
    uint32_t               live[1];

    ut_section("the host: its overlay's wishes, gated by the level and the epoch");
    fresh(MP_NPC_RUN_HOST);
    serial = wish(NPC_SPAWN_WISH_REMOVE_OWN, 1u);
    (void)substep(&in);
    ut_check(s_run.record.count == 0u && s_run.record.wishes_taken == 0u &&
                 s_run.counters.wishes_read == 0u && s_panel.count == 1u,
             "in the lobby its own wish waits in its overlay's record, taken by nobody");
    in = an_input(false, true);
    (void)substep(&in);
    ut_check(s_run.record.wishes_taken == serial && s_run.host.counters.removals == 1u,
             "and is taken once the level is entered");
    serial = wish(NPC_SPAWN_WISH_SPAWN, 1u);
    (void)substep(&in);
    ut_check(last_entry() != NULL && last_entry()->kind == NPC_SPAWN_GRANT_REFUSED &&
                 last_entry()->wish == serial && last_entry()->reason == NPC_SPAWN_REFUSED_POOL &&
                 s_run.record.wishes_taken == serial && s_run.counters.published >= 1u,
             "a spawn before the pool was counted is refused, into a record the publisher takes");
    answer(s_run.record.first, false);

    walk(NULL, 0u);
    serial = wish(NPC_SPAWN_WISH_SPAWN, 0u);
    (void)substep(&in);
    ut_check(s_run.counters.wishes_stale == 1u && s_run.record.count == 0u &&
                 s_run.counters.answers_to_refusals == 1u,
             "a wish of the last epoch is thrown away; the answer to a refusal is only counted");
    serial = wish(NPC_SPAWN_WISH_SPAWN, 1u);
    (void)substep(&in);
    ut_check(last_entry() != NULL && last_entry()->kind == NPC_SPAWN_GRANT_BUILD &&
                 last_entry()->wish == serial && last_entry()->owner == 0u &&
                 mp_npc_copies_run_next_granted(&s_run, &k) && k == 0u &&
                 !mp_npc_copies_run_next_granted(&s_run, &k),
             "in a level it is granted to the host's overlay, k 0, told once to the aim");
    answer(s_run.record.first, false);
    live[0] = 0u;
    walk(live, 1u);
    (void)substep(&in);
    ut_check(s_run.record.count == 0u && s_run.counters.announced == 0u &&
                 (s_run.host.row[0].duties & MP_NPC_COPY_DUTY_ANNOUNCE) != 0u,
             "seen alive with nobody joined, its announcement waits instead of passing for sent");
    in             = an_input(true, true);
    s_channel_full = true;
    (void)substep(&in);
    ut_check(s_run.counters.announced == 0u && s_run.counters.unsent >= 1u &&
                 (s_run.host.row[0].duties & MP_NPC_COPY_DUTY_ANNOUNCE) != 0u,
             "and a channel that will not take it leaves it owed");
    s_channel_full = false;
    in.now_ms      = 1100u;
    (void)substep(&in);
    ut_check(s_run.counters.announced == 1u && sent_entry(0u, &entry) &&
                 entry.kind == NPC_SPAWN_GRANT_BUILD && entry.k == 0u && entry.level == 77u &&
                 entry.world == 5u && s_sent[0].slot == -1,
             "once it goes, to everybody, under the level and the world entered");
    in.now_ms = 1249u;
    (void)substep(&in);
    ut_check(s_run.counters.repeated == 0u, "it is repeated no sooner than a quarter second");
    in.now_ms = 1250u;
    (void)substep(&in);
    ut_check(s_run.counters.repeated == 1u, "and then");

    serial = wish(NPC_SPAWN_WISH_REMOVE_OWN, 1u);
    (void)substep(&in);
    ut_check(last_entry() != NULL && last_entry()->kind == NPC_SPAWN_GRANT_CANCEL &&
                 last_entry()->key == NPC_SPAWN_KEY_FIRST,
             "removing its own copies cancels them");
}

static void check_the_host_and_its_clients(void)
{
    mp_npc_copies_run_in_t in = an_input(true, true);
    mp_npc_copy_entry_t    entry;
    uint32_t               i;

    ut_section("the host and its clients' wishes: the gate, the answer to the asker alone");
    fresh(MP_NPC_RUN_HOST);
    wire_wish(NPC_SPAWN_WISH_SPAWN, 9u, 5u, 1000u);
    ut_check(s_run.counters.wire_in_lobby == 1u && s_run.host.counters.taken == 0u,
             "a wish that arrives before a substep opened the level is dropped, undecided");
    walk(NULL, 0u);
    (void)substep(&in);
    wire_wish(NPC_SPAWN_WISH_SPAWN, 10u, 5u, 1000u);
    ut_check(s_run.host.row[0].state == MP_NPC_COPY_GRANTED && s_run.host.row[0].who.owner == 3u &&
                 s_run.host.row[0].who.wish == 10u,
             "in the level it is granted, owned by its asker");
    wire_wish(NPC_SPAWN_WISH_SPAWN, 11u, 4u, 1000u);
    s_sent_count = 0u;
    (void)substep(&in);
    ut_check(sent_entry(0u, &entry) && entry.kind == NPC_SPAWN_GRANT_REFUSED &&
                 entry.serial == 11u && entry.reason == NPC_SPAWN_REFUSED_NO_LEVEL &&
                 entry.world == 4u && s_sent[0].slot == 3,
             "a wish of another world is refused to its asker alone, with the world it named");
    for (i = 0; i < 6u; ++i) {
        wire_wish(NPC_SPAWN_WISH_SPAWN, (uint16_t)(20u + i), 5u, 1000u);
    }
    s_sent_count = 0u;
    (void)substep(&in);
    ut_check(s_run.counters.too_fast_quiet == 3u && s_run.counters.wire_refusals_sent == 2u,
             "a burst past the throttle is told it is too fast once, not once a wish");
    wire_wish(NPC_SPAWN_WISH_SPAWN, 30u, 4u, 5000u);
    s_connection[3] = 0x300000002ull;
    (void)substep(&in);
    ut_check(s_run.counters.wire_refusals_dropped == 1u,
             "a refusal owed to a player who left is not said to the one on the slot now");
    wire_wish(NPC_SPAWN_WISH_SPAWN, 31u, 5u, 9000u);
    mp_npc_copies_run_new_world(&s_run);
    ut_check(s_run.record.epoch == 2u && !s_run.in_level && s_run.wire_refusals == 0u &&
                 s_run.host.row[0].state == MP_NPC_COPY_FREE && s_run.dirty,
             "a new world raises the epoch, closes the gate and empties table and queues");
}

static void check_the_client(void)
{
    mp_npc_copies_run_in_t in = an_input(true, true);
    mp_npc_copy_wish_t     out;
    uint32_t               serial;

    ut_section("the client: a wish goes out only when it can be answered");
    fresh(MP_NPC_RUN_CLIENT);
    in.own_slot = 0u;
    serial      = wish(NPC_SPAWN_WISH_SPAWN, 1u);
    (void)substep(&in);
    ut_check(last_entry() != NULL && last_entry()->reason == NPC_SPAWN_REFUSED_NO_LEVEL &&
                 last_entry()->wish == serial && s_sent_count == 0u &&
                 s_run.record.own_slot == 0u && s_run.record.cap == 0u,
             "before its host told it its slot it asks for nothing and says so to its overlay");
    in.own_slot = 2u;
    in.can_park = false;
    serial      = wish(NPC_SPAWN_WISH_SPAWN, 1u);
    (void)substep(&in);
    ut_check(last_entry() != NULL && last_entry()->reason == NPC_SPAWN_REFUSED_NO_PARKING &&
                 s_run.record.own_slot == 2u,
             "a machine that cannot hold a copy still asks for none");
    in.can_park = true;
    serial      = wish(NPC_SPAWN_WISH_RESTORE, 1u);
    (void)substep(&in);
    ut_check(last_entry() != NULL && last_entry()->reason == NPC_SPAWN_REFUSED_UNSOUND,
             "a restore is the host's alone");
    (void)wish(NPC_SPAWN_WISH_SPAWN, 0u);
    (void)substep(&in);
    ut_check(s_run.counters.wishes_stale == 1u && s_sent_count == 0u,
             "a wish of the last epoch is thrown away here as well, never sent");
    s_channel_full = true;
    serial         = wish(NPC_SPAWN_WISH_SPAWN, 1u);
    (void)substep(&in);
    ut_check(s_run.counters.wishes_held == 1u && s_run.record.wishes_taken == serial - 1u &&
                 s_run.client.counters.wishes_opened == 0u,
             "a wish the channel will not take is not taken: it waits, unopened");
    s_channel_full = false;
    (void)substep(&in);
    ut_check(s_sent_count == 1u &&
                 mp_npc_copy_wish_decode(s_sent[0].bytes, s_sent[0].count, &out) &&
                 out.serial == (uint16_t)serial && out.level == 77u && out.world == 5u &&
                 s_run.record.wishes_taken == serial && s_run.client.counters.wishes_opened == 1u,
             "and goes the next substep, with the level and the world entered");
}

static void entry_to_client(uint32_t k, uint8_t generation, uint8_t owner, uint8_t world)
{
    mp_npc_copy_entry_t entry;
    uint8_t             bytes[MP_NPC_COPY_ENTRY_BYTES];
    size_t              count;

    memset(&entry, 0, sizeof entry);
    entry.kind       = NPC_SPAWN_GRANT_BUILD;
    entry.k          = (uint16_t)k;
    entry.generation = generation;
    entry.owner      = owner;
    entry.level      = 77u;
    entry.world      = world;
    entry.desc       = a_desc();
    (void)mp_npc_copy_desc_round(&entry.desc);
    count = mp_npc_copy_entry_encode(&entry, bytes, sizeof bytes);
    (void)mp_npc_copies_run_take(&s_run, 0u, 0u, bytes, count, 0u);
}

static void check_the_client_builds(void)
{
    mp_npc_copies_run_in_t in = an_input(true, true);

    ut_section("the client builds only with an overlay that builds, and hands up a failed park");
    fresh(MP_NPC_RUN_CLIENT);
    s_panel.panel = 0u;
    walk(NULL, 0u);
    (void)substep(&in);
    entry_to_client(3u, 1u, 0u, 5u);
    (void)substep(&in);
    ut_check(s_run.counters.entries_no_panel == 1u && s_run.record.count == 0u,
             "without an overlay that builds nothing is handed, and nothing fills the note");
    s_panel.panel = NPC_SPAWN_PANEL_BUILDER;
    (void)substep(&in);
    entry_to_client(3u, 1u, 0u, 4u);
    ut_check(s_run.counters.entries_stale == 1u, "an entry of another world is dropped");
    entry_to_client(3u, 1u, 0u, 5u);
    (void)substep(&in);
    ut_check(last_entry() != NULL && last_entry()->kind == NPC_SPAWN_GRANT_BUILD &&
                 last_entry()->key == NPC_SPAWN_KEY_FIRST + 3u && last_entry()->generation == 1u,
             "with one, the host's repetition is handed");
    mp_npc_copies_run_spawned(&s_run, 3u, 0x4000u, false);
    (void)substep(&in);
    ut_check(s_run.counters.park_failed == 1u && last_entry() != NULL &&
                 last_entry()->kind == NPC_SPAWN_GRANT_CANCEL,
             "a copy that could not be held still is cancelled at once");
    mp_npc_copies_run_spawned(&s_run, 9u, 0x4100u, true);
    ut_check(s_run.counters.spawned_unknown == 1u, "a build under a key nobody handed is counted");
}

static void check_the_queues_and_the_wire(void)
{
    mp_npc_copies_run_in_t in = an_input(true, true);
    mp_npc_copy_entry_t    entry;
    uint32_t               serial;
    uint32_t               live[40];
    uint32_t               i;

    ut_section("the builder is kept, an arming forgets it, a wish is taken once");
    fresh(MP_NPC_RUN_HOST);
    walk(NULL, 0u);
    (void)substep(&in);
    ut_check(s_run.record.own_slot == 0u, "a host names no slot of its own, whatever it is told");
    s_panel.panel = 0u;
    serial        = wish(NPC_SPAWN_WISH_SPAWN, 1u);
    (void)substep(&in);
    ut_check(last_entry() != NULL && last_entry()->kind == NPC_SPAWN_GRANT_BUILD &&
                 last_entry()->wish == serial,
             "a builder once read is kept when a later read says nothing of it");
    ut_check(!substep(&in) && s_run.host.counters.answers_open == 0u,
             "a substep that changes nothing publishes nothing, and asks no open answer");
    mp_npc_copies_run_arm(&s_run, MP_NPC_RUN_HOST, 16u, 0u, MP_NPC_RUN_ORPHAN_BLOCKS);
    walk(NULL, 0u);
    serial = wish(NPC_SPAWN_WISH_SPAWN, 2u);
    (void)substep(&in);
    ut_check(last_entry() != NULL && last_entry()->kind == NPC_SPAWN_GRANT_REFUSED &&
                 last_entry()->reason == NPC_SPAWN_REFUSED_NO_BUILDER,
             "but a new arming asks again, and without a builder a spawn is refused for it");
    serial = wish(NPC_SPAWN_WISH_REMOVE_OWN, 2u);
    (void)mp_npc_copies_run_substep(&s_run, &in);
    (void)mp_npc_copies_run_substep(&s_run, &in);
    ut_check(s_run.counters.wishes_read == 3u,
             "a wish the overlay has not dropped yet is taken once, not once a substep");

    ut_section("a full ring holds the grants back, and a new world empties every queue");
    fresh(MP_NPC_RUN_HOST);
    walk(NULL, 0u);
    for (i = 0; i < NPC_SPAWN_GRANT_SLOTS + 1u; ++i) {
        (void)wish(NPC_SPAWN_WISH_SPAWN, 1u);
    }
    (void)substep(&in);
    ut_check(s_run.record.count == NPC_SPAWN_GRANT_SLOTS && s_run.counters.ring_full >= 1u,
             "six grants, five places: the sixth waits and the wait is counted");
    /* A pool counted too full to take another: every spawn is refused, behind a full ring. */
    mp_npc_copies_run_census_begin(&s_run);
    mp_npc_copies_run_census_end(&s_run, true, 120u, 128u);
    for (i = 0; i < MP_NPC_RUN_REFUSALS + 2u; ++i) {
        (void)wish(NPC_SPAWN_WISH_SPAWN, 1u);
        (void)substep(&in);
    }
    ut_check(s_run.own_refusals == MP_NPC_RUN_REFUSALS && s_run.counters.own_overflow == 0u,
             "with the queue of refusals full, the wishes wait in the overlay's record: none is "
             "left without its answer");
    mp_npc_copies_run_new_world(&s_run);
    ut_check(s_run.own_refusals == 0u && s_run.wire_refusals == 0u,
             "and a new world empties the queue with the ring");

    ut_section("the wire is paced, and a refusal for a copy that never stood goes to its asker");
    fresh(MP_NPC_RUN_HOST);
    mp_npc_copies_run_arm(&s_run, MP_NPC_RUN_HOST, 64u, 0u, MP_NPC_RUN_ORPHAN_BLOCKS);
    walk(NULL, 0u);
    in = an_input(false, true);
    for (i = 0; i < 40u; ++i) {
        (void)wish(NPC_SPAWN_WISH_SPAWN, 2u);   /* the second arming's epoch */
        (void)substep(&in);
        answer(s_run.record.first + s_run.record.count - 1u, false);
        live[i] = i;
    }
    walk(live, 40u);
    in           = an_input(true, true);
    s_sent_count = 0u;
    (void)substep(&in);
    ut_checkf(s_run.counters.announced == MP_NPC_RUN_WIRE_PER_SUBSTEP,
              "forty copies seen alive at once are announced a few a substep, not all at once "
              "(%u of %u granted)", (unsigned)s_run.counters.announced,
              (unsigned)s_run.host.counters.granted);
    fresh(MP_NPC_RUN_HOST);
    walk(NULL, 0u);
    (void)substep(&in);
    wire_wish(NPC_SPAWN_WISH_SPAWN, 70u, 5u, 1000u);
    s_sent_count = 0u;
    for (i = 0; i <= MP_NPC_COPIES_SEEN_DEADLINE + 1u; ++i) {
        walk(NULL, 0u);
        (void)substep(&in);
    }
    ut_check(s_sent_count == 1u && sent_entry(0u, &entry) &&
                 entry.kind == NPC_SPAWN_GRANT_REFUSED &&
                 entry.serial == 70u && entry.reason == NPC_SPAWN_REFUSED_NOT_BUILT &&
                 s_sent[0].slot == 3,
             "a grant never seen alive is refused to the client that asked, and only to it");

    ut_section("a client asks for nothing it could not be answered for");
    fresh(MP_NPC_RUN_CLIENT);
    in        = an_input(false, true);
    serial    = wish(NPC_SPAWN_WISH_SPAWN, 1u);
    (void)substep(&in);
    ut_check(last_entry() != NULL && last_entry()->wish == serial &&
                 last_entry()->reason == NPC_SPAWN_REFUSED_NO_LEVEL && s_sent_count == 0u,
             "with no host joined, a wish is refused here");
    in     = an_input(true, false);
    serial = wish(NPC_SPAWN_WISH_SPAWN, 1u);
    (void)substep(&in);
    ut_check(last_entry() != NULL && last_entry()->wish == serial &&
                 last_entry()->reason == NPC_SPAWN_REFUSED_NO_LEVEL && s_sent_count == 0u,
             "and in the lobby");
    fresh(MP_NPC_RUN_CLIENT);
    in             = an_input(true, true);
    s_fail_next    = 1u;
    (void)wish(NPC_SPAWN_WISH_SPAWN, 1u);
    serial         = wish(NPC_SPAWN_WISH_SPAWN, 1u);
    (void)mp_npc_copies_run_substep(&s_run, &in);
    ut_check(s_run.record.wishes_taken == 0u && s_sent_count == 0u,
             "a wish the channel refused holds back the ones behind it, in order");
    (void)mp_npc_copies_run_substep(&s_run, &in);
    ut_check(s_run.record.wishes_taken == serial && s_sent_count == 2u,
             "and both go next substep");
    mp_npc_copies_run_removed(&s_run, 4u, 1u, true);
    ut_check(s_run.counters.removed == 1u, "a removal from the host reaches the table");
}

static void check_the_core_edge_cases(void)
{
    mp_npc_copies_run_in_t in = an_input(true, true);
    uint8_t                bytes[MP_NPC_COPY_WISH_BYTES];
    mp_npc_copy_wish_t     w;
    size_t                 count;
    uint32_t               i;
    uint32_t               live[1];

    ut_section("the core's edge cases, one by one");
    fresh(MP_NPC_RUN_HOST);
    walk(NULL, 0u);
    (void)wish(NPC_SPAWN_WISH_SPAWN, 1u);
    (void)substep(&in);
    mp_npc_copies_run_new_world(&s_run);
    ut_check(s_run.record.count == 0u && s_run.record.first == 2u,
             "a new world empties the grant ring and names the next serial first");

    fresh(MP_NPC_RUN_HOST);
    walk(NULL, 0u);
    (void)substep(&in);
    memset(&w, 0, sizeof w);
    w.kind = NPC_SPAWN_WISH_REMOVE_OWN;
    count  = mp_npc_copy_wish_encode(&w, bytes, sizeof bytes);
    ut_check(mp_npc_copies_run_take(&s_run, 0u, 0u, bytes, count, 1000u) &&
                 s_run.counters.wire_wrong_role == 1u && s_run.host.counters.taken == 0u,
             "a wish on the wire in the host's own slot is nobody's, and is not decided");
    wire_wish(NPC_SPAWN_WISH_SPAWN, 40u, 5u, 1000u);
    wire_wish(NPC_SPAWN_WISH_REMOVE_OWN, 41u, 5u, 1000u);
    ut_check(s_run.host.row[0].state == MP_NPC_COPY_ENDING,
             "a client's removal takes the copies it owns on the connection it asked from");
    wire_wish(NPC_SPAWN_WISH_SPAWN, 42u, 5u, 1000u);
    (void)wish(NPC_SPAWN_WISH_REMOVE_ALL, 1u);
    (void)substep(&in);
    ut_check(s_run.host.row[1].state == MP_NPC_COPY_ENDING,
             "and the host's removal of all takes a client's copy too");

    fresh(MP_NPC_RUN_HOST);
    walk(NULL, 0u);
    (void)substep(&in);
    wire_wish(NPC_SPAWN_WISH_SPAWN, 43u, 5u, 1000u);
    s_connection[3] = 0x300000009ull;
    s_sent_count    = 0u;
    for (i = 0; i <= MP_NPC_COPIES_SEEN_DEADLINE + 1u; ++i) {
        walk(NULL, 0u);
        (void)substep(&in);
    }
    ut_check(s_sent_count == 0u && s_run.host.counters.refusals_dropped == 1u,
             "a refusal owed to a player who has left is dropped, not said to the one there now");

    fresh(MP_NPC_RUN_HOST);
    walk(NULL, 0u);
    in = an_input(false, true);
    (void)wish(NPC_SPAWN_WISH_SPAWN, 1u);
    (void)substep(&in);
    live[0] = 0u;
    walk(live, 1u);
    in.now_ms    = 9000u;
    s_sent_count = 0u;
    (void)substep(&in);
    ut_check(s_run.counters.repeated == 0u && s_sent_count == 0u,
             "a host with nobody joined repeats nothing");

    fresh(MP_NPC_RUN_CLIENT);
    in          = an_input(true, true);
    in.can_park = false;
    walk(NULL, 0u);
    (void)substep(&in);
    entry_to_client(6u, 1u, 0u, 5u);
    ut_check(s_run.counters.entries_no_parking == 1u &&
                 mp_npc_copies_client_state(&s_run.client, 6u) == MP_NPC_HELD_NONE,
             "a client that cannot hold a copy still takes no entry for one");
}

static void check_a_player_leaving(void)
{
    mp_npc_copies_run_in_t in = an_input(true, true);
    bool                   told = false;
    uint32_t               i;
    uint32_t               step;

    ut_section("a player who leaves hands their copies to the host");
    fresh(MP_NPC_RUN_HOST);
    walk(NULL, 0u);
    (void)substep(&in);
    wire_wish(NPC_SPAWN_WISH_SPAWN, 50u, 5u, 1000u);
    ut_check(s_run.host.row[0].who.owner == 3u, "a client's copy is its own");
    s_connection[3] = 0x300000077ull;
    for (step = 0; step < 3u && !told; ++step) {
        walk(NULL, 0u);
        (void)substep(&in);
        for (i = 0; i < s_run.record.count; ++i) {
            told = told || s_run.record.entry[i].kind == NPC_SPAWN_GRANT_OWNER;
        }
    }
    ut_check(s_run.host.row[0].who.owner == 0u && s_run.host.counters.owners_changed == 1u && told,
             "once another player sits on its slot the copy is the host's, and its overlay hears");
}

int main(void)
{
    check_nothing_runs_outside_a_session();
    check_the_host();
    check_the_host_and_its_clients();
    check_the_client();
    check_the_client_builds();
    check_the_queues_and_the_wire();
    check_the_core_edge_cases();
    check_a_player_leaving();
    return ut_summary("mp_npc_copies_run");
}
