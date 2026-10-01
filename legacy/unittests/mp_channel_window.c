/* mp_channel_window.c: a sender never lays a message the receiver's window cannot hold.
 *
 * The receiver holds messages up to thirty two ahead of the one it expects, and a packet that
 * carries one past that is not acknowledged; its payload used to be thrown away with it and is
 * handed out now, because the session's inbox can fill. The sender used to lay any of its sixty
 * four queued messages into a packet, so a lost packet followed by a burst was enough for the next
 * packet's snapshot to be thrown away with it, and nothing counted it. The rule is Fiedler's: never
 * send an id at or past the oldest unacknowledged one plus the receiver's window.
 */
#include "unittest.h"

#include "mp_channel.h"
/* For MP_SESSION_CHANNEL_CAP alone, the capacity the only real caller hands the builder. */
#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* File scope rather than stack: each channel is around 117 KB. */
static mp_channel_t s_sender;
static mp_channel_t s_receiver;

/* Larger than the eager size, so each rests after it travels, which is what lets a second packet
 * reach past the first one's messages. */
#define BODY_BYTES 40u
#define QUEUED     48u

static void check_a_burst_after_a_lost_packet(void)
{
    uint8_t        body[BODY_BYTES];
    uint8_t        packet[MP_SESSION_CHANNEL_CAP];
    size_t         bytes = 0;
    const uint8_t *payload = NULL;
    size_t         payload_bytes = 0;
    unsigned       queued = 0;
    unsigned       i;

    ut_section("a burst after a lost packet does not cost the next packet its payload");
    mp_channel_init(&s_sender);
    mp_channel_init(&s_receiver);
    memset(body, 0x5A, sizeof body);
    for (i = 0; i < QUEUED; ++i) {
        queued += mp_channel_send(&s_sender, body, sizeof body) ? 1u : 0u;
    }
    ut_check(queued == QUEUED, "forty eight messages are queued at once");
    ut_check(mp_channel_packet_build(&s_sender, 0u, "p1", 2u, packet, sizeof packet, &bytes),
             "the first packet is built, and lost on the way");
    ut_check(mp_channel_packet_build(&s_sender, 16u, "p2", 2u, packet, sizeof packet, &bytes),
             "the second is built a packet interval later");
    ut_check(mp_channel_packet_receive_at(&s_receiver, 20u, packet, bytes, &payload,
                                          &payload_bytes) &&
                 payload_bytes == 2u && memcmp(payload, "p2", 2u) == 0,
             "and arrives with its payload, because it carries nothing past the receiver's window");
    ut_check(mp_channel_refused_past_window(&s_receiver) == 0u, "the receiver refused nothing");
    ut_check(mp_channel_held_past_window(&s_sender) != 0u,
             "and the sender says it held back what the window could not take");
}

static void check_everything_still_arrives(void)
{
    uint8_t        body[BODY_BYTES];
    uint8_t        packet[MP_SESSION_CHANNEL_CAP];
    uint8_t        read[MP_CHANNEL_MESSAGE_BYTES];
    size_t         bytes = 0;
    size_t         got = 0;
    const uint8_t *payload = NULL;
    size_t         payload_bytes = 0;
    uint32_t       now = 0;
    unsigned       delivered = 0;
    unsigned       i;
    int            round;

    ut_section("held back is not lost: every message arrives, in order");
    mp_channel_init(&s_sender);
    mp_channel_init(&s_receiver);
    for (i = 0; i < QUEUED; ++i) {
        memset(body, (int)i, sizeof body);
        (void)mp_channel_send(&s_sender, body, sizeof body);
    }
    for (round = 0; round < 200 && delivered < QUEUED; ++round) {
        now += 16u;
        if (mp_channel_packet_build(&s_sender, now, NULL, 0u, packet, sizeof packet, &bytes)) {
            (void)mp_channel_packet_receive_at(&s_receiver, now, packet, bytes, &payload,
                                               &payload_bytes);
        }
        /* Read in every round, as the session does: it moves each message out of the channel
         * the moment the packet that carried it is taken. */
        while (mp_channel_message_read(&s_receiver, read, sizeof read, &got)) {
            if (got == BODY_BYTES && read[0] == (uint8_t)delivered) {
                ++delivered;
            }
        }
        if (mp_channel_packet_build(&s_receiver, now, NULL, 0u, packet, sizeof packet, &bytes)) {
            (void)mp_channel_packet_receive_at(&s_sender, now, packet, bytes, &payload,
                                               &payload_bytes);
        }
    }
    ut_checkf(delivered == QUEUED, "all %u messages were read in the order they were sent",
              (unsigned)delivered);
    ut_check(mp_channel_send_pending(&s_sender) == 0u, "and the sender holds none of them");
}

/* A message that goes out in every packet and is never answered, and a message that never gets a
 * seat, share one clock: the wait, measured from the first build that could have carried the
 * message. A long wait with no seat lost to the payload is the first kind, and the warning must
 * not name the payload as its reason.
 *
 * Built with the capacity the session really passes, which is the whole packet budget, and with a
 * payload of nought: nothing here is kept out by a payload, so a wait that appears is the other
 * kind by construction. */
static void check_the_two_kinds_of_wait(void)
{
    uint8_t  body[BODY_BYTES];
    uint8_t  packet[MP_SESSION_CHANNEL_CAP];
    size_t   bytes = 0;
    uint32_t now   = 1000u;
    unsigned i;

    ut_section("a message out and unanswered is not a seat the payload took");
    mp_channel_init(&s_sender);
    memset(body, 7, sizeof body);
    body[0] = 0x96u;   /* a tag, which is what the warning names */
    ut_check(mp_channel_send(&s_sender, body, sizeof body), "one message is queued");

    /* Twenty builds over two seconds, every one of them carrying it, none of them answered. */
    for (i = 0; i < 20u; ++i) {
        now += 100u;
        (void)mp_channel_packet_build(&s_sender, now, NULL, 0u, packet, sizeof packet, &bytes);
    }

    ut_check(mp_channel_oldest_unacked_ms(&s_sender) >= 1900u,
             "the oldest message has been out for the whole stretch, and says so");
    ut_check(mp_channel_oldest_tag(&s_sender) == 0x96u,
             "and it names its tag, so the warning can say which message it is");
    ut_check(mp_channel_oldest_bytes(&s_sender) == (uint16_t)sizeof body,
             "and its size, which is the other half of the question");
    ut_check(mp_channel_seats_lost_to_payload(&s_sender) == 0u,
             "no payload was in the way: not one seat was lost to one");
    ut_check(mp_channel_least_room_left(&s_sender) == 0u,
             "and no packet carried a payload at all, so there is no floor to report");
}

/* A send slot is taken again after sixty four messages, and it has to forget when its last occupant
 * first went out: otherwise the age of the oldest unanswered message is the age of the slot, the
 * warning picks its sentence by that age, and the report prints it. */
static void check_a_reused_slot_forgets_its_last_message(void)
{
    uint8_t        body[BODY_BYTES];
    uint8_t        packet[MP_SESSION_CHANNEL_CAP];
    uint8_t        read[MP_CHANNEL_MESSAGE_BYTES];
    size_t         bytes = 0;
    size_t         got = 0;
    const uint8_t *payload = NULL;
    size_t         payload_bytes = 0;
    uint32_t       now = 0;
    unsigned       sent = 0;
    unsigned       taken = 0;
    int            round;

    ut_section("a send slot taken again forgets when its last message first went out");
    mp_channel_init(&s_sender);
    mp_channel_init(&s_receiver);
    memset(body, 0x33, sizeof body);
    for (round = 0; round < 400 && sent < 70u; ++round) {
        now += 31u;
        if (mp_channel_send_pending(&s_sender) < 8u &&
            mp_channel_send(&s_sender, body, sizeof body)) {
            ++sent;
        }
        if (mp_channel_packet_build(&s_sender, now, NULL, 0u, packet, sizeof packet, &bytes)) {
            (void)mp_channel_packet_receive_at(&s_receiver, now, packet, bytes, &payload,
                                               &payload_bytes);
        }
        while (mp_channel_message_read(&s_receiver, read, sizeof read, &got)) {
            ++taken;
        }
        if (mp_channel_packet_build(&s_receiver, now, NULL, 0u, packet, sizeof packet, &bytes)) {
            (void)mp_channel_packet_receive_at(&s_sender, now, packet, bytes, &payload,
                                               &payload_bytes);
        }
    }
    ut_checkf(sent == 70u && taken == 70u && mp_channel_send_pending(&s_sender) == 0u,
              "%u messages went through, %u were read and every one was answered", sent, taken);

    now += 5000u;
    ut_check(mp_channel_send(&s_sender, body, sizeof body), "one more is queued, into a used slot");
    now += 31u;
    (void)mp_channel_packet_build(&s_sender, now, NULL, 0u, packet, sizeof packet, &bytes);
    now += 31u;
    (void)mp_channel_packet_build(&s_sender, now, NULL, 0u, packet, sizeof packet, &bytes);
    ut_checkf(mp_channel_oldest_unacked_ms(&s_sender) == 31u,
              "its oldest unanswered message has been out %u ms, one build interval",
              (unsigned)mp_channel_oldest_unacked_ms(&s_sender));
}

/* A receiver that stores and never reads, the case the session's inbox exists for, taken to the
 * end: the sender's oldest unacknowledged id moves past the receiver's window and the next packet
 * is refused. It is not acknowledged, so the sender keeps its messages, and its payload is handed
 * out with the packet's own sequence. The older receive form answers false with no payload. */
static void check_a_refused_packet_hands_on_its_payload(void)
{
    uint8_t              body[BODY_BYTES];
    uint8_t              packet[MP_SESSION_CHANNEL_CAP];
    uint8_t              ack[MP_SESSION_CHANNEL_CAP];
    size_t               bytes = 0;
    size_t               ack_bytes = 0;
    const uint8_t       *payload = NULL;
    size_t               payload_bytes = 0;
    const uint8_t       *other = NULL;
    size_t               other_bytes = 0;
    uint16_t             sequence = 0;
    uint16_t             built = 0;
    uint32_t             now = 0;
    mp_channel_receipt_t receipt = MP_CHANNEL_DROPPED;
    unsigned             i;
    int                  round;

    ut_section("a packet refused for the window is not acknowledged, and its payload is handed on");
    mp_channel_init(&s_sender);
    mp_channel_init(&s_receiver);
    memset(body, 0x5A, sizeof body);
    for (i = 0; i < QUEUED; ++i) {
        (void)mp_channel_send(&s_sender, body, sizeof body);
    }
    for (round = 0; round < 50 && receipt != MP_CHANNEL_PAST_WINDOW; ++round) {
        now += 120u;
        built = s_sender.local_sequence;
        if (!mp_channel_packet_build(&s_sender, now, "pw", 2u, packet, sizeof packet, &bytes)) {
            break;
        }
        receipt = mp_channel_packet_take(&s_receiver, now, packet, bytes, &payload,
                                         &payload_bytes, &sequence);
        if (receipt == MP_CHANNEL_ACCEPTED &&
            mp_channel_packet_build(&s_receiver, now, NULL, 0u, ack, sizeof ack, &ack_bytes)) {
            (void)mp_channel_packet_receive_at(&s_sender, now, ack, ack_bytes, &other,
                                               &other_bytes);
        }
    }
    ut_check(receipt == MP_CHANNEL_PAST_WINDOW,
             "the sender's window moved past a receiver that stored and read nothing");
    ut_check(payload != NULL && payload_bytes == 2u && memcmp(payload, "pw", 2u) == 0,
             "the refused packet's payload is handed out");
    ut_check(sequence == built, "with the packet's own sequence");
    ut_check(mp_channel_refused_past_window(&s_receiver) == 1u, "and the refusal is counted");

    /* What the receiver says next acknowledges what it accepted and not the refused packet. */
    now += 31u;
    if (mp_channel_packet_build(&s_receiver, now, NULL, 0u, ack, sizeof ack, &ack_bytes)) {
        (void)mp_channel_packet_receive_at(&s_sender, now, ack, ack_bytes, &other, &other_bytes);
    }
    ut_checkf(mp_channel_send_pending(&s_sender) == 22u,
              "the sender still holds the %u messages the refused packet carried",
              (unsigned)mp_channel_send_pending(&s_sender));

    ut_check(!mp_channel_packet_receive_at(&s_receiver, now, packet, bytes, &other, &other_bytes) &&
                 other == NULL && other_bytes == 0u,
             "the older receive form answers false for the same packet and hands out nothing");
    ut_check(mp_channel_refused_past_window(&s_receiver) == 2u, "and counts that refusal too");
}

int main(void)
{
    check_a_burst_after_a_lost_packet();
    check_everything_still_arrives();
    check_the_two_kinds_of_wait();
    check_a_reused_slot_forgets_its_last_message();
    check_a_refused_packet_hands_on_its_payload();
    return ut_summary("mp_channel_window");
}
