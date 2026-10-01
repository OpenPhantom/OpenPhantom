/* mp_npc_shot_zap.h: the two arcs of an NPC's player zap, seen on a client.
 *
 * Layer 3. The player zap, kind 24, does not travel as a bolt: its handler posts a contact to
 * whichever player is alive on the machine it runs on, so a copy on a client would hurt that
 * client. What a watcher misses without it are its two arcs, from the zap to the player it named,
 * one second and one and a half. They travel as a world event at the actor that fired the zap.
 *
 * On the host the shot hull tells the bolt relay about the zap before the actor has stored it as
 * its tracked shot, so the zap is noted and its actor found at the start of the next substep, by
 * the tracked shot whose body is the zap's; the event goes then, a substep late. It names the
 * host's own world slot, because the handler names the host's own player, and it is not sent when
 * that player was dead, because then the handler drew nothing.
 *
 * A client calls fxzappo_create twice with the zap's own two parameter sets, from the shooter's
 * replica to the far body of the player the host named, or on the shooter alone when that player
 * is this machine's own. The ends are chosen by mp_npc_shot_zap_ends, which never hangs an arc on a
 * body of class 1: the engine's arc tick hurts that body once a second.
 */
#ifndef MULTIPLAYER_MP_NPC_SHOT_ZAP_H
#define MULTIPLAYER_MP_NPC_SHOT_ZAP_H

#include <stdbool.h>
#include <stdint.h>

/* Resolves the arcs' function and registers the player of the kind. */
bool mp_npc_shot_zap_install(void);

/* On the host: an NPC fired a player zap whose own body is `object`. */
void mp_npc_shot_zap_note(uint32_t object);

/* On the host, at the start of a substep: every zap noted since is posted at its actor. */
void mp_npc_shot_zap_resolve(void);

void mp_npc_shot_zap_report(void);

#endif /* MULTIPLAYER_MP_NPC_SHOT_ZAP_H */
