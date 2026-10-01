/* mp_quest.c: the codec for the thirty-four shared story bits, and the splice into the bank.
 *
 * The header carries the reasoning. What is here is the arithmetic, and there is only one piece of
 * it worth reading twice: the band does not start on a byte boundary and must not be rounded to
 * one, because the bits either side of it belong to the hero.
 */
#include "mp_quest.h"

#include <string.h>

/* The living bank's length, repeated here rather than included from mp_scratch.h so that this file
 * stays a codec with no opinion about the module next door. The bound is only ever used to refuse
 * a splice that would run off the end, and 1250 is byte-proven in both places. */
#define QUEST_BANK_BYTES 1250u

/* The bits above the band inside the last packed byte. Thirty-four bits fill four bytes and two
 * bits, so six bits of the fifth are spare and are required to be zero. Its complement is written
 * out rather than formed with ~, which on a promoted constant sets twenty-four bits nobody wants
 * and costs a truncation warning to say so. */
#define QUEST_SPARE_MASK 0xFCu   /* bits 2..7 of buffer byte 4 */
#define QUEST_BAND_MASK  0x03u   /* and the two that are the band's */

/* The band has to fit the bank, and it does with room to spare: bit 84 is byte 10 of 1250. It is
 * asserted rather than tested at run time, because a band that outgrew the bank is a mistake in
 * this file and not a state a running game can reach. */
_Static_assert((MP_QUEST_LAST_BIT >> 3) < QUEST_BANK_BYTES,
               "the quest band runs past the end of the campaign bank");
_Static_assert(MP_QUEST_BYTES * 8u >= MP_QUEST_BIT_COUNT,
               "the packed set is too small for the band it carries");

bool mp_quest_get(const mp_quest_set_t *set, uint32_t index)
{
    if (set == NULL || index >= MP_QUEST_BIT_COUNT) {
        return false;
    }
    return (set->bit[index >> 3] & (uint8_t)(1u << (index & 7u))) != 0u;
}

void mp_quest_put(mp_quest_set_t *set, uint32_t index, bool value)
{
    if (set == NULL || index >= MP_QUEST_BIT_COUNT) {
        return;
    }
    if (value) {
        set->bit[index >> 3] |= (uint8_t)(1u << (index & 7u));
    } else {
        set->bit[index >> 3] &= (uint8_t)~(1u << (index & 7u));
    }
}

bool mp_quest_equal(const mp_quest_set_t *a, const mp_quest_set_t *b)
{
    return a != NULL && b != NULL && memcmp(a->bit, b->bit, sizeof a->bit) == 0;
}

uint32_t mp_quest_count(const mp_quest_set_t *set)
{
    uint32_t index;
    uint32_t count = 0u;

    for (index = 0u; index < MP_QUEST_BIT_COUNT; ++index) {
        if (mp_quest_get(set, index)) {
            ++count;
        }
    }
    return count;
}

uint32_t mp_quest_first_difference(const mp_quest_set_t *a, const mp_quest_set_t *b, uint32_t from)
{
    uint32_t index;

    if (a == NULL || b == NULL) {
        return MP_QUEST_BIT_COUNT;
    }
    for (index = from; index < MP_QUEST_BIT_COUNT; ++index) {
        if (mp_quest_get(a, index) != mp_quest_get(b, index)) {
            return index;
        }
    }
    return MP_QUEST_BIT_COUNT;
}

/* ==============================================================================================
 * The bank.
 *
 * The engine's own accessor is `g_storyFlags[bit >> 3] & (1 << (bit & 7))`, so a bank bit and a
 * packed bit are the same shape and differ only by the 0x33 the band starts at. Doing it one bit
 * at a time rather than with a shifted memcpy is deliberate: a shift across a byte boundary is
 * where an off-by-one in this would live, and an off-by-one here silently gives one player another
 * player's key, taking bit 50 from the hero and handing them bit 85, with nothing in the engine or
 * a field log to say so. The unit test pins it from the other side: a bank with every one of its
 * 10000 bits set, spliced with an empty band, and then every bit outside the band counted, so a one
 * bit slip fails it and so does a band that rounds to a byte.
 * ============================================================================================ */

void mp_quest_from_bank(const uint8_t *bank, mp_quest_set_t *out)
{
    uint32_t index;

    if (bank == NULL || out == NULL) {
        return;
    }
    memset(out, 0, sizeof *out);
    for (index = 0u; index < MP_QUEST_BIT_COUNT; ++index) {
        uint32_t bit = MP_QUEST_FIRST_BIT + index;

        if ((bank[bit >> 3] & (uint8_t)(1u << (bit & 7u))) != 0u) {
            mp_quest_put(out, index, true);
        }
    }
}

void mp_quest_into_bank(const mp_quest_set_t *set, uint8_t *bank)
{
    uint32_t index;

    if (set == NULL || bank == NULL) {
        return;
    }
    for (index = 0u; index < MP_QUEST_BIT_COUNT; ++index) {
        uint32_t bit  = MP_QUEST_FIRST_BIT + index;
        uint8_t  mask = (uint8_t)(1u << (bit & 7u));

        if (mp_quest_get(set, index)) {
            bank[bit >> 3] |= mask;
        } else {
            bank[bit >> 3] &= (uint8_t)~mask;
        }
    }
}

/* ==============================================================================================
 * The state note: all thirty-four bits, absolute, from the side that owns the story.
 * ============================================================================================ */

size_t mp_quest_encode_state(const mp_quest_set_t *set, uint16_t level, uint8_t *buffer,
                             size_t capacity)
{
    if (set == NULL || buffer == NULL || capacity < MP_QUEST_STATE_BYTES) {
        return 0u;
    }
    buffer[0] = (uint8_t)MP_QUEST_STATE_TAG;
    buffer[1] = (uint8_t)(level & 0xFFu);
    buffer[2] = (uint8_t)((level >> 8) & 0xFFu);
    memcpy(buffer + 3, set->bit, MP_QUEST_BYTES);
    /* The spare bits go out as zero, so that the recogniser below can require it. */
    buffer[3 + MP_QUEST_BYTES - 1u] &= (uint8_t)QUEST_BAND_MASK;
    return MP_QUEST_STATE_BYTES;
}

bool mp_quest_is_state(const uint8_t *buffer, size_t bytes)
{
    /* Tag, exact length AND the spare bits, all three. The band is a fixed width, so a note that
     * carries anything in the six bits above it was written by a build whose band is not this
     * one, and adopting its bits would move quest flags this build has never heard of. */
    return buffer != NULL && bytes == MP_QUEST_STATE_BYTES &&
           buffer[0] == (uint8_t)MP_QUEST_STATE_TAG &&
           (buffer[3 + MP_QUEST_BYTES - 1u] & QUEST_SPARE_MASK) == 0u;
}

bool mp_quest_decode_state(const uint8_t *buffer, size_t bytes, mp_quest_set_t *out,
                           uint16_t *level)
{
    if (out == NULL || !mp_quest_is_state(buffer, bytes)) {
        return false;
    }
    memset(out, 0, sizeof *out);
    memcpy(out->bit, buffer + 3, MP_QUEST_BYTES);
    if (level != NULL) {
        *level = (uint16_t)((uint16_t)buffer[1] | ((uint16_t)buffer[2] << 8));
    }
    return true;
}

/* ==============================================================================================
 * The claim: one bit a client watched move, offered to the side that decides.
 * ============================================================================================ */

size_t mp_quest_encode_claim(uint32_t index, bool value, uint16_t level, uint8_t *buffer,
                             size_t capacity)
{
    if (buffer == NULL || capacity < MP_QUEST_CLAIM_BYTES || index >= MP_QUEST_BIT_COUNT) {
        return 0u;
    }
    buffer[0] = (uint8_t)MP_QUEST_CLAIM_TAG;
    buffer[1] = (uint8_t)(level & 0xFFu);
    buffer[2] = (uint8_t)((level >> 8) & 0xFFu);
    buffer[3] = (uint8_t)index;
    buffer[4] = value ? 1u : 0u;
    return MP_QUEST_CLAIM_BYTES;
}

bool mp_quest_is_claim(const uint8_t *buffer, size_t bytes)
{
    return buffer != NULL && bytes == MP_QUEST_CLAIM_BYTES &&
           buffer[0] == (uint8_t)MP_QUEST_CLAIM_TAG;
}

bool mp_quest_decode_claim(const uint8_t *buffer, size_t bytes, uint32_t *index, bool *value,
                           uint16_t *level)
{
    if (!mp_quest_is_claim(buffer, bytes)) {
        return false;
    }
    /* An index past the band is refused rather than clamped. Clamping would move bit 33 whenever a
     * stranger sent 200, and bit 33 is a key. */
    if (buffer[3] >= MP_QUEST_BIT_COUNT || buffer[4] > 1u) {
        return false;
    }
    if (index != NULL) {
        *index = buffer[3];
    }
    if (value != NULL) {
        *value = buffer[4] != 0u;
    }
    if (level != NULL) {
        *level = (uint16_t)((uint16_t)buffer[1] | ((uint16_t)buffer[2] << 8));
    }
    return true;
}
