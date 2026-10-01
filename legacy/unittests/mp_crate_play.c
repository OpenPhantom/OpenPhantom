/* mp_crate_play.c: both push block machines, each driven by the other side played by hand.
 *
 * SIZE NOTE: over 600 lines: the small world behind the table of engine calls and two sides played
 * by hand. The client's half is the seam, with the world moved to a header the two share.
 *
 * The host's machine and the client's machine are compiled here as they ship. The engine under
 * them is a small world of this file's own, reached through the same table of calls mp_crate.c
 * fills with the engine's functions: a push moves a block, refuses past a wall, slides without
 * progress along a rail, and drops a block over an edge, which it tells the host's machine the
 * way the redirected drop does. The other side of the wire is never the other machine: each test
 * writes the messages that side would send with the codec and reads what comes back, so a
 * mistake in one machine cannot be covered by the same mistake in the other.
 *
 * Every message is built into a buffer of exactly one reliable message's size, the capacity the
 * real sender hands over.
 */
#include "unittest.h"

#include "mp_channel.h"
#include "mp_crate_client.h"
#include "mp_crate_host.h"
#include "mp_crate_play.h"
#include "mp_crate_rule.h"
#include "mp_crate_wire.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define LEVEL        78u
#define MOVERS       60u
#define PLAIN_BLOCK  39u     /* FEDSHIP 39: falls and lands, never sinks */
#define SINK_BLOCK   41u     /* FEDSHIP 41: sinks where it lands */
#define A_DOOR       10u
#define CLIENT_SLOT  1u
#define OTHER_SLOT   2u
#define BODILESS     3u

typedef struct fake_world {
    bool             used[MOVERS];
    mp_crate_body_t  body[MOVERS];
    bool             has_body[MP_CRATE_SLOT_LIMIT];
    float            wall_x;     /* a push that would end past this x is refused */
    float            rail_x;     /* past this x a push is slid back to where it started */
    float            edge_y;     /* past this y a block goes over and falls */
    mp_crate_host_t *host;       /* told of a fall, as the redirected drop tells it */
    uint32_t         tick;
    int32_t          fall_answer;
    unsigned         placed;
    unsigned         dropped;
    unsigned         sunk;
} fake_world_t;

static fake_world_t    s_world;
static mp_crate_host_t s_host;
static mp_crate_client_t s_client;
static uint8_t         s_buffer[MP_CHANNEL_MESSAGE_BYTES];
static unsigned        s_said;
static uint8_t         s_said_slot;

static void record_holder(uint8_t id, uint8_t slot)
{
    (void)id;
    ++s_said;
    s_said_slot = slot;
}

static bool fake_read(void *context, uint32_t id, mp_crate_body_t *out)
{
    fake_world_t *world = (fake_world_t *)context;

    if (id >= MOVERS || !world->used[id]) {
        return false;
    }
    *out = world->body[id];
    return true;
}

static int32_t fake_push(void *context, uint8_t slot, uint32_t id, float step[3])
{
    fake_world_t    *world = (fake_world_t *)context;
    mp_crate_body_t *block;
    float            to[3];

    if (slot >= MP_CRATE_SLOT_LIMIT || !world->has_body[slot]) {
        return -1;
    }
    block = &world->body[id];
    if ((block->flags & MP_CRATE_FLAG_FALLING) != 0u) {
        return 0;
    }
    to[0] = block->position[0] + step[0];
    to[1] = block->position[1] + step[1];
    to[2] = block->position[2];
    if (to[0] > world->wall_x) {
        return 0;
    }
    if (to[0] > world->rail_x) {
        step[0] = 0.0f;
        step[1] = 0.0f;
        return 1;
    }
    if (to[1] > world->edge_y) {
        block->flags = (uint8_t)(block->flags | MP_CRATE_FLAG_FALLING);
        if (mp_crate_rule_can_sink(block->rig_flags)) {
            block->flags = (uint8_t)(block->flags | MP_CRATE_FLAG_SINK);
        }
        mp_crate_host_fell(world->host, world->tick, id, to, step);
        memcpy(block->position, to, sizeof to);
        return 0;
    }
    memcpy(block->position, to, sizeof to);
    return 1;
}

static bool fake_place(void *context, uint32_t id, const float position[3], uint8_t host_flags)
{
    fake_world_t *world = (fake_world_t *)context;

    memcpy(world->body[id].position, position, 3u * sizeof(float));
    world->body[id].flags = mp_crate_rule_merge_flags(world->body[id].flags, host_flags);
    ++world->placed;
    return true;
}

static int32_t fake_fall(void *context, uint32_t id, const float position[3],
                         const float direction[2])
{
    fake_world_t *world = (fake_world_t *)context;

    (void)direction;
    if (world->fall_answer == 1) {
        world->body[id].flags = (uint8_t)(world->body[id].flags | MP_CRATE_FLAG_FALLING);
        memcpy(world->body[id].position, position, 3u * sizeof(float));
    }
    ++world->dropped;
    return world->fall_answer;
}

static bool fake_sink(void *context, uint32_t id)
{
    fake_world_t *world = (fake_world_t *)context;

    world->body[id].kind = (int32_t)MP_CRATE_KIND_SUNK;
    ++world->sunk;
    return true;
}

static const mp_crate_engine_t s_engine = {
    &s_world, &fake_read, &fake_push, &fake_place, &fake_fall, &fake_sink
};

static void put_block(uint32_t id, int32_t rig, float x, float y, float z)
{
    mp_crate_body_t *block = &s_world.body[id];

    s_world.used[id] = true;
    memset(block, 0, sizeof *block);
    block->kind      = (int32_t)MP_CRATE_KIND_BLOCK;
    block->rig_flags = rig;
    block->position[0] = x;
    block->position[1] = y;
    block->position[2] = z;
    memcpy(block->home, block->position, sizeof block->home);
    block->radius  = 0.5f;
    block->reveals = rig == 0x29 ? 53 : -1;
}

static void make_world(void)
{
    memset(&s_world, 0, sizeof s_world);
    put_block(PLAIN_BLOCK, 0x04, 128.5f, 131.5f, 35.5f);
    put_block(SINK_BLOCK, 0x29, 134.5f, 149.5f, 35.5f);
    s_world.used[A_DOOR]         = true;
    s_world.body[A_DOOR].kind    = 2;
    s_world.has_body[0]          = true;
    s_world.has_body[CLIENT_SLOT] = true;
    s_world.has_body[OTHER_SLOT] = true;
    s_world.wall_x      = 1000.0f;
    s_world.rail_x      = 1000.0f;
    s_world.edge_y      = 1000.0f;
    s_world.host        = &s_host;
    s_world.fall_answer = 1;
}

static size_t wish(uint8_t id, uint16_t sequence, uint8_t flags, float x, float y, float z,
                   uint16_t level)
{
    mp_crate_push_t push;

    memset(&push, 0, sizeof push);
    push.tick       = 1000u + sequence;
    push.level      = level;
    push.generation = 1u;
    push.id         = id;
    push.sequence   = sequence;
    push.flags      = flags;
    push.target[0]  = x;
    push.target[1]  = y;
    push.target[2]  = z;
    return mp_crate_push_encode(&push, s_buffer, sizeof s_buffer);
}

static bool host_note(uint32_t now, mp_crate_note_t *out)
{
    size_t bytes = mp_crate_host_next_note(&s_host, &s_engine, now, s_buffer, sizeof s_buffer);

    memset(out, 0, sizeof *out);
    if (bytes == 0u) {
        return false;
    }
    mp_crate_host_note_sent(&s_host, true, now);
    return mp_crate_note_decode(s_buffer, bytes, out);
}

static const mp_crate_entry_t *entry_for(const mp_crate_note_t *note, uint8_t id)
{
    size_t i;

    for (i = 0; i < note->count; ++i) {
        if (note->entry[i].id == id) {
            return &note->entry[i];
        }
    }
    return NULL;
}

/* Runs the host for `limit` substeps from `now`, and answers the substep after the last. */
static uint32_t run_host(uint32_t now, uint32_t limit)
{
    uint32_t i;

    for (i = 0; i < limit; ++i) {
        s_world.tick = now + i;
        mp_crate_host_run(&s_host, &s_engine, now + i);
    }
    return now + limit;
}

static void test_the_host_describes_its_blocks(void)
{
    mp_crate_note_t        note;
    const mp_crate_entry_t *entry;

    ut_section("the host: its blocks, found by their rig, described whole");
    make_world();
    mp_crate_host_init(&s_host);
    s_host.say_held = &record_holder;
    mp_crate_host_level(&s_host, &s_engine, LEVEL, 1u, MOVERS);
    ut_check(s_host.stats.blocks == 2u && s_host.stats.can_sink == 1u,
             "two push blocks in the level, one able to sink, and the door is none");
    ut_check(host_note(1u, &note) && note.whole && note.count == 2u && note.level == LEVEL,
             "the first note is whole and names both");
    entry = entry_for(&note, SINK_BLOCK);
    ut_check(entry != NULL && entry->pusher == MP_CRATE_NOBODY &&
                 entry->kind == MP_CRATE_KIND_BLOCK && entry->position[1] == 149.5f,
             "where the level put it, pushed by nobody");
    ut_check(!host_note(2u, &note), "nothing changed, nothing goes");
}

static void test_the_host_pushes_a_wish(void)
{
    mp_crate_note_t        note;
    const mp_crate_entry_t *entry;
    size_t                 bytes;
    uint32_t               now = 10u;

    ut_section("the host pushes a client's wish with that client's body");
    bytes = wish(PLAIN_BLOCK, 1u, MP_CRATE_PUSH_FORWARD, 128.6f, 131.5f, 35.5f, LEVEL);
    ut_check(mp_crate_host_take(&s_host, CLIENT_SLOT, s_buffer, bytes), "a wish is the host's");
    now = run_host(now, 1u);
    ut_check(s_host.stats.taken == 1u, "and is taken inside the next substep");
    ut_check(s_said == 1u && s_said_slot == CLIENT_SLOT,
             "and the block's new holder is said, once");
    ut_near(s_world.body[PLAIN_BLOCK].position[0], 128.5 + 1.25 / 64.0, 1e-5,
            "one step at the catch up pace, no more");
    ut_check(host_note(now, &note) && (entry = entry_for(&note, PLAIN_BLOCK)) != NULL &&
                 entry->pusher == CLIENT_SLOT && entry->sequence == 1u &&
                 entry->verdict == MP_CRATE_VERDICT_ON_THE_WAY,
             "the change note names the client, its wish and that it is on the way");
    now = run_host(now, 8u);
    ut_near(s_world.body[PLAIN_BLOCK].position[0], 128.6, 1.0 / 128.0,
            "within a few substeps the block stands where the wish put it");
    ut_check(s_host.stats.reached == 1u, "and it is counted reached");
    ut_check(host_note(now, &note) && (entry = entry_for(&note, PLAIN_BLOCK)) != NULL &&
                 entry->verdict == MP_CRATE_VERDICT_REACHED,
             "which the next note says");

    ut_check(!mp_crate_host_own_push(&s_host, PLAIN_BLOCK, now),
             "the host's own player may not push it while the client holds it");
    ut_check(s_host.stats.own_refused == 1u, "counted as refused by the owner rule");

    bytes = wish(PLAIN_BLOCK, 1u, MP_CRATE_PUSH_FORWARD, 129.0f, 131.5f, 35.5f, LEVEL);
    (void)mp_crate_host_take(&s_host, CLIENT_SLOT, s_buffer, bytes);
    bytes = wish(PLAIN_BLOCK, 7u, MP_CRATE_PUSH_FORWARD, 129.0f, 131.5f, 35.5f, 57u);
    (void)mp_crate_host_take(&s_host, CLIENT_SLOT, s_buffer, bytes);
    now = run_host(now, 1u);
    ut_check(s_host.stats.stale == 2u && s_host.stats.taken == 1u,
             "a sequence number already taken and a wish of another level are stale");

    bytes = wish(PLAIN_BLOCK, 2u, MP_CRATE_PUSH_FORWARD, 129.0f, 131.5f, 35.5f, LEVEL);
    (void)mp_crate_host_take(&s_host, OTHER_SLOT, s_buffer, bytes);
    now = run_host(now, 1u);
    ut_check(s_host.stats.other_owner == 1u, "another client is refused for the owner");

    bytes = wish(PLAIN_BLOCK, 2u, MP_CRATE_PUSH_LET_GO, 128.6f, 131.5f, 35.5f, LEVEL);
    (void)mp_crate_host_take(&s_host, CLIENT_SLOT, s_buffer, bytes);
    now = run_host(now, 2u);
    ut_check(mp_crate_host_own_push(&s_host, PLAIN_BLOCK, now),
             "a let-go frees the block at once, for the host's player too");
    mp_crate_host_end_substep(&s_host, now);
    mp_crate_host_end_substep(&s_host, now + 1u);
}

static void test_what_the_host_refuses(void)
{
    size_t   bytes;
    uint32_t now = 100u;

    ut_section("what the host refuses before the engine");
    bytes = wish((uint8_t)MOVERS, 1u, MP_CRATE_PUSH_FORWARD, 1.0f, 1.0f, 1.0f, LEVEL);
    (void)mp_crate_host_take(&s_host, OTHER_SLOT, s_buffer, bytes);
    bytes = wish(A_DOOR, 1u, MP_CRATE_PUSH_FORWARD, 1.0f, 1.0f, 1.0f, LEVEL);
    (void)mp_crate_host_take(&s_host, OTHER_SLOT, s_buffer, bytes);
    now = run_host(now, 1u);
    ut_check(s_host.stats.no_block == 2u,
             "the mover count as an index and a door are no push block");
    bytes = wish(SINK_BLOCK, 1u, MP_CRATE_PUSH_FORWARD, 134.6f, 149.5f, 35.5f, LEVEL);
    (void)mp_crate_host_take(&s_host, BODILESS, s_buffer, bytes);
    now = run_host(now, 1u);
    ut_check(s_host.stats.no_body == 1u && s_world.body[SINK_BLOCK].position[0] == 134.5f,
             "a slot with no body here is refused and nothing moves");
    ut_check(!mp_crate_host_take(&s_host, CLIENT_SLOT, s_buffer, MP_CRATE_PUSH_BYTES - 1u) &&
                 s_host.stats.torn == 0u,
             "a message of the wrong length is not a wish");
    s_buffer[0] = (uint8_t)MP_CRATE_PUSH_TAG;
    s_buffer[14] = 0x80u;
    ut_check(mp_crate_host_take(&s_host, CLIENT_SLOT, s_buffer, MP_CRATE_PUSH_BYTES) &&
                 s_host.stats.torn == 1u,
             "a wish that does not decode is the host's all the same, and counted torn");
}

static void test_the_engine_says_no(void)
{
    mp_crate_note_t        note;
    const mp_crate_entry_t *entry;
    size_t                 bytes;
    uint32_t               now = 200u;

    ut_section("the engine refuses, a wall slides, a block goes over the edge");
    s_world.wall_x = 134.55f;
    bytes = wish(SINK_BLOCK, 1u, MP_CRATE_PUSH_FORWARD, 134.8f, 149.5f, 35.5f, LEVEL);
    (void)mp_crate_host_take(&s_host, OTHER_SLOT, s_buffer, bytes);
    now = run_host(now, 6u);
    ut_check(s_host.stats.engine_refused == 1u, "a push into a wall is refused by the engine");
    (void)host_note(now, &note);
    entry = entry_for(&note, SINK_BLOCK);
    ut_check(entry != NULL && entry->verdict == MP_CRATE_VERDICT_ENGINE &&
                 entry->pusher == OTHER_SLOT,
             "and the note says so to its pusher");

    s_world.wall_x = 1000.0f;
    s_world.rail_x = 134.55f;
    bytes = wish(SINK_BLOCK, 2u, MP_CRATE_PUSH_FORWARD, 134.8f, 149.5f, 35.5f, LEVEL);
    (void)mp_crate_host_take(&s_host, OTHER_SLOT, s_buffer, bytes);
    now = run_host(now, 6u);
    ut_check(s_host.stats.stalled == 1u,
             "a push the engine slid back to where it began stops with no progress");

    s_world.rail_x = 1000.0f;
    s_world.edge_y = 149.55f;
    bytes = wish(SINK_BLOCK, 3u, MP_CRATE_PUSH_FORWARD, 134.5f, 149.8f, 35.5f, LEVEL);
    (void)mp_crate_host_take(&s_host, OTHER_SLOT, s_buffer, bytes);
    now = run_host(now, 6u);
    ut_check(s_host.stats.falls == 1u && s_host.stats.engine_refused == 1u,
             "over the edge is a fall, not a refusal, although the engine answered 0");
    bytes = mp_crate_host_next_fall(&s_host, s_buffer, sizeof s_buffer);
    {
        mp_crate_fall_t fall;

        ut_check(bytes == MP_CRATE_FALL_BYTES && mp_crate_fall_decode(s_buffer, bytes, &fall) &&
                     fall.id == SINK_BLOCK && fall.direction[1] > 0.99f &&
                     fall.position[1] > 149.55f,
                 "the fall goes out once, with where it went over and which way");
        mp_crate_host_fall_sent(&s_host, false);
        ut_check(s_host.stats.falls_waited == 1u &&
                     mp_crate_host_next_fall(&s_host, s_buffer, sizeof s_buffer) == bytes,
                 "a full channel keeps it for the next substep");
        mp_crate_host_fall_sent(&s_host, true);
        ut_check(mp_crate_host_next_fall(&s_host, s_buffer, sizeof s_buffer) == 0u &&
                     s_host.stats.falls_sent == 1u,
                 "and then it is gone");
    }
    (void)host_note(now, &note);
    entry = entry_for(&note, SINK_BLOCK);
    ut_check(entry != NULL && entry->verdict == MP_CRATE_VERDICT_REACHED &&
                 (entry->flags & MP_CRATE_FLAG_FALLING) != 0u,
             "and the wish counts reached: the block went where it was pushed");

    mp_crate_host_arrival(&s_host);
    ut_check(host_note(now + 1u, &note) && note.whole, "a peer that arrives gets a whole note");
}

/* ==============================================================================================
 * The client, with the host played by hand.
 * ============================================================================================ */

static size_t note_of(bool whole, uint32_t tick, uint32_t generation, uint16_t level,
                      const mp_crate_entry_t *entries, uint8_t count)
{
    mp_crate_note_t note;

    memset(&note, 0, sizeof note);
    note.tick       = tick;
    note.level      = level;
    note.generation = generation;
    note.whole      = whole;
    note.count      = count;
    memcpy(note.entry, entries, count * sizeof entries[0]);
    return mp_crate_note_encode(&note, s_buffer, sizeof s_buffer);
}

static mp_crate_entry_t resting(uint8_t id, float x, float y, float z)
{
    mp_crate_entry_t entry;

    memset(&entry, 0, sizeof entry);
    entry.id          = id;
    entry.kind        = (uint8_t)MP_CRATE_KIND_BLOCK;
    entry.position[0] = x;
    entry.position[1] = y;
    entry.position[2] = z;
    entry.pusher      = (uint8_t)MP_CRATE_NOBODY;
    return entry;
}

static void client_step(uint32_t now)
{
    mp_crate_client_level(&s_client, LEVEL);
    mp_crate_client_apply(&s_client, &s_engine, CLIENT_SLOT, now);
}

static void test_the_client_follows(void)
{
    mp_crate_entry_t e[2];
    size_t           bytes;
    uint32_t         now = 1u;

    ut_section("a client draws the host's blocks where the host has them");
    make_world();
    mp_crate_client_init(&s_client);
    e[0] = resting(PLAIN_BLOCK, 128.6f, 131.5f, 35.5f);
    e[1] = resting(SINK_BLOCK, 135.5f, 149.5f, 35.5f);
    bytes = note_of(false, 50u, 1u, LEVEL, e, 2u);
    ut_check(mp_crate_client_take(&s_client, s_buffer, bytes, now), "a note is the client's");
    ut_check(s_client.stats.change_before_whole == 1u,
             "a change note before any whole one is taken all the same, and counted");
    client_step(now++);
    ut_near(s_world.body[PLAIN_BLOCK].position[0], 128.5 + 1.25 / 64.0, 1e-5,
            "a block a tenth away is drawn a step toward the host's");
    ut_near(s_world.body[SINK_BLOCK].position[0], 135.5, 1e-5,
            "one a unit away is put there");
    ut_check(s_client.stats.chased == 1u && s_client.stats.jumped == 1u &&
                 fabsf(s_client.stats.worst_jump - 1.0f) < 1e-4f,
             "one chased and one jumped, the worst jump a unit");
    for (; now < 12u; ++now) {
        client_step(now);
    }
    ut_near(s_world.body[PLAIN_BLOCK].position[0], 128.6, 1.0 / 128.0,
            "within a few substeps it stands where the host has it");
    ut_check(s_client.stats.chase_steps >= 5u && s_client.stats.chase_steps <= 7u,
             "in the five to seven steps a tenth takes at the catch up pace");

    e[0] = resting(PLAIN_BLOCK, 128.6f, 131.5f, 35.5f);
    bytes = note_of(true, 40u, 1u, LEVEL, e, 1u);
    (void)mp_crate_client_take(&s_client, s_buffer, bytes, now);
    ut_check(s_client.stats.older == 1u,
             "a whole note older than the entry held does not undo it");
    bytes = note_of(true, 60u, 1u, 57u, e, 1u);
    (void)mp_crate_client_take(&s_client, s_buffer, bytes, now);
    client_step(now++);
    ut_check(s_client.stats.elsewhere == 1u, "a note of another level is refused");
    bytes = note_of(true, 70u, 3u, LEVEL, e, 1u);
    (void)mp_crate_client_take(&s_client, s_buffer, bytes, now);
    bytes = note_of(true, 80u, 2u, LEVEL, e, 1u);
    (void)mp_crate_client_take(&s_client, s_buffer, bytes, now);
    ut_check(s_client.stats.stale_generation == 1u,
             "and one from before a level change the host has already told of");
    client_step(now++);
    ut_check(s_client.stats.agreed >= 1u, "the block that arrived agrees");
}

static void test_the_client_sinks_only_for_the_host(void)
{
    mp_crate_entry_t e[1];
    size_t           bytes;
    uint32_t         now = 100u;

    ut_section("a client sinks a block only when the host did");
    mp_crate_client_hold_sink(&s_client, SINK_BLOCK);
    s_world.body[SINK_BLOCK].flags = MP_CRATE_FLAG_SINK;
    ut_check(s_client.stats.sinks_held == 1u, "its own landing is held");
    ut_check(mp_crate_client_gate(&s_client, &s_engine, SINK_BLOCK, CLIENT_SLOT, now) ==
                 MP_CRATE_GATE_FALLING_OR_SUNK,
             "and the block is not pushed on while it waits");
    e[0] = resting(SINK_BLOCK, 135.5f, 149.5f, 35.5f);
    bytes = note_of(true, 200u, 3u, LEVEL, e, 1u);
    (void)mp_crate_client_take(&s_client, s_buffer, bytes, now);
    client_step(now++);
    ut_check(s_client.stats.sinks_cancelled == 1u && s_world.sunk == 0u &&
                 (s_world.body[SINK_BLOCK].flags & MP_CRATE_FLAG_SINK) == 0u,
             "the host has it whole where it was: the landing is given up and the sink bit goes");

    e[0]      = resting(SINK_BLOCK, 136.0f, 149.5f, 34.0f);
    e[0].kind = (uint8_t)MP_CRATE_KIND_SUNK;
    e[0].flags = MP_CRATE_FLAG_SINK;
    bytes = note_of(false, 210u, 3u, LEVEL, e, 1u);
    (void)mp_crate_client_take(&s_client, s_buffer, bytes, now);
    client_step(now++);
    ut_check(s_world.sunk == 1u && s_client.stats.sinks_performed == 1u &&
                 s_world.body[SINK_BLOCK].position[2] == 34.0f,
             "the host sank it: put where the host has it and sunk here, once");
    client_step(now++);
    ut_check(s_world.sunk == 1u, "and not again");
}

static void test_a_fall_crosses_once(void)
{
    mp_crate_fall_t fall;
    size_t          bytes;
    uint32_t        now = 300u;

    ut_section("a fall runs here with the engine's own drop");
    memset(&fall, 0, sizeof fall);
    fall.tick         = 300u;
    fall.level        = LEVEL;
    fall.generation   = 3u;
    fall.id           = PLAIN_BLOCK;
    fall.position[0]  = 128.6f;
    fall.position[1]  = 132.0f;
    fall.position[2]  = 35.5f;
    fall.direction[1] = 1.0f;
    bytes = mp_crate_fall_encode(&fall, s_buffer, sizeof s_buffer);
    ut_check(mp_crate_client_take(&s_client, s_buffer, bytes, now), "a fall is the client's");
    client_step(now++);
    ut_check(s_world.dropped == 1u && s_client.stats.falls_performed == 1u &&
                 (s_world.body[PLAIN_BLOCK].flags & MP_CRATE_FLAG_FALLING) != 0u,
             "and drops the block here");
    bytes = mp_crate_fall_encode(&fall, s_buffer, sizeof s_buffer);
    (void)mp_crate_client_take(&s_client, s_buffer, bytes, now);
    client_step(now++);
    ut_check(s_client.stats.falls_already == 1u && s_world.dropped == 1u,
             "a second fall of a block already falling here is not dropped again");
    s_world.body[PLAIN_BLOCK].flags = 0u;
    s_world.fall_answer             = 2;
    bytes = mp_crate_fall_encode(&fall, s_buffer, sizeof s_buffer);
    (void)mp_crate_client_take(&s_client, s_buffer, bytes, now);
    client_step(now++);
    ut_check(s_client.stats.falls_otherwise == 1u,
             "a drop that answers otherwise here is counted and left to the host's next note");
    s_world.fall_answer = 1;
}

static size_t take_push(uint32_t now, mp_crate_push_t *out)
{
    size_t bytes = mp_crate_client_next_push(&s_client, LEVEL, 3u, now, s_buffer,
                                             sizeof s_buffer);

    if (bytes == 0u) {
        return 0u;
    }
    mp_crate_client_push_sent(&s_client, true, now);
    return mp_crate_push_decode(s_buffer, bytes, out) ? bytes : 0u;
}

/* One substep of this player pushing block `id` a sixty fourth along x, as the engine's player
 * phase does it, then the end of the substep and whatever wish is due. */
static size_t push_once(uint32_t now, uint32_t id, mp_crate_push_t *out)
{
    mp_crate_gate_t gate;

    client_step(now);
    gate = mp_crate_client_gate(&s_client, &s_engine, id, CLIENT_SLOT, now);
    if (gate == MP_CRATE_GATE_LET) {
        s_world.body[id].position[0] += 1.0f / 64.0f;
        mp_crate_client_pushed(&s_client, id, 1, &s_world.body[id], false, now);
    }
    mp_crate_client_end_substep(&s_client, now);
    return take_push(now, out);
}

static void test_the_client_pushes(void)
{
    mp_crate_push_t  push;
    mp_crate_entry_t e[1];
    size_t           bytes;
    uint32_t         now = 400u;
    unsigned         wishes = 0u;
    unsigned         limit;
    float            target_three[3];
    float            target_four[3];

    ut_section("this player's own push is a prediction the host answers");
    make_world();
    mp_crate_client_init(&s_client);
    e[0] = resting(PLAIN_BLOCK, 128.5f, 131.5f, 35.5f);
    bytes = note_of(true, 500u, 3u, LEVEL, e, 1u);
    (void)mp_crate_client_take(&s_client, s_buffer, bytes, now);

    ut_check(push_once(now++, PLAIN_BLOCK, &push) == MP_CRATE_PUSH_BYTES &&
                 push.sequence == 1u && push.flags == MP_CRATE_PUSH_FORWARD &&
                 fabsf(push.target[0] - (128.5f + 1.0f / 64.0f)) < 1e-4f,
             "the first push goes to the host at once, with where the block now stands");
    ut_check(push_once(now++, PLAIN_BLOCK, &push) == 0u &&
                 push_once(now++, PLAIN_BLOCK, &push) == 0u &&
                 push_once(now++, PLAIN_BLOCK, &push) == 0u,
             "the next three substeps send nothing");
    ut_check(push_once(now++, PLAIN_BLOCK, &push) == MP_CRATE_PUSH_BYTES && push.sequence == 2u,
             "the fourth sends the newest target");
    wishes = 2u;
    for (limit = 0u; wishes < 4u && limit < 64u; ++now, ++limit) {
        if (push_once(now, PLAIN_BLOCK, &push) != 0u) {
            ++wishes;
            memcpy(wishes == 3u ? target_three : target_four, push.target, sizeof target_three);
        }
    }
    ut_check(push.sequence == 4u, "four wishes out and none answered");
    ut_check(mp_crate_client_gate(&s_client, &s_engine, PLAIN_BLOCK, CLIENT_SLOT, now) ==
                 MP_CRATE_GATE_AHEAD,
             "and this player stops: four ahead of the host is the window");
    ut_check(s_client.stats.wishes_sent == 4u, "four wishes counted sent");

    e[0]          = resting(PLAIN_BLOCK, target_three[0], target_three[1], target_three[2]);
    e[0].pusher   = CLIENT_SLOT;
    e[0].sequence = 3u;
    e[0].verdict  = (uint8_t)MP_CRATE_VERDICT_REACHED;
    bytes = note_of(false, 520u, 3u, LEVEL, e, 1u);
    (void)mp_crate_client_take(&s_client, s_buffer, bytes, now);
    client_step(now);
    ut_check(s_client.stats.corrections == 0u,
             "the host reached wish three where it said: nothing to correct");
    ut_check(mp_crate_client_gate(&s_client, &s_engine, PLAIN_BLOCK, CLIENT_SLOT, now) ==
                 MP_CRATE_GATE_LET,
             "and the window is open again");

    e[0].position[0] = target_four[0] + 0.1f;
    e[0].sequence    = 4u;
    bytes = note_of(false, 530u, 3u, LEVEL, e, 1u);
    (void)mp_crate_client_take(&s_client, s_buffer, bytes, now);
    client_step(now++);
    ut_check(s_client.stats.corrections == 1u && s_client.stats.worst_correction > 0.09f,
             "a tenth off where wish four put it is one correction of this player's own block");

    e[0].verdict  = (uint8_t)MP_CRATE_VERDICT_ENGINE;
    e[0].sequence = 4u;
    bytes = note_of(false, 540u, 3u, LEVEL, e, 1u);
    (void)mp_crate_client_take(&s_client, s_buffer, bytes, now);
    client_step(now);
    ut_check(mp_crate_client_gate(&s_client, &s_engine, PLAIN_BLOCK, CLIENT_SLOT, now) ==
                 MP_CRATE_GATE_LOCKED,
             "a refusal keeps this player's push off the block");
    ut_check(mp_crate_client_gate(&s_client, &s_engine, PLAIN_BLOCK, CLIENT_SLOT, now + 33u) !=
                 MP_CRATE_GATE_LOCKED,
             "for a second");

    mp_crate_client_end_substep(&s_client, now);
    mp_crate_client_end_substep(&s_client, now + 1u);
    ut_check(take_push(now + 1u, &push) == MP_CRATE_PUSH_BYTES &&
                 (push.flags & MP_CRATE_PUSH_LET_GO) != 0u && s_client.stats.let_go_sent == 1u,
             "a player who stopped pushing lets go, once");
    ut_check(take_push(now + 2u, &push) == 0u, "and only once");

    e[0].verdict  = (uint8_t)MP_CRATE_VERDICT_ON_THE_WAY;
    e[0].pusher   = OTHER_SLOT;
    e[0].sequence = 9u;
    bytes = note_of(false, 550u, 3u, LEVEL, e, 1u);
    (void)mp_crate_client_take(&s_client, s_buffer, bytes, now + 40u);
    client_step(now + 40u);
    ut_check(mp_crate_client_gate(&s_client, &s_engine, PLAIN_BLOCK, CLIENT_SLOT, now + 41u) ==
                 MP_CRATE_GATE_OTHER_OWNER,
             "another player's block is not pushed here");
    ut_check(s_client.stats.refused_owner + s_client.stats.refused_ahead +
                     s_client.stats.refused_locked >= 1u,
             "every refusal is counted by its reason");
}

int main(void)
{
    test_the_host_describes_its_blocks();
    test_the_host_pushes_a_wish();
    test_what_the_host_refuses();
    test_the_engine_says_no();
    test_the_client_follows();
    test_the_client_sinks_only_for_the_host();
    test_a_fall_crosses_once();
    test_the_client_pushes();
    return ut_summary("the push block machines, each against the other side by hand");
}
