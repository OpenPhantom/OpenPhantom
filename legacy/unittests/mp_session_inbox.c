/* mp_session_inbox.c: a receiver that pumps between substeps and does not read loses nothing.
 *
 * The idle pumps receive while no substep runs: every level load, every blend and every pause. A
 * receive stores and acknowledges the reliable messages of a packet, and the only readers run in a
 * substep or in an open lobby. The channel's order window counts from what has been taken off it,
 * so a receiver that stored thirty two and read none refused every later packet that carried a
 * message, and a refused packet took its payload with it: the far body and the host's world stopped
 * for as long as the load lasted. The sessions here are real and meet over the loopback, so every
 * packet is built with the capacity the session really hands the channel.
 */
#include "unittest.h"

#include "mp_inbox.h"
#include "mp_loopback.h"
#include "mp_session.h"
#include "mp_transport.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Static: a session is several megabytes and the loopback ring several hundred kilobytes. */
static mp_loopback_t s_net;
static mp_session_t  s_host;
static mp_session_t  s_client;

/* One substep of the engine is 31.25 ms, which is the rate the host sets a payload at. The receiver
 * acknowledges only in its keepalive, every hundred milliseconds, as a loading client does; with
 * rounds much shorter than that no acknowledgement would arrive and the sender would hold back on
 * its own, which would hide the defect. */
#define ROUND_MS 31u

/* The digest of the movers of the largest garden level is about this size. */
#define NOTE_BYTES 200u

/* A payload that leaves room for a note beside it, so what is measured is the reader and not the
 * seat. A payload that leaves no room is a different defect with a test of its own. */
#define PAYLOAD_BYTES 400u

#define MOST_ROUNDS 260u

static uint32_t s_now;

typedef struct outcome {
    unsigned queued;       /* notes the host's channel took */
    unsigned landed;       /* distinct payloads the client read while reading no note */
    unsigned delivered;    /* notes read afterwards */
    unsigned out_of_order;
} outcome_t;

static void pair_up(void)
{
    static mp_transport_t host_t;
    static mp_transport_t client_t;
    int tick;

    s_now = 0u;
    mp_loopback_init(&s_net, NULL, 0x1B0Eu);
    host_t   = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_A);
    client_t = mp_loopback_transport(&s_net, MP_LOOPBACK_ENDPOINT_B);
    mp_session_init(&s_host, MP_SESSION_HOST, &host_t, 0x777u);
    mp_session_init(&s_client, MP_SESSION_CLIENT, &client_t, 0x888u);
    mp_session_connect(&s_client, MP_LOOPBACK_ENDPOINT_A);
    for (tick = 0; tick < 4000; ++tick) {
        s_now += 16u;
        mp_loopback_pump(&s_net, s_now);
        mp_session_update(&s_host, s_now);
        mp_session_update(&s_client, s_now);
        if (mp_session_is_connected(&s_client) && mp_session_peer_count(&s_host) == 1u) {
            return;
        }
    }
}

/* Payloads carry their round in the first two bytes, notes their number in the second and third. */
static void put_index(uint8_t *at, unsigned index)
{
    at[0] = (uint8_t)(index & 0xFFu);
    at[1] = (uint8_t)((index >> 8) & 0xFFu);
}

static unsigned get_index(const uint8_t *at)
{
    return (unsigned)at[0] | ((unsigned)at[1] << 8);
}

/* The idle pump as the client runs it: a receive, the payload drain and a service. No reliable
 * read. */
static void pump_client(bool seen[])
{
    uint8_t buffer[MP_SESSION_PAYLOAD_BYTES];
    size_t  bytes = 0;

    mp_session_receive(&s_client, s_now);
    while (mp_session_read_payload(&s_client, 0u, buffer, sizeof buffer, &bytes)) {
        if (bytes == PAYLOAD_BYTES && get_index(buffer) < MOST_ROUNDS) {
            seen[get_index(buffer)] = true;
        }
    }
    mp_session_service(&s_client, s_now);
}

static void read_notes(outcome_t *out)
{
    uint8_t read[MP_CHANNEL_MESSAGE_BYTES];
    size_t  bytes = 0;

    while (mp_session_read_reliable(&s_client, 0u, read, sizeof read, &bytes)) {
        if (bytes == NOTE_BYTES && read[0] == 0x96u) {
            out->out_of_order += (get_index(read + 1) != out->delivered) ? 1u : 0u;
            ++out->delivered;
        }
    }
}

/* The host offers a note every `note_every` substeps and a payload in every one, for `rounds`
 * substeps, while the client only pumps; then the client reads, as its first substep would. */
static void run(unsigned rounds, unsigned note_every, outcome_t *out)
{
    static bool seen[MOST_ROUNDS];
    uint8_t     note[NOTE_BYTES];
    uint8_t     payload[PAYLOAD_BYTES];
    unsigned    round;

    memset(out, 0, sizeof *out);
    memset(seen, 0, sizeof seen);
    pair_up();
    for (round = 0; round < rounds; ++round) {
        s_now += ROUND_MS;
        if (round % note_every == 0u) {
            memset(note, 0x96, sizeof note);
            put_index(note + 1, out->queued);
            out->queued += mp_session_send_reliable(&s_host, 0u, note, sizeof note) ? 1u : 0u;
        }
        memset(payload, 0x5A, sizeof payload);
        put_index(payload, round);
        (void)mp_session_set_payload(&s_host, 0u, payload, sizeof payload);
        mp_loopback_pump(&s_net, s_now);
        mp_session_update(&s_host, s_now);
        pump_client(seen);
    }
    /* What is still on the wire lands; the host sets no payload any more. */
    for (round = 0; round < 8u; ++round) {
        s_now += ROUND_MS;
        mp_loopback_pump(&s_net, s_now);
        mp_session_update(&s_host, s_now);
        pump_client(seen);
    }
    for (round = 0; round < rounds; ++round) {
        out->landed += seen[round] ? 1u : 0u;
    }

    read_notes(out);
    for (round = 0; round < 64u && out->delivered < out->queued; ++round) {
        s_now += ROUND_MS;
        mp_loopback_pump(&s_net, s_now);
        mp_session_update(&s_host, s_now);
        mp_session_update(&s_client, s_now);
        read_notes(out);
    }
}

static void check_a_receiver_that_only_pumps(void)
{
    outcome_t out;

    ut_section("a receiver that pumps for five seconds and reads no note loses no payload");
    run(160u, 2u, &out);
    ut_checkf(out.queued >= 76u, "the host queued %u notes, about one every other substep",
              out.queued);
    ut_checkf(mp_session_refused_past_window(&s_client) == 0u,
              "the client refused %u packet(s) for a message past its window, and must refuse none",
              (unsigned)mp_session_refused_past_window(&s_client));
    ut_checkf(out.landed == 160u, "%u of 160 payloads landed while no note was read", out.landed);
    ut_checkf(mp_session_inbox_most_notes(&s_client) > MP_CHANNEL_ORDER_SLOTS,
              "the inbox held %u notes at most, more than the %u the channel alone would have "
              "held unread", (unsigned)mp_session_inbox_most_notes(&s_client),
              (unsigned)MP_CHANNEL_ORDER_SLOTS);
    ut_check(mp_session_inbox_stalls(&s_client) == 0u, "and it was never full");
    ut_checkf(out.delivered == out.queued && out.out_of_order == 0u,
              "then every one of the %u notes is read, %u of them out of order", out.delivered,
              out.out_of_order);
}

/* A note every substep for eight seconds: the inbox fills, the channel's window stands again, and
 * packets with a message are refused. Their payloads are handed on all the same, and once the
 * reader runs every note arrives in order. */
static void check_a_full_inbox(void)
{
    outcome_t out;

    ut_section("a full inbox refuses packets again, and their payloads still land");
    run(MOST_ROUNDS, 1u, &out);
    ut_checkf(mp_session_inbox_stalls(&s_client) > 0u, "the inbox filled %u time(s)",
              (unsigned)mp_session_inbox_stalls(&s_client));
    ut_checkf(mp_session_refused_past_window(&s_client) > 0u,
              "and the channel refused %u packet(s) behind it",
              (unsigned)mp_session_refused_past_window(&s_client));
    ut_checkf(mp_session_payloads_past_window(&s_client) > 0u,
              "%u payload(s) of those packets were handed on",
              (unsigned)mp_session_payloads_past_window(&s_client));
    ut_checkf(out.landed == MOST_ROUNDS, "%u of %u payloads landed", out.landed,
              (unsigned)MOST_ROUNDS);
    ut_checkf(out.delivered == out.queued && out.out_of_order == 0u,
              "and every one of the %u notes the host queued is read, %u out of order",
              out.delivered, out.out_of_order);
}

/* The price in memory, measured rather than estimated, for the comments that state it. */
static void check_the_size(void)
{
    ut_section("what a session costs");
    ut_checkf(sizeof(mp_session_t) < 4u * 1024u * 1024u, "a session is %u bytes, a peer %u",
              (unsigned)sizeof(mp_session_t), (unsigned)sizeof(mp_peer_t));
}

int main(void)
{
    check_a_receiver_that_only_pumps();
    check_a_full_inbox();
    check_the_size();
    return ut_summary("mp_session_inbox");
}
