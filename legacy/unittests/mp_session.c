/* mp_session.c: two sessions meet over the loopback, handshake and all.
 *
 * The loopback half: it plays a lossy network. The handshake is the part no offline check could
 * prove: a host and a client complete the salt exchange and reach connected across a link that
 * drops packets, then exchange reliable messages both ways. A client with no host on the far end
 * gives up rather than trying forever. The other half, over a mailbox the test drives packet by
 * packet, is mp_session_mailbox.c.
 *
 * SIZE NOTE: over 600 lines. Each handshake case needs its own fresh pair of sessions and its own
 * run, and the judge's cases of wire 35 (a refusal with its detail, no statement on either side,
 * the same statement admitted) came on top. The seam is the judge's section, which shares only
 * fresh_pair and run_both with the rest and would move to a file of its own with them.
 */
#include "unittest.h"

#include "mp_entropy.h"
#include "mp_loopback.h"
#include "mp_savefile.h"
#include "mp_session.h"
#include "mp_transport.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Runs both sessions forward until the client is connected and the host has it, or the tick cap is
 * hit. Returns the tick it connected on, or -1. */
static int run_until_connected(mp_loopback_t *net, mp_session_t *host, mp_session_t *client,
                               uint32_t *now)
{
    int tick;

    for (tick = 0; tick < 4000; ++tick) {
        *now += 16u;
        mp_loopback_pump(net, *now);
        mp_session_update(host, *now);
        mp_session_update(client, *now);
        if (mp_session_is_connected(client) && mp_session_peer_count(host) == 1u) {
            return tick;
        }
    }
    return -1;
}

/* Static, not on the stack: a session is over two megabytes (a channel and a bulk ring per peer,
 * fifteen peers), the loopback ring is another three hundred kilobytes, and two sessions plus the
 * ring overflow the default stack. The real caller keeps a session in static memory too. */
static mp_loopback_t s_net;
static mp_session_t  s_host;
static mp_session_t  s_client;

static void check_handshake(uint32_t loss, const char *label)
{
    mp_loopback_conditions_t cond;
    mp_loopback_t           *net    = &s_net;
    mp_session_t            *host   = &s_host;
    mp_session_t            *client = &s_client;
    mp_transport_t           host_t;
    mp_transport_t           client_t;
    uint32_t                 now = 0;
    int                      connected_on;

    ut_section(label);

    memset(&cond, 0, sizeof cond);
    cond.loss_percent    = loss;
    cond.reorder_percent = 15u;
    cond.delay_ms        = 20u;
    mp_loopback_init(net, &cond, 0xABCDEFu);
    host_t   = mp_loopback_transport(net, MP_LOOPBACK_ENDPOINT_A);
    client_t = mp_loopback_transport(net, MP_LOOPBACK_ENDPOINT_B);

    mp_session_init(host, MP_SESSION_HOST, &host_t, 0x111u);
    mp_session_init(client, MP_SESSION_CLIENT, &client_t, 0x222u);
    mp_session_connect(client, MP_LOOPBACK_ENDPOINT_A);

    connected_on = run_until_connected(net, host, client, &now);

    ut_check(connected_on >= 0, "the handshake completes");
    ut_check(mp_session_is_connected(client), "the client is connected");
    ut_check(mp_session_peer_count(host) == 1u, "and the host carries exactly one peer");
    ut_check(mp_session_joins(host) == 1u && mp_session_joins(client) == 1u,
             "each side counted one join");

    {
        /* The two sides agree on the connection id, which is the shared secret a spoofer lacks. */
        const mp_peer_t *hp = mp_session_peer(host, 0);
        const mp_peer_t *cp = mp_session_peer(client, 0);

        ut_check(hp != NULL && cp != NULL && hp->connection_id == cp->connection_id &&
                 hp->connection_id != 0u, "both sides hold the same non-zero connection id");
    }

    /* Both ways, a reliable message crosses the connected session and is read back whole. */
    ut_check(mp_session_send_reliable(client, 0, "ping", 4u), "the client queues a message");
    ut_check(mp_session_broadcast_reliable(host, "pong", 4u) == 1u,
             "the host broadcasts to its one peer");

    {
        bool    host_got = false;
        bool    client_got = false;
        uint8_t buffer[16];
        size_t  bytes;
        int     tick;

        for (tick = 0; tick < 4000 && !(host_got && client_got); ++tick) {
            now += 16u;
            mp_loopback_pump(net, now);
            mp_session_update(host, now);
            mp_session_update(client, now);
            if (!host_got && mp_session_read_reliable(host, 0, buffer, sizeof buffer, &bytes)) {
                host_got = (bytes == 4u && memcmp(buffer, "ping", 4u) == 0);
            }
            if (!client_got && mp_session_read_reliable(client, 0, buffer, sizeof buffer, &bytes)) {
                client_got = (bytes == 4u && memcmp(buffer, "pong", 4u) == 0);
            }
        }
        ut_check(host_got, "the host reads the client's message");
        ut_check(client_got, "the client reads the host's message");
    }
}

static void check_no_host(void)
{
    mp_loopback_t  *net    = &s_net;
    mp_session_t   *client = &s_client;
    mp_transport_t  client_t;
    uint32_t        now = 0;
    int             tick;

    ut_section("a client with no host keeps asking for a while, then gives up");

    mp_loopback_init(net, NULL, 5u);
    client_t = mp_loopback_transport(net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(client, MP_SESSION_CLIENT, &client_t, 0x333u);
    mp_session_connect(client, MP_LOOPBACK_ENDPOINT_A);

    /* No host is updated, so nothing ever answers. Two things have to hold, and the field showed
     * why both matter. One unanswered handshake must NOT end the attempt: a player who joins
     * before the host has opened its lobby would otherwise be stuck with a screen that still says
     * it is connecting, and only a restart would get him out. And the asking must still stop, or
     * a wrong address would be asked about for the rest of the process.
     *
     * 600 ticks of 16 ms is 9600 ms: well past one 5000 ms handshake and well inside the 30000 ms
     * join window. */
    for (tick = 0; tick < 600; ++tick) {
        now += 16u;
        mp_loopback_pump(net, now);
        mp_session_update(client, now);
    }
    ut_check(!mp_session_is_connected(client), "it never claims to be connected");
    ut_check(mp_session_peer(client, 0)->state != MP_PEER_FREE,
             "after one unanswered handshake it is still asking");

    /* And on to 38400 ms, which is past the window. */
    for (tick = 0; tick < 1800; ++tick) {
        now += 16u;
        mp_loopback_pump(net, now);
        mp_session_update(client, now);
    }
    ut_check(!mp_session_is_connected(client), "it still never claims to be connected");
    ut_check(mp_session_peer(client, 0)->state == MP_PEER_FREE,
             "and past the window its peer slot is free again");
}

/* Both sides forward `ticks` steps; `run_host` false leaves the host frozen, as a level load does.
 * */
static void run_both(mp_loopback_t *net, mp_session_t *host, mp_session_t *client, uint32_t *now,
                     int ticks, bool run_host)
{
    int tick;

    for (tick = 0; tick < ticks; ++tick) {
        *now += 16u;
        mp_loopback_pump(net, *now);
        if (run_host) {
            mp_session_update(host, *now);
        }
        mp_session_update(client, *now);
    }
}

/* A statement for the test: `bytes` of its text, 0 for none. The session carries it as bytes and
 * knows nothing of what it says. */
static void state(mp_session_t *session, const char *text)
{
    ut_check(mp_session_set_statement(session, (const uint8_t *)text,
                                      text != NULL ? strlen(text) : 0u),
             "the statement is taken");
}

static uint32_t s_judged;

/* The judge the host is handed, standing in for the feature's: two statements that are not the
 * same bytes are refused as a mod, and the detail says what the host stated. */
static uint8_t judge_by_bytes(const uint8_t *own, size_t own_bytes, const uint8_t *far,
                              size_t far_bytes, uint8_t *detail, size_t capacity,
                              size_t *detail_bytes)
{
    ++s_judged;
    *detail_bytes = 0u;
    if (own_bytes == far_bytes && memcmp(own, far, own_bytes) == 0) {
        return (uint8_t)MP_DENY_NONE;
    }
    *detail_bytes = own_bytes < capacity ? own_bytes : capacity;
    memcpy(detail, own, *detail_bytes);
    return (uint8_t)MP_DENY_MODS;
}

static void fresh_pair(const char *host_statement, const char *client_statement,
                       mp_transport_t *host_t, mp_transport_t *client_t)
{
    mp_loopback_init(&s_net, NULL, 0x5EEDu);
    *host_t   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    *client_t = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(&s_host, MP_SESSION_HOST, host_t, 0x444u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, client_t, 0x555u);
    state(&s_host, host_statement);
    state(&s_client, client_statement);
    mp_session_set_judge(&s_host, judge_by_bytes);
    s_judged = 0u;
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
}

static void check_content_gate(void)
{
    mp_transport_t host_t;
    mp_transport_t client_t;
    uint32_t       now = 0;
    const uint8_t *detail = NULL;
    size_t         detail_bytes;

    ut_section("a client whose statement the host's judge refuses is refused, and told why");

    fresh_pair("multiplayer 6ABB2222", "multiplayer 6ABB0000", &host_t, &client_t);
    run_both(&s_net, &s_host, &s_client, &now, 60, true);
    ut_check(!mp_session_is_connected(&s_client) && mp_session_peer_count(&s_host) == 0u,
             "neither side connects");
    ut_check(mp_session_last_deny(&s_client) == MP_DENY_MODS,
             "the client knows it was the judge's reason, not a full host");
    detail_bytes = mp_session_last_deny_detail(&s_client, &detail);
    ut_check(detail_bytes == strlen("multiplayer 6ABB2222") && detail != NULL &&
                 memcmp(detail, "multiplayer 6ABB2222", detail_bytes) == 0,
             "and it holds the detail the judge wrote behind the reason, byte for byte");
    ut_check(mp_session_peer(&s_client, 0)->state == MP_PEER_FREE && !s_client.connect_wanted,
             "and it stops asking: a refusal is an answer");

    fresh_pair("multiplayer 6ABB2222", NULL, &host_t, &client_t);
    ut_check(run_until_connected(&s_net, &s_host, &s_client, &now) >= 0,
             "a client that states nothing still joins");
    ut_check(mp_session_last_deny(&s_client) == MP_DENY_NONE && s_judged == 0u,
             "with nothing refused and nothing judged");

    fresh_pair(NULL, "multiplayer 6ABB2222", &host_t, &client_t);
    ut_check(run_until_connected(&s_net, &s_host, &s_client, &now) >= 0 && s_judged == 0u,
             "and a host that states nothing, the dedicated server's case, judges nobody");

    fresh_pair("multiplayer 6ABB2222", "multiplayer 6ABB2222", &host_t, &client_t);
    ut_check(run_until_connected(&s_net, &s_host, &s_client, &now) >= 0 && s_judged != 0u,
             "the same statement is judged and admitted");
    ut_check(mp_session_last_deny_detail(&s_client, &detail) == 0u,
             "and a client never refused holds no detail");

    ut_check(!mp_session_set_statement(&s_client, (const uint8_t *)"x",
                                       MP_SESSION_STATEMENT_BYTES + 1u) &&
                 mp_session_statement_bytes(&s_client) == 0u,
             "a statement longer than its room is refused, and leaves none");
}

static void check_leave_notice(void)
{
    mp_transport_t host_t;
    mp_transport_t client_t;
    uint32_t       now = 0;

    ut_section("a peer that leaves frees the far slot at once, not at the timeout");

    fresh_pair(NULL, NULL, &host_t, &client_t);
    ut_check(run_until_connected(&s_net, &s_host, &s_client, &now) >= 0, "joined");
    mp_session_disconnect(&s_client);
    run_both(&s_net, &s_host, &s_client, &now, 10, true);   /* 160 ms, far under the timeout */
    ut_check(mp_session_peer_count(&s_host) == 0u && mp_session_leaves(&s_host) == 1u,
             "the host counted one leave and holds no peer");
    ut_check(!mp_session_is_connected(&s_client) && !s_client.connect_wanted,
             "the leaver does not try to come back");

    fresh_pair(NULL, NULL, &host_t, &client_t);
    ut_check(run_until_connected(&s_net, &s_host, &s_client, &now) >= 0, "joined again");
    mp_session_disconnect(&s_host);
    run_both(&s_net, &s_host, &s_client, &now, 10, true);
    ut_check(!mp_session_is_connected(&s_client) && mp_session_leaves(&s_client) == 1u,
             "a host that quits takes the client down with a notice");
    run_both(&s_net, &s_host, &s_client, &now, 100, true);
    ut_check(mp_session_peer(&s_client, 0)->state == MP_PEER_FREE,
             "and the client does not rejoin a host that left on purpose");
}

/* The host's one way to part with a single peer, which a deathmatch uses on a client whose
 * content differs: a denial with the reason, and the slot free at once. */
static void check_sent_away(void)
{
    mp_transport_t host_t;
    mp_transport_t client_t;
    uint32_t       now = 0;

    ut_section("a host sends one peer away and tells it why");

    fresh_pair(NULL, NULL, &host_t, &client_t);
    ut_check(run_until_connected(&s_net, &s_host, &s_client, &now) >= 0, "joined");
    ut_check(!mp_session_drop(&s_client, 0u, MP_DENY_CONTENT), "a client sends nobody away");
    ut_check(mp_session_drop(&s_host, 0u, MP_DENY_CONTENT) &&
                 mp_session_peer_count(&s_host) == 0u,
             "the host sends its peer away and holds the slot free at once");
    ut_check(!mp_session_drop(&s_host, 0u, MP_DENY_CONTENT), "an empty slot sends nobody away");
    run_both(&s_net, &s_host, &s_client, &now, 10, true);
    ut_check(!mp_session_is_connected(&s_client) &&
                 mp_session_last_deny(&s_client) == MP_DENY_CONTENT,
             "the client is out and knows it was the content");
    ut_check(!s_client.connect_wanted, "and does not ask again on its own");
}

static void check_rejoin(void)
{
    mp_transport_t host_t;
    mp_transport_t client_t;
    uint32_t       now = 0;

    ut_section("a client whose host went silent past the timeout gives it up at once");

    fresh_pair(NULL, NULL, &host_t, &client_t);
    ut_check(run_until_connected(&s_net, &s_host, &s_client, &now) >= 0, "joined");

    /* The host stops for thirty-two seconds, longer than the connected timeout, as a host whose
     * process has died does; a level load no longer stops it, the idle pump services it. */
    run_both(&s_net, &s_host, &s_client, &now, 2000, false);
    ut_check(!mp_session_is_connected(&s_client) && mp_session_drops(&s_client) == 1u,
             "the client counted the drop");
    ut_check(mp_session_peer(&s_client, 0)->state == MP_PEER_FREE && !s_client.connect_wanted,
             "and asks no more, rather than spend a minute rejoining a level that answers "
             "nothing");
    run_both(&s_net, &s_host, &s_client, &now, 200, true);
    ut_check(!mp_session_is_connected(&s_client),
             "a host that resumes does not pull it back in on its own");

    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    ut_check(run_until_connected(&s_net, &s_host, &s_client, &now) >= 0,
             "a player who connects again is taken back in: coming back is the host's side");
    ut_check(mp_session_joins(&s_client) == 2u && mp_session_joins(&s_host) == 2u,
             "both sides counted a second join");
    ut_check(mp_session_peer(&s_host, 0)->connection_id ==
                 mp_session_peer(&s_client, 0)->connection_id,
             "under a fresh shared id");
}

/* ================================== THE BULK LANE ============================================
 *
 * The lane exists because the reliable channel could not carry a block, and the way it failed was
 * the worst kind: silently, for ever, taking every other reliable message down with it. So the
 * property checked here is not "a bulk note arrives" but the one the field run disproved: the
 * lane is not the channel. A channel jammed to refusal must not cost the lane one note.
 */
static void check_the_bulk_lane_is_not_the_channel(void)
{
    mp_loopback_conditions_t cond;
    mp_transport_t           host_t;
    mp_transport_t           client_t;
    static uint8_t           big[MP_CHANNEL_MESSAGE_BYTES];
    uint8_t                  note[MP_SESSION_BULK_BYTES];
    uint32_t                 now = 0;
    size_t                   queued = 0;
    size_t                   bytes = 0;
    size_t                   taken = 0;
    int                      tick;

    ut_section("a jammed reliable channel does not touch the bulk lane");

    memset(&cond, 0, sizeof cond);
    cond.delay_ms = 20u;
    mp_loopback_init(&s_net, &cond, 0x51DE5u);
    host_t   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    client_t = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(&s_host, MP_SESSION_HOST, &host_t, 0x111u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &client_t, 0x222u);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    ut_check(run_until_connected(&s_net, &s_host, &s_client, &now) >= 0, "the two are connected");

    memset(big, 0x5A, sizeof big);
    while (mp_session_send_reliable(&s_host, 0, big, sizeof big)) {
        ++queued;
    }
    ut_checkf(queued == MP_CHANNEL_SEND_SLOTS,
              "the channel takes its whole queue of %u and then refuses, not %u",
              (unsigned)MP_CHANNEL_SEND_SLOTS, (unsigned)queued);
    ut_check(!mp_session_send_reliable(&s_host, 0, big, sizeof big),
             "so the reliable channel is jammed as hard as it can be");

    memset(note, 0xC3, sizeof note);
    ut_check(mp_session_send_bulk(&s_host, 0, note, 64u),
             "and a bulk note goes out all the same: it does not queue, so a "
             "queue cannot refuse it");

    for (tick = 0; tick < 40 && taken == 0u; ++tick) {
        now += 16u;
        mp_loopback_pump(&s_net, now);
        mp_session_update(&s_host, now);
        mp_session_update(&s_client, now);
        while (mp_session_read_bulk(&s_client, 0, note, sizeof note, &bytes)) {
            ++taken;
        }
    }
    ut_checkf(taken == 1u, "and it arrives, %u of 1", (unsigned)taken);
    ut_checkf(bytes == 64u, "whole, %u bytes of 64", (unsigned)bytes);

    ut_section("a ring nobody empties loses notes and says so");
    for (tick = 0; tick < (int)MP_SESSION_BULK_RING + 4; ++tick) {
        (void)mp_session_send_bulk(&s_host, 0, note, 32u);
    }
    for (tick = 0; tick < 40; ++tick) {
        now += 16u;
        mp_loopback_pump(&s_net, now);
        mp_session_update(&s_host, now);
        mp_session_update(&s_client, now);
    }
    ut_checkf(mp_session_bulk_overrun(&s_client) == 4u,
              "four past the ring of %u were dropped and counted, not %u",
              (unsigned)MP_SESSION_BULK_RING, (unsigned)mp_session_bulk_overrun(&s_client));
}

/* The whole protocol, across a link that loses a third of everything.
 *
 * The sender walks its slices round robin, skips what the mask names, and rests a slice after
 * sending it. The receiver answers with its whole mask on a timer. The file must arrive complete
 * and hash to its own name, and, this is the part the old shape got wrong, the SENDER must only
 * call it finished once the receiver's mask says so.
 */
static void check_a_block_crosses_a_lossy_link(void)
{
    static uint8_t                file[40u * 1024u + 77u];
    static mp_savefile_assembly_t assembly;
    static uint8_t                acked[MP_SAVEFILE_MASK_BYTES_FOR(MP_SAVEFILE_MAX_CHUNKS)];
    static uint8_t                ever[MP_SAVEFILE_MASK_BYTES_FOR(MP_SAVEFILE_MAX_CHUNKS)];
    static uint32_t               sent_ms[MP_SAVEFILE_MAX_CHUNKS];
    mp_loopback_conditions_t      cond;
    mp_transport_t                host_t;
    mp_transport_t                client_t;
    uint8_t                       note[MP_SESSION_BULK_BYTES];
    uint32_t                      now = 0;
    uint32_t                      id;
    uint16_t                      count;
    uint16_t                      walk = 0;
    uint32_t                      last_ack_ms = 0;
    size_t                        bytes = 0;
    size_t                        i;
    bool                          sender_says_done = false;
    int                           tick;

    ut_section("a block crosses a link that loses a third of everything");

    for (i = 0; i < sizeof file; ++i) {
        file[i] = (uint8_t)(i * 31u + (i >> 8));
    }
    id    = mp_savefile_digest(file, sizeof file);
    count = mp_savefile_chunk_count((uint32_t)sizeof file);

    memset(&cond, 0, sizeof cond);
    cond.loss_percent    = 33u;
    cond.reorder_percent = 20u;
    cond.delay_ms        = 20u;
    mp_loopback_init(&s_net, &cond, 0xB10Cu);
    host_t   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    client_t = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(&s_host, MP_SESSION_HOST, &host_t, 0x111u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &client_t, 0x222u);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    ut_check(run_until_connected(&s_net, &s_host, &s_client, &now) >= 0, "the two are connected");

    memset(acked, 0, sizeof acked);
    memset(ever, 0, sizeof ever);
    memset(sent_ms, 0, sizeof sent_ms);
    mp_savefile_assembly_reset(&assembly);
    ut_check(mp_savefile_assembly_open(&assembly, id, (uint32_t)sizeof file),
             "the receiver names the file it wants");

    for (tick = 0; tick < 4000 && !sender_says_done; ++tick) {
        uint32_t laid    = 0;
        uint16_t scanned = 0;

        now += 16u;
        mp_loopback_pump(&s_net, now);
        mp_session_update(&s_host, now);
        mp_session_update(&s_client, now);

        /* The sender. */
        while (scanned < count && laid < 4u) {
            uint16_t index = (uint16_t)((walk + scanned) % count);
            size_t   length;

            ++scanned;
            if (mp_savefile_mask_has(acked, index)) {
                continue;
            }
            if (mp_savefile_mask_has(ever, index) && now - sent_ms[index] < 100u) {
                continue;
            }
            length = mp_savefile_chunk_encode(id, file, (uint32_t)sizeof file, index, note,
                                              sizeof note);
            if (length == 0u || !mp_session_send_bulk(&s_host, 0, note, length)) {
                break;
            }
            mp_savefile_mask_set(ever, index);
            sent_ms[index] = now;
            ++laid;
        }
        walk = (uint16_t)((walk + scanned) % count);

        /* The receiver takes what arrived and says what it holds. */
        while (mp_session_read_bulk(&s_client, 0, note, sizeof note, &bytes)) {
            mp_savefile_chunk_t chunk;

            if (mp_savefile_chunk_decode(note, bytes, &chunk)) {
                (void)mp_savefile_assembly_take(&assembly, &chunk);
            }
        }
        if (now - last_ack_ms >= 100u) {
            size_t length = mp_savefile_ack_encode(id, count, assembly.have, note, sizeof note);

            last_ack_ms = now;
            if (length > 0u) {
                (void)mp_session_send_bulk(&s_client, 0, note, length);
            }
        }

        /* And the sender learns what to stop sending. */
        while (mp_session_read_bulk(&s_host, 0, note, sizeof note, &bytes)) {
            const uint8_t *mask       = NULL;
            uint32_t       said_id    = 0;
            uint16_t       said_count = 0;

            if (mp_savefile_ack_decode(note, bytes, &said_id, &said_count, &mask) &&
                said_id == id && said_count == count) {
                memcpy(acked, mask, MP_SAVEFILE_MASK_BYTES_FOR(count));
                if (mp_savefile_mask_count(acked, count) == count) {
                    sender_says_done = true;
                }
            }
        }
    }

    ut_checkf(assembly.complete, "the file is whole on the far side after %d tick(s)", tick);
    ut_check(assembly.file_id == id,
             "and it hashed to its own name, so no byte is in the wrong place");
    ut_check(memcmp(assembly.bytes, file, sizeof file) == 0, "byte for byte");
    ut_check(sender_says_done,
             "and the SENDER called it finished only because the receiver's mask said so");
    ut_checkf(mp_savefile_mask_first_missing(acked, count) == count,
              "with nothing left unacknowledged, lowest missing %u of %u",
              (unsigned)mp_savefile_mask_first_missing(acked, count), (unsigned)count);
}

/* ============================================================================================== */

/* The digest the handshake stands on, against the two first cases of RFC 4231, so the provider is
 * known to be HMAC-SHA256 and not merely something that answers. And the one property the empty
 * password leans on: every key shorter than a block is padded with zeros, so one zero byte and two
 * are the same key. */
static void check_the_digest(void)
{
    static const uint8_t CASE1_KEY[20] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b
    };
    static const uint8_t CASE1[MP_ENTROPY_HMAC_BYTES] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf,
        0xce, 0xaf, 0x0b, 0xf1, 0x2b, 0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83,
        0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7
    };
    static const uint8_t CASE2[MP_ENTROPY_HMAC_BYTES] = {
        0x5b, 0xdc, 0xc1, 0x46, 0xbf, 0x60, 0x75, 0x4e, 0x6a, 0x04, 0x24,
        0x26, 0x08, 0x95, 0x75, 0xc7, 0x5a, 0x00, 0x3f, 0x08, 0x9d, 0x27,
        0x39, 0x83, 0x9d, 0xec, 0x58, 0xb9, 0x64, 0xec, 0x38, 0x43
    };
    static const uint8_t ONE_ZERO[1] = { 0u };
    static const uint8_t TWO_ZEROS[2] = { 0u, 0u };
    uint8_t out[MP_ENTROPY_HMAC_BYTES];
    uint8_t again[MP_ENTROPY_HMAC_BYTES];

    ut_section("the handshake's digest is HMAC-SHA256, by RFC 4231's own cases");

    ut_check(mp_entropy_hmac(CASE1_KEY, sizeof CASE1_KEY, "Hi There", 8u, out) &&
                 memcmp(out, CASE1, sizeof out) == 0,
             "test case 1: twenty bytes of 0x0b over \"Hi There\"");
    ut_check(mp_entropy_hmac("Jefe", 4u, "what do ya want for nothing?", 28u, out) &&
                 memcmp(out, CASE2, sizeof out) == 0,
             "test case 2: \"Jefe\" over \"what do ya want for nothing?\"");
    ut_check(mp_entropy_hmac(ONE_ZERO, sizeof ONE_ZERO, "x", 1u, out) &&
                 mp_entropy_hmac(TWO_ZEROS, sizeof TWO_ZEROS, "x", 1u, again) &&
                 mp_entropy_equal(out, again, sizeof out),
             "one zero byte and two are one key, so an empty password is the key of one zero");
    ut_check(mp_entropy_hmac(NULL, 0u, "x", 1u, again) && mp_entropy_equal(out, again, sizeof out),
             "and no key at all is that same key");
    again[31] ^= 1u;
    ut_check(!mp_entropy_equal(out, again, sizeof out), "a digest one bit off is not the digest");
}

int main(void)
{
    check_the_digest();
    check_handshake(0u, "a handshake on a clean link");
    check_handshake(30u, "a handshake through thirty percent loss");
    check_no_host();
    check_content_gate();
    check_leave_notice();
    check_sent_away();
    check_rejoin();
    check_the_bulk_lane_is_not_the_channel();
    check_a_block_crosses_a_lossy_link();
    return ut_summary("mp_session");
}
