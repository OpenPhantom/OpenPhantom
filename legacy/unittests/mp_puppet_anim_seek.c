/* mp_puppet_anim_seek.c: a puppet's track moved to the sender's head, and its clip events.
 *
 * The moves are engine writes, so they are driven over a far body made up in memory rather than
 * over plain numbers: a seed inside the window left at frame 0, a sender behind held with the
 * engine's own bit and released when it has caught up, a hold another body in the bank must not
 * write through, and a sender ahead moved forward. With no game in the process nothing resolves,
 * so the track advance and the dispatcher are absent and the fallbacks are what runs.
 */
#include "unittest.h"

#include "mp_puppet_anim.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* A far body made up in memory for the moves: the object with its actor at +0x14, its thing at
 * +0x9C and its base slot at +0xEC; the thing's puppet at +0x18 with four tracks eight bytes in,
 * 0x14C apart; one clip whose descriptor names the keyframe at +0x3C and two events at +0x38, one
 * on frame 17 and one on frame 25, 0x60 apart. */
typedef struct far_body {
    uint8_t object[0x100];
    uint8_t thing[0x40];
    uint8_t puppet[8u + 4u * 0x14Cu];
    uint8_t actor[0x100];
    uint8_t clip[0x40];
    uint8_t events[2u * 0x60u];
    uint8_t keyframe[0x40];
    uint32_t table[1];
} far_body_t;

static far_body_t far_body;

static void put32(uint8_t *at, uint32_t value)
{
    memcpy(at, &value, sizeof value);
}

static uint32_t at32(const uint8_t *at)
{
    uint32_t value;

    memcpy(&value, at, sizeof value);
    return value;
}

static uint32_t addr_of(const void *p)
{
    return (uint32_t)(uintptr_t)p;
}

static uint8_t *far_track(void)
{
    return far_body.puppet + 8u;
}

static void build_far_body(void)
{
    float fps = 30.0f;

    memset(&far_body, 0, sizeof far_body);
    put32(far_body.object + 0x14, addr_of(far_body.actor));
    put32(far_body.object + 0x9C, addr_of(far_body.thing));
    put32(far_body.object + 0xEC, 0u);
    put32(far_body.thing + 0x18, addr_of(far_body.puppet));
    put32(far_body.actor + 0xC8, 1u);
    put32(far_body.actor + 0xE4, addr_of(far_body.table));
    far_body.table[0] = addr_of(far_body.clip);
    put32(far_body.clip + 0x1C, 2u);
    put32(far_body.clip + 0x38, addr_of(far_body.events));
    put32(far_body.clip + 0x3C, addr_of(far_body.keyframe));
    put32(far_body.events + 0x00, 17u);
    put32(far_body.events + 0x60, 25u);
    put32(far_body.keyframe + 0x34, 40u);
    put32(far_track() + 0x000, 0x3u);
    memcpy(far_track() + 0x010, &fps, sizeof fps);
    put32(far_track() + 0x128, addr_of(far_body.keyframe));
}

static void check_moves(void)
{
    mp_puppet_anim_counters_t  before;
    mp_puppet_anim_counters_t  after;
    mp_puppet_anim_channel_t   channel;
    mp_puppet_anim_decision_t  back;
    mp_puppet_anim_decision_t  none;
    uint32_t                   object;

    ut_section("the moves of a running track, and their events");
    build_far_body();
    object = addr_of(far_body.object);
    mp_puppet_anim_counters(&before);

    mp_puppet_anim_seed(object, 0xECu, 5.0f);
    mp_puppet_anim_seed(object, 0xECu, 20.0f);
    mp_puppet_anim_counters(&after);
    ut_check(after.seeds_in_window == before.seeds_in_window + 1u && after.seeds == before.seeds,
             "five frames at thirty a second is inside the window and starts at frame 0; twenty "
             "is past it, and with no track advance nothing is seeded");

    memset(&channel, 0, sizeof channel);
    channel.live   = true;
    channel.head   = 20.0f;
    channel.puppet = addr_of(far_body.puppet);
    channel.slot   = 0u;
    channel.track  = addr_of(far_track());
    memset(&back, 0, sizeof back);
    back.action   = MP_PUPPET_ANIM_SEEK;
    back.frames   = 16.0f;
    back.distance = -4.0f;
    memset(&none, 0, sizeof none);
    none.action = MP_PUPPET_ANIM_NONE;

    mp_puppet_anim_seek(1u, object, &channel, &back);
    mp_puppet_anim_counters(&after);
    ut_check((at32(far_track()) & 0x10u) != 0u && after.holds == before.holds + 1u,
             "a sender four frames behind holds the track with the engine's own bit instead of "
             "setting it back");
    ut_check(after.events_not_twice == before.events_not_twice + 1u,
             "and the event on frame 17, between the sender's head and the track's, does not "
             "play a second time");

    mp_puppet_anim_settle_hold(1u, object, &channel, &back);
    mp_puppet_anim_seek(1u, object, &channel, &back);
    mp_puppet_anim_counters(&after);
    ut_check((at32(far_track()) & 0x10u) != 0u && after.holds == before.holds + 1u,
             "while the sender is still behind the hold stays, counted once");

    mp_puppet_anim_settle_hold(1u, object, &channel, &none);
    mp_puppet_anim_counters(&after);
    ut_check((at32(far_track()) & 0x10u) == 0u && after.released == before.released + 1u &&
                 (at32(far_track()) & 0x3u) == 0x3u,
             "when the sender has caught up the bit is cleared and nothing else of the flags");

    mp_puppet_anim_seek(2u, object, &channel, &back);
    mp_puppet_anim_settle_hold(2u, object + 4u, &channel, &none);
    ut_check((at32(far_track()) & 0x10u) != 0u,
             "another body in the bank drops the hold without writing into a track not its own");
    put32(far_track(), 0x3u);

    back.frames   = 26.0f;
    back.distance = 6.0f;
    mp_puppet_anim_seek(1u, object, &channel, &back);
    mp_puppet_anim_counters(&after);
    ut_check(after.seeks == before.seeks + 1u,
             "a sender ahead is a seek forward, written as two time words with no track advance");
}

int main(void)
{
    mp_puppet_anim_resolve();
    check_moves();
    return ut_summary("the moves of a puppet's track");
}
