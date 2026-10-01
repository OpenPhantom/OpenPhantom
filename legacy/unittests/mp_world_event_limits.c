/* mp_world_event_limits.c: what the host of the world events refuses, drops and counts.
 *
 * The world, the census and the log are the stand-in's, shared with mp_world_event. This file
 * holds the host at its edges: a full part in the room a four player session really leaves, a
 * substep with more events than it takes, the events no census settles, and an actor the census met
 * and read under no key. Where the host's own report is the only witness, its counters are read
 * back out of the log the stand-in keeps.
 *
 * What would be silent if it were wrong:
 *
 *   a full part that pushes the records out of a payload the size the bridge really gives;
 *   a burst of script sounds that costs a limb or a blast in the same substep;
 *   an event of a substep with no census, or of an actor nothing named, carried to a peer;
 *   a reset that leaves a client's queue or the host's ring behind;
 *   and an event put on a key's life although the census read another actor under that key.
 */
#include "unittest.h"

#include "mp_world_event_stand_in.h"

#include "mp_enemy_sync.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The room one peer's enemy block gets in a four player session: the 1168 byte payload, less the
 * block's length, the snapshot's header and three far bodies at their largest. */
#define FOUR_PLAYER_ROOM (1168u - 2u - 24u - 66u * 3u)

static void check_the_real_room(void)
{
    uint32_t tick;
    size_t   key;
    size_t   frame;
    unsigned posted = 0;

    ut_section("a full part in the room a four player session really leaves");
    ws_fresh_host();
    for (key = 1u; key <= 40u; ++key) {
        ws_enemy(key, 5.0f, 10.0f, 30.0f);
    }
    ws_census();
    tick = ws_send_blocks(1u, FOUR_PLAYER_ROOM);
    mp_enemy_sync_acked_for(0u, tick, 0u);
    for (key = 1u; key <= MP_WORLD_EVENT_POSTS_PER_SUBSTEP + 1u; ++key) {
        posted += ws_post(MP_WORLD_EVENT_NPC_CLANG, key, 1u) ? 1u : 0u;
    }
    ut_checkf(posted == MP_WORLD_EVENT_POSTS_PER_SUBSTEP,
              "a host takes %u events in a substep and refuses the next", posted);
    ws_census();
    frame = mp_enemy_sync_frame_bytes(0u);
    ut_checkf(frame == MP_ENEMY_SYNC_HEADER_BYTES + MP_WORLD_EVENT_PART_MAX * 10u,
              "the frame holds the head and a full part of 24 clangs at 10 bytes (%u)",
              (unsigned)frame);
    ut_check(FOUR_PLAYER_ROOM == 944u, "the room is 944 bytes");
    (void)ws_send_blocks(1u, FOUR_PLAYER_ROOM);
    ut_checkf(ws_events_in(0u) == MP_WORLD_EVENT_PART_MAX && ws.block[0][0] > 0u &&
                  ws.bytes[0] <= FOUR_PLAYER_ROOM,
              "the block carries 24 of them, records beside them, in the room (%u bytes)",
              (unsigned)ws.bytes[0]);
    tick = ws_send_blocks(1u, FOUR_PLAYER_ROOM);
    ut_check(ws_events_in(0u) == MP_WORLD_EVENT_PART_MAX,
             "the next block the same 24 again, the newest, while none is proved");
    mp_enemy_sync_acked_for(0u, tick, 0u);
    (void)ws_send_blocks(1u, FOUR_PLAYER_ROOM);
    ut_checkf(ws_events_in(0u) == MP_WORLD_EVENT_POSTS_PER_SUBSTEP - MP_WORLD_EVENT_PART_MAX,
              "and once they are proved the eight older ones that gave way (%u)",
              ws_events_in(0u));
}

static void check_the_exits(void)
{
    uint32_t tick;

    ut_section("an event no census settled is dropped, and the reset leaves nothing behind");
    ws_fresh_host();
    ws_enemy(7u, 5.0f, 10.0f, 30.0f);
    ws_census();
    (void)ws_post(MP_WORLD_EVENT_NPC_CLANG, 7u, 1u);
    mp_world_event_census_done();   /* a host with no peer runs no census */
    tick = ws_send_blocks(1u, sizeof ws.block[0]);
    ut_check(ws_events_in(0u) == 0u, "the event of a substep with no census is nobody's");
    ws.live[7] = false;
    (void)ws_post(MP_WORLD_EVENT_NPC_CLANG, 7u, 1u);
    tick = ws_send_blocks(1u, sizeof ws.block[0]);
    ut_check(ws_events_in(0u) == 0u,
             "nor is one whose actor the census did not read and whose key nothing named");
    ws.live[7] = true;
    (void)ws_post(MP_WORLD_EVENT_NPC_CLANG, 7u, 1u);
    tick = ws_send_blocks(1u, sizeof ws.block[0]);
    ws_client_takes(0u, tick);
    mp_enemy_sync_reset();
    ut_check(mp_world_event_flush() == 0u, "a reset empties a client's queue");
    ut_check(!mp_world_event_waits_on(0u, 7u, 1u), "and the host's ring");
}

/* The census walks every actor in the pool and reads, under each key, the last actor that carries
 * it. An event of an actor it met and did not read belongs to no life the host describes: it is
 * dropped as it always was, and counted on its own, while an actor the walk did not meet is gone,
 * which is the one case a life counted when it was posted may stand for. */
static void check_an_actor_passed_over(void)
{
    const uintptr_t beside = (uintptr_t)0x00B02000u;
    unsigned        passed;
    unsigned        unclaimed;

    ut_section("an event of an actor the census met and read under no key is its own count");
    ws_fresh_host();
    ws_enemy(7u, 5.0f, 10.0f, 30.0f);
    ws_census();
    passed    = ws_host_count(" for an actor the census passed over");
    unclaimed = ws_host_count(" for an actor the census did not read");
    ws.beside = beside;
    ut_check(mp_world_event_post_at_actor(MP_WORLD_EVENT_NPC_CLANG, beside, 1u, NULL, 0u),
             "a clang of an actor beside the one key 7 is read on is posted");
    (void)ws_send_blocks(1u, sizeof ws.block[0]);
    ut_check(ws_events_in(0u) == 0u, "no block carries it");
    ut_checkf(ws_host_count(" for an actor the census passed over") == passed + 1u &&
                  ws_host_count(" for an actor the census did not read") == unclaimed,
              "it is counted as passed over, not as unread (%u, %u)",
              ws_host_count(" for an actor the census passed over") - passed,
              ws_host_count(" for an actor the census did not read") - unclaimed);
    ws.beside = 0u;
    (void)mp_world_event_post_at_actor(MP_WORLD_EVENT_NPC_CLANG, beside, 1u, NULL, 0u);
    (void)ws_send_blocks(1u, sizeof ws.block[0]);
    ut_check(ws_host_count(" for an actor the census did not read") == unclaimed + 1u &&
                 ws_host_count(" for an actor the census passed over") == passed + 1u,
             "an actor the walk no longer meets is gone, and with no life noted it is unread");
}

/* 32 events a substep for every kind together. Script sounds come in bursts; a limb, a blast, an
 * emitter, a clang or a zap in the same substep takes the place of the newest sound, and only a
 * substep full of what is seen refuses one of them. */
static void check_a_full_substep(void)
{
    const float    at[3] = { 5.0f, 5.0f, 7.0f };
    const uint8_t  pitch[4] = { 0u, 0u, 0x80u, 0x3Fu };
    unsigned       refused;
    unsigned       refused_seen;
    unsigned       gave_way;
    unsigned       posted = 0u;
    uint32_t       tick;
    size_t         key;
    unsigned       i;

    ut_section("a full substep: what is seen takes the place of a sound, never the other way");
    ws_fresh_host();
    for (key = 1u; key <= 40u; ++key) {
        ws_enemy(key, 5.0f, 10.0f, 30.0f);
    }
    ws_census();
    tick = ws_send_blocks(1u, sizeof ws.block[0]);
    mp_enemy_sync_acked_for(0u, tick, 0u);
    refused      = ws_host_count(" refused for a full substep or ring");
    refused_seen = ws_host_count(" of them a kind that is seen");
    gave_way     = ws_host_count(" that gave way in a full substep");
    for (key = 1u; key <= MP_WORLD_EVENT_POSTS_PER_SUBSTEP; ++key) {
        posted += ws_post(MP_WORLD_EVENT_SCRIPT_SOUND, key, 9u) ? 1u : 0u;
    }
    ut_checkf(posted == MP_WORLD_EVENT_POSTS_PER_SUBSTEP, "32 script sounds fill the substep (%u)",
              posted);
    ut_check(ws_post(MP_WORLD_EVENT_LIMB_FLY, 33u, 5u) &&
                 mp_world_event_post_at_actor_placed(MP_WORLD_EVENT_EXPLODE_AT, ws_actor_of(34u),
                                                     0u, at, 0.0f, pitch, sizeof pitch) &&
                 ws_post(MP_WORLD_EVENT_SCRIPT_EMITTER, 35u, 0x0102u) &&
                 ws_post(MP_WORLD_EVENT_NPC_CLANG, 36u, 1u) &&
                 ws_post(MP_WORLD_EVENT_ZAP_ARCS, 37u, 1u),
             "a limb, a blast, an emitter, a clang and a zap still find a place");
    ut_check(!ws_post(MP_WORLD_EVENT_SCRIPT_SOUND, 38u, 9u), "a 33rd sound does not");
    for (i = 0; i < MP_WORLD_EVENT_POSTS_PER_SUBSTEP - 5u; ++i) {
        (void)ws_post(MP_WORLD_EVENT_NPC_CLANG, 36u, 1u);
    }
    ut_check(!ws_post(MP_WORLD_EVENT_NPC_CLANG, 36u, 1u),
             "and a substep full of what is seen refuses the next clang");
    for (i = 0; i < 3u; ++i) {
        tick = ws_send_blocks(1u, sizeof ws.block[0]);
        ws_client_takes(0u, tick);
        (void)mp_world_event_flush();
        mp_enemy_sync_acked_for(0u, tick, 0u);
    }
    ut_checkf(ws.played[MP_WORLD_EVENT_LIMB_FLY] == 1u &&
                  ws.played[MP_WORLD_EVENT_EXPLODE_AT] == 1u &&
                  ws.played[MP_WORLD_EVENT_SCRIPT_EMITTER] == 1u &&
                  ws.played[MP_WORLD_EVENT_ZAP_ARCS] == 1u &&
                  ws.played[MP_WORLD_EVENT_NPC_CLANG] == MP_WORLD_EVENT_POSTS_PER_SUBSTEP - 4u,
              "the client performs every one that was kept (limb %u, blast %u, emitter %u, zap "
              "%u, clang %u)", ws.played[MP_WORLD_EVENT_LIMB_FLY],
              ws.played[MP_WORLD_EVENT_EXPLODE_AT], ws.played[MP_WORLD_EVENT_SCRIPT_EMITTER],
              ws.played[MP_WORLD_EVENT_ZAP_ARCS], ws.played[MP_WORLD_EVENT_NPC_CLANG]);
    ut_checkf(ws_host_count(" refused for a full substep or ring") == refused + 2u &&
                  ws_host_count(" of them a kind that is seen") == refused_seen + 1u &&
                  ws_host_count(" that gave way in a full substep") == gave_way + 32u,
              "two refused, one of them seen, and every sound gave way (%u, %u, %u)",
              ws_host_count(" refused for a full substep or ring") - refused,
              ws_host_count(" of them a kind that is seen") - refused_seen,
              ws_host_count(" that gave way in a full substep") - gave_way);
}

int main(void)
{
    check_the_real_room();
    check_the_exits();
    check_an_actor_passed_over();
    check_a_full_substep();
    return ut_summary("mp_world_event_limits");
}
