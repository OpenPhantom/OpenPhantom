/* mp_puppet_anim_seek_engine.c: a puppet's track moved forward through an engine the test plays.
 *
 * The companion test runs the moves with nothing resolved. This one hands them the two routines a
 * forward move calls, played in memory the way the engine's are: the track advance moves the time
 * words by the frames it is given, unless the track carries the engine's own hold bit, and answers
 * the frames either way; the dispatcher fires the clip's events over the span behind the track's
 * time. The far body is the same one: a clip with events on frames 17 and 25.
 *
 * What would be silent if it were wrong: a forward move over a track the engine holds, at the
 * end of a clip that releases there, at the end of a fade that keeps its slot, or on a held clip,
 * would fire the span behind a head that did not move, and the clang or the footfall there would
 * play a second time.
 */
#include "unittest.h"

#include "mp_cells.h"
#include "mp_puppet_anim.h"
#include "mp_signatures.h"
#include "mp_signatures_clip.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define TRACK_BYTES   0x14Cu
#define TRACK_FLAGS   0x000u
#define TRACK_FPS     0x010u
#define TRACK_TIME    0x120u
#define TRACK_PREV    0x124u
#define TRACK_HELD    0x010u
#define TRACK_PLAYING 0x003u

typedef struct far_body {
    uint8_t  object[0x100];
    uint8_t  thing[0x40];
    uint8_t  puppet[8u + 4u * TRACK_BYTES];
    uint8_t  actor[0x100];
    uint8_t  clip[0x40];
    uint8_t  events[2u * 0x60u];
    uint8_t  keyframe[0x40];
    uint32_t table[1];
} far_body_t;

static far_body_t far_body;

typedef struct dispatched {
    unsigned calls;
    float    frames;
    float    time;   /* the track's time the span ends at */
} dispatched_t;

static dispatched_t dispatched;
static float        frame_delta = 1.0f / 32.0f;

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

static void put_f(uint8_t *at, float value)
{
    memcpy(at, &value, sizeof value);
}

static float at_f(const uint8_t *at)
{
    float value;

    memcpy(&value, at, sizeof value);
    return value;
}

static uint32_t addr_of(const void *p)
{
    return (uint32_t)(uintptr_t)p;
}

static uint8_t *track_at(uint32_t puppet, uint32_t slot)
{
    return (uint8_t *)(uintptr_t)puppet + 8u + slot * TRACK_BYTES;
}

/* ==============================================================================================
 * The engine the moves call.
 * ============================================================================================ */

/* The single track advance: a track with no flags is not a track; a held one is not moved; the
 * frames it was handed come back whether or not it moved. */
static float __cdecl fake_update_track(void *puppet, float dt, uint32_t slot, float frames)
{
    uint8_t *track = track_at(addr_of(puppet), slot);
    uint32_t flags = at32(track + TRACK_FLAGS);

    (void)dt;
    if (flags == 0u) {
        return 0.0f;
    }
    if ((flags & TRACK_HELD) == 0u) {
        put_f(track + TRACK_PREV, at_f(track + TRACK_TIME));
        put_f(track + TRACK_TIME, at_f(track + TRACK_TIME) + frames);
    }
    return frames;
}

static void __cdecl fake_dispatch(void *object, int32_t slot, float frames)
{
    (void)object;
    ++dispatched.calls;
    dispatched.frames = frames;
    dispatched.time   = at_f(track_at(addr_of(far_body.puppet), (uint32_t)slot) + TRACK_TIME);
}

uintptr_t mp_signatures_address(mp_site_t site)
{
    return site == MP_SITE_RDPUPPET_UPDATE_TRACK ? (uintptr_t)&fake_update_track : 0u;
}

uintptr_t mp_signatures_clip_address(mp_clip_site_t site)
{
    return site == MP_CLIP_SITE_DISPATCH_EVENTS ? (uintptr_t)&fake_dispatch : 0u;
}

uintptr_t mp_cells_address(mp_cell_t cell)
{
    return cell == MP_CELL_FRAME_DELTA ? (uintptr_t)&frame_delta : 0u;
}

/* What the moves ask of the puppet module, which this test does not link: the base track, whether
 * a mode word loops, and the histogram's bucket. */
bool mp_puppet_anim_track_of(uint32_t object, uint32_t slot_offset, uint32_t *puppet,
                             uint32_t *slot, uint32_t *track)
{
    (void)object;
    (void)slot_offset;
    *puppet = addr_of(far_body.puppet);
    *slot   = 0u;
    *track  = addr_of(track_at(*puppet, 0u));
    return true;
}

bool mp_puppet_anim_loops(uint32_t mode_flags)
{
    return (mode_flags & 0x2u) == 0u;
}

size_t mp_puppet_anim_seek_bucket(float distance)
{
    (void)distance;
    return 0u;
}

/* ==============================================================================================
 * The far body.
 * ============================================================================================ */

static void build_far_body(float time)
{
    memset(&far_body, 0, sizeof far_body);
    put32(far_body.object + 0x14, addr_of(far_body.actor));
    put32(far_body.object + 0x9C, addr_of(far_body.thing));
    put32(far_body.thing + 0x18, addr_of(far_body.puppet));
    put32(far_body.actor + 0xC8, 1u);
    put32(far_body.actor + 0xE4, addr_of(far_body.table));
    far_body.table[0] = addr_of(far_body.clip);
    put32(far_body.clip + 0x1C, 2u);
    put32(far_body.clip + 0x38, addr_of(far_body.events));
    put32(far_body.clip + 0x3C, addr_of(far_body.keyframe));
    put32(far_body.events + 0x00, 17u);
    put32(far_body.events + 0x60, 25u);
    put32(track_at(addr_of(far_body.puppet), 0u) + TRACK_FLAGS, TRACK_PLAYING);
    put_f(track_at(addr_of(far_body.puppet), 0u) + TRACK_FPS, 30.0f);
    put_f(track_at(addr_of(far_body.puppet), 0u) + TRACK_TIME, time);
    put_f(track_at(addr_of(far_body.puppet), 0u) + TRACK_PREV, time);
    put32(track_at(addr_of(far_body.puppet), 0u) + 0x128u, addr_of(far_body.keyframe));
    memset(&dispatched, 0, sizeof dispatched);
}

static mp_puppet_anim_channel_t channel_at(float head)
{
    mp_puppet_anim_channel_t channel;

    memset(&channel, 0, sizeof channel);
    channel.live       = true;
    channel.head       = head;
    channel.mode_flags = 0x2u;   /* a clip that does not loop */
    channel.puppet     = addr_of(far_body.puppet);
    channel.slot       = 0u;
    channel.track      = addr_of(track_at(channel.puppet, 0u));
    return channel;
}

static mp_puppet_anim_decision_t forward_to(float frames, float distance)
{
    mp_puppet_anim_decision_t decision;

    memset(&decision, 0, sizeof decision);
    decision.action   = MP_PUPPET_ANIM_SEEK;
    decision.frames   = frames;
    decision.distance = distance;
    return decision;
}

static void check_a_forward_move(void)
{
    mp_puppet_anim_counters_t before;
    mp_puppet_anim_counters_t after;
    mp_puppet_anim_channel_t  channel;
    mp_puppet_anim_decision_t decision;
    uint32_t                  object;

    ut_section("a forward move fires the events it passed, once, and only when the track moved");
    ut_check(mp_puppet_anim_seek_resolve(), "the advance and the dispatcher resolve to the "
                                            "engine this test plays");

    build_far_body(20.0f);
    object   = addr_of(far_body.object);
    channel  = channel_at(20.0f);
    decision = forward_to(26.0f, 6.0f);
    mp_puppet_anim_seek_counters(&before);
    mp_puppet_anim_seek(1u, object, &channel, &decision);
    mp_puppet_anim_seek_counters(&after);
    ut_checkf(at_f(track_at(channel.puppet, 0u) + TRACK_TIME) == 26.0f && dispatched.calls == 1u &&
                  dispatched.frames == 6.0f,
              "a running track is advanced to the sender's head and the dispatcher goes over the "
              "six frames behind it (%u call(s))", dispatched.calls);
    ut_checkf(after.events_fired == before.events_fired + 1u &&
                  after.jumps_fired == before.jumps_fired + 1u,
              "the event on frame 25 fired once (%u)",
              (unsigned)(after.events_fired - before.events_fired));

    build_far_body(26.0f);
    put32(track_at(addr_of(far_body.puppet), 0u) + TRACK_FLAGS, TRACK_PLAYING | TRACK_HELD);
    channel  = channel_at(26.0f);
    decision = forward_to(30.0f, 4.0f);
    mp_puppet_anim_seek_counters(&before);
    mp_puppet_anim_seek(1u, object, &channel, &decision);
    mp_puppet_anim_seek_counters(&after);
    ut_checkf(at_f(track_at(channel.puppet, 0u) + TRACK_TIME) == 26.0f,
              "a track the engine holds does not move, although the advance answers the frames "
              "it was handed (%.1f)", (double)at_f(track_at(channel.puppet, 0u) + TRACK_TIME));
    ut_checkf(dispatched.calls == 0u && after.events_fired == before.events_fired &&
                  after.jumps_fired == before.jumps_fired,
              "so nothing fires: the span behind a head that stood still holds the event on frame "
              "25 it already played (%u call(s), %u event(s))", dispatched.calls,
              (unsigned)(after.events_fired - before.events_fired));
    ut_checkf(after.held_in_place == before.held_in_place + 1u &&
                  after.no_dispatch == before.no_dispatch,
              "and the move is counted as one the engine's hold kept in place, not as a jump with "
              "no dispatcher (%u)", (unsigned)(after.held_in_place - before.held_in_place));
    ut_check((at32(track_at(channel.puppet, 0u) + TRACK_FLAGS) & TRACK_HELD) != 0u,
             "and the engine's hold is left where the engine put it");
}

int main(void)
{
    check_a_forward_move();
    return ut_summary("a puppet's track moved forward through the engine");
}
