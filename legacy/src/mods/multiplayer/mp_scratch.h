/* mp_scratch.h: the world's scratchpad on the wire, which is the campaign rather than the scenery.
 *
 * Layer 1, pure logic. No engine, no address, no socket.
 *
 * ================================= What the scratchpad IS =====================================
 *
 * The engine says itself what it considers persistent, because it writes exactly that into its
 * savegame block. Two things in it outlive a level and are therefore the campaign:
 *
 *   `g_storyFlags`, 1250 bytes and exactly 10000 bits, the living campaign memory. Cutscene
 *   gates, progress, inventory and keys are all bits in here, and the level scripts touch it 1661
 *   times across the eleven shipped levels.
 *
 *   `g_aiFlag[4]` with its previous values and its expiry times, the blackboard the scripts
 *   compare against.
 *
 * When the host owns the level, only the host runs the scripts, so only the host sets these. A
 * client that does not receive them ends the evening in a different campaign from the player
 * sitting next to it: its doors stay locked, its cutscenes do not fire, and its inventory is
 * whatever it was at the join.
 *
 * ============================ Two things that deliberately do NOT travel ======================
 *
 * **The per-hero window.** Bytes 6 to 11 of the story bank, bits 48 to 95, are NOT global even
 * though they sit in the middle of the global bank: the hero swap copies them out to the outgoing
 * hero's status record and copies the incoming hero's back in. The 34 inventory bits and the seven
 * keys live inside that window. A blanket transfer of the bank would therefore hand one player the
 * other's keys and take away their own, which is why this module refuses to encode those bytes at
 * all rather than trusting a caller to remember.
 *
 * That exclusion is not a special case, it is the authority model applied: the window belongs to a
 * PLAYER, and a player's own state is owned by the machine that player sits at. It travels with
 * that player's body, not with the level.
 *
 * **The checkpoint bank.** `g_storyFlagsCheckpoint` is a copy of the living bank taken when a
 * level begins. If the living bank agrees at that moment, both machines take the same copy, so
 * sending it would be sending something the receiver can produce for itself. Twelve hundred and
 * fifty bytes saved by an argument rather than by a compression. The condition is real: it holds
 * only while the living bank is in step at level begin, and a join in the middle of a level that
 * then rolls back to its checkpoint would find the client's checkpoint older than the host's.
 *
 * ==================================== Why a run delta =========================================
 *
 * Two banks are 2500 bytes against a packet budget of 1187, so a whole bank never fits and a
 * periodic full copy is not an option. What the bank actually does is change a handful of bits at
 * a time, when a script fires. So the wire carries RUNS of changed bytes, and an encode that
 * cannot fit them all says where to resume: a join, where everything differs, is then a sweep
 * across packets that finishes on its own, and steady play is a few bytes. Measured in the test,
 * a bank differing in every byte converges in five packets and 1272 bytes at a budget of 300,
 * against 1244 world bytes, so the headers are about two per cent and not the message.
 */
#ifndef MULTIPLAYER_MP_SCRATCH_H
#define MULTIPLAYER_MP_SCRATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The living bank. 1250 is byte-proven rather than assumed: the clear at `0x43338c` zeroes 0x138
 * dwords PLUS one word, which is 0x4e2, and the copies at 0x4333a5 (into the checkpoint) and
 * 0x4333c3 (back out of it) use the same loop count. 1250 * 8 is the 10000 bits the scripts
 * index, 1661 times across the eleven shipped levels: 357 sets, 71 clears, 945 tests and 288
 * set-or-clears. */
#define MP_SCRATCH_BANK_BYTES 1250u

/* The per-hero window inside it: bytes 6 to 11, bits 48 to 95. The hero swap at 0x00459AB7 does
 * two six-byte copies against `playerStatus+0x44`, one out at 0x459b03 and one in at 0x459bdc.
 * Inside the window sit the 34 inventory bits the pause menu lists and the seven keys at bits
 * 0x4b to 0x51, which the locked button at 0x0044C9A0 forms as key + 0x4a, tests and clears. */
#define MP_SCRATCH_HERO_FIRST 6u
#define MP_SCRATCH_HERO_BYTES 6u

/* How many blackboard slots the engine has. Four, and every one of the three registers is four
 * entries wide. */
#define MP_SCRATCH_AI_SLOTS 4u

/* One run of changed bytes: where it starts and how long it is. A length is a byte, so a change
 * longer than 255 becomes several runs, which costs three bytes per 255 and keeps the header
 * small for the case that actually happens, a single bit flipping. */
#define MP_SCRATCH_RUN_HEADER_BYTES 3u

/* The most a single run can carry. */
#define MP_SCRATCH_RUN_MAX 255u

/* How many unchanged bytes a run may swallow rather than breaking.
 *
 * The number is the arithmetic and not a taste. Two changed bytes with a gap of G between them
 * cost 3 + (G + 2) as one run and 3 + 1 twice as two, so one run wins while 5 + G < 8, ties at
 * G = 3 and loses from G = 4 on. Three is therefore the largest gap that is not a loss, and the
 * tie goes to the single run because it is also one header fewer for the decoder to walk.
 *
 * An earlier value of four was a byte worse at exactly G = 4, and the comment that justified it
 * did not compute its own example. */
#define MP_SCRATCH_RUN_BRIDGE 3u

/* Where an encode stopped, so the next one continues rather than starting over. A cursor of
 * MP_SCRATCH_BANK_BYTES or more means there is nothing left. */
typedef struct mp_scratch_cursor {
    uint32_t at;        /* the first byte the next encode should look at */
    uint32_t pending;   /* how many bytes still differ behind it, for the report */
} mp_scratch_cursor_t;

/* The blackboard as it travels. The expiry is the one field that cannot be copied: the engine
 * holds it as an ABSOLUTE world time in a float (every writer and reader of the cell treats it as
 * one, whatever an earlier reconstruction typed it as), and two machines do not share a world
 * clock, so it travels as the time REMAINING and the receiver adds its own. Zero means no timer,
 * which is the engine's own meaning for it and survives the conversion unchanged. */
typedef struct mp_scratch_ai {
    int32_t  flag[MP_SCRATCH_AI_SLOTS];
    int32_t  previous[MP_SCRATCH_AI_SLOTS];
    float    remaining[MP_SCRATCH_AI_SLOTS];   /* seconds from now, never an absolute time */
} mp_scratch_ai_t;

/* The duration travels as whole milliseconds in a u32. Milliseconds rather than a fixed point over
 * some range, because no census of these timers exists and a range chosen without one is a wrap
 * waiting to happen; a u32 of milliseconds reaches forty nine days. */

#define MP_SCRATCH_AI_BYTES (MP_SCRATCH_AI_SLOTS * (4u + 4u + 4u))

/* True when this byte of the bank belongs to a hero rather than to the world. The encoder asks it
 * for every byte; a caller never has to remember the window. */
bool mp_scratch_is_per_hero(uint32_t offset);

/* Encodes the difference between `mirror` (what the far side is believed to hold) and `live` into
 * runs, starting at `cursor->at` and stopping when the budget is spent or the bank ends.
 *
 * It does not touch the mirror, and that separation is the whole point. An earlier version moved
 * the mirror as it wrote, which quietly meant "encoded" and "delivered" were the same thing. They
 * are not: the reliable channel refuses a message when its send queue is full, and a caller that
 * had already advanced its mirror would then believe the far side holds bytes that were never
 * sent. Nothing would ever correct it, because the difference it would have to notice is exactly
 * the one it just forgot, and `pending` would read zero.
 *
 * So the caller encodes, hands the bytes to the channel, and calls the commit below ONLY when the
 * channel took them. `cursor_out` receives where the next encode should start; pass it to the
 * commit as well.
 *
 * Returns false only when the arguments are unusable. An encode with nothing to say writes a zero
 * run count, which is two bytes, and the caller may drop it. */
bool mp_scratch_encode_bank(const uint8_t *mirror, const uint8_t *live,
                            const mp_scratch_cursor_t *cursor, uint8_t *out, size_t capacity,
                            size_t *bytes, mp_scratch_cursor_t *cursor_out);

/* Moves the mirror over the range an accepted encode covered, and nothing else.
 *
 * Every world byte between the two cursors is either one the encode wrote or one that already
 * agreed, so making the mirror equal the live bank across that range is exactly what was sent and
 * never more. The per-hero window is skipped here as it is everywhere else.
 *
 * `pending` in `cursor_out` is recomputed here rather than at encode time, because before the
 * commit the honest answer still counts the bytes that have not been delivered. */
void mp_scratch_commit_bank(uint8_t *mirror, const uint8_t *live, const mp_scratch_cursor_t *from,
                            mp_scratch_cursor_t *to);

/* Applies runs to `bank`. Refuses the whole message rather than applying what it can when a run
 * runs past the end of the bank or past the end of the buffer, and refuses one that would touch
 * the per-hero window, because a message that names those bytes did not come from this build.
 *
 * `written` receives how many bytes of the bank were changed, so a receiver can say in its report
 * whether the wire is actually carrying campaign progress. */
bool mp_scratch_decode_bank(uint8_t *bank, const uint8_t *buffer, size_t bytes, size_t *written);

/* The blackboard, three registers of four. `now` is the sender's world time on encode and the
 * receiver's on decode, and it is the only reason this pair exists rather than a memcpy. */
size_t mp_scratch_encode_ai(const int32_t *flag, const int32_t *previous, const float *expiry,
                            float now, uint8_t *out, size_t capacity);
bool   mp_scratch_decode_ai(const uint8_t *buffer, size_t bytes, mp_scratch_ai_t *out);

/* The absolute expiry a receiver should write, from the remaining time it was handed. Kept here so
 * that the conversion exists once rather than at every call site: doing it wrong produces timers
 * that expire immediately or never, and neither looks like a clock error. */
float mp_scratch_expiry_at(float remaining, float now);

#endif /* MULTIPLAYER_MP_SCRATCH_H */
