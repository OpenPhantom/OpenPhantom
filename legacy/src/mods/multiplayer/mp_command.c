/* mp_command.c: the command stream and the sink that de-duplicates it.
 *
 * A packet is a count and then that many command records, newest first, each a tick, a turn and
 * move axis through mp_wire's quantisation, and a button word. The sink keeps the last applied tick
 * per client and, fed a decoded packet, walks its commands oldest first and applies only the ones
 * past that mark, so redundant repeats and out-of-order arrivals never replay an input.
 */
#include "mp_command.h"

#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool mp_command_encode(const mp_command_t *commands, size_t count, uint8_t *buffer, size_t capacity,
                       size_t *bytes)
{
    mp_wire_writer_t w;
    size_t           index;

    if (count > MP_COMMAND_REDUNDANCY) {
        count = MP_COMMAND_REDUNDANCY;
    }

    mp_wire_writer_init(&w, buffer, capacity);
    mp_wire_put_u8(&w, (uint8_t)count);
    for (index = 0; index < count; ++index) {
        const mp_command_t *command = &commands[index];

        mp_wire_put_u32(&w, command->tick);
        /* Both are stick axes over [-1, 1], never headings, so they take the signed axis quantiser
         * and never the angle one: an angle quantiser wraps a full left turn into a turn to the
         * right. A value that is not a number fails the encode; a value beyond full deflection
         * is clamped to it. */
        if (!mp_wire_put_axis(&w, command->turn) || !mp_wire_put_axis(&w, command->move)) {
            return false;
        }
        mp_wire_put_u32(&w, command->buttons);
    }

    if (w.overflowed) {
        return false;
    }
    if (bytes != NULL) {
        *bytes = w.at;
    }
    return true;
}

bool mp_command_decode(const uint8_t *buffer, size_t bytes, mp_command_t *out, size_t max,
                       size_t *count)
{
    mp_wire_reader_t r;
    uint8_t          n = 0;
    size_t           index;

    mp_wire_reader_init(&r, buffer, bytes);
    if (!mp_wire_get_u8(&r, &n)) {
        return false;
    }
    if (n > MP_COMMAND_REDUNDANCY || (size_t)n > max) {
        return false;   /* a packet claiming more than the cap or than the caller holds is
                         * refused */
    }

    for (index = 0; index < n; ++index) {
        mp_command_t *command = &out[index];

        if (!mp_wire_get_u32(&r, &command->tick) || !mp_wire_get_axis(&r, &command->turn) ||
            !mp_wire_get_axis(&r, &command->move) ||
            !mp_wire_get_u32(&r, &command->buttons)) {
            return false;
        }
    }
    if (r.overran) {
        return false;
    }
    if (count != NULL) {
        *count = n;
    }
    return true;
}

void mp_command_sink_init(mp_command_sink_t *sink)
{
    sink->have_applied      = false;
    sink->last_applied_tick = 0u;
}

/* Whether lhs is the more recent of two wrapping tick numbers, the same test the channel and the
 * snapshot history use, so the mark stays right across the tick counter's own wrap. */
static bool tick_newer(uint32_t lhs, uint32_t rhs)
{
    return (lhs != rhs) && ((uint32_t)(lhs - rhs) < 0x80000000u);
}

size_t mp_command_sink_feed(mp_command_sink_t *sink, const mp_command_t *commands, size_t count,
                            void (*apply)(void *context, const mp_command_t *command),
                            void *context)
{
    uint32_t highest = sink->last_applied_tick;
    bool     saw_any = false;
    size_t   applied = 0;
    size_t   pass;

    /* The packet carries commands newest first, so applying them in tick order oldest first means
     * a full backward walk. The count is small (the redundancy), so the cost is nothing, and the
     * clarity of applying strictly in order is worth more than a sort. */
    for (pass = 0; pass < count; ++pass) {
        /* oldest first */
        size_t              index = count - 1u - pass;
        const mp_command_t *command = &commands[index];

        if (!sink->have_applied || tick_newer(command->tick, sink->last_applied_tick)) {
            if (apply != NULL) {
                apply(context, command);
            }
            ++applied;
            if (!saw_any || tick_newer(command->tick, highest)) {
                highest = command->tick;
                saw_any = true;
            }
        }
    }

    if (saw_any) {
        sink->last_applied_tick = highest;
        sink->have_applied      = true;
    }
    return applied;
}
