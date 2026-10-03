/* mp_teleport_host.h: a client's own player put beside the host, because the player asked.
 *
 * Layer 3. The second button of the developer menu's Multiplayer heading, "Teleport to host". It
 * is wiring and nothing new: the host's pose is the one every substep resolves for his puppet,
 * the place beside him comes from the one seat search, and the body is moved by the seat machine
 * a scene's seat uses, under a short fade through the placement, and by the engine's own respawn
 * when the player stays in a mode the teleport may not move. Nothing travels: the host and the
 * other players see the body where its own state says it is.
 *
 * Two phases. The SEARCH asks the seat search once a frame for a place beside the host as he
 * stands on that frame, for five seconds at most, and takes only a place beside him: when the
 * search falls back to an authored point of the level the teleport is given up. The door that
 * let the press through is asked again on every look, so a host that dies, leaves the level or
 * walks up to the player ends it. The MOVE then runs the seat machine to its end. A level that
 * changes under either phase ends the teleport, and every way out gives a held fade back.
 *
 * It makes the place right and not the world: movers, push blocks and doors are not brought
 * into step, so a player can be put beside a host behind a door that is shut on his own machine.
 *
 * The decisions are mp_player_help_rule's; who answers the press is mp_player_help.c.
 */
#ifndef MULTIPLAYER_MP_TELEPORT_HOST_H
#define MULTIPLAYER_MP_TELEPORT_HOST_H

#include "mp_player_help_rule.h"

#include <stdbool.h>
#include <stdint.h>

/* Whether a teleport could be carried out on this executable at all: the seat's probes and the
 * scene's fade and modes resolved. */
bool mp_teleport_host_bound(void);

/* A press. Asks the door, and where it answers OPEN begins the search. What comes back is the
 * answer to the press either way. `substeps` is the frame pump's count, the clock of the search
 * and of the move. */
mp_player_help_verdict_t mp_teleport_host_ask(bool overlay_holds, uint32_t substeps);

/* One frame of a teleport under way, before the placement and the respawn are ticked, so a
 * place handed over on this frame is taken on this frame. True on the frame it ends, and
 * `ended` then says how. */
bool mp_teleport_host_frame(uint32_t substeps, mp_player_help_verdict_t *ended);

/* Every other way out: the player's repair, and the session's end, which the frame pump does
 * not live to see. A held fade is given back. True when a teleport was under way, and `reason`
 * then says why its answer is a refusal. `why` is the line's word for who ended it. */
bool mp_teleport_host_leave(const char *why, uint8_t *reason);

void mp_teleport_host_report(void);

#endif /* MULTIPLAYER_MP_TELEPORT_HOST_H */
