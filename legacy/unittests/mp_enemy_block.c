/* mp_enemy_block.c: an NPC's blade clang, from the host's cooldown to one clang on a replica.
 *
 * Three properties carry the feature, and each fails without a word in the field. A host that
 * posted every call of the clang would send the calls its cooldown held, which played nothing
 * there, so a client would hear more clangs than the host. A client that played an event once per
 * block that carried it would play it several times. And a replica the engine cannot play on,
 * a body with no thing behind it, must never reach the engine, which asserts on it.
 *
 * The host half runs here for real: the verdict, the note and the world event it becomes, settled
 * by the census. The client half plays through the world events' own queue and this module's
 * player, with mp_sound stood in for by the test.
 */
#include "unittest.h"

#include "mp_enemy_block.h"
#include "mp_enemy_block_rule.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_sync_internal.h"
#include "mp_enemy_wire.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The replica a client plays a clang on: an actor with a body at +0x34, the body's thing at
 * +0x9C and the actor's position at +0xD0. The engine reads nothing else before the call. */
#define ACTOR_BODY  0x34u
#define BODY_THING  0x9Cu
#define ACTOR_POS   0xD0u

#define HERE 57u
#define KEY  9u

static uint32_t float_bits(float value)
{
    uint32_t bits;

    memcpy(&bits, &value, sizeof bits);
    return bits;
}

/* What mp_sound would do: the test is the site, and it counts. */
static uint32_t replays;
static int32_t  last_kind;
static bool     site_up;

static bool test_ready(void)
{
    return site_up;
}

static bool test_replay(uintptr_t actor, int32_t kind)
{
    (void)actor;
    ++replays;
    last_kind = kind;
    return true;
}

typedef struct fake_replica {
    uint8_t  actor[0x200];
    uint8_t  body[0x100];
    uint32_t thing;
} fake_replica_t;

static fake_replica_t s_replica;

static void build_replica(fake_replica_t *r)
{
    uint32_t body  = (uint32_t)(uintptr_t)r->body;
    uint32_t thing = (uint32_t)(uintptr_t)&r->thing;
    float    at[3] = { 10.0f, 20.0f, 3.0f };

    memset(r, 0, sizeof *r);
    memcpy(r->actor + ACTOR_BODY, &body, sizeof body);
    memcpy(r->body + BODY_THING, &thing, sizeof thing);
    memcpy(r->actor + ACTOR_POS, at, sizeof at);
}

static void check_the_gate(void)
{
    const float    world_time = 37.40625f;
    const uint32_t closed     = float_bits(world_time + 0.2f);

    ut_section("the cooldown's verdict is read off the cell before and after the call");
    ut_check(mp_enemy_block_gate(true, 0u, true, closed) == MP_ENEMY_BLOCK_PASSED,
             "a cell that moved from 0 to world time plus 0.2 let the call through");
    ut_check(mp_enemy_block_gate(true, closed, true, closed) == MP_ENEMY_BLOCK_HELD,
             "a cell that still holds the same bits held it: the engine wrote nothing");
    ut_check(mp_enemy_block_gate(false, 0u, true, closed) == MP_ENEMY_BLOCK_UNREAD,
             "a cell that did not read before is no verdict");
    ut_check(mp_enemy_block_gate(true, 0u, false, 0u) == MP_ENEMY_BLOCK_UNREAD,
             "and neither is one that did not read after");
    ut_check(mp_enemy_block_gate(true, float_bits(1.0f), true, float_bits(1.2f)) ==
                 MP_ENEMY_BLOCK_PASSED,
             "any movement of the bits is a pass, whatever the value was before");
}

static void check_the_packing(void)
{
    int32_t kind = -1;

    ut_section("the kind travels offset by one in the low byte");
    ut_check(mp_enemy_block_pack(0) == 1u && mp_enemy_block_pack(1) == 2u &&
                 mp_enemy_block_pack(2) == 3u,
             "a ricochet, blade on blade and blade on armour are 1, 2 and 3");
    ut_check(mp_enemy_block_pack(3) == 0u && mp_enemy_block_pack(-1) == 0u,
             "a kind the engine never passes is refused as 0, which reads as no clang");
    ut_check(mp_enemy_block_unpack(3u, &kind) && kind == 2, "the kind comes back out of it");
    ut_check(!mp_enemy_block_unpack(0x0700u, &kind), "a low byte of 0 is no clang");
}

/* A host in a level with the clanging actor alive under KEY, its census about to run. */
static void host_with_the_actor(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    mp_enemy_sync_reset();
    mp_enemy_sync_set_enabled(true);
    mp_enemy_sync_set_level(true, HERE);
    (void)mp_enemy_sync_begin_send();
    s->placement[KEY].live  = true;
    s->placement[KEY].actor = (uintptr_t)s_replica.actor;
}

static void check_only_what_passed_is_posted(void)
{
    enemy_sync_state_t *s     = mp_enemy_sync_state();
    uint32_t            actor = (uint32_t)(uintptr_t)s_replica.actor;

    ut_section("the host posts only a clang the engine's cooldown let through");
    host_with_the_actor();
    mp_enemy_block_note(actor, 0, MP_ENEMY_BLOCK_HELD);
    mp_enemy_block_note(actor, 1, MP_ENEMY_BLOCK_UNREAD);
    mp_enemy_sync_describe_census();
    ut_check(mp_world_event_part_bytes(0u) == 0u,
             "a call the cooldown held, and one whose verdict did not read, post nothing: they "
             "played nothing on the host");
    mp_world_event_census_done();

    mp_enemy_block_note(actor, 2, MP_ENEMY_BLOCK_PASSED);
    s->placement[KEY].was_live = true;
    mp_enemy_sync_describe_census();
    s->placement[KEY].current_ok = true;   /* what the census reads off a real actor */
    ut_check(mp_world_event_waits_on(0u, KEY, s->placement[KEY].generation) &&
                 mp_world_event_part_bytes(0u) == 10u,
             "a pass is one event at the actor, settled on its key and life by the census");
    mp_world_event_census_done();

    s->send_ready = false;
    mp_enemy_block_note(actor, 1, MP_ENEMY_BLOCK_PASSED);
    s->send_ready = true;
    mp_enemy_sync_describe_census();
    ut_check(mp_world_event_part_bytes(0u) == 10u,
             "a pass on a side that is not describing, a client's own blocker, is not posted");
    mp_world_event_census_done();
}

/* One clang of `kind` as a client receives it, performed from the queue. Returns what was
 * performed. */
static uint32_t client_receives(uint16_t sequence, int32_t kind)
{
    enemy_sync_state_t   *s = mp_enemy_sync_state();
    mp_world_event_t      event;
    mp_world_event_head_t head = { 1u, 0u, 0u };
    uint8_t               part[64];
    size_t                wrote = 0;
    size_t                at    = 0;

    memset(&event, 0, sizeof event);
    event.sequence = sequence;
    event.kind     = MP_WORLD_EVENT_NPC_CLANG;
    event.at_actor = true;
    event.key      = KEY;
    event.life     = s->placement[KEY].generation;
    event.a        = (uint16_t)mp_enemy_block_pack(kind);
    mp_world_event_put_head(&head, part);
    (void)mp_world_event_put(&event, part + MP_WORLD_EVENT_HEAD_BYTES,
                             sizeof part - MP_WORLD_EVENT_HEAD_BYTES, &wrote);
    if (mp_world_event_stage(part, MP_WORLD_EVENT_HEAD_BYTES + wrote, &at)) {
        mp_world_event_take_staged(100u + sequence);
    }
    return mp_world_event_flush();
}

static void check_the_replica(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    ut_section("a client plays the host's clang once, on the replica, through mp_sound's site");
    host_with_the_actor();
    s->placement[KEY].generation = 1u;
    s->placement[KEY].known      = true;
    mp_enemy_block_set_replay(&test_ready, &test_replay);
    site_up = true;
    replays = 0u;
    ut_check(client_receives(1u, 1) == 1u && replays == 1u && last_kind == 1,
             "one clang, blade on blade, as the host had it");
    ut_check(client_receives(1u, 1) == 0u && replays == 1u,
             "the same clang in a second block is not played twice");

    site_up = false;
    ut_check(client_receives(2u, 0) == 0u && replays == 1u,
             "with no site to play through nothing is played, it is counted");
    site_up = true;

    memset(s_replica.body + BODY_THING, 0, sizeof(uint32_t));
    ut_check(client_receives(3u, 0) == 0u && replays == 1u,
             "a replica with no thing behind its body is never handed to the engine, which "
             "asserts on it");
    build_replica(&s_replica);
}

int main(void)
{
    build_replica(&s_replica);
    check_the_gate();
    check_the_packing();
    check_only_what_passed_is_posted();
    check_the_replica();
    return ut_summary("mp_enemy_block");
}
