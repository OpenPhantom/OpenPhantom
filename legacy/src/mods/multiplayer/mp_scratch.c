/* mp_scratch.c: the campaign bank as runs of changed bytes, and the blackboard as durations.
 *
 * The two judgements this file makes are where a run ends and what a receiver refuses. Both are
 * written out rather than tuned, because both are silent when wrong: a run that ends too eagerly
 * costs bandwidth nobody measures, and a receiver that accepts a run into the per-hero window
 * hands one player another player's keys.
 */
#include "mp_scratch.h"

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* A duration in whole milliseconds, with the two ends a caller cannot be trusted to have handled:
 * a value that is not finite fails every comparison and therefore lands on zero, and one past what
 * a u32 of milliseconds reaches is clamped rather than wrapped. Forty nine days is not a timer any
 * script sets, so the clamp is a guard and not a policy. */
static uint32_t milliseconds_of(float seconds)
{
    const float ceiling = 4294967.0f;   /* UINT32_MAX milliseconds, in seconds */

    if (!(seconds > 0.0f)) {
        return 0u;
    }
    if (seconds >= ceiling) {
        return 0xFFFFFFFFu;
    }
    return (uint32_t)(seconds * 1000.0f + 0.5f);
}

bool mp_scratch_is_per_hero(uint32_t offset)
{
    return offset >= MP_SCRATCH_HERO_FIRST &&
           offset < MP_SCRATCH_HERO_FIRST + MP_SCRATCH_HERO_BYTES;
}

/* The first byte at or after `from` that differs and is the world's rather than a hero's. Returns
 * MP_SCRATCH_BANK_BYTES when there is none. */
static uint32_t next_difference(const uint8_t *mirror, const uint8_t *live, uint32_t from)
{
    uint32_t at;

    for (at = from; at < MP_SCRATCH_BANK_BYTES; ++at) {
        if (!mp_scratch_is_per_hero(at) && mirror[at] != live[at]) {
            return at;
        }
    }
    return MP_SCRATCH_BANK_BYTES;
}

/* Where a run that began at `start` should end.
 *
 * It bridges short stretches of agreement instead of breaking, because a break costs three bytes
 * of header and a bridged byte costs one. Two changed bytes with a gap of G cost 5 + G as one run
 * and 8 as two, so one run wins while G is under three, ties at three and loses from four on; the
 * bridge width is that break even and not a taste. It stops at the per-hero window whatever else
 * is true, so that window can never end up inside a run by accident.
 *
 * Returns the length, which is at least one and at most MP_SCRATCH_RUN_MAX. */
static uint32_t run_length(const uint8_t *mirror, const uint8_t *live, uint32_t start)
{
    uint32_t length = 0;
    uint32_t agreed = 0;
    uint32_t at;

    for (at = start; at < MP_SCRATCH_BANK_BYTES && at - start < MP_SCRATCH_RUN_MAX; ++at) {
        if (mp_scratch_is_per_hero(at)) {
            break;
        }
        if (mirror[at] != live[at]) {
            length = at - start + 1u;
            agreed = 0;
        } else if (++agreed > MP_SCRATCH_RUN_BRIDGE) {
            break;
        }
    }
    return length;
}

/* How many world bytes still differ from `from` on, for the report. Counted rather than estimated
 * because a caller that sees "0 pending" and a client that is still behind would have no other way
 * to tell the two apart. */
static uint32_t pending_from(const uint8_t *mirror, const uint8_t *live, uint32_t from)
{
    uint32_t count = 0;
    uint32_t at;

    for (at = from; at < MP_SCRATCH_BANK_BYTES; ++at) {
        if (!mp_scratch_is_per_hero(at) && mirror[at] != live[at]) {
            ++count;
        }
    }
    return count;
}

bool mp_scratch_encode_bank(const uint8_t *mirror, const uint8_t *live,
                            const mp_scratch_cursor_t *cursor, uint8_t *out, size_t capacity,
                            size_t *bytes, mp_scratch_cursor_t *cursor_out)
{
    mp_wire_writer_t w;
    uint32_t         at;
    uint16_t         runs = 0;

    if (mirror == NULL || live == NULL || cursor == NULL || out == NULL || bytes == NULL ||
        cursor_out == NULL || capacity < 2u) {
        return false;
    }
    mp_wire_writer_init(&w, out, capacity);
    mp_wire_put_u16(&w, 0u);   /* the run count, written again once it is known */

    at = cursor->at < MP_SCRATCH_BANK_BYTES ? cursor->at : 0u;
    for (;;) {
        uint32_t start = next_difference(mirror, live, at);
        uint32_t length;
        uint32_t i;

        if (start >= MP_SCRATCH_BANK_BYTES) {
            at = MP_SCRATCH_BANK_BYTES;
            break;
        }
        length = run_length(mirror, live, start);
        if (w.at + MP_SCRATCH_RUN_HEADER_BYTES + length > capacity) {
            at = start;         /* the budget is spent; the next encode starts here */
            break;
        }
        mp_wire_put_u16(&w, (uint16_t)start);
        mp_wire_put_u8(&w, (uint8_t)length);
        for (i = 0; i < length; ++i) {
            mp_wire_put_u8(&w, live[start + i]);
        }
        ++runs;
        at = start + length;
    }

    if (w.overflowed) {
        return false;
    }
    {
        /* The count goes back through a writer of its own rather than through two hand placed
         * bytes. Writing it by hand meant assuming a byte order, and the assumption was wrong: the
         * decoder read every message this encoder produced as several hundred runs and refused all
         * of them. A second writer over the same two bytes cannot disagree with the reader. */
        mp_wire_writer_t head;

        mp_wire_writer_init(&head, out, 2u);
        mp_wire_put_u16(&head, runs);
    }
    cursor_out->at = at;
    /* Still counted against the OLD mirror, because nothing has been delivered yet. The commit
     * recomputes it once the channel has taken the bytes. */
    cursor_out->pending = pending_from(mirror, live, 0u);
    *bytes = w.at;
    return true;
}

void mp_scratch_commit_bank(uint8_t *mirror, const uint8_t *live, const mp_scratch_cursor_t *from,
                            mp_scratch_cursor_t *to)
{
    uint32_t at;
    uint32_t first;
    uint32_t last;

    if (mirror == NULL || live == NULL || from == NULL || to == NULL) {
        return;
    }
    first = from->at < MP_SCRATCH_BANK_BYTES ? from->at : 0u;
    last  = to->at < MP_SCRATCH_BANK_BYTES ? to->at : MP_SCRATCH_BANK_BYTES;
    for (at = first; at < last; ++at) {
        if (!mp_scratch_is_per_hero(at)) {
            mirror[at] = live[at];
        }
    }
    to->pending = pending_from(mirror, live, 0u);
}

/* Walks the runs without touching the bank, and answers whether every one of them is usable.
 *
 * It exists because "refused whole, never applied in part" is a promise a single pass cannot keep:
 * a decoder that writes as it reads has already changed the bank by the time it meets the run that
 * is wrong, and a half applied campaign is worse than a rejected message, because nothing says it
 * happened. */
static bool runs_are_sound(const uint8_t *buffer, size_t bytes, uint16_t *runs_out)
{
    mp_wire_reader_t r;
    uint16_t         runs = 0;
    uint16_t         run;

    mp_wire_reader_init(&r, buffer, bytes);
    if (!mp_wire_get_u16(&r, &runs)) {
        return false;
    }
    for (run = 0; run < runs; ++run) {
        uint16_t start = 0;
        uint8_t  length = 0;
        uint32_t i;

        if (!mp_wire_get_u16(&r, &start) || !mp_wire_get_u8(&r, &length)) {
            return false;
        }
        if (length == 0u || (uint32_t)start + length > MP_SCRATCH_BANK_BYTES) {
            return false;   /* past the end of the bank */
        }
        for (i = 0; i < length; ++i) {
            uint8_t value = 0;

            if (mp_scratch_is_per_hero((uint32_t)start + i)) {
                /* No sender of this build ever writes these, so a message that names them is
                 * either a different build or a forgery. Either way the bytes behind it are a
                 * player's inventory and keys, and they are not this message's to set. */
                return false;
            }
            if (!mp_wire_get_u8(&r, &value)) {
                return false;
            }
        }
    }
    if (runs_out != NULL) {
        *runs_out = runs;
    }
    return !r.overran;
}

bool mp_scratch_decode_bank(uint8_t *bank, const uint8_t *buffer, size_t bytes, size_t *written)
{
    mp_wire_reader_t r;
    uint16_t         runs = 0;
    uint16_t         run;
    size_t           changed = 0;

    if (bank == NULL || buffer == NULL || written == NULL) {
        return false;
    }
    if (!runs_are_sound(buffer, bytes, &runs)) {
        return false;
    }
    /* The second pass cannot fail: the first one has already read every byte it will read. */
    mp_wire_reader_init(&r, buffer, bytes);
    (void)mp_wire_get_u16(&r, &runs);
    for (run = 0; run < runs; ++run) {
        uint16_t start = 0;
        uint8_t  length = 0;
        uint32_t i;

        (void)mp_wire_get_u16(&r, &start);
        (void)mp_wire_get_u8(&r, &length);
        for (i = 0; i < length; ++i) {
            uint8_t value = 0;

            (void)mp_wire_get_u8(&r, &value);
            if (bank[start + i] != value) {
                ++changed;
            }
            bank[start + i] = value;
        }
    }
    *written = changed;
    return true;
}

float mp_scratch_expiry_at(float remaining, float now)
{
    /* Zero is the engine's own "no timer" and has to survive the round trip as zero rather than
     * becoming "expires exactly now", which would fire on the first comparison. */
    return remaining <= 0.0f ? 0.0f : now + remaining;
}

size_t mp_scratch_encode_ai(const int32_t *flag, const int32_t *previous, const float *expiry,
                            float now, uint8_t *out, size_t capacity)
{
    mp_wire_writer_t w;
    size_t           i;

    if (flag == NULL || previous == NULL || expiry == NULL || out == NULL) {
        return 0u;
    }
    mp_wire_writer_init(&w, out, capacity);
    for (i = 0; i < MP_SCRATCH_AI_SLOTS; ++i) {
        mp_wire_put_u32(&w, (uint32_t)flag[i]);
    }
    for (i = 0; i < MP_SCRATCH_AI_SLOTS; ++i) {
        mp_wire_put_u32(&w, (uint32_t)previous[i]);
    }
    for (i = 0; i < MP_SCRATCH_AI_SLOTS; ++i) {
        /* An absolute time turned into a duration, and the duration into whole milliseconds.
         *
         * Milliseconds rather than a fixed point over some range, because no census of these
         * timers exists and a range picked without one is a wrap waiting to happen; a u32 of
         * milliseconds reaches forty nine days and needs no such guess. A millisecond is also far
         * finer than the substep the receiver will compare it against, so nothing is lost.
         *
         * A timer that has already run out travels as zero rather than as a negative, because
         * zero is what the engine means by "none" and a receiver would otherwise be handed a
         * moment in its own past. */
        float remaining = expiry[i] <= 0.0f ? 0.0f : expiry[i] - now;

        mp_wire_put_u32(&w, milliseconds_of(remaining));
    }
    return w.overflowed ? 0u : w.at;
}

bool mp_scratch_decode_ai(const uint8_t *buffer, size_t bytes, mp_scratch_ai_t *out)
{
    mp_wire_reader_t r;
    size_t           i;

    if (buffer == NULL || out == NULL || bytes != MP_SCRATCH_AI_BYTES) {
        return false;
    }
    memset(out, 0, sizeof *out);
    mp_wire_reader_init(&r, buffer, bytes);
    for (i = 0; i < MP_SCRATCH_AI_SLOTS; ++i) {
        uint32_t value = 0;

        if (!mp_wire_get_u32(&r, &value)) {
            return false;
        }
        out->flag[i] = (int32_t)value;
    }
    for (i = 0; i < MP_SCRATCH_AI_SLOTS; ++i) {
        uint32_t value = 0;

        if (!mp_wire_get_u32(&r, &value)) {
            return false;
        }
        out->previous[i] = (int32_t)value;
    }
    for (i = 0; i < MP_SCRATCH_AI_SLOTS; ++i) {
        uint32_t ms = 0;

        if (!mp_wire_get_u32(&r, &ms)) {
            return false;
        }
        out->remaining[i] = (float)ms / 1000.0f;
    }
    return !r.overran;
}
