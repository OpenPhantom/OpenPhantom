/* The host's announcer over its own clock, with the socket real and the routing not relied on.
 *
 * What is pinned is the cadence and the gates: nothing leaves while it is disabled or not yet
 * configured, the first tick speaks, the next inside the cycle does not, and the socket comes up
 * only when there is something to say. Whether the broadcast arrives anywhere is the machine's
 * business; the counters say whether the stack accepted it.
 */
#include "unittest.h"

#include "mp_announcer.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Not the real announce port, so a game running on this machine cannot leak into the test. */
#define TEST_PORT 27978u

static mp_announcer_t s_announcer;

static void check_the_gates(void)
{
    ut_section("nothing leaves before it is configured and enabled");
    mp_announcer_init(&s_announcer);
    mp_announcer_set_facts(&s_announcer, 15u, 0x1234u, 27960u, 1u);
    ut_check(!mp_announcer_tick(&s_announcer, 0u, TEST_PORT), "unconfigured: silent");
    ut_check(!s_announcer.socket.up, "and no socket was opened for it");
    mp_announcer_configure(&s_announcer, "Naboo Hangar", 4u, 0u, false);
    ut_check(!mp_announcer_tick(&s_announcer, 10u, TEST_PORT), "disabled: silent");
    ut_check(!s_announcer.socket.up, "still no socket");
}

static void check_the_cadence(void)
{
    ut_section("enabled, the first tick speaks and the next inside the cycle does not");
    mp_announcer_configure(&s_announcer, "Naboo Hangar", 4u, MP_ANNOUNCE_F_PASSWORD, true);
    ut_check(mp_announcer_tick(&s_announcer, 100u, TEST_PORT), "the first tick sends");
    ut_check(s_announcer.socket.up, "the socket came up for it");
    ut_check(s_announcer.sent == 1u, "counted");
    ut_check(!mp_announcer_tick(&s_announcer, 100u + MP_ANNOUNCER_CYCLE_MS - 1u, TEST_PORT),
             "one short of a cycle later: silent");
    ut_check(mp_announcer_tick(&s_announcer, 100u + MP_ANNOUNCER_CYCLE_MS, TEST_PORT),
             "a cycle later: sends again");
    ut_check(s_announcer.sent == 2u, "counted again");
    ut_check(s_announcer.announce.flags == MP_ANNOUNCE_F_PASSWORD,
             "the flags are the host's, kept to the known bits");
    ut_check(strcmp(s_announcer.announce.name, "Naboo Hangar") == 0, "the name is the host's");

    ut_section("the facts are what the bridge last said");
    mp_announcer_set_facts(&s_announcer, 15u, 0xABCDu, 27960u, 3u);
    ut_check(mp_announcer_tick(&s_announcer, 100u + 2u * MP_ANNOUNCER_CYCLE_MS, TEST_PORT),
             "sends with them");
    ut_check(s_announcer.announce.players == 3u && s_announcer.announce.fingerprint == 0xABCDu,
             "players and fingerprint as set");

    ut_section("a claim the announce refuses is counted, not sent, and does not burst");
    mp_announcer_set_facts(&s_announcer, 15u, 0xABCDu, 27960u, 9u);   /* nine in four slots */
    ut_check(!mp_announcer_tick(&s_announcer, 100u + 3u * MP_ANNOUNCER_CYCLE_MS, TEST_PORT),
             "refused by the encoder");
    ut_check(s_announcer.failed == 1u, "counted as failed");
    ut_check(!mp_announcer_tick(&s_announcer, 101u + 3u * MP_ANNOUNCER_CYCLE_MS, TEST_PORT),
             "and the next tick waits for the cycle rather than retrying at once");

    ut_section("disabling closes the socket; closing keeps the configuration");
    mp_announcer_configure(&s_announcer, "Naboo Hangar", 4u, 0u, false);
    ut_check(!s_announcer.socket.up, "disabled: the socket is down");
    mp_announcer_configure(&s_announcer, "Naboo Hangar", 4u, 0u, true);
    mp_announcer_set_facts(&s_announcer, 15u, 0xABCDu, 27960u, 2u);
    ut_check(mp_announcer_tick(&s_announcer, 100u + 5u * MP_ANNOUNCER_CYCLE_MS, TEST_PORT),
             "enabled again: it opens again and sends");
    mp_announcer_close(&s_announcer);
    ut_check(!s_announcer.socket.up && s_announcer.configured, "closed, still configured");
}

int main(void)
{
    check_the_gates();
    check_the_cadence();
    return ut_summary("mp_announcer");
}
