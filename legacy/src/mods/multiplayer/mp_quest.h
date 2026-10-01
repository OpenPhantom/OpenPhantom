/* mp_quest.h: the thirty-four story bits every player shares, on the wire.
 *
 * Layer 1, pure logic. No engine, no address, no socket.
 *
 * ================================ The story this breaks today =================================
 *
 * The campaign bank travels already (mp_scratch), and it deliberately leaves out bytes 6 to 11,
 * bits 48 to 95: that is the PER-HERO WINDOW, which `status_setActivePlayer 0x00459AB7` copies out
 * to the outgoing hero's status record and back in for the incoming one. Sending it wholesale
 * would hand one player the other's keys and take away their own, so mp_scratch refuses those
 * bytes in its encoder AND again in its writer.
 *
 * The quest items live inside that window. `pausemenu_run 0x00442E2F` builds the item strip by
 * walking 0x22 bits from bit 0x33, which is bits 51 to 84, and the ten key models are the tail
 * of the same table. So today a quest item the CLIENT picks up lands in the client's own hero
 * window, never travels, and the host never learns of it.
 *
 * That is not a theoretical divergence. In ESPA, Anakin's closing dialogue tests the hyperdrive
 * parts as flag bits 51 to 56. If the client is the one who collects a part, the HOST's Anakin
 * refuses the conversation for the rest of the evening and the level cannot be finished by
 * anybody.
 *
 * ==================================== Why a note of its own ===================================
 *
 * The obvious repair is to widen mp_scratch's exclusion, and it cannot be done: that encoder works
 * in BYTES, and bits 51 to 84 begin and end in the middle of one. Byte 6 carries bits 48 to 50,
 * which are the hero's, alongside 51 to 55, which are the story's. There is no byte-granular rule
 * that separates them.
 *
 * So this is a separate note with its own tag, carrying the thirty-four bits packed on their own,
 * and mp_scratch is not touched at all. Its proven byte-run encoder keeps refusing the whole
 * window, exactly as it does today, and the two mechanisms cannot disagree about a byte because
 * only one of them ever writes these bits.
 *
 * ================================== The authority, stated once ================================
 *
 * A quest bit is STORY, not loot. The host owns the level, so the host owns the story:
 *
 *   * the host repeats all thirty-four bits, absolute, as the truth;
 *   * a client applies them over its own, whole, and holds no opinion of its own;
 *   * a client that sees one of its own bits move CLAIMS the change, and the host decides.
 *
 * Absolute and repeated rather than incremental, for the same reason the pickup list is: a client
 * that joins in the tenth minute is told the whole truth in its first note, and a lost packet
 * costs a second rather than a quest item.
 *
 * The keys are in here and that is a decision, not an oversight. Bits 75 to 81 are the seven
 * keys (key k is bit k + 0x4A, k from 1 to 7; bit 74 is the last quest item), and `0x0044C9A0`
 * CONSUMES one when a locked button is used. Sharing them means whoever
 * fetches a key opens the door for both, and whoever uses it spends it for both. The alternative
 * was leaving them per-hero, which costs that exactly the player who fetched the key has to be the
 * one standing at the door. There is one gated button in the whole game (ASSAULT, k=6, flag 80),
 * so the price of sharing is small and the price of not sharing is a stuck pair.
 *
 * SIZE NOTE: under 200 lines, no seam.
 */
#ifndef MULTIPLAYER_MP_QUEST_H
#define MULTIPLAYER_MP_QUEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The band, byte-proven from the pause menu's own item scan: 0x22 bits from bit 0x33, so 51 to 84
 * inclusive. Not rounded to a byte anywhere, because the bits either side of it belong to the
 * hero and this file's whole point is that the two are different things. The scan, as the
 * decompiler reads it out of 0x00442E2F:
 *
 *     for (i = 0; i < 0x22; i++)
 *         if (g_storyFlags[(i + 0x33) >> 3] & (1 << ((i + 0x33) & 7)))
 *             g_wPause[23 + nSlots++].state = i;
 *
 * and `i` is also the index into the 34 entry item model table at 0x004B0758: the power coil,
 * the servo and the rest of the pwr models, then the ten key models. A quest item and a key are
 * the same kind of thing to the engine, which is why they are one band here. Byte 6 carries bits
 * 48 to 50, the hero's, beside 51 to 55, the story's; byte 10 carries 80 to 84, the story's,
 * beside 85 to 87, which are not. The whole feature is eight bytes once a second on a host that
 * changes nothing, five of them the story. */
#define MP_QUEST_FIRST_BIT 0x33u
#define MP_QUEST_BIT_COUNT 0x22u
#define MP_QUEST_LAST_BIT  (MP_QUEST_FIRST_BIT + MP_QUEST_BIT_COUNT - 1u)

/* Packed for the wire: thirty-four bits is five bytes with six spare, and the spare bits are
 * written as zero and required to BE zero on the way in, so a sender that grows the band without
 * reading this file is refused rather than half understood. */
#define MP_QUEST_BYTES 5u

/* The tags. 0x9A and 0x9B were the next free ones when these were added. Which tags are spent is
 * the lobby rule's table to say (unittests/mp_lobby_note_rule.c), not a comment's: a comment that
 * named the next free tag was wrong three times running. */
#define MP_QUEST_STATE_TAG 0x9Au
#define MP_QUEST_CLAIM_TAG 0x9Bu

/* tag, level (2), the packed bits. The level rides along for the same reason it does in every
 * other note here: a note that crosses a level load describes a bank that no longer exists. */
#define MP_QUEST_STATE_BYTES (3u + MP_QUEST_BYTES)

/* tag, level (2), which bit (1), what it became (1). One bit per claim, because a claim is a
 * change a player MADE and changes arrive one at a time; a client with two of them sends two. */
#define MP_QUEST_CLAIM_BYTES 5u

/* The thirty-four bits, packed low bit first, which is the engine's own order:
 * `story_testBit` at 0x00434A60 is `g_storyFlags[bit >> 3] & (1 << (bit & 7))`. */
typedef struct mp_quest_set {
    uint8_t bit[MP_QUEST_BYTES];
} mp_quest_set_t;

/* One bit of the set, indexed 0 to 33 rather than 51 to 84. The offset belongs to the bank and is
 * applied where the bank is, so that no caller outside this file ever adds 0x33 to anything. */
bool mp_quest_get(const mp_quest_set_t *set, uint32_t index);
void mp_quest_put(mp_quest_set_t *set, uint32_t index, bool value);
bool mp_quest_equal(const mp_quest_set_t *a, const mp_quest_set_t *b);

/* How many of the thirty-four are set, for the log line. A number that is a story's worth of
 * progress reads better in a report than a hex blob, and it is the one figure a player can check
 * against the item strip in their own pause menu. */
uint32_t mp_quest_count(const mp_quest_set_t *set);

/* The first index at or after `from` where the two disagree, or MP_QUEST_BIT_COUNT for none. This
 * is how a client finds the change it has to claim, and how the report says what moved. */
uint32_t mp_quest_first_difference(const mp_quest_set_t *a, const mp_quest_set_t *b,
                                   uint32_t from);

/* ============================================================================================
 * The bank. These two are the ONLY functions in the tree that address the per-hero window on
 * purpose, and they touch exactly thirty-four bits of it.
 * ========================================================================================== */

/* Reads the band out of a 1250 byte copy of the living bank. */
void mp_quest_from_bank(const uint8_t *bank, mp_quest_set_t *out);

/* Writes the band back into a 1250 byte copy, leaving every other bit of it, including bits 48 to
 * 50 and 85 to 95, which are the hero's and stay the hero's, exactly as it found them. */
void mp_quest_into_bank(const mp_quest_set_t *set, uint8_t *bank);

/* ============================================================================================
 * The two notes.
 * ========================================================================================== */

size_t mp_quest_encode_state(const mp_quest_set_t *set, uint16_t level, uint8_t *buffer,
                             size_t capacity);
bool   mp_quest_is_state(const uint8_t *buffer, size_t bytes);
bool   mp_quest_decode_state(const uint8_t *buffer, size_t bytes, mp_quest_set_t *out,
                             uint16_t *level);

size_t mp_quest_encode_claim(uint32_t index, bool value, uint16_t level, uint8_t *buffer,
                             size_t capacity);
bool   mp_quest_is_claim(const uint8_t *buffer, size_t bytes);
bool   mp_quest_decode_claim(const uint8_t *buffer, size_t bytes, uint32_t *index, bool *value,
                             uint16_t *level);

#endif /* MULTIPLAYER_MP_QUEST_H */
