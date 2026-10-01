/* mp_enemy_relay.c: removals with their reason, pickup claims, and the table of lives a removal
 * was sent for.
 *
 * There is no engine here, so no actor exists and no removal can be performed. What has to hold
 * without one is that the messages are recognised, refused on the right grounds, and never
 * performed on nothing; and that the pure rules beside them, the claim interval, the reach and
 * the table of lives, answer right at their edges.
 */
#include "unittest.h"

#include "mp_enemy_relay.h"
#include "mp_enemy_sync.h"
#include "mp_events.h"
#include "mp_pickup_relay.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static size_t despawn(uint8_t *out, size_t capacity, uint16_t level, uint8_t index,
                      uint8_t generation, uint8_t reason)
{
    mp_event_t event;

    memset(&event, 0, sizeof event);
    event.kind             = MP_EVENT_DESPAWN;
    event.tick             = 7u;
    event.level_id         = level;
    event.actor_index      = index;
    event.actor_generation = generation;
    event.actor_reason     = reason;
    return mp_event_encode(&event, out, capacity);
}

int main(void)
{
    uint8_t note[MP_EVENT_MAX_BYTES];
    size_t  bytes;

    ut_section("a removal message is recognised and, with nothing here, taken and dropped");
    bytes = despawn(note, sizeof note, 57u, 4u, 1u, MP_EVENT_REMOVE_DELETED);
    ut_checkf(bytes == 12u && bytes == MP_EVENT_DESPAWN_BYTES,
              "a despawn encodes to %u bytes, the last of them the burst", (unsigned)bytes);
    ut_check(mp_enemy_relay_take_message(note, bytes),
             "it is ours by tag and length, and taken even with nothing bound, so nothing else "
             "reads it as a body event");
    ut_check(!mp_enemy_relay_take_message(note, bytes - 1u),
             "a byte short, the removal of wire 29, is not a despawn and is left to the other "
             "recognisers");
    note[0] = MP_EVENT_PICKUP;
    ut_check(!mp_enemy_relay_take_message(note, bytes),
             "a note of the same length with another tag is somebody else's");
    ut_check(!mp_enemy_relay_take_message(NULL, bytes), "a null note is nobody's");

    ut_section("a pickup claim is recognised, and repeated no more than once a second");
    {
        mp_event_t event;

        memset(&event, 0, sizeof event);
        event.kind             = MP_EVENT_PICKUP;
        event.tick             = 7u;
        event.level_id         = 57u;
        event.actor_index      = 4u;
        event.actor_generation = 1u;
        event.pickup_kind      = 0x0Du;
        bytes = mp_event_encode(&event, note, sizeof note);
        ut_checkf(bytes == MP_EVENT_PICKUP_BYTES, "a claim encodes to %u bytes", (unsigned)bytes);
        ut_check(mp_pickup_relay_take_message(1u, 1u, note, bytes),
                 "it is ours by tag and length, and taken with nothing bound");
        ut_check(!mp_enemy_relay_take_message(note, bytes),
                 "and the enemy relay, whose spawn has the same length, leaves it alone");
        event.pickup_kind = 0x0Bu;
        ut_check(mp_event_encode(&event, note, sizeof note) == 0u,
                 "a class the shipped data never places is refused by the codec before it "
                 "leaves: it reaches an uninitialised stack slot in the retail routine");

        ut_check(mp_pickup_relay_may_claim(0u, 5u), "a placement never claimed may be claimed");
        ut_check(!mp_pickup_relay_may_claim(100u, 131u),
                 "and not again inside the interval");
        ut_check(mp_pickup_relay_may_claim(100u, 132u), "but at the interval it may");
        ut_check(mp_pickup_relay_may_claim(100u, 3u),
                 "a clock that went backwards, which is a new session, does not hold a claim");
    }

    ut_section("a deathmatch grants a pickup only within reach of the claimant");
    {
        const float pickup[3] = { 10.0f, 2.0f, -3.0f };
        float       claimant[3] = { 10.0f, 2.0f, -3.0f };

        ut_check(mp_pickup_relay_in_reach(claimant, pickup),
                 "a claimant standing on the pickup is in reach");
        claimant[0] = 10.0f + MP_PICKUP_RELAY_REACH - 0.01f;
        ut_check(mp_pickup_relay_in_reach(claimant, pickup), "so is one just inside the reach");
        claimant[0] = 10.0f + MP_PICKUP_RELAY_REACH + 0.01f;
        ut_check(!mp_pickup_relay_in_reach(claimant, pickup), "one just outside it is not");
        claimant[0] = 10.0f;
        claimant[2] = -3.0f - MP_PICKUP_RELAY_REACH - 0.01f;
        ut_check(!mp_pickup_relay_in_reach(claimant, pickup),
                 "and every axis counts, not only the first");
    }

    ut_section("the report runs without a session");
    mp_enemy_relay_report();
    ut_section("a removal sent twice for one life is counted, a new life is not");
    {
        static mp_enemy_relay_lives_t lives;

        ut_check(!mp_enemy_relay_life_sent_before(&lives, 3u, 7u, 1u),
                 "the first removal of a life is new");
        ut_check(mp_enemy_relay_life_sent_before(&lives, 3u, 7u, 1u),
                 "the same life again is a repeat");
        ut_check(!mp_enemy_relay_life_sent_before(&lives, 3u, 7u, 2u),
                 "the next life on the placement is new");
        ut_check(!mp_enemy_relay_life_sent_before(&lives, 4u, 7u, 2u),
                 "the same generation in another level is new");
        ut_check(!mp_enemy_relay_life_sent_before(&lives, 4u, 255u, 2u) &&
                     mp_enemy_relay_life_sent_before(&lives, 4u, 255u, 2u),
                 "the last placement index has its own bit");
        ut_check(!mp_enemy_relay_life_sent_before(NULL, 4u, 8u, 2u), "no table answers no");
    }

    mp_pickup_relay_report();
    mp_enemy_sync_report();
    ut_check(true, "reporting on a relay that never carried anything is not a fault");

    return ut_summary("mp_enemy_relay");
}
