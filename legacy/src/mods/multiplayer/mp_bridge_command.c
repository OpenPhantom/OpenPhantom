/* mp_bridge_command.c: the loopback's command path; the header carries what it proves and why.
 *
 * Left mp_bridge.c on 2026-09-04, when the timeline code put that file past the seam its own size
 * note had named. Everything here serves one shape of the bridge and touches the engine only
 * through the input split.
 */
#include "mp_bridge_command.h"

#include "mp_bridge_world.h"
#include "mp_command.h"
#include "mp_input.h"
#include "mp_payload_prefix.h"
#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* The wire's bit order and the engine's action ids grew separately (the wire packs fifteen bits
 * densely, the engine numbers its actions with gaps), so the translation is a table rather than
 * a shift, and the unit test pins every pairing: a wrong pairing would not look wrong in review,
 * it would jump when the far player meant to fire. The objectives bit is carried and mapped to
 * nothing: the input phase answers that key with a quick save, which is the local machine's to
 * make and not the far player's. */
typedef struct command_bit_map {
    uint32_t wire_bit;
    uint32_t action_id;
} command_bit_map_t;

static const command_bit_map_t bit_map[] = {
    { MP_CMD_ATTACK,      2u  },
    { MP_CMD_JUMP,        3u  },
    { MP_CMD_FORCE,       4u  },
    { MP_CMD_SIDLE,       5u  },
    { MP_CMD_USE,         6u  },
    { MP_CMD_RUN,         7u  },
    { MP_CMD_WEAPON1,     0x0Bu },
    { MP_CMD_WEAPON2,     0x0Cu },
    { MP_CMD_WEAPON3,     0x0Du },
    { MP_CMD_WEAPON4,     0x0Eu },
    { MP_CMD_WEAPON5,     0x0Fu },
    { MP_CMD_WEAPON6,     0x10u },
    { MP_CMD_WEAPON_PREV, 0x14u },
    { MP_CMD_WEAPON_NEXT, 0x15u }
};

/* The turn and move axes cross unchanged. The relative (mouse) axis is never fed from the wire,
 * because a networked command is a per substep intent, which is what the digital axes model. */
void mp_bridge_map_command(const mp_command_t *command, mp_input_command_t *out)
{
    size_t i;

    out->turn_axis  = 0.0f;
    out->move_axis  = 0.0f;
    out->mouse_turn = 0.0f;
    out->buttons    = 0u;
    if (command == NULL) {
        return;
    }
    out->turn_axis = command->turn;
    out->move_axis = command->move;
    for (i = 0; i < sizeof bit_map / sizeof bit_map[0]; ++i) {
        if (command->buttons & bit_map[i].wire_bit) {
            out->buttons |= 1u << bit_map[i].action_id;
        }
    }
}

void mp_bridge_command_reset(mp_bridge_command_t *state)
{
    mp_command_sink_init(&state->sink);
}

void mp_bridge_command_enable_spin(mp_bridge_command_t *state)
{
    state->spin = true;
}

static void apply_wire_command(void *context, const mp_command_t *command)
{
    mp_bridge_command_t *state = (mp_bridge_command_t *)context;
    mp_input_command_t   input;

    mp_bridge_map_command(command, &input);
    mp_input_set_command(&input);
    ++state->applied;
}

/* One command for this substep, the synthetic spin, kept in the redundancy window, newest first,
 * so every packet carries the last several and a lost packet never starves the host. The
 * acknowledgement rides in front of the commands, as it does in front of a UDP client's state,
 * so the loopback host keeps encoding deltas against a baseline the client holds. */
void mp_bridge_command_author(mp_bridge_command_t *state, mp_session_t *client)
{
    uint8_t      payload[MP_SESSION_PAYLOAD_BYTES];
    size_t       bytes = 0;
    mp_command_t command;
    uint8_t     *commands = payload + MP_PAYLOAD_ACK_BYTES;

    memset(&command, 0, sizeof command);
    command.tick = ++state->tick;
    command.turn = state->spin ? 1.0f : 0.0f;

    if (state->window_count < MP_COMMAND_REDUNDANCY) {
        ++state->window_count;
    }
    memmove(&state->window[1], &state->window[0],
            (state->window_count - 1u) * sizeof state->window[0]);
    state->window[0] = command;

    if (mp_bridge_world_put_ack(payload, sizeof payload) &&
        mp_command_encode(state->window, state->window_count, commands,
                          sizeof payload - MP_PAYLOAD_ACK_BYTES, &bytes) &&
        mp_session_set_payload(client, 0, payload, bytes + MP_PAYLOAD_ACK_BYTES)) {
        ++state->sent;
    }
}

void mp_bridge_command_drain(mp_bridge_command_t *state, mp_session_t *host)
{
    uint8_t payload[MP_SESSION_PAYLOAD_BYTES];
    size_t  bytes = 0;

    while (mp_session_read_payload(host, 0, payload, sizeof payload, &bytes)) {
        mp_command_t     decoded[MP_COMMAND_REDUNDANCY];
        size_t           count = 0;
        mp_payload_ack_t acked;

        if (bytes > MP_PAYLOAD_ACK_BYTES && mp_payload_get_ack(payload, bytes, &acked) &&
            mp_command_decode(payload + MP_PAYLOAD_ACK_BYTES, bytes - MP_PAYLOAD_ACK_BYTES,
                              decoded, MP_COMMAND_REDUNDANCY, &count)) {
            mp_bridge_world_acknowledged(0u, &acked);   /* the loopback's one client */
            mp_command_sink_feed(&state->sink, decoded, count, &apply_wire_command, state);
        } else {
            ++state->refusals;
        }
    }
}
