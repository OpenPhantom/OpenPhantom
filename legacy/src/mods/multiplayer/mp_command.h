/* mp_command.h: the input a client sends the host, and the redundancy that survives loss.
 *
 * Layer 1, pure logic, the Client-to-host direction that mirrors mp_snapshot's host-to-client one.
 * One command is one simulation tick's worth of a player's input: two analogue axes and a bitfield
 * of the digital actions. The client sends not just the newest command but the last several
 * unacknowledged ones in every packet, because at thirty-two ticks a second a handful of commands
 * is a few dozen bytes and sending them redundantly means a lost packet never starves the host's
 * input for that player. The host takes only the commands newer than the last it applied and
 * ignores the repeats.
 *
 * The action bits are the ones the player pipeline reads each substep, recovered from the engine's
 * own input phases: two axes plus attack, jump, force, sidle, use, run, the six weapon selects and
 * the two weapon cycles. Everything else a body needs is state the host already holds, not input.
 */
#ifndef MULTIPLAYER_MP_COMMAND_H
#define MULTIPLAYER_MP_COMMAND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The digital action bits, by the engine's own action ids. A command carries them as one word. */
#define MP_CMD_ATTACK       (1u << 0)
#define MP_CMD_JUMP         (1u << 1)
#define MP_CMD_FORCE        (1u << 2)
#define MP_CMD_SIDLE        (1u << 3)
#define MP_CMD_USE          (1u << 4)
#define MP_CMD_RUN          (1u << 5)
#define MP_CMD_OBJECTIVES   (1u << 6)
#define MP_CMD_WEAPON1      (1u << 7)
#define MP_CMD_WEAPON2      (1u << 8)
#define MP_CMD_WEAPON3      (1u << 9)
#define MP_CMD_WEAPON4      (1u << 10)
#define MP_CMD_WEAPON5      (1u << 11)
#define MP_CMD_WEAPON6      (1u << 12)
#define MP_CMD_WEAPON_PREV  (1u << 13)
#define MP_CMD_WEAPON_NEXT  (1u << 14)

typedef struct mp_command {
    uint32_t tick;      /* the simulation tick this input is for */
    float    turn;      /* the turn axis as the engine's digital read returns it, [-1, 1] */
    float    move;      /* the move axis, forward positive, [-1, 1]; both clamped on encode */
    uint32_t buttons;   /* the digital action bits above */
} mp_command_t;

/* How many past commands a packet carries. A little over a round trip plus a throttle at the
 * feature's rate, so a lost packet is covered by the next several that repeat its commands. */
#define MP_COMMAND_REDUNDANCY 12u

/* Encodes up to MP_COMMAND_REDUNDANCY commands, newest first, into a packet. `count` is how many of
 * `commands` to send, capped at the redundancy. False when the buffer is too small; the axes are
 * quantised by mp_wire, so a value beyond full deflection is clamped to full rather than wrapped,
 * and only a value that is not a number is refused. */
bool mp_command_encode(const mp_command_t *commands, size_t count, uint8_t *buffer, size_t capacity,
                       size_t *bytes);

/* Decodes a command packet into `out`, up to `max`, and reports how many were read into `count`.
 * False when the packet is malformed or claims more commands than `max` holds. */
bool mp_command_decode(const uint8_t *buffer, size_t bytes, mp_command_t *out, size_t max,
                       size_t *count);

/* ============================ The host's side: take the new, drop the repeats ==================
 *
 * The host keeps, per client, the tick of the last command it applied. Feeding a decoded packet
 * here applies each command newer than that and updates the mark, so a redundant command already
 * applied is ignored and a packet arriving out of order cannot replay an old input.
 */
typedef struct mp_command_sink {
    bool     have_applied;
    uint32_t last_applied_tick;
} mp_command_sink_t;

void mp_command_sink_init(mp_command_sink_t *sink);

/* Applies the commands in `packet` that are newer than the last applied, calling `apply` for each
 * in tick order oldest first, and advances the mark. Returns how many were applied. `apply` may be
 * NULL to just advance the mark (a client catching its own echo up). */
size_t mp_command_sink_feed(mp_command_sink_t *sink, const mp_command_t *commands, size_t count,
                            void (*apply)(void *context, const mp_command_t *command),
                            void *context);

#endif /* MULTIPLAYER_MP_COMMAND_H */
