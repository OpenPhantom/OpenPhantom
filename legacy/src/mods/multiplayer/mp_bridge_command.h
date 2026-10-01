/* mp_bridge_command.h: the loopback's command path, the in-process proof of the input binding.
 *
 * One shape of the bridge, the loopback, carries commands rather than state: the client authors
 * one command per substep and sends it with redundancy as its unreliable payload, the host reads
 * the window, feeds the deduplicating sink, and hands the newest command to the input split, so
 * the second body moves on input that crossed the whole wire path. That is what the in-process
 * acceptance proved and keeps proving. The UDP roles exchange state through the world module and
 * author no commands yet; the command stream stays built for the day a host simulates remote bodies
 * with prediction.
 *
 * The translation between the wire's action bits and the engine's action ids is the one piece
 * here a test can pin exactly, and it is pure.
 */
#ifndef MULTIPLAYER_MP_BRIDGE_COMMAND_H
#define MULTIPLAYER_MP_BRIDGE_COMMAND_H

#include "mp_command.h"
#include "mp_input.h"
#include "mp_session.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mp_bridge_command {
    bool              spin;         /* author a full turn every substep */
    uint32_t          tick;         /* the tick the client stamps its commands with */
    mp_command_t      window[MP_COMMAND_REDUNDANCY];   /* newest first */
    size_t            window_count;
    mp_command_sink_t sink;

    uint32_t          sent;         /* command packets set as the client's payload */
    uint32_t          applied;      /* commands the sink accepted into the input split */
    uint32_t          refusals;     /* payloads the codec refused */
} mp_bridge_command_t;

/* Translates one wire command into the input split's form: turn and move onto the two axes, and
 * the wire's action bits onto the engine's own action ids. Pure, so the unit test can pin the
 * mapping without a game; a wrong bit here would not look wrong, it would jump when the far
 * player meant to fire. */
void mp_bridge_map_command(const mp_command_t *command, mp_input_command_t *out);

/* The sink starts over, on every arrival of a peer: a restarted client counts its ticks from one
 * again, and a sink still holding the old session's last tick would refuse every one of them. */
void mp_bridge_command_reset(mp_bridge_command_t *state);

/* Arms the spin: the client authors a constant full turn every substep, so a circling second body
 * is the visible proof the command stream crossed the wire. Without it the client sends
 * stillness, which is itself the decoupling over the wire. */
void mp_bridge_command_enable_spin(mp_bridge_command_t *state);

/* The client's command for this substep, into the redundancy window and onto the session as its
 * payload, behind the acknowledgement of what the client holds of the host's world, which the
 * world module builds out of its own history as it does for a client's state. */
void mp_bridge_command_author(mp_bridge_command_t *state, mp_session_t *client);

/* The host's read of every command payload in the session's ring, oldest first: the
 * acknowledgement off the front into the world module, the window into the sink, and what the
 * sink accepts into the input split. */
void mp_bridge_command_drain(mp_bridge_command_t *state, mp_session_t *host);

#endif /* MULTIPLAYER_MP_BRIDGE_COMMAND_H */
