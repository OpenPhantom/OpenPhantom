/* mp_payload_prefix.c: the client's acknowledgement with its bits, and the host's enemy length.
 *
 * The bits are derived from the client's own history and nowhere else, so the cases are
 * histories: an empty one, one with every tick, one with a gap, the newest tick's own place, and a
 * tick counter that wraps. The codec is round tripped over random words and over random bytes,
 * both ways: a decoder that takes a prefix the encoder refuses would let a stranger name a bit no
 * history can set, and one that refuses what the encoder writes would stop every acknowledgement.
 */
#include "unittest.h"

#include "mp_fuzz_input.h"

#include "mp_payload_prefix.h"
#include "mp_snapshot.h"
#include "mp_snapshot_history.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static mp_snapshot_history_t s_history;

static void hold(uint32_t tick)
{
    mp_snapshot_t snapshot;

    mp_snapshot_clear(&snapshot);
    snapshot.tick = tick;
    mp_snapshot_history_store(&s_history, &snapshot);
}

static void check_the_bits(void)
{
    mp_payload_ack_t ack;
    uint32_t         tick;

    ut_section("the bits are what the history holds behind its newest tick");
    mp_snapshot_history_init(&s_history);
    mp_payload_ack_from(&s_history, &ack);
    ut_check(ack.newest == 0u && ack.bits == 0u, "an empty history says nought and names nothing");

    hold(100u);
    mp_payload_ack_from(&s_history, &ack);
    ut_check(ack.newest == 100u && ack.bits == 0u, "one tick held is the newest and no bit");

    mp_snapshot_history_init(&s_history);
    for (tick = 100u; tick < 100u + MP_SNAPSHOT_HISTORY; ++tick) {
        hold(tick);
    }
    mp_payload_ack_from(&s_history, &ack);
    ut_checkf(ack.newest == 131u && ack.bits == 0x7FFFFFFFu,
              "a history with every tick sets the thirty one bits it can (%08X)",
              (unsigned)ack.bits);
    ut_check((ack.bits & MP_PAYLOAD_ACK_UNUSED) == 0u,
             "and bit 31 stays clear: the tick thirty two back shares the newest's place");

    hold(132u);
    mp_payload_ack_from(&s_history, &ack);
    ut_check(ack.newest == 132u && ack.bits == 0x7FFFFFFFu,
             "one tick more writes the oldest over, and the window moves with it");

    mp_snapshot_history_init(&s_history);
    hold(100u);
    hold(102u);
    hold(103u);
    mp_payload_ack_from(&s_history, &ack);
    ut_checkf(ack.newest == 103u && ack.bits == 0x5u,
              "a gap: 102 is bit 0, 101 missing is bit 1 clear, 100 is bit 2 (%08X)",
              (unsigned)ack.bits);
    ut_check(mp_payload_ack_holds(&ack, 102u) && !mp_payload_ack_holds(&ack, 101u) &&
                 mp_payload_ack_holds(&ack, 100u),
             "and the host reads each of the three back out of the bits");
    ut_check(!mp_payload_ack_holds(&ack, 103u) && !mp_payload_ack_holds(&ack, 104u) &&
                 !mp_payload_ack_holds(&ack, 103u - 32u),
             "the newest is no bit, nor a tick after it, nor one past the window");
    ut_check(mp_payload_ack_widest_gap(&ack) == 1u,
             "the widest gap is the one tick between two held ones");
    ack.bits = 0x4u;
    ut_check(mp_payload_ack_widest_gap(&ack) == 2u,
             "the newest counts as held: two missing below it before the next held one");
    ack.bits = 0x7u;
    ut_check(mp_payload_ack_widest_gap(&ack) == 0u,
             "and clear bits past the oldest held tick are where the history begins, no gap");

    ut_section("the tick counter wraps and the bits follow it");
    mp_snapshot_history_init(&s_history);
    hold(0xFFFFFFFEu);
    hold(0xFFFFFFFFu);
    hold(1u);
    mp_payload_ack_from(&s_history, &ack);
    ut_checkf(ack.newest == 1u && ack.bits == 0x6u,
              "held 0xFFFFFFFE, 0xFFFFFFFF and 1: bits 1 and 2, and none for the nought no "
              "substep carries (%08X)", (unsigned)ack.bits);
    ut_check(mp_payload_ack_holds(&ack, 0xFFFFFFFFu) && mp_payload_ack_holds(&ack, 0xFFFFFFFEu),
             "both ticks before the wrap are held as the host counts them");
}

static void check_the_codec(void)
{
    mp_payload_ack_t ack;
    mp_payload_ack_t back;
    uint8_t          bytes[MP_PAYLOAD_ACK_BYTES + 4u];
    unsigned         i;
    bool             round_trips = true;
    bool             refuses_alike = true;

    ut_section("the prefix on the wire: eight bytes, least significant first");
    ack.newest = 0x01020304u;
    ack.bits   = 0x05060708u;
    ut_check(mp_payload_put_ack(bytes, sizeof bytes, &ack), "it writes");
    ut_check(bytes[0] == 4u && bytes[3] == 1u && bytes[4] == 8u && bytes[7] == 5u,
             "the newest tick first, then the bits");
    ut_check(mp_payload_get_ack(bytes, MP_PAYLOAD_ACK_BYTES, &back) && back.newest == ack.newest &&
                 back.bits == ack.bits,
             "and reads back");
    ut_check(!mp_payload_get_ack(bytes, MP_PAYLOAD_ACK_BYTES - 1u, &back) &&
                 !mp_payload_put_ack(bytes, MP_PAYLOAD_ACK_BYTES - 1u, &ack),
             "seven bytes neither hold one nor take one");

    ut_section("what no history produces is refused both ways");
    ack.bits = MP_PAYLOAD_ACK_UNUSED;
    ut_check(!mp_payload_put_ack(bytes, sizeof bytes, &ack), "bit 31 is not written");
    memset(bytes, 0, sizeof bytes);
    bytes[0] = 1u;
    bytes[7] = 0x80u;
    ut_check(!mp_payload_get_ack(bytes, MP_PAYLOAD_ACK_BYTES, &back), "nor read");
    ack.newest = 0u;
    ack.bits   = 1u;
    ut_check(!mp_payload_put_ack(bytes, sizeof bytes, &ack),
             "a bit beside a newest of nought is not written");
    memset(bytes, 0, sizeof bytes);
    bytes[4] = 1u;
    ut_check(!mp_payload_get_ack(bytes, MP_PAYLOAD_ACK_BYTES, &back), "nor read");

    ut_section("round trip over random words and random bytes");
    for (i = 0; i < ROUNDS; ++i) {
        uint8_t raw[MP_PAYLOAD_ACK_BYTES];
        uint8_t again[MP_PAYLOAD_ACK_BYTES];
        bool    took;

        ack.newest = (i % 7u == 0u) ? 0u : next_random();
        ack.bits   = next_random() & (i % 3u == 0u ? 0xFFFFFFFFu : 0x7FFFFFFFu);
        took       = mp_payload_put_ack(raw, sizeof raw, &ack);
        if (took && (!mp_payload_get_ack(raw, sizeof raw, &back) || back.newest != ack.newest ||
                     back.bits != ack.bits)) {
            round_trips = false;
        }
        fill_random(raw, sizeof raw);
        took = mp_payload_get_ack(raw, sizeof raw, &back);
        if (took != (took && mp_payload_put_ack(again, sizeof again, &back) &&
                     memcmp(again, raw, sizeof raw) == 0)) {
            refuses_alike = false;
        }
    }
    ut_check(round_trips, "every prefix the encoder writes decodes to itself");
    ut_check(refuses_alike, "and every one the decoder takes the encoder writes again, alike");
}

static void check_the_world(void)
{
    uint8_t payload[16];
    size_t  enemy_bytes = 0;
    size_t  at          = 0;

    ut_section("the host's world opens with the enemy length");
    ut_check(mp_payload_world_reserve(0u) == MP_PAYLOAD_SNAPSHOT_HEADER_BYTES &&
                 mp_payload_world_reserve(3u) ==
                     MP_PAYLOAD_SNAPSHOT_HEADER_BYTES + 3u * MP_WIRE_BODY_MAX_BYTES,
             "the reserve is the snapshot's head and the largest record per body");
    ut_check(mp_payload_put_enemy_length(payload, sizeof payload, 0u) && payload[0] == 0u &&
                 payload[1] == 0u,
             "a world with no enemies still says a length of nought");
    ut_check(mp_payload_split_world(payload, 10u, &enemy_bytes, &at) && enemy_bytes == 0u &&
                 at == MP_PAYLOAD_ENEMY_LENGTH_BYTES,
             "and its snapshot follows the length at once");
    ut_check(mp_payload_put_enemy_length(payload, sizeof payload, 6u) &&
                 mp_payload_split_world(payload, 8u, &enemy_bytes, &at) && enemy_bytes == 6u &&
                 at == 8u,
             "a block of six ends where the snapshot begins, even with no snapshot behind it");
    ut_check(!mp_payload_split_world(payload, 7u, &enemy_bytes, &at),
             "a length past the payload is a torn payload, snapshot and all");
    ut_check(!mp_payload_split_world(payload, 1u, &enemy_bytes, &at),
             "one byte is not even a length");
    ut_check(!mp_payload_put_enemy_length(payload, sizeof payload, 0x10000u) &&
                 !mp_payload_put_enemy_length(payload, 1u, 0u),
             "a length two bytes cannot say, or no room for them, is not written");
}

int main(void)
{
    check_the_bits();
    check_the_codec();
    check_the_world();
    return ut_summary("mp_payload_prefix");
}
