/* mp_enemy_fx.h: the emitter a script hangs on an NPC, on the machine that only watches.
 *
 * Layer 2. A script opcode attaches a continuous emitter to an actor, and on a client the actor
 * is a parked replica whose scripts never run, so nothing of it appears there.
 *
 * An event, not a state. The engine keeps one handle on the actor but stacks: a second start
 * overwrites the handle without ending the first emitter, and a destroyer's stun sparks are
 * followed by its death blast. So every start travels as a world event of its own
 * (mp_world_event.h), and the replica starts one emitter per event. Two starts in one substep, the
 * smoke and the sparks a gonk's script makes together, are two events, and neither is lost.
 *
 * The template and the node share the event's sixteen bits, the template offset by one in the low
 * byte and the node in the high one. The node travels raw: for an enemy both machines build the
 * actor out of the same level file, so the same number is the same joint.
 */
#ifndef MULTIPLAYER_MP_ENEMY_FX_H
#define MULTIPLAYER_MP_ENEMY_FX_H

#include <stdbool.h>
#include <stdint.h>

/* The largest template index and node the event can carry: the template takes the low byte offset
 * by one, the node the byte above it. A script asking for more is counted and dropped. */
#define MP_ENEMY_FX_MAX_TEMPLATE 254u
#define MP_ENEMY_FX_MAX_NODE     255u

/* Pack and unpack the event, pure. Packing refuses what the event cannot hold by answering 0. */
uint32_t mp_enemy_fx_pack(uint32_t template_index, uint32_t node);
bool     mp_enemy_fx_unpack(uint32_t packed, uint32_t *template_index, uint32_t *node);

/* Whether this side is a client of a started session, and in `runs` whether a session runs: the
 * session's own answer, handed in because this module links where the session does not. */
typedef bool (*mp_enemy_fx_role_fn_t)(bool *runs, uint8_t *generation);

/* The hull on both sides, and the player of the kind on a client. The player is handed to the
 * world events even when the hull cannot stand, so an emitter the host sent to a side with no site
 * is counted here as having none. On a client, a start of an actor whose life the host describes
 * is withheld: its emitters are the host's events. */
bool mp_enemy_fx_install(mp_enemy_fx_role_fn_t role);

void mp_enemy_fx_report(void);

#endif /* MULTIPLAYER_MP_ENEMY_FX_H */
