/* mp_range_gate_rule.h: which player an enemy's range is measured against.
 *
 * Layer 1, pure. The engine has ONE distance test, `within_range` at 0x00428eb3, and exactly two
 * callers: the activation scan decides with it what wakes, and the entity loop decides with it
 * what is removed again. Both hand it the LOCAL player's body position, and in a session that is
 * the wrong question: two players who walk apart hold different worlds, so an enemy standing next
 * to the far player is out of range here and the host takes it away under him.
 *
 * The question this answers is "is any player within the radius", and it is the same question for
 * both gates, which is why one rule serves both.
 *
 * Why the summation order is fixed: the engine sums z, then y, then x, and float addition is not
 * associative: another order can put a body exactly on its own radius on the other side of the
 * comparison. The local player's answer never goes through this file at all, it stays the engine's
 * own call, so only the far players are judged here. They are still judged in the engine's order,
 * because a far player and the local one standing in the same spot must answer the same.
 *
 * The comparison is strictly less than, as the engine's is, so a body exactly on the radius is
 * out. A radius of zero therefore keeps nothing, which is also what the engine reads it as.
 *
 * The caller census that decides whether the hull may go on at all is here too. It searches a
 * buffer it is handed and reads nothing else, because the census that read its own bytes did so
 * one five byte window at a time, one open of the executable per byte, and stood the host's menu
 * still for eleven seconds.
 */
#ifndef MULTIPLAYER_MP_RANGE_GATE_RULE_H
#define MULTIPLAYER_MP_RANGE_GATE_RULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The players this side can measure against, filled once per pass and read many times. `have`
 * says whether that row carries a position at all: a bank with no body, a player still loading
 * or a slot nobody sits in has none, and a row without a position is skipped rather than read
 * as the origin. */
typedef struct mp_range_gate_players {
    float positions[4][3];
    bool  have[4];
    size_t count;            /* how many rows of `positions` and `have` are in use */
} mp_range_gate_players_t;

/* The engine's own test, in the engine's own order. Exposed because the hull needs to ask it of
 * a far player with exactly the arithmetic the engine would have used for the local one. */
bool mp_range_gate_within(const float at[3], const float player[3], float radius);

/* True when ANY player in `players` is within `radius` of `at`. False with nothing to measure,
 * which leaves the engine's own answer standing. */
bool mp_range_gate_any_within(const mp_range_gate_players_t *players, const float at[3],
                              float radius);

/* A near call is E8 followed by a signed displacement from the byte after it. */
#define MP_RANGE_GATE_CALL_OPCODE 0xE8u
#define MP_RANGE_GATE_CALL_BYTES  5u

/* How many near calls in a code section land on `target`. `code` is the section as the file
 * carries it, `code_va` the LIVE address of its first byte and `target` a live address too: a file
 * address measured against a live one finds nothing on a rebased image, and with the image at its
 * preferred base nobody would see it. Every offset is a candidate, overlapping ones included, as
 * the old window loop had it.
 *
 * The count goes on past `max_returns`, so a third caller answers 3 and never 2. The address AFTER
 * each call goes into `returns`, in ascending order, because that is what the hull's
 * _ReturnAddress reads and the order is what names the two callers in the log. The sum wraps in 32
 * bits, where the old signed one could overflow. */
size_t mp_range_gate_count_callers(const uint8_t *code, size_t size, uint32_t code_va,
                                   uint32_t target, uint32_t *returns, size_t max_returns);

#endif /* MULTIPLAYER_MP_RANGE_GATE_RULE_H */
