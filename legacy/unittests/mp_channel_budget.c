/* mp_channel_budget.c: what the channel says is due, and what it then seats.
 *
 * The bridge sizes the enemy block by the channel's answer to "what would a build seat", so the
 * answer has to be the build's own: the same skips of a message too large for the room left, the
 * same resend throttle, the same far window, the same thirty two a packet. Every packet here is
 * built with the capacity the session hands the builder, MP_CHANNEL_BUDGET_BYTES.
 */
#include "unittest.h"

#include "mp_budget_rule.h"
#include "mp_channel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_channel_t s_channel;
static mp_channel_t s_copy;
static mp_channel_t s_far;

static uint32_t next_random(uint32_t *seed)
{
    *seed = *seed * 1103515245u + 12345u;
    return *seed >> 8;
}

/* Builds with a payload that leaves exactly `room` bytes for messages, on a copy so the original
 * queue is untouched, and answers the bytes of messages it seated. */
static size_t seated_with_room(const mp_channel_t *channel, uint32_t now_ms, size_t room)
{
    static uint8_t payload[MP_CHANNEL_PAYLOAD_BYTES];
    uint8_t        packet[MP_CHANNEL_BUDGET_BYTES];
    size_t         payload_bytes = MP_CHANNEL_PAYLOAD_BYTES - room;
    size_t         bytes = 0;

    s_copy = *channel;
    if (!mp_channel_packet_build(&s_copy, now_ms, payload, payload_bytes, packet, sizeof packet,
                                 &bytes)) {
        return (size_t)-1;
    }
    return bytes - MP_CHANNEL_HEADER_BYTES - payload_bytes;
}

static void check_the_query_is_the_build(void)
{
    uint32_t seed = 4242u;
    uint32_t round;
    uint32_t apart = 0;
    uint32_t cases = 0;
    uint32_t now = 0;

    ut_section("the due bytes the query names are exactly what the build seats in that room");
    mp_channel_init(&s_channel);
    mp_channel_init(&s_far);
    for (round = 0; round < 600u; ++round) {
        uint8_t  data[MP_CHANNEL_MESSAGE_BYTES];
        uint8_t  packet[MP_CHANNEL_BUDGET_BYTES];
        size_t   bytes = 0;
        uint32_t queued = next_random(&seed) % 4u;
        size_t   limit;
        size_t   due;

        now += 7u + next_random(&seed) % 60u;
        while (queued-- > 0u) {
            /* Sizes from a byte, which rides eagerly, to a kilobyte, which rarely fits. */
            size_t spread = (next_random(&seed) % 4u == 0u) ? 1000u : 60u;
            size_t size   = 1u + next_random(&seed) % spread;

            memset(data, (int)round, size);
            (void)mp_channel_send(&s_channel, data, size);
        }
        limit = next_random(&seed) % (MP_CHANNEL_PAYLOAD_BYTES + 1u);
        due   = mp_channel_due_bytes(&s_channel, now, limit);
        ++cases;
        if (due > limit || seated_with_room(&s_channel, now, due) != due) {
            ++apart;
        }
        /* Now and then a real packet goes, and now and then the far side answers, so the queue
         * holds messages resting under the throttle and messages past the far window. */
        if (next_random(&seed) % 3u == 0u) {
            (void)mp_channel_packet_build(&s_channel, now, NULL, 0u, packet, sizeof packet,
                                          &bytes);
            if (next_random(&seed) % 2u == 0u) {
                const uint8_t *payload = NULL;
                size_t         payload_bytes = 0;

                (void)mp_channel_packet_receive_at(&s_far, now, packet, bytes, &payload,
                                                   &payload_bytes);
            }
        }
        if (next_random(&seed) % 9u == 0u) {
            const uint8_t *payload = NULL;
            size_t         payload_bytes = 0;

            (void)mp_channel_packet_build(&s_far, now, NULL, 0u, packet, sizeof packet, &bytes);
            (void)mp_channel_packet_receive_at(&s_channel, now, packet, bytes, &payload,
                                               &payload_bytes);
            while (mp_channel_message_read(&s_far, data, sizeof data, &bytes)) {
            }
        }
    }
    ut_checkf(apart == 0u, "%u random queues and rooms, %u where the build seated anything else",
              (unsigned)cases, (unsigned)apart);
    ut_check(mp_channel_due_bytes(NULL, 0u, 100u) == 0u, "no channel has nothing due");
}

/* The message ids one built packet carries, in order. */
static size_t ids_in(const uint8_t *packet, size_t bytes, uint16_t *ids, size_t most)
{
    size_t  at = MP_CHANNEL_HEADER_BYTES;
    size_t  count = 0;
    uint8_t index;

    for (index = 0; index < packet[12] && count < most && at + 4u <= bytes; ++index) {
        uint16_t size = (uint16_t)(packet[at + 2] | (packet[at + 3] << 8));

        ids[count++] = (uint16_t)(packet[at] | (packet[at + 1] << 8));
        at += 4u + size;
    }
    return count;
}

static void check_the_overflow_packet(void)
{
    static uint8_t payload[1100];
    uint8_t        packet[MP_CHANNEL_BUDGET_BYTES];
    uint8_t        big[900];
    uint16_t       ids[MP_CHANNEL_PACKET_MESSAGES];
    size_t         bytes = 0;
    size_t         count;
    size_t         due;

    ut_section("the overflow packet carries what the first left for room, and nothing twice");
    mp_channel_init(&s_channel);
    memset(big, 0x42, sizeof big);
    (void)mp_channel_send(&s_channel, "eager", 5u);        /* id 0, rides every packet */
    (void)mp_channel_send(&s_channel, big, sizeof big);    /* id 1, too large beside the payload */
    ut_check(mp_channel_packet_build(&s_channel, 500u, payload, sizeof payload, packet,
                                     sizeof packet, &bytes) &&
             packet[12] == 1u && mp_channel_left_behind(&s_channel),
             "the payload packet seats the small one and leaves the large one behind for room");
    due = mp_channel_overflow_bytes(&s_channel, 500u);
    ut_checkf(due == 4u + sizeof big, "the overflow would carry the large one alone: %u byte(s)",
              (unsigned)due);
    ut_check(mp_channel_packet_build_overflow(&s_channel, 500u, packet, sizeof packet, &bytes),
             "and is built");
    count = ids_in(packet, bytes, ids, MP_CHANNEL_PACKET_MESSAGES);
    ut_checkf(count == 1u && ids[0] == 1u && bytes == MP_CHANNEL_HEADER_BYTES + due,
              "carrying id 1 and not the small one laid a moment ago (%u message(s))",
              (unsigned)count);
    ut_check(mp_channel_overflow_bytes(&s_channel, 500u) == 0u,
             "after it nothing is left that an overflow could carry");

    mp_channel_init(&s_channel);
    for (count = 0; count < 40u; ++count) {
        (void)mp_channel_send(&s_channel, big, 300u);
    }
    (void)mp_channel_packet_build(&s_channel, 900u, payload, sizeof payload, packet,
                                  sizeof packet, &bytes);
    {
        bool   inside = true;
        size_t sent;
        int    round;

        for (round = 0; round < 20; ++round) {
            if (!mp_channel_packet_build_overflow(&s_channel, 900u + (uint32_t)round * 200u,
                                                  packet, sizeof packet, &bytes)) {
                break;
            }
            sent = ids_in(packet, bytes, ids, MP_CHANNEL_PACKET_MESSAGES);
            while (sent-- > 0u) {
                inside = inside && ids[sent] < MP_CHANNEL_ORDER_SLOTS;
            }
        }
        ut_check(inside, "with forty queued and none answered, no overflow packet lays an id "
                         "past the far side's window");
    }
}

static void check_the_rate(void)
{
    mp_budget_bucket_t bucket;

    ut_section("the rate never refuses the payload packet, and holds an extra one to its tokens");
    memset(&bucket, 0, sizeof bucket);
    ut_check(!mp_budget_bucket_allows(&bucket, 1u), "a bucket never started allows nothing");
    mp_budget_bucket_start(&bucket, 1000u);
    ut_check(mp_budget_bucket_allows(&bucket, MP_BUDGET_DEPTH_BYTES) &&
             !mp_budget_bucket_allows(&bucket, MP_BUDGET_DEPTH_BYTES + 1u),
             "a fresh one allows its depth and nothing more");
    mp_budget_bucket_spend(&bucket, 9000u);
    ut_check(!mp_budget_bucket_allows(&bucket, 1200u) && mp_budget_bucket_allows(&bucket, 600u),
             "600 left: a packet of 1200 waits, one of 600 may go");
    mp_budget_bucket_spend(&bucket, 5000u);
    ut_checkf(bucket.tokens == -4400, "the payload packet is spent all the same, into debt (%d)",
              (int)bucket.tokens);
    mp_budget_bucket_fill(&bucket, 1100u);
    ut_checkf(bucket.tokens == -4400 + (int32_t)(MP_BUDGET_RATE_BYTES_A_SECOND / 10u),
              "a tenth of a second pays back a tenth of the rate (%d)", (int)bucket.tokens);
    mp_budget_bucket_fill(&bucket, 9000u);
    ut_check(bucket.tokens == (int32_t)MP_BUDGET_DEPTH_BYTES,
             "and a long gap fills it to its depth and no further");
    mp_budget_bucket_spend(&bucket, 100000u);
    mp_budget_bucket_spend(&bucket, 100000u);
    ut_check(bucket.tokens == -(int32_t)MP_BUDGET_DEPTH_BYTES,
             "a debt never grows past one depth");
}

/* Two packets a substep, the first with a payload that leaves no room for the large message, and
 * the sequence numbers running through their wrap: every message arrives once and in order, and
 * none is retired that never arrived. */
static void check_two_packets_a_substep_across_the_wrap(void)
{
    static uint8_t payload[1000];
    uint8_t        packet[MP_CHANNEL_BUDGET_BYTES];
    uint8_t        data[MP_CHANNEL_MESSAGE_BYTES];
    size_t         bytes = 0;
    uint32_t       seed = 99u;
    uint32_t       queued = 0;
    uint32_t       read = 0;
    uint32_t       expected = 0;
    bool           in_order = true;
    uint32_t       substep;

    ut_section("two packets a substep across the sequence wrap: once each, in order");
    mp_channel_init(&s_channel);
    mp_channel_init(&s_far);
    s_channel.local_sequence = 65500u;
    for (substep = 0; substep < 260u; ++substep) {
        uint32_t now = substep * 31u;
        int      packet_index;

        if (substep < 200u) {
            uint32_t sizes[2] = { 700u, 20u };
            int      which;

            for (which = 0; which < 2; ++which) {
                memset(data, 0, sizeof data);
                memcpy(data, &queued, sizeof queued);
                if (mp_channel_send(&s_channel, data, sizes[which])) {
                    ++queued;
                }
            }
        }
        for (packet_index = 0; packet_index < 2; ++packet_index) {
            bool           built;
            const uint8_t *carried = NULL;
            size_t         carried_bytes = 0;

            built = packet_index == 0
                        ? mp_channel_packet_build(&s_channel, now, payload, sizeof payload, packet,
                                                  sizeof packet, &bytes)
                        : mp_channel_left_behind(&s_channel) &&
                              mp_channel_packet_build_overflow(&s_channel, now, packet,
                                                               sizeof packet, &bytes);
            if (built && next_random(&seed) % 10u != 0u) {   /* a tenth is lost */
                (void)mp_channel_packet_receive_at(&s_far, now, packet, bytes, &carried,
                                                   &carried_bytes);
            }
        }
        while (mp_channel_message_read(&s_far, data, sizeof data, &bytes)) {
            uint32_t number = 0;

            memcpy(&number, data, sizeof number);
            in_order = in_order && number == expected;
            expected = number + 1u;
            ++read;
        }
        if (mp_channel_packet_build(&s_far, now, NULL, 0u, packet, sizeof packet, &bytes)) {
            const uint8_t *carried = NULL;
            size_t         carried_bytes = 0;

            (void)mp_channel_packet_receive_at(&s_channel, now, packet, bytes, &carried,
                                               &carried_bytes);
        }
    }
    ut_checkf(read == queued && in_order,
              "%u queued, %u read, each once and in order, through sequence %u",
              (unsigned)queued, (unsigned)read, (unsigned)s_channel.local_sequence);
    ut_checkf(mp_channel_send_pending(&s_channel) == 0u && s_channel.local_sequence < 65500u,
              "nothing is pending and the numbering wrapped (%u pending)",
              (unsigned)mp_channel_send_pending(&s_channel));
}

int main(void)
{
    check_the_query_is_the_build();
    check_the_overflow_packet();
    check_the_rate();
    check_two_packets_a_substep_across_the_wrap();
    return ut_summary("mp_channel_budget");
}
