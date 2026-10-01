/* mp_budget_rule.c: the shares of one packet, and the note that could not get a seat.
 *
 * A field run with four players left 45 bytes beside a full payload, so a map digest of 183
 * never found a seat while the enemy block was full. The check below builds that packet twice on
 * a real channel with the capacity the session hands the builder: once with the split the bridge
 * used before, kept here as the reference, which leaves the note behind, and once with the rule,
 * which seats it in the same packet.
 */
#include "unittest.h"

#include "mp_budget_rule.h"
#include "mp_channel.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The bodies' reserve as the bridge computes it: the snapshot's header of 24 and 66 a body at most,
 * the numbers a field run with four players had. */
#define RESERVE(bodies) (24u + 66u * (bodies))
#define PAYLOAD_MAX 1168u   /* MP_SESSION_PAYLOAD_BYTES */

/* The enemy floor of a quiet substep, which nothing that may not wait raises. */
#define FIXED MP_BUDGET_ENEMY_FLOOR_BYTES

static mp_channel_t s_channel;

/* The split the bridge used before: the enemies took everything the bodies left. */
static size_t old_enemy_room(size_t reserve)
{
    return PAYLOAD_MAX - MP_BUDGET_ENEMY_LENGTH_BYTES - reserve;
}

static void check_the_arithmetic(void)
{
    ut_section("the shares of a packet, in the numbers of two and four players");
    ut_checkf(mp_budget_message_limit(RESERVE(3u), FIXED) == 822u,
              "four players: messages may have up to 822 bytes beside the bodies and the floor "
              "(%u)", (unsigned)mp_budget_message_limit(RESERVE(3u), FIXED));
    ut_checkf(mp_budget_message_limit(RESERVE(1u), FIXED) == 954u,
              "two players: up to 954 (%u)", (unsigned)mp_budget_message_limit(RESERVE(1u), FIXED));
    ut_checkf(mp_budget_message_limit(RESERVE(3u), FIXED + 172u) == 822u - 172u,
              "a floor raised to 300 takes its 172 more out of the messages: %u left",
              (unsigned)mp_budget_message_limit(RESERVE(3u), FIXED + 172u));
    ut_check(mp_budget_enemy_room(PAYLOAD_MAX, RESERVE(3u),
                                  mp_budget_message_limit(RESERVE(3u), FIXED + 172u)) ==
                 FIXED + 172u,
             "and with every byte of that due, the enemies keep the raised floor");
    ut_check(mp_budget_message_limit(RESERVE(3u), 2000u) == 0u &&
                 mp_budget_enemy_room(PAYLOAD_MAX, RESERVE(3u), 0u) == old_enemy_room(RESERVE(3u)),
             "a floor past the packet leaves the messages nothing and the enemies all there is");
    ut_check(mp_budget_enemy_room(PAYLOAD_MAX, RESERVE(3u), 0u) == old_enemy_room(RESERVE(3u)),
             "with nothing due the enemies get exactly what they got before");
    ut_check(mp_budget_enemy_room(PAYLOAD_MAX, RESERVE(1u), 4u) == old_enemy_room(RESERVE(1u)),
             "and a few bytes due change nothing either: the payload's own buffer is the smaller");
    ut_checkf(mp_budget_enemy_room(PAYLOAD_MAX, RESERVE(3u), 187u) == 763u,
              "a digest of 183 due takes its 187 out of the enemy block: 763 left (%u)",
              (unsigned)mp_budget_enemy_room(PAYLOAD_MAX, RESERVE(3u), 187u));
    ut_check(mp_budget_enemy_room(PAYLOAD_MAX, RESERVE(3u),
                                  mp_budget_message_limit(RESERVE(3u), FIXED)) ==
                 MP_BUDGET_ENEMY_FLOOR_BYTES,
             "and however much is due, the enemies keep their floor");
    ut_check(mp_budget_message_limit(2000u, FIXED) == 0u &&
                 mp_budget_enemy_room(PAYLOAD_MAX, 2000u, 0u) == 0u,
             "a reserve larger than the packet leaves nothing to either");
}

/* One packet: `enemy` bytes of a full enemy block behind its length, the bodies' reserve in full,
 * and whatever the channel seats beside them. Answers whether the digest got its seat. */
static bool digest_seated_beside(size_t enemy, size_t reserve)
{
    uint8_t payload[PAYLOAD_MAX];
    uint8_t packet[MP_CHANNEL_BUDGET_BYTES];
    uint8_t digest[183];
    size_t  bytes = 0;
    size_t  payload_bytes = MP_BUDGET_ENEMY_LENGTH_BYTES + enemy + reserve;

    mp_channel_init(&s_channel);
    memset(digest, 0x86, sizeof digest);
    memset(payload, 0x5A, sizeof payload);
    (void)mp_channel_send(&s_channel, digest, sizeof digest);
    if (payload_bytes > sizeof payload ||
        !mp_channel_packet_build(&s_channel, 100u, payload, payload_bytes, packet, sizeof packet,
                                 &bytes)) {
        return false;
    }
    return packet[12] == 1u && mp_channel_seats_lost_to_payload(&s_channel) == 0u;
}

static void check_the_digest_gets_its_seat(void)
{
    size_t reserve = RESERVE(3u);
    size_t due;

    ut_section("a 183 byte digest beside a full enemy block, four players");
    ut_check(!digest_seated_beside(old_enemy_room(reserve), reserve),
             "the reference, the split before: the full block leaves it no seat");
    mp_channel_init(&s_channel);
    {
        uint8_t digest[183];

        memset(digest, 0x86, sizeof digest);
        (void)mp_channel_send(&s_channel, digest, sizeof digest);
    }
    due = mp_channel_due_bytes(&s_channel, 100u, mp_budget_message_limit(reserve, FIXED));
    ut_checkf(due == 187u, "the query names it due: %u byte(s) with its header", (unsigned)due);
    ut_check(digest_seated_beside(mp_budget_enemy_room(PAYLOAD_MAX, reserve, due), reserve),
             "with the rule the enemy block gives way and it rides the same packet");
}

int main(void)
{
    check_the_arithmetic();
    check_the_digest_gets_its_seat();
    return ut_summary("mp_budget_rule");
}
