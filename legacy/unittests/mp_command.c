/* mp_command.c: the input stream over its edges, and the sink that drops repeats.
 *
 * A packet of commands round-trips. The redundancy is the point: several commands ride each packet
 * so a lost one is covered by the next, and the sink applies each command exactly once however many
 * packets repeat it, and never replays an old one that arrives after a newer has been applied. The
 * refusals guard the deserialiser: a packet claiming more commands than fit, a truncated one, and
 * an axis outside the quantiser's range.
 */
#include "unittest.h"

#include "mp_command.h"
#include "mp_wire.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static mp_command_t a_command(uint32_t tick, float turn, uint32_t buttons)
{
    mp_command_t command;

    command.tick = tick;
    command.turn = turn;
    command.move = 0.5f;
    command.buttons = buttons;
    return command;
}

static void check_round_trip(void)
{
    mp_command_t sent[MP_COMMAND_REDUNDANCY];
    mp_command_t got[MP_COMMAND_REDUNDANCY];
    uint8_t      packet[512];
    size_t       bytes = 0;
    size_t       count = 0;
    size_t       i;

    ut_section("a full redundant packet round-trips");

    for (i = 0; i < MP_COMMAND_REDUNDANCY; ++i) {
        sent[i] = a_command((uint32_t)(100u + i), -1.0f, MP_CMD_ATTACK | MP_CMD_JUMP);
    }

    ut_check(mp_command_encode(sent, MP_COMMAND_REDUNDANCY, packet, sizeof packet, &bytes),
             "twelve commands encode");
    ut_check(bytes == 1u + MP_COMMAND_REDUNDANCY * 12u,
             "at twelve bytes a command: tick, two signed axes, buttons");
    ut_check(mp_command_decode(packet, bytes, got, MP_COMMAND_REDUNDANCY, &count),
             "and decode");
    ut_check(count == MP_COMMAND_REDUNDANCY, "all twelve come back");
    ut_check(got[0].tick == 100u && got[11].tick == 111u, "in the order they were sent");
    ut_check(got[0].buttons == (MP_CMD_ATTACK | MP_CMD_JUMP), "with their buttons");

    {
        float error = got[0].turn + 1.0f;

        if (error < 0) {
            error = -error;
        }
        ut_check(error <= MP_WIRE_AXIS_ERROR,
                 "and a full left turn is still a full LEFT turn on the far side");
        ut_check(got[0].turn < 0.0f, "not wrapped into a turn to the right, as the angle path did");
        error = got[0].move - 0.5f;
        if (error < 0) {
            error = -error;
        }
        ut_check(error <= MP_WIRE_AXIS_ERROR, "and the move axis to the wire's tolerance");
    }
}

static void check_refusals(void)
{
    mp_command_t one = a_command(1u, 0.0f, 0u);
    mp_command_t got[4];
    uint8_t      packet[512];
    size_t       bytes = 0;
    size_t       count = 0;

    ut_section("the deserialiser refuses what it cannot trust");

    mp_command_encode(&one, 1u, packet, sizeof packet, &bytes);
    ut_check(mp_command_decode(packet, bytes, got, 4u, &count) && count == 1u,
             "a one-command packet decodes");

    /* Say the packet holds twelve, hand a buffer for four. */
    {
        mp_command_t twelve[MP_COMMAND_REDUNDANCY];
        uint8_t      big[512];
        size_t       big_bytes = 0;
        size_t       i;

        for (i = 0; i < MP_COMMAND_REDUNDANCY; ++i) {
            twelve[i] = a_command((uint32_t)i, 0.0f, 0u);
        }
        mp_command_encode(twelve, MP_COMMAND_REDUNDANCY, big, sizeof big, &big_bytes);
        ut_check(!mp_command_decode(big, big_bytes, got, 4u, &count),
                 "a packet claiming more than the buffer holds is refused");
    }

    ut_check(!mp_command_decode(packet, 2u, got, 4u, &count),
             "a truncated packet is refused rather than read past");

    {
        mp_command_t bad = a_command(1u, 0.0f, 0u);
        mp_command_t back[1];
        uint8_t      small[64];
        size_t       small_bytes = 0;
        volatile float zero = 0.0f;

        bad.move = 1.0e30f;   /* beyond full deflection, which means full deflection */
        ut_check(mp_command_encode(&bad, 1u, small, sizeof small, &small_bytes) &&
                 mp_command_decode(small, small_bytes, back, 1u, &count) && back[0].move == 1.0f,
                 "an axis beyond full deflection is clamped to full, not refused and not wrapped");

        bad.turn = zero / zero;
        ut_check(!mp_command_encode(&bad, 1u, small, sizeof small, &small_bytes),
                 "an axis that is not a number fails the encode");
    }
}

/* Applies into a running record so the test can see exactly which commands the sink let through. */
typedef struct applied_log {
    uint32_t ticks[64];
    size_t   count;
} applied_log_t;

static void record_apply(void *context, const mp_command_t *command)
{
    applied_log_t *log = (applied_log_t *)context;

    if (log->count < 64u) {
        log->ticks[log->count++] = command->tick;
    }
}

static void check_sink(void)
{
    mp_command_sink_t sink;
    applied_log_t     log = { { 0 }, 0 };
    mp_command_t      commands[4];

    ut_section("the sink applies each command once and never replays an old one");

    mp_command_sink_init(&sink);

    /* A packet with ticks 12, 11, 10 (newest first). All new, applied oldest first. */
    commands[0] = a_command(12u, 0.0f, 0u);
    commands[1] = a_command(11u, 0.0f, 0u);
    commands[2] = a_command(10u, 0.0f, 0u);
    ut_check(mp_command_sink_feed(&sink, commands, 3u, &record_apply, &log) == 3u,
             "the first packet applies all three");
    ut_check(log.count == 3u && log.ticks[0] == 10u && log.ticks[2] == 12u,
             "in tick order, oldest first");

    /* The same packet again: every command is a repeat, none applied. */
    ut_check(mp_command_sink_feed(&sink, commands, 3u, &record_apply, &log) == 0u,
             "a redundant repeat of the same packet applies nothing");

    /* A packet with ticks 14, 13, 12: 12 is a repeat, 13 and 14 are new. */
    commands[0] = a_command(14u, 0.0f, 0u);
    commands[1] = a_command(13u, 0.0f, 0u);
    commands[2] = a_command(12u, 0.0f, 0u);
    ut_check(mp_command_sink_feed(&sink, commands, 3u, &record_apply, &log) == 2u,
             "an overlapping packet applies only the two genuinely new commands");

    /* An out-of-order stale packet (ticks 9, 8): both older than the mark, none applied. */
    commands[0] = a_command(9u, 0.0f, 0u);
    commands[1] = a_command(8u, 0.0f, 0u);
    ut_check(mp_command_sink_feed(&sink, commands, 2u, &record_apply, &log) == 0u,
             "a stale packet that arrives late replays nothing");
}

static void check_wraparound(void)
{
    mp_command_sink_t sink;
    mp_command_t      commands[2];

    ut_section("the mark stays right across the tick counter's wrap");

    mp_command_sink_init(&sink);
    commands[0] = a_command(0xFFFFFFFFu, 0.0f, 0u);
    ut_check(mp_command_sink_feed(&sink, commands, 1u, NULL, NULL) == 1u,
             "the maximum tick applies");

    commands[0] = a_command(0u, 0.0f, 0u);   /* the counter wrapped */
    ut_check(mp_command_sink_feed(&sink, commands, 1u, NULL, NULL) == 1u,
             "and a tick after the wrap is newer, not a stale replay");
}

int main(void)
{
    check_round_trip();
    check_refusals();
    check_sink();
    check_wraparound();

    return ut_summary("mp_command");
}
