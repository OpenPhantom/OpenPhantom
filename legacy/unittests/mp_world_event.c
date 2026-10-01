/* mp_world_event.c: the world events from a hull on the host to a player on a client, delivered.
 *
 * The world, the census and the log are the stand-in's, shared with mp_world_event_limits. This
 * file holds the delivery: an event carried to every peer it concerns until an acknowledgement
 * proves it, taken once on a client and performed on its replica, and the single place rule the
 * events replaced riding along as the reference for one event in a window.
 *
 * What would be silent if it were wrong:
 *
 *   two events in one substep on one actor, of which a client performs one;
 *   an event a client performs twice because it rode two blocks;
 *   an event dropped from a peer's blocks before any acknowledgement proved it, or carried on
 *   after one did;
 *   a peer whose acknowledgements lag starving the others, or taking the others' proof as its own;
 *   an event that waits for a replica forever, or one dropped while its replica was still coming;
 *   a key a peer does not hold whose event arrives without the record that builds the replica;
 *   a sound heard by a peer out of earshot, or a block that says another peer's music;
 *   and an explosion, which carries its place, sent by that place rather than by the actor.
 */
#include "unittest.h"

#include "mp_world_event_stand_in.h"

#include "mp_enemy_interest_rule.h"
#include "mp_enemy_sync.h"
#include "mp_enemy_sync_internal.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ==============================================================================================
 * The single place rule the events replaced, as it stood, for the reference.
 * ============================================================================================ */

typedef struct old_row {
    bool     known;
    uint8_t  sequence;
    uint16_t what;
    uint8_t  age;
} old_row_t;

/* One substep of the old row: an event stamps it, anything else ages it. */
static void old_step(old_row_t *row, uint16_t what)
{
    if ((what & 0xFFu) == 0u) {
        row->age += row->age < 10u ? 1u : 0u;
        return;
    }
    row->sequence = row->sequence >= 255u ? 1u : (uint8_t)(row->sequence + 1u);
    row->what     = what;
    row->age      = 0u;
}

/* The client of the old rule: a new number inside the window fires. */
static bool old_fires(uint8_t *seen, const old_row_t *row)
{
    if (row->sequence == 0u || row->sequence == *seen) {
        return false;
    }
    *seen = row->sequence;
    return row->age < 10u;
}

/* Starts per substep for the old rule: two in one substep on one actor keep only the newer. */
static unsigned old_rule_fires(const unsigned *starts, size_t substeps)
{
    old_row_t row;
    uint8_t   seen  = 0u;
    unsigned  fired = 0;
    size_t    i;

    memset(&row, 0, sizeof row);
    for (i = 0; i < substeps; ++i) {
        old_step(&row, starts[i] != 0u ? 0x0105u : 0u);
        fired += old_fires(&seen, &row) ? 1u : 0u;
    }
    return fired;
}

static unsigned new_channel_fires(const unsigned *starts, size_t substeps)
{
    size_t   i;
    unsigned n;

    ws_fresh_host();
    ws_enemy(4u, 5.0f, 10.0f, 30.0f);
    ws_census();
    for (i = 0; i < substeps; ++i) {
        uint32_t tick;

        for (n = 0; n < starts[i]; ++n) {
            (void)ws_post(MP_WORLD_EVENT_SCRIPT_EMITTER, 4u, 0x0105u);
        }
        tick = ws_send_blocks(1u, sizeof ws.block[0]);
        ws_client_takes(0u, tick);
        (void)mp_world_event_flush();
        mp_enemy_sync_acked_for(0u, tick, 0u);
    }
    return ws.played[MP_WORLD_EVENT_SCRIPT_EMITTER];
}

static void check_against_the_old_rule(void)
{
    static const unsigned SPACED[20] = { 1, 0, 0, 0, 0, 0, 0, 1, 0, 0,
                                         0, 0, 0, 0, 1, 0, 0, 0, 0, 0 };
    static const unsigned PAIRED[20] = { 2 };

    ut_section("one event a window: the channel performs what the old field did");
    ut_check(old_rule_fires(SPACED, 20u) == 3u && new_channel_fires(SPACED, 20u) == 3u,
             "three starts seven substeps apart are three emitters on both");

    ut_section("two in one substep on one actor: the old field lost the first, the channel none");
    ut_check(old_rule_fires(PAIRED, 20u) == 1u,
             "the reference: a gonk's smoke and sparks in one substep came out as one");
    ut_check(new_channel_fires(PAIRED, 20u) == 2u, "the channel performs both");
}

/* ==============================================================================================
 * Delivery.
 * ============================================================================================ */

static void check_until_proved(void)
{
    uint32_t tick;
    uint32_t first;
    unsigned carried = 0;
    unsigned i;

    ut_section("an event rides every block to a peer until an acknowledgement proves it");
    ws_fresh_host();
    ws_enemy(9u, 5.0f, 10.0f, 30.0f);
    ws_census();
    tick = ws_send_blocks(1u, sizeof ws.block[0]);
    mp_enemy_sync_acked_for(0u, tick, 0u);
    ut_check(!ws_post(MP_WORLD_EVENT_KINDS, 9u, 1u), "a kind with no table row is not posted");
    ut_check(ws_post(MP_WORLD_EVENT_NPC_CLANG, 9u, 2u), "a clang is posted");
    first = ws_send_blocks(1u, sizeof ws.block[0]);
    carried += ws_events_in(0u);
    for (i = 0; i < 3u; ++i) {
        tick = ws_send_blocks(1u, sizeof ws.block[0]);
        carried += ws_events_in(0u);
    }
    ut_checkf(carried == 4u, "four blocks with no acknowledgement carry it four times (%u)",
              carried);
    mp_enemy_sync_acked_for(0u, first, 0u);
    (void)ws_send_blocks(1u, sizeof ws.block[0]);
    ut_check(ws_events_in(0u) == 0u,
             "the acknowledgement of the first block that carried it proves it: it rides no more");
    ut_check(mp_enemy_sync_state()->view[0].confirmed, "and the records' view is confirmed too");
    (void)tick;
}

/* The acknowledgement names the newest payload a client holds, and its bits the ones before it.
 * A block that carried an event and was only overtaken by a newer payload is proved by its bit,
 * even where the payload the acknowledgement names carried nothing of this view's. */
static void check_the_bits_prove_it(void)
{
    uint32_t tick;
    uint32_t first;

    ut_section("the bits of an acknowledgement prove the events of the blocks they name");
    ws_fresh_host();
    ws_enemy(9u, 5.0f, 10.0f, 30.0f);
    ws_census();
    tick = ws_send_blocks(1u, sizeof ws.block[0]);
    mp_enemy_sync_acked_for(0u, tick, 0u);
    ut_check(ws_post(MP_WORLD_EVENT_NPC_CLANG, 9u, 2u), "a clang is posted");
    first = ws_send_blocks(1u, sizeof ws.block[0]);
    ut_check(ws_events_in(0u) == 1u, "and the next block carries it");
    mp_enemy_sync_acked_for(0u, first + 1u, 0u);
    (void)ws_send_blocks(1u, sizeof ws.block[0]);
    ut_check(ws_events_in(0u) == 1u,
             "an acknowledgement of a substep that carried nothing proves nothing");
    tick = ws_send_blocks(1u, sizeof ws.block[0]);
    mp_enemy_sync_acked_for(0u, tick + 1u, 1u << (tick - first));
    (void)ws_send_blocks(1u, sizeof ws.block[0]);
    ut_check(ws_events_in(0u) == 0u,
             "one whose bits name the block that carried it proves it: it rides no more");
}

static void check_four_peers(void)
{
    uint32_t tick = 0;
    uint32_t t1   = 0;
    unsigned in0  = 0;
    unsigned in1  = 0;
    unsigned in2  = 0;
    unsigned i;

    ut_section("four players: three peers, each with its own acknowledgements");
    ws_fresh_host();
    ws_enemy(20u, 5.0f, 10.0f, 30.0f);
    ws_census();
    tick = ws_send_blocks(7u, sizeof ws.block[0]);
    mp_enemy_sync_acked_for(0u, tick, 0u);
    mp_enemy_sync_acked_for(1u, tick, 0u);
    mp_enemy_sync_acked_for(2u, tick, 0u);
    (void)ws_post(MP_WORLD_EVENT_LIMB_FLY, 20u, 5u);
    for (i = 0; i < MP_WORLD_EVENT_WINDOW + 2u; ++i) {
        tick = ws_send_blocks(7u, sizeof ws.block[0]);
        in0 += ws_events_in(0u);
        in1 += ws_events_in(1u);
        in2 += ws_events_in(2u);
        if (i == 0u) {
            mp_enemy_sync_acked_for(0u, tick, 0u);   /* peer 0 answers at once */
        }
        if (i == 0u) {
            t1 = tick;
        }
        if (i == 3u) {
            mp_enemy_sync_acked_for(1u, t1, 0u);     /* peer 1 answers late, for the first block */
        }
    }
    ut_checkf(in0 == 1u, "peer 0 acknowledged the first block, so it went once (%u)", in0);
    ut_checkf(in1 == 4u, "peer 1 acknowledged it three blocks late, so it went four times (%u)",
              in1);
    ut_checkf(in2 == MP_WORLD_EVENT_WINDOW,
              "peer 2 never answered, so it went for the whole window and no longer (%u)", in2);
}

static void check_the_client(void)
{
    uint32_t tick;
    uint32_t again;

    ut_section("a client takes each event once, whatever number of blocks carry it");
    ws_fresh_host();
    ws_enemy(30u, 5.0f, 10.0f, 30.0f);
    ws_census();
    (void)ws_post(MP_WORLD_EVENT_SCRIPT_EMITTER, 30u, 0x0E17u);
    tick = ws_send_blocks(1u, sizeof ws.block[0]);
    ws_client_takes(0u, tick);
    tick = ws_send_blocks(1u, sizeof ws.block[0]);
    ws_client_takes(0u, tick);
    ut_check(mp_world_event_flush() == 1u, "two blocks carried it, one emitter is performed");
    ut_check(ws.last_replica == ws_actor_of(30u) && ws.last_a == 0x0E17u,
             "on the replica of its key, with what the host posted");
    again = ws.played[MP_WORLD_EVENT_SCRIPT_EMITTER];
    tick  = ws_send_blocks(1u, sizeof ws.block[0]);
    ws_client_takes(0u, tick);
    (void)mp_world_event_flush();
    ut_check(ws.played[MP_WORLD_EVENT_SCRIPT_EMITTER] == again, "and a third block adds nothing");
}

static void check_waiting_for_a_replica(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    uint32_t            tick;
    unsigned            i;

    ut_section("an event whose replica is not built yet waits for it, for the window and no more");
    ws_fresh_host();
    ws_enemy(40u, 5.0f, 10.0f, 30.0f);
    ws_census();
    (void)ws_post(MP_WORLD_EVENT_NPC_CLANG, 40u, 1u);
    tick = ws_send_blocks(1u, sizeof ws.block[0]);
    ws_client_takes(0u, tick);
    s->placement[40].live = false;   /* the client has no body for it yet */
    ut_check(mp_world_event_flush() == 0u, "nothing is performed while there is no replica");
    s->placement[40].live = true;
    ut_check(mp_world_event_flush() == 1u && ws.played[MP_WORLD_EVENT_NPC_CLANG] == 1u,
             "and the replica that turns up inside the window gets the clang, once");

    (void)ws_post(MP_WORLD_EVENT_NPC_CLANG, 40u, 2u);
    tick = ws_send_blocks(1u, sizeof ws.block[0]);
    ws_client_takes(0u, tick);
    s->placement[40].live = false;
    for (i = 0; i < MP_WORLD_EVENT_WINDOW + 1u; ++i) {
        (void)mp_world_event_flush();
    }
    s->placement[40].live = true;
    ut_check(mp_world_event_flush() == 0u && ws.played[MP_WORLD_EVENT_NPC_CLANG] == 1u,
             "a replica that comes after the window hears nothing: that clang is past");
}

static void check_the_record_comes_with_its_event(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    size_t              key;

    ut_section("an event on a key a peer does not hold brings the key's record into the block");
    ws_fresh_host();
    for (key = 50u; key < 90u; ++key) {
        ws_enemy(key, 20.0f, 10.0f, 30.0f);   /* middle for every peer */
    }
    ws_census();
    /* A small block: the head, one event and one whole record of 52 bytes at most. */
    (void)ws_send_blocks(1u, MP_ENEMY_SYNC_HEADER_BYTES + 10u + 60u);
    (void)ws_post(MP_WORLD_EVENT_SCRIPT_EMITTER, 88u, 0x0102u);
    ut_check(!mp_enemy_interest_holds(&s->view[0].interest.row[88], s->placement[88].generation),
             "the peer has never been told of key 88");
    (void)ws_send_blocks(1u, MP_ENEMY_SYNC_HEADER_BYTES + 10u + 60u);
    ut_check(ws_events_in(0u) == 1u && s->view[0].interest.row[88].seen,
             "the block carries the event and the record that builds its replica, ahead of the "
             "crowd");
}

static void check_relevance(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();

    ut_section("an event on a far key a peer was never told of does not go to that peer");
    ws_fresh_host();
    ws_enemy(100u, 200.0f, 10.0f, 30.0f);   /* far from every peer at the origin */
    ws.viewers[1].position[0] = 200.0f;     /* but peer 1 stands beside it */
    ws_census();
    (void)ws_send_blocks(7u, sizeof ws.block[0]);
    (void)ws_post(MP_WORLD_EVENT_SCRIPT_EMITTER, 100u, 0x0102u);
    (void)ws_send_blocks(7u, sizeof ws.block[0]);
    ut_check(ws_events_in(0u) == 0u && ws_events_in(2u) == 0u, "peers 0 and 2 are not concerned");
    ut_check(ws_events_in(1u) == 1u, "peer 1 is");
    (void)s;
}

static void check_a_torn_part(void)
{
    uint8_t block[64];
    size_t  at = 0;

    ut_section("a part that lies about what it carries tears the block");
    memset(block, 0, sizeof block);
    block[0] = 1u;   /* one event, and no bytes of it */
    ut_check(!mp_world_event_stage(block, MP_WORLD_EVENT_HEAD_BYTES, &at),
             "an event counted and not carried is torn");
    at       = 0;
    block[0] = MP_WORLD_EVENT_PART_MAX + 1u;
    ut_check(!mp_world_event_stage(block, sizeof block, &at),
             "and so is a count past what a part can carry");
    at       = 0;
    block[0] = 0u;
    ut_check(mp_world_event_stage(block, MP_WORLD_EVENT_HEAD_BYTES, &at) &&
                 at == MP_WORLD_EVENT_HEAD_BYTES,
             "an empty part is its head alone");
}

/* The music this test claims for each peer, as the scripts' sounds do, and the heard event a
 * client's player was handed. */
static uint16_t         s_music[MP_ENEMY_SYNC_VIEWS][2];
static mp_world_event_t s_heard;
static bool             s_heard_any;

static void test_music(size_t view, uint16_t *state, uint16_t *sequence)
{
    *state    = s_music[view][0];
    *sequence = s_music[view][1];
}

static bool heard_player(const mp_world_event_t *event, uintptr_t replica)
{
    (void)replica;
    s_heard     = *event;
    s_heard_any = true;
    return true;
}

static void check_heard_and_music(void)
{
    const float           at[3] = { 200.0f, 5.0f, 7.0f };
    mp_world_event_head_t head;
    uint16_t              state    = 1u;
    uint16_t              sequence = 1u;
    uint32_t              tick;

    ut_section("a sound is heard by the peer in earshot, and a block says its peer's music");
    ws_fresh_host();
    (void)mp_world_event_set_player(MP_WORLD_EVENT_SCRIPT_SOUND, &heard_player);
    ws_enemy(100u, 200.0f, 400.0f, 400.0f);   /* every peer would be told of it */
    ws.viewers[1].position[0] = 200.0f;       /* peer 1 stands beside it, the others 200 u off */
    ws_census();
    (void)ws_send_blocks(7u, sizeof ws.block[0]);
    ut_check(mp_world_event_post_heard(MP_WORLD_EVENT_SCRIPT_SOUND,
                                       (uintptr_t)(0x00A01000u + 16u * 100u), 42u, at, 7.0f),
             "the host posts a sound at its actor with the actor's place and the record's reach");
    ut_check(!mp_world_event_post_heard(MP_WORLD_EVENT_SCRIPT_EMITTER,
                                        (uintptr_t)(0x00A01000u + 16u * 100u), 1u, at, 7.0f),
             "a kind that is not heard is not posted by earshot");
    s_music[1][0] = 1100u;
    s_music[1][1] = 2611u;
    mp_world_event_set_music_source(&test_music);
    tick = ws_send_blocks(7u, sizeof ws.block[0]);
    ut_check(ws_events_in(0u) == 0u && ws_events_in(2u) == 0u,
             "peers 0 and 2 stand 200 u off, beyond the reach and the margin, and hear nothing");
    ut_check(ws_events_in(1u) == 1u, "peer 1 is in earshot");
    mp_world_event_get_head(ws.block[1] + WS_PART_AT, &head);
    ut_check(head.music_state == 1100u && head.music_sequence == 2611u,
             "peer 1's block says the music claimed for peer 1");
    mp_world_event_get_head(ws.block[0] + WS_PART_AT, &head);
    ut_check(head.music_state == 0u && head.music_sequence == 0u, "and peer 0's says none");
    ws_client_takes(1u, tick);
    mp_world_event_music_heard(&state, &sequence);
    ut_check(state == 1100u && sequence == 2611u, "a client holds what the host last said");
    s_heard_any = false;
    (void)mp_world_event_flush();
    ut_check(s_heard_any && s_heard.a == 42u && s_heard.has_place &&
                 s_heard.place[0] != 0u,
             "the client's player gets the call and the place it was made at");
    mp_world_event_set_music_source(NULL);
}

/* The explosion's hull posts with the place, a radius of 0 and the pitch, through the same post as
 * every other kind. */
static bool explode(size_t key, const float at[3])
{
    const uint8_t pitch[4] = { 0x00u, 0x00u, 0x80u, 0x3Fu };

    return mp_world_event_post_at_actor_placed(MP_WORLD_EVENT_EXPLODE_AT, ws_actor_of(key), 0u, at,
                                               0.0f, pitch, sizeof pitch);
}

static void check_an_explosion_with_a_place(void)
{
    const float at[3] = { 200.0f, 5.0f, 7.0f };
    long        apart;
    long        earshot;

    ut_section("an explosion carries its place, and reaches a peer by interest alone");
    ws_fresh_host();
    (void)mp_world_event_set_player(MP_WORLD_EVENT_EXPLODE_AT, &ws_test_player);
    ws_enemy(100u, 200.0f, 400.0f, 400.0f);   /* every peer is told of this actor */
    ws.viewers[1].position[0] = 200.0f;       /* peer 1 stands beside it, the others 200 u off */
    ws_census();
    (void)ws_send_blocks(7u, sizeof ws.block[0]);
    ut_check(explode(100u, at), "the host posts it at the actor with the place of the blast");
    (void)ws_send_blocks(7u, sizeof ws.block[0]);
    ut_check(ws_events_in(0u) == 1u && ws_events_in(1u) == 1u && ws_events_in(2u) == 1u,
             "every peer told of the actor gets it, beside it or 200 u off: the place is carried, "
             "and does not decide");

    ws_fresh_host();
    ws_enemy(101u, 200.0f, 10.0f, 30.0f);   /* only a peer beside it is told of this one */
    ws.viewers[1].position[0] = 200.0f;
    ws_census();
    (void)ws_send_blocks(7u, sizeof ws.block[0]);
    apart   = ws_peer_says(0u, " not relevant to it");
    earshot = ws_peer_says(0u, " of them out of earshot");
    ut_check(explode(101u, at), "and posts one at an actor only peer 1 is told of");
    (void)ws_send_blocks(7u, sizeof ws.block[0]);
    ut_check(ws_events_in(0u) == 0u && ws_events_in(2u) == 0u && ws_events_in(1u) == 1u,
             "the peers never told of the actor do not get it; peer 1 does");
    ut_checkf(ws_peer_says(0u, " not relevant to it") == apart + 1 &&
                  ws_peer_says(0u, " of them out of earshot") == earshot && earshot >= 0,
              "to peer 0 it is not relevant, and never out of earshot: an explosion is no kind "
              "heard at a place (%ld, %ld)", ws_peer_says(0u, " not relevant to it") - apart,
              ws_peer_says(0u, " of them out of earshot") - earshot);
}

int main(void)
{
    check_against_the_old_rule();
    check_until_proved();
    check_the_bits_prove_it();
    check_four_peers();
    check_the_client();
    check_waiting_for_a_replica();
    check_the_record_comes_with_its_event();
    check_relevance();
    check_a_torn_part();
    check_heard_and_music();
    check_an_explosion_with_a_place();
    return ut_summary("mp_world_event");
}
