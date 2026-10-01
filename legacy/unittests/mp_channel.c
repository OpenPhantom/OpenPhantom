/* mp_channel.c: the reliability layer, driven over its edges with the test playing the network.
 *
 * SIZE NOTE: over 600 lines, one check function per property of the channel, and the
 * two loss-pattern drivers are the long ones because each plays a whole network in a loop. The
 * seam, should it grow, is the wrap and id cases, which share nothing with the loss drivers.
 *
 * Nothing here opens a socket or reads a clock, so every loss pattern, reordering and window
 * boundary is reproducible to the byte. The properties worth a test are the refusals: a message
 * over the budget refused rather than fragmented, a message past the run ahead window refusing
 * its whole packet rather than vanishing from an acknowledged one, a duplicate dropped by its id
 * rather than delivered twice. Each is silent in review and a lost message in the field.
 */
#include "unittest.h"

#include "mp_channel.h"
/* For MP_SESSION_CHANNEL_CAP alone: the capacity the ONLY real caller hands the builder. A check
 * that builds into MP_CHANNEL_PACKET_BYTES, which no caller supplies, passes a message the channel
 * accepts and no real packet can carry, so the checks that matter here build into this one. Header
 * only; nothing here calls into the session. */
#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* File scope rather than stack: each channel is around 117 KB. */
static mp_channel_t chan_a;
static mp_channel_t chan_b;

/* The test writes protocol bytes itself wherever the claim is about the format, so the format is
 * pinned by the test rather than by the code under test agreeing with itself. */
static void put_u16_le(uint8_t *at, uint16_t value)
{
    at[0] = (uint8_t)(value & 0xFFu);
    at[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void put_u32_le(uint8_t *at, uint32_t value)
{
    at[0] = (uint8_t)(value & 0xFFu);
    at[1] = (uint8_t)((value >> 8) & 0xFFu);
    at[2] = (uint8_t)((value >> 16) & 0xFFu);
    at[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static size_t craft_bare(uint8_t *packet, uint16_t sequence, uint16_t ack, uint32_t ack_bits)
{
    put_u32_le(packet, MP_CHANNEL_PROTOCOL_ID);
    put_u16_le(packet + 4, sequence);
    put_u16_le(packet + 6, ack);
    put_u32_le(packet + 8, ack_bits);
    packet[12] = 0;
    return 13u;
}

static size_t craft_with_message(uint8_t *packet, uint16_t sequence, uint16_t id,
                                 const uint8_t *body, uint16_t body_bytes)
{
    size_t at = craft_bare(packet, sequence, 0xFFFFu, 0u);

    packet[12] = 1;
    put_u16_le(packet + at, id);
    put_u16_le(packet + at + 2, body_bytes);
    memcpy(packet + at + 4, body, body_bytes);
    return at + 4u + body_bytes;
}

static void check_sequence_compare(void)
{
    ut_section("sequence comparison at the wrap");

    ut_check(mp_channel_sequence_newer(1u, 0u), "one is newer than zero");
    ut_check(!mp_channel_sequence_newer(0u, 1u), "and zero is not newer than one");
    ut_check(mp_channel_sequence_newer(0u, 0xFFFFu),
             "zero is newer than 0xFFFF, because the sequence wraps rather than ends");
    ut_check(!mp_channel_sequence_newer(0xFFFFu, 0u), "and 0xFFFF is older than zero");
    ut_check(!mp_channel_sequence_newer(5u, 5u), "a sequence is not newer than itself");
    ut_check(mp_channel_sequence_newer(0x8000u, 0u) && !mp_channel_sequence_newer(0u, 0x8000u),
             "exactly half the range reads as newer in one direction only, peers cannot disagree");
    ut_check(!mp_channel_sequence_newer(0x8001u, 0u),
             "and one past half the range reads as older");
}

static void check_header_bytes(void)
{
    uint8_t packet[MP_CHANNEL_PACKET_BYTES];
    size_t  bytes = 0;

    ut_section("the header lands in the bytes the protocol states");

    mp_channel_init(&chan_a);
    ut_check(mp_channel_packet_build(&chan_a, 0u, NULL, 0u, packet, sizeof packet, &bytes),
             "an empty packet builds");
    ut_check(bytes == MP_CHANNEL_HEADER_BYTES, "and is exactly the thirteen header bytes");
    ut_check(packet[0] == (MP_CHANNEL_PROTOCOL_ID & 0xFFu) &&
             packet[1] == ((MP_CHANNEL_PROTOCOL_ID >> 8) & 0xFFu) &&
             packet[2] == ((MP_CHANNEL_PROTOCOL_ID >> 16) & 0xFFu) &&
             packet[3] == ((MP_CHANNEL_PROTOCOL_ID >> 24) & 0xFFu),
             "the protocol id opens the packet, least significant byte first, whatever the host");
    ut_check((MP_CHANNEL_PROTOCOL_ID & 0xFFu) == MP_CHANNEL_PROTOCOL_VERSION,
             "and its low byte is the protocol version, so builds that disagree cannot talk");
    ut_check(packet[4] == 0u && packet[5] == 0u, "the first sequence is zero");
    ut_check(packet[6] == 0xFFu && packet[7] == 0xFFu && packet[8] == 0u && packet[9] == 0u &&
             packet[10] == 0u && packet[11] == 0u,
             "before anything arrives the ack names a never sent sequence and retires nothing");
    ut_check(packet[12] == 0u, "and the message count is zero");

    ut_check(mp_channel_packet_build(&chan_a, 0u, NULL, 0u, packet, sizeof packet, &bytes) &&
             packet[4] == 1u && packet[5] == 0u,
             "the next packet takes the next sequence even though it carried nothing");
}

static void check_budget(void)
{
    static uint8_t big[MP_CHANNEL_MESSAGE_BYTES + 1u];
    static uint8_t pay[MP_CHANNEL_PAYLOAD_BYTES + 1u];
    uint8_t packet[MP_CHANNEL_PACKET_BYTES];
    uint8_t small[100];
    size_t  bytes = 0;

    /* The sizes here are measured against MP_CHANNEL_BUDGET_BYTES, which is what a carrier leaves
     * this module, and no longer against the 1200 byte packet: a check written against the packet
     * is a check no caller can ever fail, which is how the defect below lived in a green suite. */
    ut_section("the budget the carrier leaves, exactly at it and one past it");

    memset(big, 0x5A, sizeof big);
    memset(pay, 0xA5, sizeof pay);
    memset(small, 0x11, sizeof small);

    mp_channel_init(&chan_a);
    ut_check(mp_channel_send(&chan_a, big, MP_CHANNEL_MESSAGE_BYTES),
             "the largest message that fits one packet is accepted");
    ut_check(mp_channel_packet_build(&chan_a, 0u, NULL, 0u, packet, sizeof packet, &bytes),
             "and it builds");
    ut_check(bytes == MP_CHANNEL_BUDGET_BYTES,
             "landing the packet exactly on the budget the carrier leaves");

    mp_channel_init(&chan_a);
    ut_check(!mp_channel_send(&chan_a, big, sizeof big),
             "one byte more is refused at the door rather than fragmented");

    ut_section("the payload against the same budget");

    mp_channel_init(&chan_a);
    ut_check(mp_channel_packet_build(&chan_a, 0u, pay, MP_CHANNEL_PAYLOAD_BYTES, packet,
                                     sizeof packet, &bytes) &&
             bytes == MP_CHANNEL_BUDGET_BYTES,
             "the largest payload fills the packet to exactly the budget");
    ut_check(!mp_channel_packet_build(&chan_a, 0u, pay, sizeof pay, packet,
                                      sizeof packet, &bytes),
             "one byte more of payload is refused, never fragmented");
    ut_check(!mp_channel_packet_build(&chan_a, 0u, NULL, 0u, packet, 12u, &bytes),
             "and a buffer smaller than the header cannot take a packet at all");

    ut_section("a full payload never crowds out a message and never truncates it");

    mp_channel_init(&chan_a);
    ut_check(mp_channel_send(&chan_a, small, sizeof small), "a message is queued");
    ut_check(mp_channel_packet_build(&chan_a, 0u, pay, MP_CHANNEL_PAYLOAD_BYTES, packet,
                                     sizeof packet, &bytes),
             "a packet with a payload that leaves it no room still builds");
    ut_check(packet[12] == 0u, "carrying the payload and not the message");
    ut_check(mp_channel_send_pending(&chan_a) == 1u, "which stays queued in full");
    ut_check(mp_channel_packet_build(&chan_a, 0u, NULL, 0u, packet, sizeof packet, &bytes) &&
             packet[12] == 1u,
             "and rides the next packet whole instead of a fragment of the crowded one");
}

/* The check that was missing, and its absence cost a field run and three wrong repairs.
 *
 * Every check above builds into MP_CHANNEL_PACKET_BYTES. No caller ever does: mp_session writes
 * its own thirteen byte envelope first and hands the builder MP_SESSION_CHANNEL_CAP. A message
 * that only fits when the whole budget is available is therefore accepted by mp_channel_send,
 * refused a seat by every packet the carrier ever builds, and stays the OLDEST message in the
 * queue for ever, and because the channel is ordered, everything behind it stops too. That is
 * not a slow transfer, it is a dead channel in both directions, and no counter said so.
 *
 * So the property is: whatever mp_channel_send accepts, the carrier's own capacity can carry.
 * It is checked against the carrier's number, never against the budget. */
static void check_the_capacity_the_carrier_really_gives(void)
{
    static uint8_t big[MP_CHANNEL_MESSAGE_BYTES];
    uint8_t packet[MP_SESSION_CHANNEL_CAP];
    size_t  bytes = 0;

    ut_section("what the channel accepts, the carrier's own capacity can seat");

    memset(big, 0x5A, sizeof big);

    ut_check(MP_CHANNEL_ENVELOPE_BYTES == MP_SESSION_PAYLOAD_HEADER,
             "the envelope the channel budgets for is the one the session actually writes");

    mp_channel_init(&chan_a);
    ut_check(mp_channel_send(&chan_a, big, MP_CHANNEL_MESSAGE_BYTES),
             "the largest message the channel accepts is queued");
    ut_check(mp_channel_packet_build(&chan_a, 0u, NULL, 0u, packet, sizeof packet, &bytes),
             "and a packet built into the carrier's capacity builds");
    ut_checkf(packet[12] == 1u,
              "AND CARRIES IT: a message the channel accepts but the carrier can never seat stops "
              "the ordered channel behind it for ever (message count %u, not 1)",
              (unsigned)packet[12]);
    ut_checkf(bytes <= MP_SESSION_CHANNEL_CAP,
              "landing inside the carrier's capacity, %u of %u",
              (unsigned)bytes, (unsigned)MP_SESSION_CHANNEL_CAP);
    ut_check(mp_channel_send_pending(&chan_a) == 1u,
             "and it is still held until an acknowledgement retires it, not before");

    ut_section("and the whole wrapped packet stays inside the 1200 byte budget");

    ut_checkf(MP_SESSION_PAYLOAD_HEADER + bytes <= MP_CHANNEL_PACKET_BYTES,
              "envelope %u plus packet %u is %u, past the %u byte budget",
              (unsigned)MP_SESSION_PAYLOAD_HEADER, (unsigned)bytes,
              (unsigned)(MP_SESSION_PAYLOAD_HEADER + bytes),
              (unsigned)MP_CHANNEL_PACKET_BYTES);
    ut_check(MP_SESSION_PAYLOAD_BYTES <= MP_CHANNEL_PAYLOAD_BYTES,
             "and the payload the session offers is one the builder will take");
}

/* The skip is the design and the silence was the defect. A message that does not fit beside the
 * payload is passed over and rides a later packet; that is right, and it is also how a savegame
 * transfer died in a level without one counter moving, because "later" never came while the
 * payload stayed full. So the property here is not that the message is seated (it must not be)
 * but that the channel SAYS what happened: how many seats the payload took from a due message,
 * and how long the oldest such message has been waiting. Both are built into the carrier's real
 * capacity, because the arithmetic that decides whether a message fits lives in that number. */
static void check_a_seat_lost_to_the_payload(void)
{
    static uint8_t big[800];
    static uint8_t pay[600];
    uint8_t packet[MP_SESSION_CHANNEL_CAP];
    size_t  bytes = 0;

    ut_section("a due message the payload alone keeps out is counted and stays queued");

    memset(big, 0x3C, sizeof big);
    memset(pay, 0xC3, sizeof pay);

    mp_channel_init(&chan_a);
    ut_check(mp_channel_send(&chan_a, big, sizeof big), "an 800 byte message is queued");
    ut_check(mp_channel_packet_build(&chan_a, 0u, pay, sizeof pay, packet, sizeof packet, &bytes),
             "a packet with a 600 byte payload builds");
    ut_check(packet[12] == 0u,
             "and carries the payload but not the message: 804 into 574 is no fit");
    ut_checkf(mp_channel_seats_lost_to_payload(&chan_a) == 1u,
              "the channel says so: 1 seat lost to the payload, not %u",
              (unsigned)mp_channel_seats_lost_to_payload(&chan_a));
    ut_check(mp_channel_send_pending(&chan_a) == 1u, "while the message is still in the queue");

    ut_check(mp_channel_packet_build(&chan_a, 40u, NULL, 0u, packet, sizeof packet, &bytes) &&
             packet[12] == 1u,
             "the same packet without a payload seats it");
    ut_checkf(mp_channel_seats_lost_to_payload(&chan_a) == 1u,
              "and the count stays at 1, not %u: a seat that was given is not a seat lost",
              (unsigned)mp_channel_seats_lost_to_payload(&chan_a));
    ut_checkf(mp_channel_seat_wait_ms(&chan_a) == 0u,
              "with nothing left waiting for a seat after that build (%u ms)",
              (unsigned)mp_channel_seat_wait_ms(&chan_a));

    ut_section("and how long the oldest such message has waited is measured, from the first offer");

    mp_channel_init(&chan_a);
    (void)mp_channel_send(&chan_a, big, sizeof big);
    (void)mp_channel_packet_build(&chan_a, 100u, pay, sizeof pay, packet, sizeof packet, &bytes);
    ut_checkf(mp_channel_seat_wait_ms(&chan_a) == 0u,
              "the first build that refuses it starts the clock at nought, reads %u ms",
              (unsigned)mp_channel_seat_wait_ms(&chan_a));
    (void)mp_channel_packet_build(&chan_a, 1300u, pay, sizeof pay, packet, sizeof packet, &bytes);
    ut_checkf(mp_channel_seat_wait_ms(&chan_a) == 1200u,
              "a build 1200 ms later that refuses it again reads a wait of 1200 ms, not %u",
              (unsigned)mp_channel_seat_wait_ms(&chan_a));
    ut_checkf(mp_channel_seats_lost_to_payload(&chan_a) == 2u,
              "and every refusal counted, 2 not %u: a message skipped by thirty "
              "packets counts thirty",
              (unsigned)mp_channel_seats_lost_to_payload(&chan_a));
    (void)mp_channel_packet_build(&chan_a, 1340u, NULL, 0u, packet, sizeof packet, &bytes);
    ut_checkf(mp_channel_seat_wait_ms(&chan_a) == 0u,
              "seated at last, the current wait falls back to nought (%u ms)",
              (unsigned)mp_channel_seat_wait_ms(&chan_a));
    ut_checkf(mp_channel_longest_seat_wait_ms(&chan_a) == 1200u,
              "and the longest wait ever, 1200 ms, is kept for the report (%u ms)",
              (unsigned)mp_channel_longest_seat_wait_ms(&chan_a));

    ut_section("the room another rider took is not the payload's fault");

    mp_channel_init(&chan_a);
    (void)mp_channel_send(&chan_a, big, sizeof big);
    (void)mp_channel_send(&chan_a, big, sizeof big);
    ut_check(mp_channel_packet_build(&chan_a, 0u, NULL, 0u, packet, sizeof packet, &bytes) &&
             packet[12] == 1u,
             "two 800 byte messages and no payload: the packet carries the first alone");
    ut_checkf(mp_channel_seats_lost_to_payload(&chan_a) == 0u,
              "and nothing is charged to a payload that was not there (%u)",
              (unsigned)mp_channel_seats_lost_to_payload(&chan_a));
    ut_checkf(mp_channel_seat_wait_ms(&chan_a) == 0u,
              "while the second, refused for the first's sake, still starts its own clock at %u ms",
              (unsigned)mp_channel_seat_wait_ms(&chan_a));
}

static void check_resend_throttle(void)
{
    uint8_t packet[MP_CHANNEL_PACKET_BYTES];
    uint8_t message[MP_CHANNEL_EAGER_BYTES + 8u];
    size_t  bytes = 0;

    ut_section("re-entry before and after the throttle, for a message above the eager size");

    memset(message, 0x42, sizeof message);
    mp_channel_init(&chan_a);
    (void)mp_channel_send(&chan_a, message, sizeof message);
    ut_check(mp_channel_packet_build(&chan_a, 0u, NULL, 0u, packet, sizeof packet, &bytes) &&
             packet[12] == 1u,
             "a message never sent is laid into the first packet regardless of time");
    ut_check(mp_channel_packet_build(&chan_a, 99u, NULL, 0u, packet, sizeof packet, &bytes) &&
             packet[12] == 0u,
             "one millisecond before the throttle it is not laid in again");
    ut_check(mp_channel_packet_build(&chan_a, 100u, NULL, 0u, packet, sizeof packet, &bytes) &&
             packet[12] == 1u,
             "at the throttle boundary it rides again, and will keep riding until acknowledged");

    ut_section("a message at the eager size rides every packet until acknowledged");

    mp_channel_init(&chan_a);
    (void)mp_channel_send(&chan_a, message, MP_CHANNEL_EAGER_BYTES);
    ut_check(mp_channel_packet_build(&chan_a, 0u, NULL, 0u, packet, sizeof packet, &bytes) &&
             packet[12] == 1u,
             "the first packet carries it");
    ut_check(mp_channel_packet_build(&chan_a, 1u, NULL, 0u, packet, sizeof packet, &bytes) &&
             packet[12] == 1u,
             "and so does the next one, a millisecond later, with no throttle in between");
    ut_check(mp_channel_packet_build(&chan_a, 2u, NULL, 0u, packet, sizeof packet, &bytes) &&
             packet[12] == 1u,
             "and the one after that");
    ut_check(mp_channel_send_pending(&chan_a) == 1u,
             "while it stays one pending message however many packets carried it");
}

/* A shot is twenty-two bytes on the reliable channel. Under a fifth of the packets lost it has to
 * reach the far side on the packet after the lost one, not a throttle and a round trip later, and
 * the copies it rides in must not pile up in the send slots: every acknowledgement of any packet
 * that carried it retires it. The loss is deterministic, every fifth packet, so the worst case
 * (the first copy on a lost packet) is reached rather than hoped for. */
static void check_eager_delivery_under_loss(void)
{
    uint8_t  packet[MP_CHANNEL_PACKET_BYTES];
    uint8_t  reply[64];
    uint8_t  message[22];
    uint8_t  read_back[64];
    const uint8_t *payload;
    size_t   payload_bytes;
    size_t   packet_bytes;
    size_t   read_bytes;
    size_t   index;
    size_t   queued = 0;
    size_t   delivered = 0;
    size_t   queued_at[64];
    size_t   worst_wait = 0;
    size_t   worst_pending = 0;
    uint32_t now;

    ut_section("a 22 byte event crosses 20 percent loss within three packets and fills no slot");

    mp_channel_init(&chan_a);
    mp_channel_init(&chan_b);
    memset(message, 0xEE, sizeof message);

    for (index = 0; index < 240u; ++index) {
        size_t pending;

        now = (uint32_t)(index * 31u);
        if ((index % 8u) == 0u && queued < 30u) {
            message[0] = (uint8_t)queued;
            queued_at[queued] = index;
            ut_checkf(mp_channel_send(&chan_a, message, sizeof message),
                      "event %u is accepted by the sender", (unsigned)queued);
            ++queued;
        }
        if (mp_channel_packet_build(&chan_a, now, NULL, 0u, packet, sizeof packet,
                                    &packet_bytes) &&
            (index % 5u) != 0u) {
            (void)mp_channel_packet_receive(&chan_b, packet, packet_bytes, &payload,
                                            &payload_bytes);
        }
        while (mp_channel_message_read(&chan_b, read_back, sizeof read_back, &read_bytes)) {
            size_t wait = index - queued_at[read_back[0]];

            if (read_bytes != sizeof message || read_back[0] != (uint8_t)delivered) {
                ut_check(0, "an event arrived out of order or torn");
            }
            if (wait > worst_wait) {
                worst_wait = wait;
            }
            ++delivered;
        }
        if (mp_channel_packet_build(&chan_b, now, NULL, 0u, reply, sizeof reply, &packet_bytes)) {
            (void)mp_channel_packet_receive(&chan_a, reply, packet_bytes, &payload, &payload_bytes);
        }
        pending = mp_channel_send_pending(&chan_a);
        if (pending > worst_pending) {
            worst_pending = pending;
        }
    }
    ut_check(delivered == 30u, "all thirty events arrive");
    ut_checkf(worst_wait <= 2u, "the slowest took %u packets from queueing, inside three",
              (unsigned)(worst_wait + 1u));
    ut_checkf(worst_pending <= 2u, "at most %u events were ever pending at once, the slots "
                                   "never fill", (unsigned)worst_pending);
    ut_check(mp_channel_send_pending(&chan_a) == 0u, "and the sender's queue drains to empty");
}

static void check_ack_window_edges(void)
{
    uint8_t packet[64];
    const uint8_t *payload = NULL;
    size_t  payload_bytes = 0;
    size_t  bytes;

    ut_section("the receive window at the edges of the 32 bit ack field");

    mp_channel_init(&chan_b);
    bytes = craft_bare(packet, 100u, 0xFFFFu, 0u);
    ut_check(mp_channel_packet_receive(&chan_b, packet, bytes, &payload, &payload_bytes),
             "a first packet is accepted at any sequence");
    ut_check(chan_b.remote_sequence == 100u && chan_b.remote_ack_bits == 0u,
             "and becomes the ack with no bits behind it");

    bytes = craft_bare(packet, 132u, 0xFFFFu, 0u);
    ut_check(mp_channel_packet_receive(&chan_b, packet, bytes, &payload, &payload_bytes),
             "a jump of exactly 32 is accepted");
    ut_check(chan_b.remote_ack_bits == 0x80000000u,
             "and the packet 32 back sits on the last bit the field has");

    bytes = craft_bare(packet, 99u, 0xFFFFu, 0u);
    ut_check(!mp_channel_packet_receive(&chan_b, packet, bytes, &payload, &payload_bytes),
             "a packet 33 back is past what the ack field can say and is refused");

    bytes = craft_bare(packet, 131u, 0xFFFFu, 0u);
    ut_check(mp_channel_packet_receive(&chan_b, packet, bytes, &payload, &payload_bytes) &&
             chan_b.remote_ack_bits == 0x80000001u,
             "a late packet one back fills the first bit while the last stays set");
    ut_check(!mp_channel_packet_receive(&chan_b, packet, bytes, &payload, &payload_bytes),
             "the same packet again is recognised by its bit and refused");

    bytes = craft_bare(packet, 100u, 0xFFFFu, 0u);
    ut_check(!mp_channel_packet_receive(&chan_b, packet, bytes, &payload, &payload_bytes),
             "and so is the packet the last bit names");
}

static void check_retirement(void)
{
    uint8_t packet[MP_CHANNEL_PACKET_BYTES];
    uint8_t ack_packet[64];
    uint8_t message[1] = { 0xABu };
    const uint8_t *payload = NULL;
    size_t  payload_bytes = 0;
    size_t  bytes = 0;
    size_t  ack_bytes;

    ut_section("a message is retired by ANY packet that carried it being acked");

    mp_channel_init(&chan_a);
    (void)mp_channel_send(&chan_a, message, 1u);
    (void)mp_channel_packet_build(&chan_a, 0u, NULL, 0u, packet, sizeof packet, &bytes);
    (void)mp_channel_packet_build(&chan_a, 100u, NULL, 0u, packet, sizeof packet, &bytes);
    ut_check(mp_channel_send_pending(&chan_a) == 1u,
             "the message has ridden two packets and is still one pending message, not two");

    ack_bytes = craft_bare(ack_packet, 7u, 0u, 0u);
    ut_check(mp_channel_packet_receive(&chan_a, ack_packet, ack_bytes, &payload, &payload_bytes),
             "an ack for the first of the two packets arrives");
    ut_check(mp_channel_send_pending(&chan_a) == 0u,
             "and retires the message although its second copy is still in flight");

    ack_bytes = craft_bare(ack_packet, 8u, 1u, 0u);
    ut_check(mp_channel_packet_receive(&chan_a, ack_packet, ack_bytes, &payload, &payload_bytes) &&
             mp_channel_send_pending(&chan_a) == 0u,
             "the ack for the second copy finds nothing left and does nothing");

    ut_section("the last ack bit retires, and an unsent sequence retires nothing");

    mp_channel_init(&chan_a);
    (void)mp_channel_send(&chan_a, message, 1u);
    (void)mp_channel_packet_build(&chan_a, 0u, NULL, 0u, packet, sizeof packet, &bytes);
    ack_bytes = craft_bare(ack_packet, 9u, 5u, 0u);
    (void)mp_channel_packet_receive(&chan_a, ack_packet, ack_bytes, &payload, &payload_bytes);
    ut_check(mp_channel_send_pending(&chan_a) == 1u,
             "an ack naming a sequence that was never sent retires nothing");

    ack_bytes = craft_bare(ack_packet, 10u, 32u, 0x80000000u);
    (void)mp_channel_packet_receive(&chan_a, ack_packet, ack_bytes, &payload, &payload_bytes);
    ut_check(mp_channel_send_pending(&chan_a) == 0u,
             "an ack carried on the last bit of the field, 32 packets back, still retires");
}

/* Two channels, a fixed loss pattern on the forward path, acks flowing back untouched. The 35 ms
 * step makes the re-entry throttle three packets long, an odd number on purpose: an even one
 * would land every retry on the same parity forever and the every second packet pattern would
 * drop every single retry. A synthetic pattern must not resonate with the throttle. */
static void run_loss_pattern(const bool *drop, size_t period, size_t *delivered_out,
                             bool *in_order_out, size_t *pending_out)
{
    uint8_t  packet[MP_CHANNEL_PACKET_BYTES];
    uint8_t  reply[64];
    uint8_t  message[1];
    const uint8_t *payload;
    size_t   payload_bytes;
    size_t   packet_bytes;
    size_t   message_bytes;
    size_t   delivered = 0;
    size_t   queued = 0;
    size_t   index;
    uint32_t now;
    bool     in_order = true;

    mp_channel_init(&chan_a);
    mp_channel_init(&chan_b);

    for (index = 0; index < 160u; ++index) {
        now = (uint32_t)(index * 35u);
        if ((index % 4u) == 0u && queued < 8u) {
            message[0] = (uint8_t)queued;
            if (mp_channel_send(&chan_a, message, 1u)) {
                ++queued;
            }
        }
        if (mp_channel_packet_build(&chan_a, now, NULL, 0u, packet, sizeof packet,
                                    &packet_bytes) &&
            !drop[index % period]) {
            (void)mp_channel_packet_receive(&chan_b, packet, packet_bytes, &payload,
                                            &payload_bytes);
        }
        while (mp_channel_message_read(&chan_b, message, sizeof message, &message_bytes)) {
            if (message[0] != (uint8_t)delivered) {
                in_order = false;
            }
            ++delivered;
        }
        if (mp_channel_packet_build(&chan_b, now, NULL, 0u, reply, sizeof reply, &packet_bytes)) {
            (void)mp_channel_packet_receive(&chan_a, reply, packet_bytes, &payload, &payload_bytes);
        }
    }
    *delivered_out = delivered;
    *in_order_out  = in_order;
    *pending_out   = mp_channel_send_pending(&chan_a);
}

static void check_delivery_over_loss(void)
{
    static const bool EVERY_SECOND[2]   = { true, false };
    static const bool THREE_IN_A_ROW[4] = { true, true, true, false };
    size_t delivered = 0;
    size_t pending = 0;
    bool   in_order = false;

    ut_section("delivery through losing every second packet");

    run_loss_pattern(EVERY_SECOND, 2u, &delivered, &in_order, &pending);
    ut_check(delivered == 8u, "all eight messages arrive although half the packets never do");
    ut_check(in_order, "in the order they were sent, each exactly once");
    ut_check(pending == 0u, "and every one is retired at the sender by a returning ack");

    ut_section("delivery through losing three packets in a row");

    run_loss_pattern(THREE_IN_A_ROW, 4u, &delivered, &in_order, &pending);
    ut_check(delivered == 8u,
             "all eight arrive through repeating triple losses, riding every later packet");
    ut_check(in_order, "still in order, each exactly once");
    ut_check(pending == 0u, "and the sender's queue drains to empty");
}

/* The two messages are above the eager size on purpose: an eager message rides the second packet
 * as well, so the later packet alone would already deliver both in order and the claim about the
 * ordering window would be proven by nothing. */
static void check_reordering(void)
{
    uint8_t first[MP_CHANNEL_PACKET_BYTES];
    uint8_t second[MP_CHANNEL_PACKET_BYTES];
    uint8_t large[MP_CHANNEL_EAGER_BYTES + 8u];
    uint8_t message[4];
    const uint8_t *payload = NULL;
    size_t  payload_bytes = 0;
    size_t  first_bytes = 0;
    size_t  second_bytes = 0;
    size_t  message_bytes = 0;

    ut_section("two packets swapped in transit still deliver in order");

    mp_channel_init(&chan_a);
    mp_channel_init(&chan_b);
    memset(large, 0, sizeof large);
    large[0] = 10u;
    (void)mp_channel_send(&chan_a, large, sizeof large);
    (void)mp_channel_packet_build(&chan_a, 0u, NULL, 0u, first, sizeof first, &first_bytes);
    large[0] = 11u;
    (void)mp_channel_send(&chan_a, large, sizeof large);
    (void)mp_channel_packet_build(&chan_a, 1u, NULL, 0u, second, sizeof second, &second_bytes);

    ut_check(mp_channel_packet_receive(&chan_b, second, second_bytes, &payload, &payload_bytes),
             "the later packet arrives first and is accepted");
    ut_check(!mp_channel_message_read(&chan_b, large, sizeof large, &message_bytes),
             "but its message is held back while the earlier one is missing");
    ut_check(mp_channel_packet_receive(&chan_b, first, first_bytes, &payload, &payload_bytes),
             "the earlier packet arrives late and is accepted");
    ut_check(mp_channel_message_read(&chan_b, large, sizeof large, &message_bytes) &&
             large[0] == 10u,
             "and the messages come out in the order they were sent");
    ut_check(mp_channel_message_read(&chan_b, large, sizeof large, &message_bytes) &&
             large[0] == 11u,
             "not the order they arrived");

    ut_section("a buffer too small refuses and keeps the message");

    mp_channel_init(&chan_a);
    mp_channel_init(&chan_b);
    (void)mp_channel_send(&chan_a, message, 4u);
    (void)mp_channel_packet_build(&chan_a, 0u, NULL, 0u, first, sizeof first, &first_bytes);
    (void)mp_channel_packet_receive(&chan_b, first, first_bytes, &payload, &payload_bytes);
    ut_check(!mp_channel_message_read(&chan_b, message, 3u, &message_bytes),
             "a four byte message is refused into a three byte buffer");
    ut_check(mp_channel_message_read(&chan_b, message, 4u, &message_bytes) &&
             message_bytes == 4u,
             "and handed out whole to a big enough one, nothing truncated in between");
}

static void check_order_window_refusal(void)
{
    uint8_t packet[64];
    uint8_t body[1] = { 0x77u };
    uint8_t message[4];
    const uint8_t *payload = NULL;
    size_t  payload_bytes = 0;
    size_t  message_bytes = 0;
    size_t  bytes;

    ut_section("a message past the run ahead window refuses its packet, loudly");

    mp_channel_init(&chan_b);
    bytes = craft_with_message(packet, 0u, MP_CHANNEL_ORDER_SLOTS, body, 1u);
    ut_check(!mp_channel_packet_receive(&chan_b, packet, bytes, &payload, &payload_bytes),
             "one past the window refuses the whole packet, a silent drop would lose it forever");
    ut_check(!chan_b.remote_seen,
             "the refused packet is not acked, so the sender lays its messages into fresh packets");

    bytes = craft_with_message(packet, 1u, 0u, body, 1u);
    ut_check(mp_channel_packet_receive(&chan_b, packet, bytes, &payload, &payload_bytes),
             "traffic inside the window is untouched by the refusal");
    ut_check(mp_channel_message_read(&chan_b, message, sizeof message, &message_bytes),
             "and delivers");

    bytes = craft_with_message(packet, 2u, MP_CHANNEL_ORDER_SLOTS, body, 1u);
    ut_check(mp_channel_packet_receive(&chan_b, packet, bytes, &payload, &payload_bytes),
             "once the window has moved the same message arrives in a later packet, nothing lost");
}

static void check_sequence_wrap_end_to_end(void)
{
    uint8_t  packet[MP_CHANNEL_PACKET_BYTES];
    uint8_t  reply[64];
    uint8_t  message[1] = { 77u };
    const uint8_t *payload = NULL;
    size_t   payload_bytes = 0;
    size_t   packet_bytes = 0;
    size_t   message_bytes = 0;
    size_t   accepted = 0;
    size_t   index;
    uint32_t now;

    ut_section("the packet sequence crosses 0xFFFF to 0 in live traffic");

    mp_channel_init(&chan_a);
    mp_channel_init(&chan_b);
    /* The counter is set rather than driven there by 65533 builds; what is under test is the
     * window arithmetic at the wrap, not the incrementing. */
    chan_a.local_sequence = 0xFFFDu;

    for (index = 0; index < 8u; ++index) {
        now = (uint32_t)(index * 35u);
        if (index == 1u) {
            (void)mp_channel_send(&chan_a, message, 1u);
        }
        (void)mp_channel_packet_build(&chan_a, now, NULL, 0u, packet, sizeof packet,
                                      &packet_bytes);
        if (mp_channel_packet_receive(&chan_b, packet, packet_bytes, &payload, &payload_bytes)) {
            ++accepted;
        }
        (void)mp_channel_packet_build(&chan_b, now, NULL, 0u, reply, sizeof reply,
                                      &packet_bytes);
        (void)mp_channel_packet_receive(&chan_a, reply, packet_bytes, &payload, &payload_bytes);
    }

    ut_check(accepted == 8u,
             "every packet across the wrap is accepted, none is mistaken for a stale one");
    ut_check(chan_b.remote_sequence == 4u,
             "and the receive window followed the sequence through 0xFFFF to the low numbers");
    ut_check(mp_channel_message_read(&chan_b, message, sizeof message, &message_bytes) &&
             message[0] == 77u,
             "a message sent just before the wrap is delivered");
    ut_check(mp_channel_send_pending(&chan_a) == 0u,
             "and retired by an ack whose sequence lies on the far side of the wrap");
}

static void check_message_id_wrap(void)
{
    uint8_t  packet[MP_CHANNEL_PACKET_BYTES];
    uint8_t  reply[64];
    uint8_t  message[1];
    const uint8_t *payload = NULL;
    size_t   payload_bytes = 0;
    size_t   packet_bytes = 0;
    size_t   message_bytes = 0;
    size_t   read_count = 0;
    size_t   index;
    bool     in_order = true;

    ut_section("message ids cross 0xFFFF to 0 without breaking the order");

    mp_channel_init(&chan_a);
    mp_channel_init(&chan_b);
    chan_a.next_message_id     = 0xFFFEu;
    chan_b.expected_message_id = 0xFFFEu;

    for (index = 0; index < 4u; ++index) {
        message[0] = (uint8_t)(20u + index);
        (void)mp_channel_send(&chan_a, message, 1u);
    }
    for (index = 0; index < 4u; ++index) {
        (void)mp_channel_packet_build(&chan_a, 0u, NULL, 0u, packet, sizeof packet, &packet_bytes);
        (void)mp_channel_packet_receive(&chan_b, packet, packet_bytes, &payload, &payload_bytes);
        (void)mp_channel_packet_build(&chan_b, 0u, NULL, 0u, reply, sizeof reply, &packet_bytes);
        (void)mp_channel_packet_receive(&chan_a, reply, packet_bytes, &payload, &payload_bytes);
    }
    while (mp_channel_message_read(&chan_b, message, sizeof message, &message_bytes)) {
        if (message[0] != (uint8_t)(20u + read_count)) {
            in_order = false;
        }
        ++read_count;
    }

    ut_check(read_count == 4u, "all four messages spanning the id wrap are delivered");
    ut_check(in_order, "in sending order, with ids 0xFFFE, 0xFFFF, 0 and 1");
    ut_check(mp_channel_send_pending(&chan_a) == 0u, "and all four are retired at the sender");
}

static void check_foreign_and_malformed(void)
{
    static uint8_t oversize[MP_CHANNEL_PACKET_BYTES + 1u];
    uint8_t packet[64];
    const uint8_t *payload = NULL;
    size_t  payload_bytes = 0;
    size_t  bytes;

    ut_section("foreign and malformed packets are refused before anything is read into");

    mp_channel_init(&chan_b);

    bytes = craft_bare(packet, 0u, 0xFFFFu, 0u);
    packet[0] ^= 0xFFu;
    ut_check(!mp_channel_packet_receive(&chan_b, packet, bytes, &payload, &payload_bytes),
             "a packet whose first four bytes are not the protocol id is refused unread");

    bytes = craft_bare(packet, 0u, 0xFFFFu, 0u);
    ut_check(!mp_channel_packet_receive(&chan_b, packet, bytes - 1u, &payload, &payload_bytes),
             "a packet shorter than the header is refused");

    bytes = craft_bare(packet, 0u, 0xFFFFu, 0u);
    packet[12] = 1u;
    ut_check(!mp_channel_packet_receive(&chan_b, packet, bytes, &payload, &payload_bytes),
             "a message count that promises bytes the packet does not have is refused");

    bytes = craft_bare(packet, 0u, 0xFFFFu, 0u);
    packet[12] = 1u;
    put_u16_le(packet + 13, 0u);
    put_u16_le(packet + 15, 200u);
    ut_check(!mp_channel_packet_receive(&chan_b, packet, bytes + 4u + 10u, &payload,
                                        &payload_bytes),
             "a message size running past the end of the packet is refused");

    (void)craft_bare(oversize, 0u, 0xFFFFu, 0u);
    ut_check(!mp_channel_packet_receive(&chan_b, oversize, sizeof oversize, &payload,
                                        &payload_bytes),
             "a packet longer than the budget is not this protocol, whatever its id says");

    ut_check(!chan_b.remote_seen,
             "and none of the refused packets left a mark on the receive window");

    ut_check(!mp_channel_packet_receive(&chan_b, NULL, 13u, &payload, &payload_bytes),
             "a packet that is not there is refused");
}

static void check_payload_passthrough(void)
{
    uint8_t packet[MP_CHANNEL_PACKET_BYTES];
    uint8_t body[5] = { 1u, 2u, 3u, 4u, 5u };
    uint8_t message[8];
    const uint8_t *payload = NULL;
    size_t  payload_bytes = 0;
    size_t  packet_bytes = 0;
    size_t  message_bytes = 0;

    ut_section("the unreliable payload is handed through untouched");

    mp_channel_init(&chan_a);
    mp_channel_init(&chan_b);
    (void)mp_channel_send(&chan_a, body, 2u);
    ut_check(mp_channel_packet_build(&chan_a, 0u, body, sizeof body, packet, sizeof packet,
                                     &packet_bytes),
             "a packet carrying both a message and a payload builds");
    ut_check(mp_channel_packet_receive(&chan_b, packet, packet_bytes, &payload, &payload_bytes),
             "and is accepted");
    ut_check(payload_bytes == sizeof body && memcmp(payload, body, sizeof body) == 0,
             "the payload comes back byte for byte, nothing about it is interpreted");
    ut_check(payload >= packet && payload + payload_bytes <= packet + packet_bytes,
             "as a view into the caller's own buffer, nothing was copied");
    ut_check(mp_channel_message_read(&chan_b, message, sizeof message, &message_bytes) &&
             message_bytes == 2u,
             "and the reliable message travelled in the same packet undisturbed");
}

int main(void)
{
    check_sequence_compare();
    check_header_bytes();
    check_budget();
    check_the_capacity_the_carrier_really_gives();
    check_a_seat_lost_to_the_payload();
    check_resend_throttle();
    check_eager_delivery_under_loss();
    check_ack_window_edges();
    check_retirement();
    check_delivery_over_loss();
    check_reordering();
    check_order_window_refusal();
    check_sequence_wrap_end_to_end();
    check_message_id_wrap();
    check_foreign_and_malformed();
    check_payload_passthrough();

    ut_section("the round trip is measured off the newest ack, and smoothed");
    {
        mp_channel_t   a;
        mp_channel_t   b;
        uint8_t        pa[MP_CHANNEL_PACKET_BYTES];
        uint8_t        pb[MP_CHANNEL_PACKET_BYTES];
        size_t         na = 0;
        size_t         nb = 0;
        const uint8_t *pl = NULL;
        size_t         pln = 0;

        mp_channel_init(&a);
        mp_channel_init(&b);
        ut_check(mp_channel_rtt_ms(&a) == 0u, "before any packet there is no round trip");
        ut_check(mp_channel_packet_build(&a, 1000u, NULL, 0u, pa, sizeof pa, &na),
                 "a sends at 1000");
        ut_check(mp_channel_packet_receive_at(&b, 1020u, pa, na, &pl, &pln), "b takes it at 1020");
        ut_check(mp_channel_packet_build(&b, 1025u, NULL, 0u, pb, sizeof pb, &nb),
                 "b answers at 1025");
        ut_check(mp_channel_packet_receive_at(&a, 1040u, pb, nb, &pl, &pln),
                 "a takes the answer at 1040");
        ut_checkf(mp_channel_rtt_ms(&a) == 40u, "and reads a round trip of 40 ms, not %u",
                  (unsigned)mp_channel_rtt_ms(&a));
        ut_check(mp_channel_rtt_ms(&b) == 0u, "b has had nothing of its own acknowledged yet");

        ut_check(mp_channel_packet_build(&a, 2000u, NULL, 0u, pa, sizeof pa, &na),
                 "a sends again at 2000");
        ut_check(mp_channel_packet_receive_at(&b, 2050u, pa, na, &pl, &pln), "b takes it");
        ut_check(mp_channel_packet_build(&b, 2050u, NULL, 0u, pb, sizeof pb, &nb), "b answers");
        ut_check(mp_channel_packet_receive_at(&a, 2080u, pb, nb, &pl, &pln),
                 "a takes the answer at 2080");
        ut_checkf(mp_channel_rtt_ms(&a) == 45u,
                  "a sample of 80 moves the estimate an eighth of the way, to 45, not %u",
                  (unsigned)mp_channel_rtt_ms(&a));
        ut_checkf(mp_channel_rtt_ms(&b) == 1025u,
                  "and b, whose only packet so far was acknowledged a second after it left, reads "
                  "that honest 1025 ms, not %u", (unsigned)mp_channel_rtt_ms(&b));

        /* The form without a time answers with the last build's clock, at most one interval off. */
        ut_check(mp_channel_packet_build(&a, 3000u, NULL, 0u, pa, sizeof pa, &na),
                 "a sends at 3000");
        ut_check(mp_channel_packet_receive_at(&b, 3010u, pa, na, &pl, &pln), "b takes it");
        ut_check(mp_channel_packet_build(&b, 3010u, NULL, 0u, pb, sizeof pb, &nb), "b answers");
        ut_check(mp_channel_packet_receive(&a, pb, nb, &pl, &pln),
                 "a takes the answer, told no time");
        ut_checkf(mp_channel_rtt_ms(&a) == 39u,
                  "so the sample is 0 against the last build and the estimate "
                  "decays, to 39, not %u",
                  (unsigned)mp_channel_rtt_ms(&a));
    }

    return ut_summary("multiplayer channel");
}
