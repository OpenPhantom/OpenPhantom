/* mp_enemy_body.c: how an enemy's body ends, carried in its record and written onto a replica.
 *
 * The first case is the one the old laying down got wrong. A death with the script's dissolve,
 * mode 2, takes the class away in the script's own death before it sets the state to 13; the old
 * rule, which read the state and not the body, took only the shadow in 13, so the replica stood
 * solid and opaque while the host's body dissolved and blocked nothing. Here the host's body
 * travels, and the replica gets what the host has. The old rule is kept below word for word as
 * the reference that disagrees.
 *
 * The replica is made up in this process's memory: a body with its flag word at +0x00, its class
 * at +0x04 and its shooter class at +0x08, a thing at +0x9C with the alpha at +0x150 and the
 * dissolve at +0x158, and for the host's count of starts a puppet at thing+0x18 whose four tracks
 * start eight bytes in, 0x14C apart, the base slot at body+0xEC.
 */
#include "unittest.h"

#include "mp_enemy_body.h"
#include "mp_enemy_body_rule.h"
#include "mp_enemy_dead_watch.h"
#include "mp_enemy_interest_rule.h"
#include "mp_enemy_wire.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define TRACK_STRIDE 0x14Cu

typedef struct fake {
    uint8_t body[0x100];
    uint8_t thing[0x160];
    uint8_t puppet[8u + 4u * TRACK_STRIDE];
    uint8_t keyframe_a[0x40];
    uint8_t keyframe_b[0x40];
    uint8_t actor[0x40];
} fake_t;

static fake_t replica;

static void put32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

static void putf(uint8_t *at, float value)
{
    memcpy(at, &value, sizeof value);
}

static uint32_t get32(const uint8_t *at)
{
    uint32_t value;

    memcpy(&value, at, sizeof value);
    return value;
}

static float getf(const uint8_t *at)
{
    float value;

    memcpy(&value, at, sizeof value);
    return value;
}

static uint32_t addr(const void *p)
{
    return (uint32_t)(uintptr_t)p;
}

static uint8_t *track(uint32_t slot)
{
    return replica.puppet + 8u + slot * TRACK_STRIDE;
}

/* A standing body: drawn, a shadow, a bit of another writer at 0x40, class and shooter class
 * `cls`, opaque and whole, playing slot 0 of a clip with 60 frames at 30 a second. */
static void build(int32_t cls)
{
    memset(&replica, 0, sizeof replica);
    put32(replica.body + 0x00, 0x43u);
    put32(replica.body + 0x04, (uint32_t)cls);
    put32(replica.body + 0x08, (uint32_t)cls);
    put32(replica.body + 0x9C, addr(replica.thing));
    put32(replica.body + 0xEC, 0u);
    putf(replica.thing + 0x150, 1.0f);
    putf(replica.thing + 0x158, 0.0f);
    put32(replica.thing + 0x18, addr(replica.puppet));
    put32(replica.keyframe_a + 0x34, 60u);
    put32(replica.keyframe_b + 0x34, 60u);
    put32(track(0) + 0x000, 0x3u);
    putf(track(0) + 0x010, 30.0f);
    put32(track(0) + 0x128, addr(replica.keyframe_a));
    put32(track(0) + 0x13C, 0x1u);   /* holds its end: the clip does not wrap */
}

static uint32_t body_addr(void)
{
    return addr(replica.body);
}

static mp_enemy_body_state_t state_of(bool drawn, bool solid, bool shadow, uint8_t alpha,
                                      uint8_t dissolve, uint8_t starts)
{
    mp_enemy_body_state_t s;

    s.has      = true;
    s.drawn    = drawn;
    s.solid    = solid;
    s.shadow   = shadow;
    s.alpha    = alpha;
    s.dissolve = dissolve;
    s.starts   = starts;
    return s;
}

static mp_enemy_record_t record_of(uint32_t generation, uint32_t state,
                                   const mp_enemy_body_state_t *body)
{
    mp_enemy_record_t r;

    memset(&r, 0, sizeof r);
    r.value[MP_ENEMY_F_GENERATION] = generation;
    r.value[MP_ENEMY_F_STATE]      = state;
    r.value[MP_ENEMY_F_BODY]       = mp_enemy_body_pack(body);
    return r;
}

/* The laying down this module replaces, as mp_enemy_bind.c had it. */
typedef struct old_marks {
    int32_t  objclass;
    uint32_t flags;
} old_marks_t;

static bool old_lay_rule(uint32_t state, old_marks_t *marks)
{
    switch (state) {
    case 11u:
    case 12u:
    case 14u:
        marks->objclass = 0;
        marks->flags &= ~0x02u;
        return true;
    case 13u:
        marks->flags &= ~0x02u;
        return true;
    default:
        return false;
    }
}

static void the_dissolving_death(void)
{
    mp_enemy_body_state_t host = state_of(true, false, false, 255u, 1u, 0u);
    mp_enemy_record_t     rec  = record_of(1u, 13u, &host);
    old_marks_t           old  = { 2, 0x43u };

    ut_section("mode 2: the host's body dissolves with its class already gone");
    build(2);
    (void)old_lay_rule(13u, &old);
    ut_check(old.objclass == 2,
             "the old rule kept the class in state 13, the reference this module disagrees with");

    mp_enemy_body_apply(0u, body_addr(), 7u, &rec, NULL);
    ut_checkf((int32_t)get32(replica.body + 0x04) == 0,
              "the replica's class is the host's, 0, so it blocks nothing (class %d)",
              (int)(int32_t)get32(replica.body + 0x04));
    ut_checkf(get32(replica.body + 0x00) == 0x41u,
              "its shadow goes as the host's did, and the drawn bit and the other writer's bit "
              "stay (flags %02X)", (unsigned)get32(replica.body + 0x00));
    ut_near(getf(replica.thing + 0x158), 1.0 / 255.0, 1e-6,
            "and it dissolves from the host's value, one step of 255");
    ut_near(getf(replica.thing + 0x150), 1.0, 1e-6, "still opaque, as the host is");
}

static void the_write_rule(void)
{
    mp_enemy_body_state_t   host;
    mp_enemy_body_state_t   acted;
    mp_enemy_body_state_t   local;
    mp_enemy_body_verdict_t v;

    ut_section("the write rule, part by part, in its four cases");
    host  = state_of(true, true, true, 255u, 0u, 0u);
    acted = host;
    local = host;
    v = mp_enemy_body_decide(&host, &acted, &local);
    ut_check(v.write == 0u && v.already == 0u && v.left == 0u,
             "the host's value stood still and the body has it: nothing to do");

    local.solid = false;
    v = mp_enemy_body_decide(&host, &acted, &local);
    ut_check(v.write == 0u && v.left == MP_ENEMY_BODY_PART_SOLID,
             "the host's value stood still and the body lost its class to its own death clip: "
             "left alone, not stood up again");

    host.solid = false;
    v = mp_enemy_body_decide(&host, &acted, &local);
    ut_check(v.write == 0u && v.already == MP_ENEMY_BODY_PART_SOLID,
             "the host's changed to what the body already has: counted, not written");

    local.solid = true;
    v = mp_enemy_body_decide(&host, &acted, &local);
    ut_check(v.write == MP_ENEMY_BODY_PART_SOLID && v.left == 0u,
             "the host's changed and the body has the old one: written");

    local = state_of(false, false, false, 10u, 10u, 0u);
    v = mp_enemy_body_decide(&host, NULL, &local);
    ut_check(v.write == (MP_ENEMY_BODY_PART_DRAWN | MP_ENEMY_BODY_PART_SHADOW |
                         MP_ENEMY_BODY_PART_ALPHA | MP_ENEMY_BODY_PART_DISSOLVE) &&
                 v.already == MP_ENEMY_BODY_PART_SOLID && v.left == 0u,
             "a first write takes every part the body does not have yet");

    host.has = false;
    v = mp_enemy_body_decide(&host, NULL, &local);
    ut_check(v.write == 0u && v.already == 0u && v.left == 0u,
             "a host that read no body says nothing about this one");

    ut_check(mp_enemy_body_class_for(true, 5) == 5 && mp_enemy_body_class_for(true, 2) == 2,
             "a solid body gets its own shooter class back, not 1");
    ut_check(mp_enemy_body_class_for(false, 5) == 0, "and a body that lies gets 0");

    host = state_of(false, true, true, 0u, 0u, 0u);
    ut_check(mp_enemy_body_flags_with(0xFFFFFFFFu, &host, MP_ENEMY_BODY_PART_DRAWN) ==
                 0xFFFFFFFEu,
             "only the drawn bit is written for the drawn part");
    ut_check(mp_enemy_body_flags_with(0xC4u, &host, MP_ENEMY_BODY_PART_SHADOW) == 0xC6u,
             "only the shadow bit for the shadow part; the pose matrix bit and the flicker stay");
    ut_check(mp_enemy_body_flags_with(0x47u, &host, MP_ENEMY_BODY_PART_SOLID) == 0x47u,
             "and none for any other part");
}

static void the_replica_over_time(void)
{
    mp_enemy_body_state_t standing = state_of(true, true, true, 255u, 0u, 0u);
    mp_enemy_body_state_t lying    = state_of(true, false, false, 255u, 0u, 0u);
    mp_enemy_record_t     a        = record_of(1u, 1u, &standing);
    mp_enemy_record_t     b;

    ut_section("a replica written record after record");
    build(7);
    mp_enemy_body_apply(0u, body_addr(), 9u, &a, NULL);
    ut_check((int32_t)get32(replica.body + 0x04) == 7, "a first write of a living host keeps 7");

    /* The replica's own death clip crosses its parameter event and takes the class, while the
     * host's record still says solid. */
    put32(replica.body + 0x04, 0u);
    mp_enemy_body_apply(0u, body_addr(), 9u, &a, &a);
    ut_check((int32_t)get32(replica.body + 0x04) == 0,
             "the replica's own death took the class, the host's stood still: left alone");

    b = record_of(1u, 11u, &lying);
    mp_enemy_body_apply(0u, body_addr(), 9u, &b, &a);
    ut_check((int32_t)get32(replica.body + 0x04) == 0 && (get32(replica.body) & 0x02u) == 0u,
             "then the host's lies too: the class it already lost stays lost, the shadow goes");

    a = record_of(2u, 1u, &standing);
    mp_enemy_body_apply(0u, body_addr(), 9u, &a, &b);
    ut_checkf((int32_t)get32(replica.body + 0x04) == 7 && (get32(replica.body) & 0x02u) != 0u,
              "a new life on the same body is a first write: class 7 and the shadow back (class "
              "%d)", (int)(int32_t)get32(replica.body + 0x04));

    put32(replica.body + 0x04, 0u);
    mp_enemy_body_forget(9u);
    mp_enemy_body_apply(0u, body_addr(), 9u, &a, &a);
    ut_check((int32_t)get32(replica.body + 0x04) == 7,
             "after the key is bound again its next write is a first one");

    put32(replica.body + 0x04, 0u);
    mp_enemy_body_reset();
    mp_enemy_body_apply(0u, body_addr(), 9u, &a, &a);
    ut_check((int32_t)get32(replica.body + 0x04) == 7,
             "and after a reset, which a savegame's load takes, too: the same address is not "
             "taken for the body before it");

    put32(replica.body + 0x04, 0u);
    mp_enemy_body_apply(0u, body_addr(), 9u, &a, NULL);
    ut_check((int32_t)get32(replica.body + 0x04) == 7,
             "a write with no record before it is a first one whatever was remembered");

    lying.alpha = 128u;
    b = record_of(2u, 12u, &lying);
    mp_enemy_body_apply(0u, body_addr(), 9u, &b, &a);
    ut_near(getf(replica.thing + 0x150), 128.0 / 255.0, 1e-6,
            "a fade on the host fades the replica: the alpha is the host's");
}

static void the_host_counts_starts(void)
{
    mp_enemy_record_t     r;
    mp_enemy_body_state_t read;

    ut_section("the host counts the starts of the base clip");
    mp_enemy_body_reset();
    build(3);
    memset(&r, 0, sizeof r);
    mp_enemy_body_read(0u, body_addr(), 4u, &r);
    mp_enemy_body_unpack(r.value[MP_ENEMY_F_BODY], &read);
    ut_check(read.has && read.drawn && read.solid && read.shadow && read.alpha == 255u &&
                 read.dissolve == 0u && read.starts == 0u,
             "a standing body reads drawn, solid, a shadow, opaque, whole, no start yet");

    putf(track(0) + 0x120, 20.0f);
    putf(track(0) + 0x124, 20.0f);
    mp_enemy_body_read(0u, body_addr(), 4u, &r);
    mp_enemy_body_unpack(r.value[MP_ENEMY_F_BODY], &read);
    ut_check(read.starts == 0u, "a head that ran on is no start");

    putf(track(0) + 0x120, 0.0f);
    putf(track(0) + 0x124, 0.0f);
    mp_enemy_body_read(0u, body_addr(), 4u, &r);
    mp_enemy_body_unpack(r.value[MP_ENEMY_F_BODY], &read);
    ut_check(read.starts == 1u,
             "a head back at 0 on a clip that holds its end is the same clip begun again");

    memcpy(track(1), track(0), TRACK_STRIDE);
    put32(replica.body + 0xEC, 1u);
    mp_enemy_body_read(0u, body_addr(), 4u, &r);
    mp_enemy_body_unpack(r.value[MP_ENEMY_F_BODY], &read);
    ut_check(read.starts == 2u, "a start in another slot, the crossfade's, is one too");

    put32(track(1) + 0x13C, 0x0u);
    putf(track(1) + 0x120, 58.0f);
    putf(track(1) + 0x124, 58.0f);
    mp_enemy_body_read(0u, body_addr(), 4u, &r);
    putf(track(1) + 0x120, 1.0f);
    putf(track(1) + 0x124, 1.0f);
    mp_enemy_body_read(0u, body_addr(), 4u, &r);
    mp_enemy_body_unpack(r.value[MP_ENEMY_F_BODY], &read);
    ut_check(read.starts == 2u, "a clip that loops coming round is no start");

    put32(track(1) + 0x128, addr(replica.keyframe_b));
    mp_enemy_body_read(0u, body_addr(), 4u, &r);
    mp_enemy_body_unpack(r.value[MP_ENEMY_F_BODY], &read);
    ut_check(read.starts == 3u, "another keyframe in the slot is a start");

    ut_check(mp_enemy_body_started(0, 1u, 10.0f, 0, 1u, 9.5f, false) == false,
             "half a frame back is the two reads rounding, no start");
}

static uint32_t next_random(uint32_t *seed)
{
    *seed = *seed * 1664525u + 1013904223u;
    return *seed;
}

static void the_field_round_trip(void)
{
    const uint32_t valid = 0x0F000000u | 0x00FF0000u | 0x0000FF00u | 0x0Fu;
    uint32_t       seed = 0x2921u;
    uint32_t       bad_state = 0;
    uint32_t       bad_field = 0;
    uint32_t       bad_wire = 0;
    uint32_t       i;

    ut_section("the field: packed, read back, and through the wire");
    for (i = 0; i < 5000u; ++i) {
        uint32_t              x = next_random(&seed);
        mp_enemy_body_state_t s = state_of((x & 1u) != 0u, (x & 2u) != 0u, (x & 4u) != 0u,
                                           (uint8_t)(x >> 8), (uint8_t)(x >> 16),
                                           (uint8_t)((x >> 24) & 0x0Fu));
        mp_enemy_body_state_t back;
        uint32_t              field = next_random(&seed) | MP_ENEMY_BODY_HAS;

        mp_enemy_body_unpack(mp_enemy_body_pack(&s), &back);
        bad_state += memcmp(&s, &back, sizeof s) != 0 ? 1u : 0u;
        mp_enemy_body_unpack(field, &back);
        bad_field += mp_enemy_body_pack(&back) != (field & valid) ? 1u : 0u;
    }
    ut_checkf(bad_state == 0u, "5000 states come back as they went (%u did not)",
              (unsigned)bad_state);
    ut_checkf(bad_field == 0u, "5000 fields come back with only the bits the layout names (%u did "
              "not)", (unsigned)bad_field);

    for (i = 0; i < 3000u; ++i) {
        mp_enemy_body_state_t s = state_of((i & 1u) != 0u, (i & 2u) != 0u, (i & 4u) != 0u,
                                           (uint8_t)next_random(&seed),
                                           (uint8_t)next_random(&seed), (uint8_t)(i & 0x0Fu));
        mp_enemy_record_t base = record_of(1u, 1u, &s);
        mp_enemy_record_t rec;
        mp_enemy_record_t out;
        uint8_t           buffer[128];
        size_t            bytes = 0;
        size_t            read = 0;

        s.solid = !s.solid;
        s.alpha = (uint8_t)next_random(&seed);
        rec     = record_of(1u, 13u, &s);
        if (!mp_enemy_wire_encode(&rec, (i & 8u) != 0u ? &base : NULL, buffer, sizeof buffer,
                                  &bytes) ||
            !mp_enemy_wire_decode(buffer, bytes, (i & 8u) != 0u ? &base : NULL, &out, &read) ||
            read != bytes || out.value[MP_ENEMY_F_BODY] != rec.value[MP_ENEMY_F_BODY]) {
            ++bad_wire;
        }
    }
    ut_checkf(bad_wire == 0u, "3000 records carry the field through the wire whole and as a delta "
              "(%u did not)", (unsigned)bad_wire);

    {
        mp_enemy_body_state_t up   = state_of(true, true, true, 255u, 0u, 0u);
        mp_enemy_body_state_t down = state_of(true, false, false, 200u, 9u, 3u);
        mp_enemy_record_t     a    = record_of(1u, 1u, &up);
        mp_enemy_record_t     b    = record_of(1u, 1u, &up);

        b.value[MP_ENEMY_F_BODY] = mp_enemy_body_pack(&down);
        ut_check(mp_enemy_interest_watched(&a) != mp_enemy_interest_watched(&b),
                 "the class the packer takes away is a bit the interest rule watches, so a body "
                 "laying down is a change a far key does not wait for");
        down = up;
        down.alpha    = 17u;
        down.dissolve = 40u;
        down.starts   = 9u;
        b.value[MP_ENEMY_F_BODY] = mp_enemy_body_pack(&down);
        ut_check(mp_enemy_interest_watched(&a) == mp_enemy_interest_watched(&b),
                 "and the alpha, the dissolve and the starts are not: they change in every "
                 "substep of a fade and would make every such key urgent");
    }
    ut_check(mp_enemy_body_quantise(NAN, 255u) == 255u && mp_enemy_body_quantise(NAN, 0u) == 0u,
             "a value that is not a number reads as the engine's start: opaque, whole");
    ut_check(mp_enemy_body_quantise(-1.0f, 7u) == 0u && mp_enemy_body_quantise(4.0f, 7u) == 255u,
             "outside 0 to 1 it is clamped");
    ut_check(mp_enemy_body_quantise(0.005f, 0u) == 1u,
             "the dissolve's first step, 0.005, is one step of 255 and not nothing");
    ut_near(mp_enemy_body_level(mp_enemy_body_quantise(0.25f, 0u)), 0.25, 0.5 / 255.0,
            "a level comes back within half a step");
    ut_check(mp_enemy_body_quantise(0.5f / 255.0f + 0.0001f, 0u) == 1u &&
                 mp_enemy_body_quantise(0.5f / 255.0f - 0.0001f, 0u) == 0u,
             "and rounds at the half step");
}

static void the_client_watch_without_a_clock(void)
{
    mp_enemy_body_state_t lying = state_of(true, true, true, 255u, 0u, 0u);
    mp_enemy_record_t     r = record_of(1u, 1u, &lying);
    uint8_t               actor[0x40];

    ut_section("the client's watch runs with no world clock");
    build(2);
    memset(actor, 0, sizeof actor);
    put32(actor + 0x34, body_addr());
    r.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(30);
    mp_enemy_dead_watch_replica(3u, 1u, addr(actor), &r);
    r.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(-2);
    mp_enemy_dead_watch_replica(3u, 1u, addr(actor), &r);
    mp_enemy_dead_watch_replica(3u, 1u, 0u, &r);
    mp_enemy_dead_watch_client_report(true);
    ut_check(true, "a life that falls and goes is read and reported with no clock and no table");
}

int main(void)
{
    the_dissolving_death();
    the_write_rule();
    the_replica_over_time();
    the_host_counts_starts();
    the_field_round_trip();
    the_client_watch_without_a_clock();
    mp_enemy_body_report(true, true);
    return ut_summary("mp_enemy_body");
}
