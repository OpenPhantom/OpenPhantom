/* mp_world_event_client.c: a client's half of the world events, through the enemy block's own
 * apply, and the one question whose output a client's own script may make.
 *
 * mp_world_event.c reads the host's part straight into the stage. Here a host writes a whole block
 * and a client takes it the way the bridge hands it over, through mp_enemy_sync_apply, so what is
 * tested is the order that matters: the part is read in the first pass and taken only with the
 * block. Both halves share one process, one after the other, as that test's do.
 *
 * What would be silent if it were wrong:
 *
 *   a block refused behind its part (a torn record, another level, an old substep) whose events
 *   were taken anyway, so the next good block finds them seen and performs nothing;
 *   the same, for bytes no honest host sends;
 *   and two answers to "is this actor the host's" on one client, so an actor plays its own sound
 *   and withholds its own blast, or the other way round.
 */
#include "unittest.h"

#include "mp_fuzz_input.h"

#include "mp_enemy_sync.h"
#include "mp_enemy_sync_internal.h"
#include "mp_enemy_wire.h"
#include "mp_wire.h"
#include "mp_world_event.h"
#include "mp_world_event_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define HERE  57u
#define AWAY  100u
#define KEY   4u

static uint8_t  s_block[2048];
static size_t   s_bytes;
static uint32_t s_tick = 7000u;
static unsigned s_played;

static uintptr_t actor_of(size_t key)
{
    return (uintptr_t)(0x00A01000u + 16u * key);
}

static bool count_player(const mp_world_event_t *event, uintptr_t replica)
{
    (void)event;
    (void)replica;
    ++s_played;
    return true;
}

/* A host with one enemy described: its census by hand, as the tests of the block do, a sound and a
 * blast posted at its actor with a place each, and one block written for the first peer. Both kinds
 * are performed at their place, so a client performs them with no replica here. */
static void host_writes_a_block(void)
{
    enemy_sync_state_t *s     = mp_enemy_sync_state();
    const float         at[3] = { 5.0f, 5.0f, 7.0f };
    const uint8_t       pitch[4] = { 0u, 0u, 0x80u, 0x3Fu };
    uint32_t            packed = 0;
    placement_t        *p      = &s->placement[KEY];
    unsigned            pass;

    mp_enemy_sync_reset();
    mp_enemy_sync_set_enabled(true);
    mp_enemy_sync_set_level(true, HERE);
    (void)mp_enemy_sync_begin_send();
    for (pass = 0; pass < 2u; ++pass) {
        p->was_live = p->live;
        p->live     = true;
        p->actor    = actor_of(KEY);
        s->send_ready = true;
        mp_enemy_sync_describe_census();
        memset(&p->current, 0, sizeof p->current);
        (void)mp_enemy_wire_put_position(5.0f, &packed);
        p->current.value[MP_ENEMY_F_POS_X] = packed;
        p->current.value[MP_ENEMY_F_POS_Y] = packed;
        p->current.value[MP_ENEMY_F_POS_Z] = packed;
        p->current.value[MP_ENEMY_F_HEALTH] = mp_enemy_wire_put_health(60);
        p->current.value[MP_ENEMY_F_INDEX] = mp_enemy_sync_wire_index(KEY);
        p->current.value[MP_ENEMY_F_GENERATION] = p->generation;
        p->current_ok = true;
        if (pass == 0u) {
            ut_check(mp_world_event_post_heard(MP_WORLD_EVENT_SCRIPT_SOUND, actor_of(KEY), 3u, at,
                                               10.0f) &&
                         mp_world_event_post_at_actor_placed(MP_WORLD_EVENT_EXPLODE_AT,
                                                             actor_of(KEY), 0u, at, 0.0f, pitch,
                                                             sizeof pitch),
                     "the host posts a sound and a blast with their place");
        }
    }
    (void)mp_enemy_sync_floor_bytes(0u);
    ut_check(mp_enemy_sync_encode_for(0u, s_block, sizeof s_block, &s_bytes) &&
                 s_block[MP_ENEMY_SYNC_HEADER_BYTES - MP_WORLD_EVENT_HEAD_BYTES] == 2u &&
                 s_bytes > MP_ENEMY_SYNC_HEADER_BYTES + 20u,
             "and writes one block with both events and the enemy's record behind them");
}

/* The same process as a client of level HERE, holding nothing yet. */
static void become_a_client(void)
{
    mp_enemy_sync_reset();
    mp_enemy_sync_set_enabled(true);
    mp_enemy_sync_set_level(true, HERE);
    (void)mp_world_event_set_player(MP_WORLD_EVENT_SCRIPT_SOUND, &count_player);
    (void)mp_world_event_set_player(MP_WORLD_EVENT_EXPLODE_AT, &count_player);
    s_played = 0u;
}

static unsigned performed(void)
{
    s_played = 0u;
    (void)mp_world_event_flush();
    return s_played;
}

static void check_a_refused_block_queues_nothing(void)
{
    uint8_t other[sizeof s_block];

    ut_section("a block refused behind its part leaves none of its events taken");
    host_writes_a_block();
    become_a_client();
    ut_check(!mp_enemy_sync_apply(s_block, s_bytes - 1u, ++s_tick),
             "a block whose last record is torn is refused");
    ut_check(performed() == 0u, "and its events, read in the first pass, are not performed");
    memcpy(other, s_block, s_bytes);
    other[1] = (uint8_t)(AWAY & 0xFFu);
    other[2] = (uint8_t)(AWAY >> 8);
    ut_check(!mp_enemy_sync_apply(other, s_bytes, ++s_tick) && performed() == 0u,
             "nor are those of a block about another level");
    ut_check(mp_enemy_sync_apply(s_block, s_bytes, ++s_tick), "the whole block is taken");
    ut_checkf(performed() == 2u,
              "and both events are performed, which they would not be had a refused block taken "
              "their numbers (%u)", s_played);
    ut_check(!mp_enemy_sync_apply(s_block, s_bytes, s_tick - 1u) && performed() == 0u,
             "an older block is refused and performs nothing");
    ut_check(mp_enemy_sync_apply(s_block, s_bytes, ++s_tick) && performed() == 0u,
             "and a newer one carrying the same events performs them no second time");
}

/* The block's own entry fed what no honest host sends. The bridge's split in front of it is fuzzed
 * with the rest of the notes; what is new behind it, the part and the records it precedes, is fed
 * here, where a refusal can be checked for having queued nothing. */
static void check_the_block_fuzzed(void)
{
    static uint8_t valid[sizeof s_block];
    static uint8_t bytes[sizeof s_block];
    size_t         valid_len;
    unsigned       accepted = 0u;
    unsigned       refused  = 0u;
    unsigned       leaked   = 0u;
    unsigned       too_many = 0u;
    unsigned       i;

    ut_section("mutated blocks: a refused one performs nothing, a taken one no "
               "more than it carried");
    host_writes_a_block();
    memcpy(valid, s_block, s_bytes);
    valid_len = s_bytes;
    become_a_client();
    for (i = 0; i < 4000u; ++i) {
        size_t len = valid_len;

        memcpy(bytes, valid, valid_len);
        if (i != 0u) {
            len = mutate(bytes, valid_len, sizeof bytes);
        }
        if (mp_enemy_sync_apply(bytes, len, ++s_tick)) {
            ++accepted;
            too_many += performed() > MP_WORLD_EVENT_PART_MAX ? 1u : 0u;
        } else {
            ++refused;
            leaked += performed() != 0u ? 1u : 0u;
        }
    }
    ut_checkf(accepted != 0u && refused != 0u && leaked == 0u && too_many == 0u,
              "%u taken and %u refused of 4000; %u refused block(s) performed an event, %u taken "
              "one(s) more than a part carries", accepted, refused, leaked, too_many);
}

/* The one question, against the two it replaced. The first asked for the actor's key and a life of
 * it in the mirror; the second asked whether the census's actor under the key is this one. Each
 * fact is set here by hand in the mirror, the way the census and the apply leave it. */
static void check_the_one_question(void)
{
    enemy_sync_state_t *s = mp_enemy_sync_state();
    placement_t        *p;
    const uintptr_t     replica = actor_of(9u);
    const uintptr_t     beside  = actor_of(300u);
    const uintptr_t     own     = actor_of(11u);

    ut_section("one question for every script output of a client: the host's replica of a life");
    become_a_client();
    p             = &s->placement[9u];
    p->actor      = replica;
    p->live       = true;
    p->known      = true;
    p->generation = 2u;
    ut_check(mp_world_event_output_is_the_hosts(replica, true),
             "the replica of a life the host described withholds its own outputs on a client");
    ut_check(!mp_world_event_output_is_the_hosts(replica, false),
             "on a host, or with no started session, nothing is withheld");
    ut_check(!mp_world_event_output_is_the_hosts(beside, true),
             "an actor that is no key's replica here is not the host's, whatever key it carries: "
             "the first question took any actor with a described key for the host's");
    s->placement[11u].actor = own;
    s->placement[11u].live  = true;
    ut_check(!mp_world_event_output_is_the_hosts(own, true),
             "an actor this client woke under a key the host never described plays its own: the "
             "second question withheld its commands 8, 9 and 19");
    p->known = false;
    ut_check(mp_world_event_output_is_the_hosts(replica, true),
             "a replica let go keeps its life's answer until a removal forgets it");
    mp_enemy_sync_forget(9u, 2u);
    ut_check(!mp_world_event_output_is_the_hosts(replica, true), "and loses it with the removal");
    ut_check(!mp_world_event_output_is_the_hosts(0u, true), "no actor is nobody's");
    mp_enemy_sync_reset();
}

int main(void)
{
    check_a_refused_block_queues_nothing();
    check_the_block_fuzzed();
    check_the_one_question();
    return ut_summary("mp_world_event_client");
}
