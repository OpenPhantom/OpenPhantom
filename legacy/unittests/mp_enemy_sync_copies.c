/* mp_enemy_sync_copies.c: the copies' part of the enemy block.
 *
 * SIZE NOTE: over 600 lines. The copies' part is a part OF the block and cannot be checked without
 * building one, so every section here lays out a whole block and reads it back; the helpers that
 * do that are shared by all of them and splitting the file would copy them instead.
 *
 * A copy an editor spawned travels under the key 256 + k, in a part behind the placements, and
 * only while the host has one alive. What would be silent if it were wrong:
 *
 *   a block with no copy that differs by a byte from the layout without copies, which a receiver
 *   would take as another set of enemies;
 *
 *   a part left out of a full block, which says the host has no copy at all;
 *
 *   copies that never travel because the placements take every byte of a crowded block;
 *
 *   a copy that reaches placement k, or a local body that happens to carry the same key;
 *
 *   and a torn part that takes the good placements in front of it along, or leaves them applied.
 *
 * There is no engine behind this, so the census finds nothing. A live row is put straight into
 * the table the census fills, through the module's internal header. Host and client share that
 * one table here, as they never do in a session; each check asks the receiving half only about
 * what the sending half does not write: the mirror, the wishes, the writes due.
 *
 * And with the copies' tables handed over: a host's copy described with its grant's life and a
 * copy the table never handed out not at all, a client's record going to the replica built for
 * that life and a record of another life giving it up.
 *
 * The block builders repeat the ones in mp_enemy_sync.c's test: a static helper that one of two
 * files leaves unused is a warning under /W4.
 */
#include "unittest.h"

#include "mp_enemy_sync.h"
#include "mp_enemy_sync_internal.h"
#include "mp_enemy_wire.h"
#include "mp_npc_copies.h"
#include "mp_npc_copies_client.h"
#include "mp_npc_copy_wire.h"
#include "mp_wire.h"

#include "common/npc_spawn_note.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The level these tests are in. */
#define HERE 57u

#define HEADER MP_ENEMY_SYNC_HEADER_BYTES

/* The copies' head with a bitmap of one byte: its length, the byte, the count. */
#define HEAD_ONE_BYTE 3u

static uint8_t s_block[2048];

/* The substep that carried a block. A real stream never hands the same one twice and never goes
 * backwards, so the helpers count for the tests that do not care which number it is. */
static uint32_t s_wire_tick;

static bool take_block(const uint8_t *block, size_t bytes)
{
    return mp_enemy_sync_apply(block, bytes, ++s_wire_tick);
}

static void commit_view(size_t view)
{
    uint32_t tick = ++s_wire_tick;

    mp_enemy_sync_sent_for(view, tick);
    mp_enemy_sync_acked_for(view, tick, 0u);
}

static void full_record(mp_enemy_record_t *record, uint8_t index, uint8_t generation, uint32_t x,
                        int32_t health)
{
    uint32_t packed = 0;

    memset(record, 0, sizeof *record);
    record->value[MP_ENEMY_F_INDEX]      = index;
    record->value[MP_ENEMY_F_GENERATION] = generation;
    (void)mp_enemy_wire_put_position((float)x, &packed);
    record->value[MP_ENEMY_F_POS_X]  = packed;
    /* A host names all three axes, and a first sighting goes whole (put_record, put_copy), because
     * a receiver with nothing to read a record against takes only a whole one. */
    (void)mp_enemy_wire_put_position(5.0f, &packed);
    record->value[MP_ENEMY_F_POS_Y]  = packed;
    (void)mp_enemy_wire_put_position(7.0f, &packed);
    record->value[MP_ENEMY_F_POS_Z]  = packed;
    record->value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(health);
}

/* A placement's record as the sender lays it out: index, generation, the delta, or with no
 * baseline the whole record. */
static size_t put_record(uint8_t *out, size_t capacity, const mp_enemy_record_t *record,
                         const mp_enemy_record_t *baseline)
{
    size_t bytes = 0;
    bool   wrote;

    out[0] = (uint8_t)record->value[MP_ENEMY_F_INDEX];
    out[1] = (uint8_t)record->value[MP_ENEMY_F_GENERATION];
    wrote  = baseline != NULL
                 ? mp_enemy_wire_encode(record, baseline, out + 2, capacity - 2, &bytes)
                 : mp_enemy_wire_encode_whole(record, out + 2, capacity - 2, &bytes);
    if (!wrote) {
        return 0;
    }
    return 2u + bytes;
}

/* A placement's record as the sender lays it out for a peer that has confirmed nothing yet:
 * index, generation, and every field. */
static size_t put_record_whole(uint8_t *out, size_t capacity, const mp_enemy_record_t *record)
{
    size_t bytes = 0;

    out[0] = (uint8_t)record->value[MP_ENEMY_F_INDEX];
    out[1] = (uint8_t)record->value[MP_ENEMY_F_GENERATION];
    if (!mp_enemy_wire_encode_whole(record, out + 2, capacity - 2, &bytes)) {
        return 0;
    }
    return 2u + bytes;
}

/* A copy's record as the sender lays it out: k in two bytes, the generation, the delta, or with
 * no baseline the whole record. */
static size_t put_copy(uint8_t *out, size_t capacity, size_t k, const mp_enemy_record_t *record,
                       const mp_enemy_record_t *baseline)
{
    size_t bytes = 0;
    bool   wrote;

    out[0] = (uint8_t)(k & 0xFFu);
    out[1] = (uint8_t)(k >> 8);
    out[2] = (uint8_t)record->value[MP_ENEMY_F_GENERATION];
    wrote  = baseline != NULL
                 ? mp_enemy_wire_encode(record, baseline, out + 3, capacity - 3, &bytes)
                 : mp_enemy_wire_encode_whole(record, out + 3, capacity - 3, &bytes);
    if (!wrote) {
        return 0;
    }
    return 3u + bytes;
}

static void begin_block(uint8_t count, uint16_t level)
{
    memset(s_block, 0, sizeof s_block);
    s_block[0] = count;
    s_block[1] = (uint8_t)(level & 0xFFu);
    s_block[2] = (uint8_t)(level >> 8);
}

static void mark_present(uint8_t index)
{
    s_block[1u + MP_ENEMY_SYNC_LEVEL_BYTES + (index >> 3)] |= (uint8_t)(1u << (index & 7u));
}

/* One placement record (index 4) and then a copies' part that the caller finishes. */
static size_t placement_then_part(uint8_t length, uint8_t count)
{
    mp_enemy_record_t record;
    size_t            at;

    begin_block(1u, HERE);
    at = HEADER;
    full_record(&record, 4u, 1u, 10u, 100);
    at += put_record(s_block + at, sizeof s_block - at, &record, NULL);
    mark_present(4u);
    s_block[at++] = length;
    at += length;   /* the bitmap, left empty: nothing here reads it yet */
    s_block[at++] = count;
    return at;
}

/* A row the census would have found alive and read, put where the encoder reads it. */
static void poke_row(size_t key, uint8_t wire_index, uint8_t generation, uint32_t x)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    full_record(&s->placement[key].current, wire_index, generation, x, 100);
    s->placement[key].current_ok = true;
    s->placement[key].generation = generation;
}

static void poke_copy(size_t k, uint8_t generation, uint32_t x)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    poke_row(MP_WIRE_KEY_COPY_BASE + k, (uint8_t)k, generation, x);
    s->copy_bitmap[k >> 3] |= (uint8_t)(1u << (k & 7u));
    if (s->copy_bitmap_bytes < k / 8u + 1u) {
        s->copy_bitmap_bytes = k / 8u + 1u;
    }
}

/* How long a record is on its own, whole. */
static size_t whole_of(const mp_enemy_record_t *record)
{
    uint8_t scratch[128];
    size_t  bytes = 0;

    return mp_enemy_wire_encode_whole(record, scratch, sizeof scratch, &bytes) ? bytes : 0u;
}

/* The bytes of the first record of placement 1 as poke_row(1u, 1u, 1u, 10u) lays it out for a
 * peer that has confirmed nothing yet, which is whole. */
static size_t one_placement(void)
{
    mp_enemy_record_t record;
    uint8_t           scratch[128];

    full_record(&record, 1u, 1u, 10u, 100);
    return put_record_whole(scratch, sizeof scratch, &record);
}

/* A sending substep over an empty pool: the level, the census, nothing described. */
static void fresh_send(void)
{
    mp_enemy_sync_reset();
    mp_enemy_sync_set_level(true, HERE);
    (void)mp_enemy_sync_begin_send();
}

static bool encode(size_t view, size_t capacity, size_t *bytes)
{
    return mp_enemy_sync_encode_for(view, s_block, capacity, bytes);
}

/* The copies' tables, as the copies' module hands them to the block. */
static mp_npc_copies_t        s_host_table;
static mp_npc_copies_client_t s_client_table;

static void desc_bytes(uint8_t *out)
{
    npc_spawn_note_desc_t desc;

    memset(&desc, 0, sizeof desc);
    desc.source      = 3u;
    desc.position[0] = 10.0f;
    memcpy(desc.file, "trooper.baf", 11u);
    (void)mp_npc_copy_desc_put(&desc, out);
}

static void check_the_host_table(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    mp_npc_copy_who_t   who;
    uint8_t             desc[MP_NPC_COPY_DESC_BYTES];
    uint8_t             g = 0;

    ut_section("a host's copy is described with the life its grant made, and only then");
    mp_npc_copies_init(&s_host_table, 0u);
    mp_npc_copies_census_begin(&s_host_table);
    mp_npc_copies_census_saw(&s_host_table, 5u, true);
    mp_npc_copies_census_end(&s_host_table, true, 20u, 128u);
    memset(&who, 0, sizeof who);
    who.asker_own = true;
    who.wish      = 1u;
    desc_bytes(desc);
    s_host_table.row[2].generation = 6u;
    (void)mp_npc_copies_grant(&s_host_table, 2u, &who, desc);

    fresh_send();
    mp_enemy_sync_set_copies(&s_host_table, NULL);
    ut_check(mp_enemy_sync_generation(MP_WIRE_KEY_COPY_BASE + 2u, &g) && g == 7u,
             "the life of a granted copy is known before any block described it");
    s->placement[MP_WIRE_KEY_COPY_BASE + 2u].live  = true;
    s->placement[MP_WIRE_KEY_COPY_BASE + 2u].actor = 0x1000u;
    s->placement[MP_WIRE_KEY_COPY_BASE + 5u].live  = true;
    s->placement[MP_WIRE_KEY_COPY_BASE + 5u].actor = 0x2000u;
    s->copy_undescribed                            = 0u;
    mp_enemy_sync_describe_census();
    ut_check((s->copy_bitmap[0] & 0x04u) != 0u && (s->copy_bitmap[0] & 0x20u) == 0u &&
                 s->copy_undescribed == 1u,
             "the granted copy is in the block; one the table never handed out is not");
    ut_check(s->placement[MP_WIRE_KEY_COPY_BASE + 2u].generation == 7u &&
                 mp_enemy_sync_generation(MP_WIRE_KEY_COPY_BASE + 2u, &g) && g == 7u,
             "with its grant's life, the one the relay sends too");
    s->placement[MP_WIRE_KEY_COPY_BASE + 2u].was_live = false;
    mp_enemy_sync_describe_census();
    ut_check(s->placement[MP_WIRE_KEY_COPY_BASE + 2u].generation == 7u,
             "and a census that finds it new does not count a life the grant did not make");
    mp_enemy_sync_set_copies(NULL, NULL);
    s->placement[MP_WIRE_KEY_COPY_BASE + 2u].was_live = false;
    mp_enemy_sync_describe_census();
    ut_check(s->placement[MP_WIRE_KEY_COPY_BASE + 2u].generation == 8u &&
                 (s->copy_bitmap[0] & 0x20u) != 0u,
             "without the table a copy counts its lives by the census");
    mp_enemy_sync_reset();
}

static size_t one_copy_block(size_t k, uint8_t generation)
{
    mp_enemy_record_t copy;
    size_t            at;

    begin_block(0u, HERE);
    at            = HEADER;
    s_block[at++] = 1u;
    s_block[at++] = (uint8_t)(1u << k);
    s_block[at++] = 1u;
    full_record(&copy, (uint8_t)k, generation, 20u, 50);
    return at + put_copy(s_block + at, sizeof s_block - at, k, &copy, NULL);
}

static void check_the_client_table(void)
{
    mp_npc_copy_entry_t entry;
    size_t              at;

    ut_section("a client's copy: its record goes to its replica, a block names it, a release");
    mp_npc_copies_client_init(&s_client_table, 30u);
    memset(&entry, 0, sizeof entry);
    entry.kind       = NPC_SPAWN_GRANT_BUILD;
    entry.k          = 3u;
    entry.generation = 4u;
    entry.desc.source = 2u;
    memcpy(entry.desc.file, "droid.baf", 9u);
    (void)mp_npc_copy_desc_round(&entry.desc);
    mp_npc_copies_client_take_entry(&s_client_table, &entry, 1u);
    mp_npc_copies_client_duty_done(&s_client_table, 3u, MP_NPC_HELD_DUTY_HAND, 9u);
    mp_npc_copies_client_take_answer(&s_client_table, 9u, NPC_SPAWN_ANSWER_DONE);
    mp_npc_copies_client_parked(&s_client_table, 3u, 0x3000u);

    mp_enemy_sync_reset();
    mp_enemy_sync_set_level(true, HERE);
    mp_enemy_sync_set_copies(NULL, &s_client_table);
    s_client_table.held[3].blocks = 7u;
    at = one_copy_block(3u, 4u);
    ut_check(take_block(s_block, at) && s_client_table.held[3].blocks == 0u,
             "a block that names the copy tells the table so");
    ut_check(mp_enemy_sync_replica_for(MP_WIRE_KEY_COPY_BASE + 3u) == 0x3000u,
             "the replica for its key is the one built here for that life");
    at = one_copy_block(3u, 5u);
    ut_check(take_block(s_block, at) &&
                 mp_npc_copies_client_state(&s_client_table, 3u) == MP_NPC_HELD_CANCELLING &&
                 mp_enemy_sync_replica_for(MP_WIRE_KEY_COPY_BASE + 3u) == 0u,
             "a record of another life gives the built one up, and nothing answers for the key");

    mp_npc_copies_client_init(&s_client_table, 30u);
    mp_npc_copies_client_take_entry(&s_client_table, &entry, 1u);
    mp_enemy_sync_release_all();
    ut_check(mp_npc_copies_client_state(&s_client_table, 3u) == MP_NPC_HELD_GIVEN_UP &&
                 s_client_table.counters.given_up[MP_NPC_GIVE_UP_RELEASE] == 1u,
             "letting the enemies go gives the client's copies up in its table");
    mp_enemy_sync_set_copies(NULL, NULL);
    mp_enemy_sync_reset();
}

static void check_no_copy_leaves_the_block_as_it_was(void)
{
    const size_t one   = one_placement();
    size_t       bytes = 0;

    ut_section("a block with no copy alive is the block it was before, crowded or not");
    {
        mp_enemy_record_t record;
        uint8_t           sent[256];
        size_t            hand;

        fresh_send();
        poke_row(1u, 1u, 1u, 10u);
        ut_check(mp_enemy_sync_encode_for(0u, sent, sizeof sent, &bytes),
                 "a host with one placement alive builds its block");
        begin_block(1u, HERE);
        full_record(&record, 1u, 1u, 10u, 100);
        hand = HEADER + put_record_whole(s_block + HEADER, sizeof s_block - HEADER, &record);
        ut_checkf(bytes == hand && memcmp(sent, s_block, hand) == 0,
                  "byte for byte the layout without copies (%u against %u bytes), with no "
                  "part behind it", (unsigned)bytes, (unsigned)hand);
        mp_enemy_sync_abandon_for(0u);
        ut_check(encode(0u, HEADER + one, &bytes) && bytes == HEADER + one && s_block[0] == 1u,
                 "a room that holds the record exactly takes it: no copy, nothing kept back");
        mp_enemy_sync_abandon_for(0u);
        ut_check(encode(0u, HEADER + one - 1u, &bytes) && bytes == HEADER && s_block[0] == 0u,
                 "and a byte less leaves it for the next block");
        mp_enemy_sync_abandon_for(0u);
    }
}

static void check_the_census(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    ut_section("what the sender makes of a census: the bitmaps, the lives, the index field");
    {
        static const size_t KS[] = { 0u, 7u, 8u, 127u };
        size_t              n;

        ut_check(mp_enemy_sync_wire_index(4u) == 4u && mp_enemy_sync_wire_index(255u) == 255u,
                 "a placement's index field holds its key");
        ut_check(mp_enemy_sync_wire_index(MP_WIRE_KEY_COPY_BASE) == 0u &&
                     mp_enemy_sync_wire_index(MP_WIRE_KEY_COPY_BASE + 127u) == 127u,
                 "a copy's holds k, because 256 + k is not a byte");
        for (n = 0; n < sizeof KS / sizeof KS[0]; ++n) {
            const size_t k   = KS[n];
            placement_t *row = &s->placement[MP_WIRE_KEY_COPY_BASE + k];

            fresh_send();
            row->live  = true;
            row->actor = 0x00A01000u;
            mp_enemy_sync_describe_census();
            ut_checkf(s->copy_bitmap_bytes == k / 8u + 1u &&
                          s->copy_bitmap[k / 8u] == (uint8_t)(1u << (k & 7u)),
                      "copy %u alive: a bitmap of %u byte(s) with its one bit", (unsigned)k,
                      (unsigned)(k / 8u + 1u));
            ut_check(row->generation == 1u, "and its first life is generation 1");
            row->was_live = true;
            mp_enemy_sync_describe_census();
            ut_check(row->generation == 1u && s->placement[k].generation == 0u,
                     "a life that goes on keeps it, and placement k counted none");
        }
        fresh_send();
        ut_check(s->copy_bitmap_bytes == 0u, "with no copy alive there is no part");
    }
}

static void check_a_copy_travels_under_its_own_key(void)
{
    enemy_sync_state_t *s     = mp_enemy_sync_state();
    size_t              bytes = 0;

    ut_section("a copy travels under 256 + k and touches neither placement k nor a wish");
    {
        static const size_t KS[] = { 0u, 5u, 127u };
        size_t              n;

        for (n = 0; n < sizeof KS / sizeof KS[0]; ++n) {
            const size_t      k      = KS[n];
            const placement_t *row   = &s->placement[MP_WIRE_KEY_COPY_BASE + k];
            mp_enemy_record_t seen;
            size_t            length = k / 8u + 1u;
            size_t            at     = HEADER;

            fresh_send();
            poke_copy(k, 3u, 40u + (uint32_t)k);
            ut_check(encode(0u, sizeof s_block, &bytes),
                     "a host with a copy alive builds its block");
            commit_view(0u);
            ut_checkf(s_block[0] == 0u && s_block[at] == length &&
                          s_block[at + 1u + k / 8u] == (uint8_t)(1u << (k & 7u)) &&
                          s_block[at + 1u + length] == 1u,
                      "k = %u: no placement, a bitmap of %u byte(s) with the one bit, one record",
                      (unsigned)k, (unsigned)length);
            at += 2u + length;
            ut_checkf(s_block[at] == (uint8_t)(k & 0xFFu) &&
                          s_block[at + 1u] == (uint8_t)(k >> 8) && s_block[at + 2u] == 3u,
                      "k = %u travels in two bytes, then its generation", (unsigned)k);

            ut_check(take_block(s_block, bytes), "and a receiver takes the block");
            ut_check(mp_enemy_sync_mirror((uint32_t)(MP_WIRE_KEY_COPY_BASE + k), &seen) &&
                         seen.value[MP_ENEMY_F_POS_X] == row->current.value[MP_ENEMY_F_POS_X],
                     "the copy's mirror holds what the host sent, in the copy's own row");
            ut_check(!mp_enemy_sync_mirror((uint32_t)k, &seen),
                     "placement k heard nothing: the two names never meet");
            ut_check(mp_enemy_sync_pending() == 0u && !row->wanted && !row->dirty && !row->let_go,
                     "and the copy is not wished for, not due a write and not let go: its body "
                     "is the editor's to build");
        }
    }
}

static void check_a_local_body_is_not_the_hosts_copy(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    ut_section("a local body under a copy's key is not the host's copy");
    {
        placement_t *copy = &s->placement[MP_WIRE_KEY_COPY_BASE + 3u];
        placement_t *four = &s->placement[4];

        mp_enemy_sync_reset();
        copy->live  = true;
        copy->actor = 0x00A02000u;
        four->live  = true;
        four->actor = 0x00A03000u;
        ut_check(mp_enemy_sync_actor_for(MP_WIRE_KEY_COPY_BASE + 3u) == 0x00A02000u,
                 "the census knows a body under 259, which on a host is the host's own copy");
        ut_check(mp_enemy_sync_replica_for(MP_WIRE_KEY_COPY_BASE + 3u) == 0u,
                 "but as a replica of what the host names it is nobody's: this machine raised it, "
                 "and a removal or a hit the host sends for its copy 3 must not reach it");
        ut_check(mp_enemy_sync_replica_for(4u) == 0x00A03000u,
                 "a placement's replica is the census's actor, as it always was");
        mp_enemy_sync_reset();
    }
}

static void check_the_head_and_the_rank(void)
{
    enemy_sync_state_t *s     = mp_enemy_sync_state();
    const size_t        one   = one_placement();
    const size_t        room  = MP_ENEMY_SYNC_COPY_IDENTITY_BYTES + mp_enemy_wire_max_bytes();
    size_t              bytes = 0;

    ut_section("the part's head is reserved before the placements, and a copy goes by its rank");
    {
        const uint32_t crowded = s->copy_crowded;
        size_t         copy_one;
        const uint32_t parts   = s->copy_blocks_applied;
        const uint32_t records = s->copy_records_applied;
        const uint32_t unbuilt = s->copy_unbuilt;
        size_t         i;

        /* A copy alive whose record could not be read: its bit and the head, and no record. */
        fresh_send();
        poke_row(1u, 1u, 1u, 10u);
        s->copy_bitmap[0]    = 0x04u;
        s->copy_bitmap_bytes = 1u;
        ut_checkf(encode(0u, HEADER + HEAD_ONE_BYTE + one, &bytes) &&
                      bytes == HEADER + HEAD_ONE_BYTE + one && s_block[0] == 1u &&
                      s_block[HEADER + one] == 1u && s_block[HEADER + one + 2u] == 0u,
                  "a room for the head and one placement record takes both, to the byte (%u)",
                  (unsigned)bytes);
        mp_enemy_sync_abandon_for(0u);
        ut_checkf(encode(0u, HEADER + HEAD_ONE_BYTE + one - 1u, &bytes) &&
                      bytes == HEADER + HEAD_ONE_BYTE && s_block[0] == 0u,
                  "a byte less leaves the placement out and keeps the head (%u)", (unsigned)bytes);
        mp_enemy_sync_abandon_for(0u);
        ut_check(!encode(0u, HEADER + HEAD_ONE_BYTE - 1u, &bytes),
                 "and a room too small for the head builds no block at all, because a block "
                 "without the part says the copies are gone");
        ut_check(s->copy_crowded == crowded + 1u, "which is counted");

        /* A copy with a record to send has its rank like a placement, and needs the room its own
         * record takes. The walk before it kept the largest record a copy could be free for the
         * first copy, which left a placement out of a block that held both. */
        poke_copy(2u, 1u, 70u);
        copy_one = MP_ENEMY_SYNC_COPY_IDENTITY_BYTES +
                   whole_of(&s->placement[MP_WIRE_KEY_COPY_BASE + 2u].current);
        ut_checkf(encode(0u, HEADER + HEAD_ONE_BYTE + one + copy_one, &bytes) &&
                      bytes == HEADER + HEAD_ONE_BYTE + one + copy_one && s_block[0] == 1u &&
                      s_block[HEADER + one + 2u] == 1u,
                  "with a copy ready the placement and the copy both go, to the byte (%u)",
                  (unsigned)bytes);
        ut_check(copy_one < room, "in less than the largest record a copy could be");
        mp_enemy_sync_abandon_for(0u);
        ut_check(encode(0u, HEADER + HEAD_ONE_BYTE + one + copy_one - 1u, &bytes) &&
                     s_block[0] == 1u && s_block[HEADER + one + 2u] == 0u,
                 "a byte less gives the one record to the first in rank, the placement, which has "
                 "waited longer; the copy follows by what it gathers meanwhile");
        mp_enemy_sync_abandon_for(0u);
        ut_check(encode(0u, HEADER + HEAD_ONE_BYTE, &bytes) && bytes == HEADER + HEAD_ONE_BYTE &&
                     s_block[0] == 0u && s_block[HEADER + 2u] == 0u,
                 "a room for the head alone carries the head alone");
        mp_enemy_sync_abandon_for(0u);

        for (i = 1u; i < 10u; ++i) {
            poke_row(i + 1u, (uint8_t)(i + 1u), 1u, 10u + (uint32_t)i);
        }
        ut_check(encode(0u, 200u, &bytes) && take_block(s_block, bytes),
                 "with room for some placements and the copy the block still reads to its end");
        ut_check(s->copy_blocks_applied == parts + 1u &&
                     s->copy_records_applied == records + 1u && s->copy_unbuilt == unbuilt + 1u,
                 "and the receiver counted the part, its record, and that no copy is built here");
        mp_enemy_sync_abandon_for(0u);
    }
}

static void check_a_copy_is_settled_per_peer(void)
{
    enemy_sync_state_t *s     = mp_enemy_sync_state();
    size_t              bytes = 0;

    ut_section("a copy's record is settled like a placement's, with a walk of its own per peer");
    {
        size_t full = 0;
        size_t first_k;

        fresh_send();
        poke_copy(1u, 1u, 11u);
        (void)encode(0u, sizeof s_block, &full);
        mp_enemy_sync_abandon_for(0u);
        (void)encode(0u, sizeof s_block, &bytes);
        ut_check(bytes == full,
                 "after an abandon the record goes whole again: the peer never got the first");
        mp_enemy_sync_abandon_for(0u);
        commit_view(0u);
        (void)encode(0u, sizeof s_block, &bytes);
        ut_check(bytes == full, "and a commit after the abandon adopts nothing");
        commit_view(0u);
        (void)encode(0u, sizeof s_block, &bytes);
        ut_checkf(bytes < full,
                  "after a commit it is a delta against what the peer holds (%u < %u bytes)",
                  (unsigned)bytes, (unsigned)full);
        mp_enemy_sync_abandon_for(0u);

        /* Four copies and a room for exactly one record: each block carries the next. */
        poke_copy(2u, 1u, 12u);
        poke_copy(3u, 1u, 13u);
        poke_copy(4u, 1u, 14u);
        mp_enemy_sync_forget_view(0u);
        mp_enemy_sync_forget_view(1u);
        (void)encode(0u, full, &bytes);
        first_k = s_block[HEADER + 3u];
        commit_view(0u);
        (void)encode(0u, full, &bytes);
        ut_checkf(s_block[HEADER + 2u] == 1u && s_block[HEADER + 3u] == first_k + 1u,
                  "the next block for that peer starts behind k = %u", (unsigned)first_k);
        commit_view(0u);
        ut_check(s->view[0].next == MP_WIRE_KEY_COPY_BASE + first_k + 2u,
                 "the cursor stands behind the copy written last: placements and copies are one "
                 "ranking now and share it");
        (void)encode(1u, full, &bytes);
        ut_check(s_block[HEADER + 3u] == first_k,
                 "while another peer's walk starts where its own stopped");
        mp_enemy_sync_abandon_for(1u);
    }
}

static void check_a_torn_part(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    ut_section("a torn copies' part refuses the whole block, its good placements included");
    {
        const uint32_t    refused = s->copy_refused;
        mp_enemy_record_t copy;
        mp_enemy_record_t seen;
        size_t            at;

        full_record(&copy, 5u, 1u, 30u, 100);

        mp_enemy_sync_reset();
        mp_enemy_sync_set_level(true, HERE);
        at = placement_then_part(1u, 1u);
        at += put_copy(s_block + at, sizeof s_block - at, 5u, &copy, NULL);
        ut_check(take_block(s_block, at), "the well formed block is taken");
        ut_check(mp_enemy_sync_mirror(4u, &seen) && mp_enemy_sync_mirror(261u, &seen),
                 "with its placement and its copy");

        mp_enemy_sync_reset();
        mp_enemy_sync_set_level(true, HERE);
        ut_check(!take_block(s_block, at - 1u), "one byte short of the copy is refused");
        ut_check(!mp_enemy_sync_mirror(4u, &seen),
                 "and the placement in front of it was NOT applied: the first pass reads the "
                 "whole block before anything changes");
        s_block[at] = 0u;
        ut_check(!take_block(s_block, at + 1u), "a byte behind the part is refused");

        at = placement_then_part((uint8_t)(MP_ENEMY_SYNC_COPY_BITMAP_BYTES + 1u), 0u);
        ut_check(!take_block(s_block, at),
                 "a bitmap longer than the wire names is refused");
        at = placement_then_part(0u, 0u);
        ut_check(!take_block(s_block, at),
                 "an empty bitmap is refused: the host sends the part only with a copy alive");
        at = placement_then_part(1u, 0u);
        ut_check(!take_block(s_block, at - 1u), "a part without its count is refused");
        at = placement_then_part(1u, 1u);
        ut_check(!take_block(s_block, at + 1u) && !take_block(s_block, at + 2u),
                 "and so is a record torn inside its three bytes of identity");
        at = placement_then_part(1u, 1u);
        at += put_copy(s_block + at, sizeof s_block - at, 8u, &copy, NULL);
        ut_check(!take_block(s_block, at), "a k past its bitmap is refused");
        at = placement_then_part(1u, 1u);
        at += put_copy(s_block + at, sizeof s_block - at, 0x0105u, &copy, NULL);
        ut_check(!take_block(s_block, at),
                 "a k whose high byte is set is read whole, not as its low byte");
        at = placement_then_part(1u, 2u);
        at += put_copy(s_block + at, sizeof s_block - at, 5u, &copy, NULL);
        ut_check(!take_block(s_block, at),
                 "two records claimed and one carried is refused");
        at += put_copy(s_block + at, sizeof s_block - at, 5u, &copy, NULL);
        ut_check(!take_block(s_block, at),
                 "and the same copy named twice is refused, because the sender's second record "
                 "would be a delta against its first");
        ut_check(!mp_enemy_sync_mirror(4u, &seen) && !mp_enemy_sync_mirror(261u, &seen),
                 "none of those left anything behind");
        ut_checkf(s->copy_refused == refused + 11u,
                  "each of the eleven counted as refused for its copies' part (%u)",
                  (unsigned)(s->copy_refused - refused));
    }
}

int main(void)
{
    mp_enemy_sync_set_enabled(true);

    check_no_copy_leaves_the_block_as_it_was();
    check_the_census();
    check_a_copy_travels_under_its_own_key();
    check_a_local_body_is_not_the_hosts_copy();
    check_the_head_and_the_rank();
    check_a_copy_is_settled_per_peer();
    check_a_torn_part();
    check_the_host_table();
    check_the_client_table();

    ut_section("the report runs");
    mp_enemy_sync_report();
    ut_check(true, "reporting after all of the above is not a fault");

    return ut_summary("mp_enemy_sync_copies");
}
