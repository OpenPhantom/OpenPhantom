/* mp_session_peers.c: the holds, with one host and three clients (mp_peers_net.c).
 *
 * A broadcast one channel refuses while the others take it, a single note that may not overtake a
 * held one, a peer that pumps and takes nothing, a peer that says nothing, a hold that is full, a
 * replacement in place: each needs a third machine, and the harness gives it.
 */
#include "unittest.h"

#include "mp_peers_net.h"

#include "mp_channel.h"
#include "mp_hold.h"
#include "mp_session.h"
#include "mp_transport.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Two players, the client silent: its channel fills, and a broadcast the one channel refuses is
 * refused and answered with nought. With one peer the hold changes nothing a caller sees: every
 * caller that repeats a refused note repeats it, and a caller that counts it counts it. */
static void check_two_players_refused_as_before(void)
{
    broadcast_run_t run;
    int             round;

    ut_section("two players, a silent client: a broadcast its full channel refuses is refused");
    memset(&run, 0, sizeof run);
    ut_check(net_build(1u), "the host and one client connected");
    s_net.muted[1] = true;
    for (round = 0; round < 90; ++round) {
        net_round(1u, &broadcast_one, &run);
    }
    ut_checkf(run.taken == MP_CHANNEL_SEND_SLOTS,
              "the channel took %u broadcasts, its %u slots, and no more (took %u)",
              (unsigned)MP_CHANNEL_SEND_SLOTS, (unsigned)MP_CHANNEL_SEND_SLOTS,
              (unsigned)run.taken);
    ut_checkf(run.refused == 90u - MP_CHANNEL_SEND_SLOTS && run.last_count == 0u,
              "every later one was refused and answered nought (%u refused)",
              (unsigned)run.refused);
    ut_checkf(mp_session_reliable_pending(&s_host) == MP_CHANNEL_SEND_SLOTS,
              "and nothing waits for the client beyond its channel (%u pending)",
              (unsigned)mp_session_reliable_pending(&s_host));
    ut_check(mp_session_peer_count(&s_host) == 1u && s_net.overflowed == 0u,
             "the client is still connected, and the harness lost nothing of its own");
}

/* Client 2 does not answer for five seconds while the host broadcasts one event a substep. Clients
 * 1 and 3 get every one of them within the substep it was sent, and client 2, once it answers
 * again, gets every one of them as well, in order. Before the hold, its channel took the first
 * sixty four and refused the rest, the broadcast still counted as sent because two peers had taken
 * it, and client 2 simply never saw them. */
static void check_a_silent_client_misses_nothing(void)
{
    broadcast_run_t run;
    reader_t        readers[CLIENTS];
    int             round;
    size_t          k;
    bool            prompt = true;

    ut_section("three clients, one silent for five seconds: nobody misses a broadcast");
    memset(&run, 0, sizeof run);
    memset(readers, 0, sizeof readers);
    ut_check(net_build(CLIENTS), "a host and three clients connected");
    s_net.muted[2] = true;
    for (round = 0; round < 280; ++round) {
        if (round == 161) {
            s_net.muted[2] = false;   /* five seconds of substeps later it answers again */
        }
        if (round < 200) {
            net_round(CLIENTS, &broadcast_one, &run);
        } else {
            net_round(CLIENTS, NULL, NULL);
        }
        for (k = 0; k < CLIENTS; ++k) {
            read_all(k, &readers[k]);
        }
        prompt = prompt && (round >= 199 ||
                            (readers[0].expected + 1u >= run.next &&
                             readers[2].expected + 1u >= run.next));
    }
    ut_checkf(run.taken == 200u && run.refused == 0u,
              "all 200 broadcasts were taken (%u), none refused", (unsigned)run.taken);
    ut_check(prompt, "clients 1 and 3 read each one no later than the next substep");
    ut_checkf(readers[0].in_order == 200u && readers[2].in_order == 200u,
              "and read all of them in order (%u and %u)", (unsigned)readers[0].in_order,
              (unsigned)readers[2].in_order);
    ut_checkf(readers[1].in_order == 200u && readers[1].out_of_order == 0u,
              "client 2 read all 200 in order once it answered again (%u in order, %u out)",
              (unsigned)readers[1].in_order, (unsigned)readers[1].out_of_order);
    ut_checkf(mp_session_peer_count(&s_host) == 3u && mp_session_reliable_pending(&s_host) == 0u,
              "nobody was dropped and nothing is left waiting (%u pending)",
              (unsigned)mp_session_reliable_pending(&s_host));
}

/* One single note to peer 0 in the round, by the plain send or by the send that holds. */
typedef struct single_run {
    uint32_t next;
    bool     or_hold;
    uint32_t sent;
    uint32_t refused;
} single_run_t;

static void single_one(void *context)
{
    single_run_t *run = (single_run_t *)context;
    uint8_t       event[EVENT_BYTES];
    bool          sent;

    make_event(run->next, event);
    sent = run->or_hold ? mp_session_send_or_hold(&s_host, 0u, event, sizeof event)
                        : mp_session_send_reliable(&s_host, 0u, event, sizeof event);
    if (sent) {
        ++run->sent;
        ++run->next;
    } else {
        ++run->refused;
    }
}

/* A single send may not overtake what is held for its peer: the plain one is refused while the
 * hold is not empty, the one that holds queues behind it, and a broadcast queues behind both. What
 * the client reads is the order the host decided on, every number once. */
static void check_order_across_single_and_broadcast(void)
{
    broadcast_run_t broadcasts;
    single_run_t    singles;
    reader_t        reader;
    int             round;

    ut_section("two players: a single note never overtakes a held one, and the order holds");
    memset(&broadcasts, 0, sizeof broadcasts);
    memset(&singles, 0, sizeof singles);
    memset(&reader, 0, sizeof reader);
    ut_check(net_build(1u), "the host and one client connected");
    s_net.muted[1] = true;
    for (round = 0; round < MP_CHANNEL_SEND_SLOTS; ++round) {
        net_round(1u, &broadcast_one, &broadcasts);
    }
    singles.next    = broadcasts.next;
    singles.or_hold = true;
    net_round(1u, &single_one, &singles);
    ut_checkf(singles.sent == 1u && mp_session_reliable_pending(&s_host) == 65u,
              "with the channel full, the send that holds held the note: 65 pending (%u)",
              (unsigned)mp_session_reliable_pending(&s_host));
    broadcasts.next = singles.next;
    net_round(1u, &broadcast_one, &broadcasts);
    ut_check(broadcasts.last_count == 1u,
             "a broadcast now queues behind the held note rather than being refused");
    singles.next    = broadcasts.next;
    singles.or_hold = false;
    net_round(1u, &single_one, &singles);
    ut_check(singles.refused == 1u, "and the plain single send is refused while anything is held");
    s_net.muted[1] = false;
    for (round = 0; round < 60; ++round) {
        net_round(1u, NULL, NULL);
        read_all(0u, &reader);
    }
    ut_checkf(reader.in_order == 66u && reader.out_of_order == 0u,
              "the client read all 66 in the order they were sent (%u in order, %u out)",
              (unsigned)reader.in_order, (unsigned)reader.out_of_order);
    ut_check(mp_session_reliable_pending(&s_host) == 0u, "and nothing is pending any more");
}

/* A client that goes on sending and takes nothing in: its oldest held note ages past the limit
 * while it pumps, and it is sent away with the reason, in the call that found it. The notice goes
 * out twice and is lost, because the client hears nothing; the packets it goes on sending are
 * answered, and once it can hear again, it learns why. The others play on and miss nothing. */
static void check_a_pumping_peer_is_sent_away(void)
{
    broadcast_run_t run;
    reader_t        readers[CLIENTS];
    uint32_t        denied_before;
    int             round;
    int             sent_away_at = -1;
    size_t          k;

    ut_section("a client that pumps and takes nothing is sent away, and learns why");
    memset(&run, 0, sizeof run);
    memset(readers, 0, sizeof readers);
    ut_check(net_build(CLIENTS), "a host and three clients connected");
    denied_before = mp_session_denied(&s_host);
    s_net.deaf[2] = true;
    for (round = 0; round < 420 && sent_away_at < 0; ++round) {
        net_round(CLIENTS, &broadcast_one, &run);
        for (k = 0; k < CLIENTS; ++k) {
            read_all(k, &readers[k]);
        }
        if (mp_session_peer_count(&s_host) == 2u) {
            sent_away_at = round;
        }
    }
    ut_checkf(sent_away_at >= 360 && sent_away_at <= 400,
              "it went ten seconds after its first note was held: round %d, the channel full at "
              "64 and ten seconds being 323 rounds", sent_away_at);
    ut_check(mp_session_peer_dropped_behind(&s_host, 1u) && s_host.dropped_behind == 1u,
             "the host says why that seat came free");
    ut_check(mp_session_denied(&s_host) == denied_before,
             "and counts it as a peer sent away, not as a refused request");
    s_net.deaf[2] = false;
    for (round = 0; round < 20; ++round) {
        net_round(CLIENTS, NULL, NULL);
        for (k = 0; k < CLIENTS; ++k) {
            read_all(k, &readers[k]);
        }
    }
    ut_checkf(!mp_session_is_connected(&s_client[1]) &&
              mp_session_last_deny(&s_client[1]) == MP_DENY_BEHIND,
              "the client, hearing again, holds the reason %u (%u answer(s) from the host)",
              (unsigned)mp_session_last_deny(&s_client[1]), (unsigned)s_host.behind_answers);
    ut_checkf(readers[0].in_order == run.taken && readers[2].in_order == run.taken,
              "clients 1 and 3 read every one of the %u broadcasts in order", (unsigned)run.taken);
}

/* A client that sends nothing at all is loading or gone, and the connected timeout decides that,
 * not the age of what is held for it. */
static void check_a_silent_peer_waits_for_the_timeout(void)
{
    broadcast_run_t run;
    int             round;
    bool            there_at_25s = false;

    ut_section("a client that sends nothing is left to the connected timeout");
    memset(&run, 0, sizeof run);
    ut_check(net_build(2u), "a host and two clients connected");
    s_net.muted[2] = true;
    for (round = 0; round < 1000; ++round) {
        net_round(2u, &broadcast_one, &run);
        if (round == 806) {   /* twenty five seconds */
            there_at_25s = mp_session_peer_count(&s_host) == 2u;
        }
    }
    ut_check(there_at_25s, "twenty five seconds on, with notes held far past the age limit, it is "
                           "still there");
    ut_checkf(mp_session_peer_count(&s_host) == 1u && mp_session_drops(&s_host) == 1u &&
              s_host.dropped_behind == 0u,
              "and at thirty the timeout took it, not the hold (%u drop(s), %u sent away)",
              (unsigned)mp_session_drops(&s_host), (unsigned)s_host.dropped_behind);
}

/* Large notes to a silent client until its hold is full: the broadcast that finds it full sends
 * that client away in the same call and reaches the other two. */
typedef struct large_run {
    uint32_t next;
    size_t   last_count;
    size_t   peers_after;
    bool     went_in_call;
} large_run_t;

static void broadcast_large(void *context)
{
    large_run_t *run = (large_run_t *)context;
    uint8_t      note[1100];
    size_t       before = mp_session_peer_count(&s_host);

    memset(note, 0x55, sizeof note);
    note[0] = (uint8_t)EVENT_TAG;
    run->last_count  = mp_session_broadcast_reliable(&s_host, note, sizeof note);
    run->peers_after = mp_session_peer_count(&s_host);
    if (before == 3u && run->peers_after == 2u) {
        run->went_in_call = true;
    }
    ++run->next;
}

static void check_a_full_hold_sends_the_peer_away_at_once(void)
{
    large_run_t run;
    int         round;

    ut_section("the broadcast that finds a hold full sends that peer away in the same call");
    memset(&run, 0, sizeof run);
    ut_check(net_build(CLIENTS), "a host and three clients connected");
    s_net.muted[1] = true;
    for (round = 0; round < 200 && !run.went_in_call; ++round) {
        net_round(CLIENTS, &broadcast_large, &run);
    }
    ut_checkf(run.went_in_call && run.last_count == 2u,
              "after %u large notes the client went inside a broadcast, which still reached the "
              "other two", (unsigned)run.next);
    ut_check(s_host.dropped_behind == 1u && mp_session_peer_dropped_behind(&s_host, 0u),
             "sent away for falling behind, the first client");
}

/* A client that restarts from its own address replaces its old connection in place, and nothing
 * held for the old one reaches the new: the first note the new connection reads is the host's
 * next, which in the game is the slot byte. */
static void check_a_replacement_starts_with_an_empty_hold(void)
{
    broadcast_run_t run;
    uint8_t         note[MP_CHANNEL_MESSAGE_BYTES];
    uint8_t         slot = 2u;
    size_t          bytes = 0;
    int             round;
    bool            first_is_slot = false;

    ut_section("a replacement in place empties the hold; the slot byte is the first note");
    memset(&run, 0, sizeof run);
    ut_check(net_build(2u), "a host and two clients connected");
    s_net.muted[2] = true;
    for (round = 0; round < 90; ++round) {
        net_round(2u, &broadcast_one, &run);
    }
    {
        const mp_peer_t *silent = mp_session_peer(&s_host, 1u);
        size_t           in_channel = mp_channel_send_pending(&silent->channel);
        size_t           held = mp_hold_count(&silent->hold);

        ut_checkf(in_channel == MP_CHANNEL_SEND_SLOTS && in_channel + held == 90u &&
                  mp_session_reliable_pending(&s_host) >= 90u,
                  "the host counts all 90 notes for the silent client as pending, %u in its "
                  "channel and %u held", (unsigned)in_channel, (unsigned)held);
    }
    ut_check(!mp_session_send_reliable(&s_host, 1u, &slot, sizeof slot),
             "a single note to it is refused while its hold is not empty");
    s_net.muted[2] = false;
    mp_session_init(&s_client[1], MP_SESSION_CLIENT, &s_transport[2], 0x9999u);
    mp_session_connect(&s_client[1], 0u);
    for (round = 0; round < 40 && s_host.replaced == 0u; ++round) {
        net_round(2u, NULL, NULL);
    }
    ut_check(s_host.replaced == 1u && mp_session_peer_count(&s_host) == 2u,
             "the restarted client took its own slot back in place");
    ut_check(mp_session_send_reliable(&s_host, 1u, &slot, sizeof slot),
             "and the host's next single note to it goes straight to the channel");
    for (round = 0; round < 10; ++round) {
        net_round(2u, NULL, NULL);
        if (!first_is_slot &&
            mp_session_read_reliable(&s_client[1], 0u, note, sizeof note, &bytes)) {
            first_is_slot = bytes == 1u && note[0] == slot;
            break;
        }
    }
    ut_check(first_is_slot, "the first note the new connection reads is that one");
}

/* The harness itself, before anything is proven with it: three clients seated in the order they
 * asked, each answering on its own endpoint, and a muted one heard by nobody. */
static void check_the_harness(void)
{
    broadcast_run_t run;
    int             round;
    size_t          k;
    bool            each_heard = true;

    ut_section("the harness seats three clients and mutes one of them");
    memset(&run, 0, sizeof run);
    ut_check(net_build(CLIENTS), "a host and three clients connected, one after the other");
    for (k = 0; k < CLIENTS; ++k) {
        each_heard = each_heard && mp_session_peer(&s_host, k)->endpoint == (uint32_t)(k + 1u);
    }
    ut_check(each_heard, "the host's peer k is the client at endpoint k + 1");
    s_net.muted[2] = true;
    for (round = 0; round < 10; ++round) {
        net_round(CLIENTS, &broadcast_one, &run);
    }
    ut_checkf(run.taken == 10u && s_net.dropped[2] != 0u && s_net.dropped[1] == 0u,
              "ten broadcasts taken while the second client's %u packet(s) were dropped and "
              "nobody else's", (unsigned)s_net.dropped[2]);
    /* The price of the hold, measured rather than estimated, for the comments that quote it. */
    ut_checkf(sizeof(mp_session_t) < 4u * 1024u * 1024u,
              "a session is %u bytes and a peer in it %u, under four megabytes of static",
              (unsigned)sizeof(mp_session_t), (unsigned)sizeof(mp_peer_t));
}

int main(void)
{
    check_the_harness();
    check_two_players_refused_as_before();
    check_a_silent_client_misses_nothing();
    check_order_across_single_and_broadcast();
    check_a_pumping_peer_is_sent_away();
    check_a_silent_peer_waits_for_the_timeout();
    check_a_full_hold_sends_the_peer_away_at_once();
    check_a_replacement_starts_with_an_empty_hold();
    return ut_summary("mp_session_peers");
}
