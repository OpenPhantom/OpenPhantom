/* mp_channel_state.c: a state note is the newest copy of its kind, not one message more.
 *
 * Two channels across a function call, the test playing the network. Every packet is built with
 * the capacity the session really hands the builder, MP_CHANNEL_BUDGET_BYTES, never the full 1200.
 */
#include "unittest.h"

#include "mp_channel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_channel_t s_sender;
static mp_channel_t s_receiver;

#define ROSTER 0x8Fu
#define DIGEST 0x86u
#define SETUP  0x92u

/* One packet from `from`, handed to `to` unless it is lost. */
static void carry(mp_channel_t *from, mp_channel_t *to, uint32_t now_ms, bool lost)
{
    uint8_t        packet[MP_CHANNEL_BUDGET_BYTES];
    size_t         bytes = 0;
    const uint8_t *payload = NULL;
    size_t         payload_bytes = 0;

    if (mp_channel_packet_build(from, now_ms, NULL, 0u, packet, sizeof packet, &bytes) && !lost) {
        (void)mp_channel_packet_receive_at(to, now_ms, packet, bytes, &payload, &payload_bytes);
    }
}

static mp_channel_state_outcome_t state(uint8_t kind, uint32_t key, char letter, size_t bytes)
{
    uint8_t data[200];

    memset(data, letter, sizeof data);
    data[0] = kind;
    return mp_channel_send_state(&s_sender, kind, key, data, bytes);
}

/* The seats a kind holds in the send ring. */
static size_t seats_of(const mp_channel_t *channel, uint8_t kind)
{
    size_t index;
    size_t seats = 0;

    for (index = 0; index < MP_CHANNEL_SEND_SLOTS; ++index) {
        seats += (channel->send_slots[index].used && channel->send_slots[index].kind == kind)
                     ? 1u : 0u;
    }
    return seats;
}

static void check_the_four_answers(void)
{
    ut_section("replaced in place, shrunk in flight, left out unchanged, "
               "waiting behind a shrunk one");
    mp_channel_init(&s_sender);
    mp_channel_init(&s_receiver);

    ut_check(state(ROSTER, 1u, 'a', 120u) == MP_CHANNEL_STATE_QUEUED, "the first roster is queued");
    ut_check(state(ROSTER, 2u, 'b', 130u) == MP_CHANNEL_STATE_REPLACED &&
             s_sender.next_message_id == 1u && s_sender.send_slots[0].bytes == 130u,
             "a second before any packet overwrites it in place and keeps its id");
    carry(&s_sender, &s_receiver, 10u, true);
    ut_check(state(ROSTER, 3u, 'c', 140u) == MP_CHANNEL_STATE_QUEUED &&
             s_sender.send_slots[0].shrunk && s_sender.send_slots[0].bytes == 0u &&
             mp_channel_send_pending(&s_sender) == 2u,
             "one after it went out shrinks the copy in flight to nothing, keeping its id, and "
             "is queued behind it");
    ut_check(state(ROSTER, 3u, 'c', 140u) == MP_CHANNEL_STATE_UNCHANGED &&
             mp_channel_send_pending(&s_sender) == 2u,
             "an equal repeat while it is on its way is left out");
    carry(&s_sender, &s_receiver, 20u, true);
    ut_check(state(ROSTER, 4u, 'd', 150u) == MP_CHANNEL_STATE_WAITING &&
             mp_channel_state_waiting(&s_sender) == 1u && seats_of(&s_sender, ROSTER) == 2u,
             "the next, with a shrunk copy still unanswered, waits outside: two seats, not three");
    ut_check(state(ROSTER, 5u, 'e', 160u) == MP_CHANNEL_STATE_REPLACED &&
             mp_channel_state_waiting(&s_sender) == 1u,
             "and a newer one replaces the one waiting");
    ut_check(mp_channel_send(&s_sender, "EV", 2u) && mp_channel_send(&s_sender, "EV", 2u) &&
             mp_channel_send_pending(&s_sender) == 4u,
             "an event is never replaced: two equal events are two messages");
    while (mp_channel_send(&s_sender, "EV", 2u)) {
    }
    ut_check(!mp_channel_state_would_take(&s_sender, SETUP, 9u, 110u) &&
             state(SETUP, 9u, 's', 110u) == MP_CHANNEL_STATE_REFUSED,
             "with every seat taken a state of a new kind is refused, and the question said so");
    ut_check(mp_channel_state_would_take(&s_sender, ROSTER, 6u, 170u),
             "while the roster, waiting outside already, still has its place");
}

/* The receiver answers, the shrunk copy is retired, the waiting one goes in, and in the end the
 * receiver has read every id in order, the last state it read being the newest. */
static void check_the_newest_arrives_last(void)
{
    uint8_t  message[MP_CHANNEL_MESSAGE_BYTES];
    size_t   bytes = 0;
    uint32_t now = 30u;
    int      round;
    char     last = 0;
    uint32_t empty = 0;

    ut_section("once the far side answers, the newest state is the last one read");
    for (round = 0; round < 20; ++round) {
        now += 40u;
        carry(&s_sender, &s_receiver, now, false);
        carry(&s_receiver, &s_sender, now, false);
        now += 40u;
        carry(&s_receiver, &s_sender, now, false);
        carry(&s_sender, &s_receiver, now, false);
        while (mp_channel_message_read(&s_receiver, message, sizeof message, &bytes)) {
            if (bytes == 0u) {
                ++empty;
            } else if (message[0] == ROSTER) {
                last = (char)message[1];
            }
        }
    }
    ut_checkf(last == 'e' && mp_channel_send_pending(&s_sender) == 0u &&
              mp_channel_state_waiting(&s_sender) == 0u,
              "the last roster read carries 'e', and nothing is pending (%u empty read)",
              (unsigned)empty);
}

/* A far side that answers nothing, and three kinds, two that change every build and one every third
 * build: none of them holds more than two seats, whatever the tick in it does. */
static void check_a_silent_peer_holds_two_seats_a_kind(void)
{
    uint32_t round;
    bool     bounded = true;
    size_t   most = 0;

    ut_section("a far side that never answers: at most two seats of a kind, however long");
    mp_channel_init(&s_sender);
    mp_channel_init(&s_receiver);
    for (round = 1u; round <= 400u; ++round) {
        size_t seats;

        (void)state(ROSTER, round, 'r', 120u);
        (void)state(DIGEST, round / 3u, 'd', 60u);   /* a key that stays put for three builds */
        (void)state(SETUP, round * 7u, 's', 110u);
        carry(&s_sender, &s_receiver, round * 31u, true);   /* delivered, never answered */
        seats   = seats_of(&s_sender, ROSTER) + seats_of(&s_sender, DIGEST) +
                  seats_of(&s_sender, SETUP);
        bounded = bounded && seats <= 6u && mp_channel_state_waiting(&s_sender) <= 3u;
        most    = seats > most ? seats : most;
    }
    ut_checkf(bounded, "400 builds later three kinds held at most %u seats between them",
              (unsigned)most);
    ut_check(mp_channel_can_send(&s_sender, 100u), "and an event still finds a seat");
}

/* The question asked before a broadcast and the send itself cannot disagree. */
static void check_the_question_agrees_with_the_send(void)
{
    uint32_t seed = 12345u;
    uint32_t step;
    uint32_t disagreements = 0;
    uint32_t refusals = 0;

    ut_section("whether the channel would take a state is the answer the send then gives");
    mp_channel_init(&s_sender);
    mp_channel_init(&s_receiver);
    for (step = 0; step < 3000u; ++step) {
        uint8_t  kinds[3] = { ROSTER, DIGEST, SETUP };
        uint8_t  kind;
        uint32_t key;
        bool     would;
        bool     took;

        seed = seed * 1103515245u + 12345u;
        kind = kinds[(seed >> 8) % 3u];
        key  = (seed >> 12) % 4u;
        /* Events fill the ring now and then, so the answer is sometimes no. */
        if ((seed >> 20) % 2u == 0u) {
            uint32_t burst;

            for (burst = 0; burst < 4u; ++burst) {
                (void)mp_channel_send(&s_sender, "event", 5u);
            }
        }
        would = mp_channel_state_would_take(&s_sender, kind, key, 90u);
        took  = state(kind, key, 'x', 90u) != MP_CHANNEL_STATE_REFUSED;
        disagreements += (would != took) ? 1u : 0u;
        refusals += took ? 0u : 1u;
        if ((seed >> 24) % 3u == 0u) {
            carry(&s_sender, &s_receiver, step * 40u, (seed >> 26) % 2u == 0u);
        }
        if ((seed >> 27) % 32u == 0u) {
            carry(&s_receiver, &s_sender, step * 40u, false);
        }
    }
    ut_checkf(disagreements == 0u, "3000 sends, %u disagreements (%u of them refused)",
              (unsigned)disagreements, (unsigned)refusals);
}

/* Nine kinds of state at once, each with a row of its own to wait outside the channel in; a
 * channel keeps a row for every kind the session sends. A kind that finds every row taken is
 * refused where it should wait, and no line counts it. */
#define KINDS_AT_ONCE 9u

static void check_nine_kinds_wait_each_in_a_row_of_its_own(void)
{
    uint32_t waiting = 0;
    uint32_t refused = 0;
    uint8_t  kind;

    ut_section("nine kinds of state, each waiting outside the channel in a row of its own");
    mp_channel_init(&s_sender);
    mp_channel_init(&s_receiver);
    for (kind = 0xA0u; kind < 0xA0u + KINDS_AT_ONCE; ++kind) {
        (void)state(kind, 1u, 'a', 40u);
    }
    carry(&s_sender, &s_receiver, 10u, true);
    for (kind = 0xA0u; kind < 0xA0u + KINDS_AT_ONCE; ++kind) {
        (void)state(kind, 2u, 'b', 40u);   /* shrinks the first in flight and is queued */
    }
    carry(&s_sender, &s_receiver, 20u, true);
    for (kind = 0xA0u; kind < 0xA0u + KINDS_AT_ONCE; ++kind) {
        mp_channel_state_outcome_t outcome = state(kind, 3u, 'c', 40u);

        waiting += (outcome == MP_CHANNEL_STATE_WAITING) ? 1u : 0u;
        refused += (outcome == MP_CHANNEL_STATE_REFUSED) ? 1u : 0u;
    }
    ut_checkf(waiting == KINDS_AT_ONCE && refused == 0u &&
                  mp_channel_state_waiting(&s_sender) == KINDS_AT_ONCE,
              "all nine wait, none is refused (%u waiting, %u refused, %u rows in a channel)",
              (unsigned)waiting, (unsigned)refused, (unsigned)MP_CHANNEL_STATE_KINDS);
}

int main(void)
{
    check_the_four_answers();
    check_the_newest_arrives_last();
    check_a_silent_peer_holds_two_seats_a_kind();
    check_the_question_agrees_with_the_send();
    check_nine_kinds_wait_each_in_a_row_of_its_own();
    return ut_summary("mp_channel_state");
}
