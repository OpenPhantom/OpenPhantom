/* mp_inbox.c: the ring of whole messages keeps their order across its end and tears none.
 */
#include "unittest.h"

#include "mp_channel.h"
#include "mp_inbox.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* File scope: the ring is 32 KB. */
static mp_inbox_t s_inbox;

static void fill(uint8_t *data, size_t bytes, unsigned index)
{
    size_t at;

    for (at = 0; at < bytes; ++at) {
        data[at] = (uint8_t)((index * 31u + at) & 0xFFu);
    }
}

static bool matches(const uint8_t *data, size_t bytes, unsigned index)
{
    size_t at;

    for (at = 0; at < bytes; ++at) {
        if (data[at] != (uint8_t)((index * 31u + at) & 0xFFu)) {
            return false;
        }
    }
    return true;
}

/* Sizes that walk the range a channel message can have, zero and the largest included. */
static size_t size_of(unsigned index)
{
    static const size_t sizes[] = {0u, 1u, 7u, 40u, 200u, 1023u, MP_CHANNEL_MESSAGE_BYTES};

    return sizes[index % (sizeof sizes / sizeof sizes[0])];
}

static void check_order_across_the_end(void)
{
    uint8_t  data[MP_CHANNEL_MESSAGE_BYTES];
    uint8_t  read[MP_CHANNEL_MESSAGE_BYTES];
    size_t   bytes = 0;
    unsigned put = 0;
    unsigned taken = 0;
    unsigned wrong = 0;
    unsigned round;
    unsigned in;

    ut_section("messages of every size come out whole and in order, many times around the ring");
    mp_inbox_init(&s_inbox);
    for (round = 0; round < 400u; ++round) {
        /* Three in, then out down to twenty or to none, so the head crosses the end of the ring
         * at every offset a length field or a message body can straddle. */
        for (in = 0; in < 3u; ++in) {
            fill(data, size_of(put), put);
            if (!mp_inbox_put(&s_inbox, data, size_of(put))) {
                break;
            }
            ++put;
        }
        while (s_inbox.count > (round % 2u == 0u ? 20u : 0u) &&
               mp_inbox_take(&s_inbox, read, sizeof read, &bytes)) {
            wrong += (bytes != size_of(taken) || !matches(read, bytes, taken)) ? 1u : 0u;
            ++taken;
        }
    }
    while (mp_inbox_take(&s_inbox, read, sizeof read, &bytes)) {
        wrong += (bytes != size_of(taken) || !matches(read, bytes, taken)) ? 1u : 0u;
        ++taken;
    }
    ut_checkf(put > 1000u, "%u messages went in", put);
    ut_checkf(taken == put && wrong == 0u, "all %u came out, %u of them wrong", taken, wrong);
    ut_check(s_inbox.count == 0u && s_inbox.used == 0u, "and the ring is empty again");
}

/* A length field split over the end of the ring: fill it to one byte short of its size, empty it,
 * and the next length is written one byte at the end and one at the start. */
static void check_a_length_split_over_the_end(void)
{
    uint8_t  data[MP_CHANNEL_MESSAGE_BYTES];
    uint8_t  read[MP_CHANNEL_MESSAGE_BYTES];
    size_t   bytes = 0;
    size_t   last;
    unsigned index;

    ut_section("a length field split over the end of the ring, and a message of no bytes");
    mp_inbox_init(&s_inbox);
    for (index = 0; index < 27u; ++index) {
        fill(data, MP_CHANNEL_MESSAGE_BYTES, index);
        (void)mp_inbox_put(&s_inbox, data, MP_CHANNEL_MESSAGE_BYTES);
    }
    last = MP_INBOX_BYTES - 1u - s_inbox.used - MP_INBOX_LENGTH_BYTES;
    fill(data, last, 27u);
    ut_check(mp_inbox_put(&s_inbox, data, last) && s_inbox.used == MP_INBOX_BYTES - 1u,
             "the ring holds one byte short of its size");
    while (mp_inbox_take(&s_inbox, read, sizeof read, &bytes)) {
        ++index;
    }
    ut_check(s_inbox.head == MP_INBOX_BYTES - 1u && s_inbox.used == 0u,
             "and once emptied its head stands on the last byte");

    ut_check(mp_inbox_put(&s_inbox, NULL, 0u), "a message of no bytes goes in, its length split");
    fill(data, MP_CHANNEL_MESSAGE_BYTES, 99u);
    ut_check(mp_inbox_put(&s_inbox, data, MP_CHANNEL_MESSAGE_BYTES), "and the largest after it");
    ut_check(mp_inbox_take(&s_inbox, read, sizeof read, &bytes) && bytes == 0u,
             "the empty one comes out empty");
    ut_check(mp_inbox_take(&s_inbox, read, sizeof read, &bytes) &&
                 bytes == MP_CHANNEL_MESSAGE_BYTES && matches(read, bytes, 99u),
             "and the largest comes out whole");
}

static void check_a_full_ring_refuses_whole(void)
{
    uint8_t  data[MP_CHANNEL_MESSAGE_BYTES];
    uint8_t  read[MP_CHANNEL_MESSAGE_BYTES];
    size_t   bytes = 0;
    size_t   before;
    unsigned index = 0;

    ut_section("a full ring refuses a message whole and keeps everything it holds");
    mp_inbox_init(&s_inbox);
    fill(data, sizeof data, 0u);
    while (mp_inbox_put(&s_inbox, data, sizeof data)) {
        ++index;
    }
    before = s_inbox.used;
    ut_checkf(index == MP_INBOX_BYTES / (MP_INBOX_LENGTH_BYTES + MP_CHANNEL_MESSAGE_BYTES),
              "%u of the largest messages fit", index);
    ut_check(!mp_inbox_fits(&s_inbox, sizeof data), "the next one does not fit");
    ut_check(s_inbox.used == before && s_inbox.count == index, "and the refusal wrote nothing");
    ut_check(mp_inbox_most_count(&s_inbox) == index && mp_inbox_most_bytes(&s_inbox) == before,
             "the most it held is what it holds");

    ut_check(!mp_inbox_take(&s_inbox, read, 10u, &bytes) && s_inbox.count == index,
             "a buffer too small takes nothing and tears nothing");
    ut_check(mp_inbox_take(&s_inbox, read, sizeof read, &bytes) && bytes == sizeof data &&
                 matches(read, bytes, 0u),
             "the right buffer takes the oldest whole");
    ut_check(mp_inbox_put(&s_inbox, data, sizeof data), "and its room is there for the next");
}

static void check_a_stall_counts_once(void)
{
    ut_section("a stall is counted where it begins, not once per move while it lasts");
    mp_inbox_init(&s_inbox);
    mp_inbox_note_move(&s_inbox, false);
    mp_inbox_note_move(&s_inbox, true);
    mp_inbox_note_move(&s_inbox, true);
    mp_inbox_note_move(&s_inbox, true);
    ut_check(mp_inbox_stalls(&s_inbox) == 1u,
             "three moves that left a message behind are one stall");
    mp_inbox_note_move(&s_inbox, false);
    mp_inbox_note_move(&s_inbox, true);
    ut_check(mp_inbox_stalls(&s_inbox) == 2u, "and a move that left nothing ends it");
}

int main(void)
{
    check_order_across_the_end();
    check_a_length_split_over_the_end();
    check_a_full_ring_refuses_whole();
    check_a_stall_counts_once();
    return ut_summary("mp_inbox");
}
