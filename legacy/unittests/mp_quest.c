/* unittests/mp_quest.c: the thirty-four shared story bits, and the splice that must not spill.
 *
 * The one thing this file is really for is the band's edges. Bits 51 to 84 begin and end in the
 * middle of a byte, and the bits either side of them belong to the HERO: 48 to 50 below and 85 to
 * 95 above. A splice that is one bit wide in the wrong direction hands one player another player's
 * key and nothing anywhere says so, so the edge cases are checked against a bank that has every
 * neighbouring bit set and must come back with every one of them still set.
 */
#include "unittest.h"

#include "mp_quest.h"

#include <string.h>

#define BANK_BYTES 1250u

static void check_the_band_matches_the_engines_own_scan(void)
{
    ut_section("the band is the pause menu's item scan, byte for byte");

    /* The pause menu's item scan, in pausemenu_run at 0x00442E2F, walks `i < 0x22` over
     * `g_storyFlags[(i + 0x33) >> 3] & (1 << ((i + 0x33) & 7))`. Both numbers are in that loop
     * and neither is rounded. */
    ut_check(MP_QUEST_FIRST_BIT == 0x33u, "it starts at bit 0x33, which is STATUS_ITEM_FLAG0");
    ut_check(MP_QUEST_BIT_COUNT == 0x22u, "and it is 0x22 bits wide, the item strip's own count");
    ut_check(MP_QUEST_LAST_BIT == 84u, "so the last bit is 84");
    ut_check(MP_QUEST_BYTES == 5u, "thirty-four bits pack into five bytes");

    /* The keys are inside it, which is the decision the header records rather than an accident:
     * `0x0044C9A0` forms `bit = key + 0x4a` for keys 1..7, so bits 75 to 81. */
    ut_check(MP_QUEST_FIRST_BIT <= 75u && MP_QUEST_LAST_BIT >= 81u,
             "and the seven key bits lie inside it, so sharing the band shares the keys");
}

static void check_one_bit_at_a_time(void)
{
    mp_quest_set_t set;
    uint32_t       i;

    ut_section("a bit goes in, comes out, and its neighbours do not move");

    memset(&set, 0, sizeof set);
    ut_check(mp_quest_count(&set) == 0u, "an empty set holds nothing");

    mp_quest_put(&set, 0u, true);
    ut_check(mp_quest_get(&set, 0u), "the first bit reads back");
    ut_check(!mp_quest_get(&set, 1u), "and the one after it did not come with it");
    ut_check(mp_quest_count(&set) == 1u, "one bit held");

    mp_quest_put(&set, MP_QUEST_BIT_COUNT - 1u, true);
    ut_check(mp_quest_get(&set, MP_QUEST_BIT_COUNT - 1u), "the last bit reads back too");
    ut_check(mp_quest_count(&set) == 2u, "two bits held");

    mp_quest_put(&set, 0u, false);
    ut_check(!mp_quest_get(&set, 0u), "and clearing one leaves the other");
    ut_check(mp_quest_get(&set, MP_QUEST_BIT_COUNT - 1u), "which is still there");

    ut_section("an index past the band moves nothing and answers false");

    memset(&set, 0, sizeof set);
    mp_quest_put(&set, MP_QUEST_BIT_COUNT, true);
    mp_quest_put(&set, 255u, true);
    ut_check(mp_quest_count(&set) == 0u, "neither of those went anywhere");
    ut_check(!mp_quest_get(&set, MP_QUEST_BIT_COUNT), "and reading past the band is false");

    ut_section("every index round trips, which is what a shifted splice would break");

    memset(&set, 0, sizeof set);
    for (i = 0; i < MP_QUEST_BIT_COUNT; ++i) {
        mp_quest_put(&set, i, true);
    }
    ut_check(mp_quest_count(&set) == MP_QUEST_BIT_COUNT, "all thirty-four are held at once");
    for (i = 0; i < MP_QUEST_BIT_COUNT; ++i) {
        ut_checkf(mp_quest_get(&set, i), "bit %u reads back", i);
    }
}

/* The case the whole file exists for. A bank in which EVERY bit is set, spliced with an empty
 * band: every bit of the band must be clear afterwards, and every other bit in all 1250 bytes must
 * still be set, including bits 48 to 50 below the band and 85 to 95 above it, which are the
 * hero's and are the ones a one-bit error would take. */
static void check_the_splice_does_not_spill(void)
{
    static uint8_t bank[BANK_BYTES];
    mp_quest_set_t empty;
    mp_quest_set_t full;
    mp_quest_set_t read;
    uint32_t       bit;
    uint32_t       spilled = 0;
    uint32_t       left    = 0;

    ut_section("an empty band spliced into a full bank clears the band and nothing else");

    memset(bank, 0xFF, sizeof bank);
    memset(&empty, 0, sizeof empty);
    mp_quest_into_bank(&empty, bank);

    for (bit = 0; bit < BANK_BYTES * 8u; ++bit) {
        int set = (bank[bit >> 3] & (1u << (bit & 7u))) != 0;

        if (bit >= MP_QUEST_FIRST_BIT && bit <= MP_QUEST_LAST_BIT) {
            if (set) {
                ++left;
            }
        } else if (!set) {
            ++spilled;
        }
    }
    ut_checkf(left == 0u, "every bit of the band is clear (%u were not)", left);
    ut_checkf(spilled == 0u,
              "and not one of the other %u bits of the bank was touched (%u were)",
              (unsigned)(BANK_BYTES * 8u - MP_QUEST_BIT_COUNT), spilled);

    ut_section("and the hero's bits either side of it by name, because those "
               "are the ones it costs");

    ut_check((bank[48u >> 3] & (1u << (48u & 7u))) != 0, "bit 48 is still the hero's");
    ut_check((bank[49u >> 3] & (1u << (49u & 7u))) != 0, "bit 49 too");
    ut_check((bank[50u >> 3] & (1u << (50u & 7u))) != 0, "and bit 50, the last one below the band");
    ut_check((bank[85u >> 3] & (1u << (85u & 7u))) != 0, "bit 85, the first one above it");
    ut_check((bank[95u >> 3] & (1u << (95u & 7u))) != 0, "and bit 95, the end of the hero window");

    ut_section("a full band spliced into an empty bank sets the band and nothing else");

    memset(bank, 0x00, sizeof bank);
    memset(&full, 0, sizeof full);
    for (bit = 0; bit < MP_QUEST_BIT_COUNT; ++bit) {
        mp_quest_put(&full, bit, true);
    }
    mp_quest_into_bank(&full, bank);

    spilled = 0;
    left    = 0;
    for (bit = 0; bit < BANK_BYTES * 8u; ++bit) {
        int set = (bank[bit >> 3] & (1u << (bit & 7u))) != 0;

        if (bit >= MP_QUEST_FIRST_BIT && bit <= MP_QUEST_LAST_BIT) {
            if (!set) {
                ++left;
            }
        } else if (set) {
            ++spilled;
        }
    }
    ut_checkf(left == 0u, "every bit of the band is set (%u were not)", left);
    ut_checkf(spilled == 0u, "and no bit outside it was set (%u were)", spilled);

    ut_section("and it reads back out as what went in");

    mp_quest_from_bank(bank, &read);
    ut_check(mp_quest_equal(&read, &full), "the band survives the round trip through a bank");
    ut_check(mp_quest_count(&read) == MP_QUEST_BIT_COUNT, "all thirty-four of it");
}

/* A bank whose band is a pattern rather than all or nothing, because an off-by-one that shifts the
 * whole band by one bit passes both of the tests above and fails this one. */
static void check_the_splice_keeps_the_order(void)
{
    static uint8_t bank[BANK_BYTES];
    mp_quest_set_t set;
    mp_quest_set_t read;
    uint32_t       i;

    ut_section("a pattern, which is what catches a band shifted by exactly one");

    memset(bank, 0, sizeof bank);
    memset(&set, 0, sizeof set);
    for (i = 0; i < MP_QUEST_BIT_COUNT; i += 3u) {
        mp_quest_put(&set, i, true);
    }
    mp_quest_into_bank(&set, bank);
    mp_quest_from_bank(bank, &read);
    ut_check(mp_quest_equal(&set, &read), "every third bit comes back as every third bit");

    /* And against the engine's own accessor, spelled out here rather than called, so that this
     * check does not agree with the code by using the same expression twice. Bit 51 is byte 6 bit
     * 3; bit 84 is byte 10 bit 4. */
    ut_check((bank[6] & 0x08u) != 0u, "bit 51 is byte 6 bit 3, and it is set");
    ut_check((bank[6] & 0x07u) == 0u, "and byte 6's low three bits, the hero's, are untouched");
    ut_check((bank[10] & 0xE0u) == 0u, "byte 10's top three bits are above the band and clear");
}

static void check_the_state_note(void)
{
    mp_quest_set_t set;
    mp_quest_set_t back;
    uint8_t        note[MP_QUEST_STATE_BYTES];
    uint16_t       level = 0;
    size_t         bytes;

    ut_section("the host's state note");

    memset(&set, 0, sizeof set);
    mp_quest_put(&set, 0u, true);
    mp_quest_put(&set, 33u, true);

    bytes = mp_quest_encode_state(&set, 0x1234u, note, sizeof note);
    ut_check(bytes == MP_QUEST_STATE_BYTES, "it is eight bytes: tag, level and five of bits");
    ut_check(mp_quest_is_state(note, bytes), "and it recognises itself");
    ut_check(mp_quest_decode_state(note, bytes, &back, &level), "and decodes");
    ut_check(mp_quest_equal(&back, &set), "to the same thirty-four bits");
    ut_check(level == 0x1234u, "carrying the level it was made in");

    ut_section("a note with anything in the six spare bits is refused");

    /* The band is a fixed width. A sender that set one of those bits has a different band, and
     * adopting its bits would move quest flags this build has never heard of. */
    note[MP_QUEST_STATE_BYTES - 1u] |= 0x80u;
    ut_check(!mp_quest_is_state(note, MP_QUEST_STATE_BYTES), "refused on the spare bits alone");
    ut_check(!mp_quest_decode_state(note, MP_QUEST_STATE_BYTES, &back, &level), "and not decoded");

    ut_section("and so is a wrong length or a wrong tag");

    bytes = mp_quest_encode_state(&set, 0u, note, sizeof note);
    ut_check(!mp_quest_is_state(note, bytes - 1u), "one byte short");
    ut_check(!mp_quest_is_state(note, bytes + 1u), "one byte long");
    note[0] = (uint8_t)(MP_QUEST_STATE_TAG + 1u);
    ut_check(!mp_quest_is_state(note, bytes), "and somebody else's tag");

    ut_section("a buffer too small produces nothing rather than half a note");

    ut_check(mp_quest_encode_state(&set, 0u, note, MP_QUEST_STATE_BYTES - 1u) == 0u,
             "no bytes written");
}

static void check_the_claim_note(void)
{
    uint8_t  note[MP_QUEST_CLAIM_BYTES];
    uint32_t index = 99u;
    uint16_t level = 0;
    bool     value = false;
    size_t   bytes;

    ut_section("a client's claim on one bit");

    bytes = mp_quest_encode_claim(7u, true, 0xBEEFu, note, sizeof note);
    ut_check(bytes == MP_QUEST_CLAIM_BYTES, "five bytes");
    ut_check(mp_quest_is_claim(note, bytes), "recognised");
    ut_check(mp_quest_decode_claim(note, bytes, &index, &value, &level), "and decoded");
    ut_check(index == 7u && value && level == 0xBEEFu, "to the bit, the value and the level");

    ut_section("an index past the band is REFUSED rather than clamped");

    /* Clamping would move the last bit of the band whenever a stranger sent 200, and the last bits
     * of the band are keys. */
    note[3] = (uint8_t)MP_QUEST_BIT_COUNT;
    ut_check(mp_quest_is_claim(note, MP_QUEST_CLAIM_BYTES), "it still looks like a claim");
    ut_check(!mp_quest_decode_claim(note, MP_QUEST_CLAIM_BYTES, &index, &value, &level),
             "and the decode refuses it");
    note[3] = 200u;
    ut_check(!mp_quest_decode_claim(note, MP_QUEST_CLAIM_BYTES, &index, &value, &level),
             "as it does a wilder one");

    ut_section("and a value that is neither nought nor one");

    bytes   = mp_quest_encode_claim(3u, false, 0u, note, sizeof note);
    note[4] = 2u;
    ut_check(!mp_quest_decode_claim(note, bytes, &index, &value, &level),
             "refused, because a bit has two states and a byte has two hundred and fifty six");

    ut_section("the two tags cannot be confused");

    ut_check(MP_QUEST_STATE_TAG != MP_QUEST_CLAIM_TAG, "they differ");
    bytes = mp_quest_encode_claim(1u, true, 0u, note, sizeof note);
    ut_check(!mp_quest_is_state(note, bytes), "a claim is not a state");
}

static void check_the_difference_walk(void)
{
    mp_quest_set_t a;
    mp_quest_set_t b;

    ut_section("what a client compares to find the change it has to claim");

    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    ut_check(mp_quest_first_difference(&a, &b, 0u) == MP_QUEST_BIT_COUNT,
             "two empty sets differ nowhere");

    mp_quest_put(&b, 5u, true);
    ut_check(mp_quest_first_difference(&a, &b, 0u) == 5u, "one bit up is found");
    ut_check(mp_quest_first_difference(&a, &b, 6u) == MP_QUEST_BIT_COUNT,
             "and searching past it finds nothing, which is how the walk terminates");

    mp_quest_put(&a, 2u, true);
    ut_check(mp_quest_first_difference(&a, &b, 0u) == 2u, "the lowest difference wins");

    ut_section("a bit going DOWN is a difference too, because a key is spent rather than gained");

    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    mp_quest_put(&a, 30u, true);
    ut_check(mp_quest_first_difference(&a, &b, 0u) == 30u,
             "the truth holds it and this side does not, which is a key that was just used");
}

int main(void)
{
    check_the_band_matches_the_engines_own_scan();
    check_one_bit_at_a_time();
    check_the_splice_does_not_spill();
    check_the_splice_keeps_the_order();
    check_the_state_note();
    check_the_claim_note();
    check_the_difference_walk();
    return ut_summary("mp_quest");
}
